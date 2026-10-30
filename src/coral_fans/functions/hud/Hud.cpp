#include "coral_fans/functions/hud/Hud.h"
#include "coral_fans/CoralFans.h"
#include "coral_fans/base/Utils.h"
#include "coral_fans/functions/data/Data.h"
#include "coral_fans/functions/freeCamera/FreeCamera.h"
#include "coral_fans/functions/func/FuncManager.h"


#include "coral_fans/functions/village/Village.h"
#include "ll/api/base/StdInt.h"
#include "ll/api/i18n/I18n.h"
#include "ll/api/service/Bedrock.h"
#include "mc/common/Brightness.h"
#include "mc/profile/ProfilerLite.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/network/packet/SetTitlePacketPayload.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/VanillaBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/HitResultType.h"
#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <string_view>


namespace coral_fans::functions {

std::vector<std::pair<std::string, uint64>> HudHelper::HudTypeVec = {
    {"mspt",      HudType::mspt     },
    {"ping",      HudType::ping     },
    {"base",      HudType::base     },
    {"redstone",  HudType::redstone },
    {"village",   HudType::village  },
    {"hopper",    HudType::hopper   },
    {"block",     HudType::block    },
    {"container", HudType::container}
};

namespace {
// Actor::getVelocity() 返回值换算成 格/秒 的系数：
// 返回的是每 tick 位移则填 20；如果返回值已经是 格/秒（矿车在动力铁轨上约 8），改成 1
constexpr float kVelocityToPerSecond = 20.0f;

// ---- container 物品名称本地化 ----
// 不手动维护中英文映射：把物品的“本地化键”（如 item.diamond.name）交给客户端，
// 由客户端按玩家自己的语言翻译。做法是用 ActionbarTextObject（与 /titleraw 相同的 rawtext JSON）发送。
// 拼接 msg 时，用 kTrBegin/kTrEnd 包住需要客户端翻译的键，发送前再转换成 rawtext。
constexpr char kTrBegin = '\x01';
constexpr char kTrEnd   = '\x02';

std::string jsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) out += std::format("\\u{:04x}", (unsigned)c);
            else out += (char)c;
        }
    }
    return out;
}

// 物品显示名：自定义名称直接显示；否则返回被 kTrBegin/kTrEnd 包住的本地化键
std::string itemNameToken(ItemStack const& item) {
    std::string name = item.getCustomName();
    if (!name.empty()) {
        std::erase(name, kTrBegin);
        std::erase(name, kTrEnd);
        return name;
    }
    std::string key = item.getDescriptionId();
    if (key.empty()) return item.getName(); // 兜底：取不到本地化键时显示原始英文名
    if (!key.ends_with(".name")) key += ".name";
    return std::string{kTrBegin} + key + kTrEnd;
}

// 把带 kTrBegin/kTrEnd 标记的文本转换成 rawtext JSON
std::string toRawtextJson(std::string const& msg) {
    std::string json = "{\"rawtext\":[";
    bool        first = true;
    auto        add   = [&](std::string const& part) {
        if (!first) json += ',';
        first  = false;
        json  += part;
    };
    size_t pos = 0;
    while (pos < msg.size()) {
        size_t b = msg.find(kTrBegin, pos);
        if (b == std::string::npos) {
            add("{\"text\":\"" + jsonEscape(std::string_view{msg}.substr(pos)) + "\"}");
            break;
        }
        if (b > pos) add("{\"text\":\"" + jsonEscape(std::string_view{msg}.substr(pos, b - pos)) + "\"}");
        size_t e = msg.find(kTrEnd, b + 1);
        if (e == std::string::npos) { // 标记不成对，当作普通文本
            add("{\"text\":\"" + jsonEscape(std::string_view{msg}.substr(b + 1)) + "\"}");
            break;
        }
        add("{\"translate\":\"" + jsonEscape(std::string_view{msg}.substr(b + 1, e - b - 1)) + "\"}");
        pos = e + 1;
    }
    json += "]}";
    return json;
}

void sendActionbarRawtext(Player& player, std::string const& json) {
    SetTitlePacket packet{SetTitlePacketPayload::TitleType::ActionbarTextObject, json, std::nullopt};
    packet.sendTo(player);
}

// 格式化为 (x, y, z)，而不是 Vec3::toString() 的 "Vec3(x,y,z)"
std::string formatVec3(Vec3 const& v, int precision = 2) {
    return std::format("({:.{}f}, {:.{}f}, {:.{}f})", v.x, precision, v.y, precision, v.z, precision);
}

} // namespace

void HudHelper::tick() {
    using ll::i18n_literals::operator""_tr;
    static int gt;
    if (gt == 1) { // delay 1 tick to get village info
        auto  level    = ll::service::getLevel();
        auto& configDb = CoralFans::getInstance().getConfigDb();
        if (level) [[likely]] {
            level->forEachPlayer([&configDb, &level](Player& player) {
                using ll::i18n_literals::operator""_tr;
                if (configDb->get("functions.players." + player.getUuid().asString() + ".cfhud.show") == "false")
                    return true;
                unsigned long hud;
                try {
                    hud = std::stoul(
                        CoralFans::getInstance()
                            .getConfigDb()
                            ->get("functions.players." + player.getUuid().asString() + ".cfhud.hud")
                            .value_or("0")
                    );
                } catch (...) {
                    return true;
                }
                // freecam 期间让 HUD 基于“灵魂”（客户端自由视角）而不是原地不动的“肉身”：
                // 作用域内临时把玩家的位置/速度/朝向替换为灵魂的，离开 lambda 时自动恢复
                SoulViewGuard soulGuard(player);
                std::string msg;
                double mspt = (double)ProfilerLite::gProfilerLiteInstance().mDebugServerTickTime->count() / 1000000.0;
                double currentTps  = mspt <= 50 ? 20 : (double)(1000.0 / mspt);
                auto   hitrst      = player.traceRay(5.25f, true, true);
                auto&  blockSource = player.getDimensionBlockSource();
                if (hud & (1 << HudHelper::HudType::mspt)) {
                    msg += std::format(
                        "MSPT:{}{:.2f}§r TPS:{}{:.2f}§r\n",
                        mspt > 50 ? "§c" : "§a",
                        mspt,
                        currentTps < 20 ? "§c" : "§a",
                        currentTps
                    );
                }
                if (hud & (1 << HudHelper::HudType::ping)) {
                    auto networkStatus = player.getNetworkStatus();
                    if (networkStatus) {
                        const auto ping = networkStatus->mCurrentPing->count();
                        const auto packetLoss = networkStatus->mCurrentPacketLoss;
                        const char* pingColor =
                            ping <= 50  ? "§a" :
                            ping <= 100 ? "§e" :
                                        "§c";
                        const char* packetLossColor =
                            packetLoss <= 1.0f ? "§a" :
                            packetLoss <= 5.0f ? "§e" :
                                                "§c";
                        msg += std::format(
                            "延迟:{}{}ms§r 丢包:{}{:.2f}%%§r\n",
                            pingColor,
                            ping,
                            packetLossColor,
                            packetLoss
                        );
                    } else {
                        msg += "Ping:§cN/A§r 丢包:§cN/A§r\n";
                    }
                }

                if (hud & (1 << HudHelper::HudType::base)) {
                    // 速度：使用 Actor::getVelocity()。
                    // 不使用 getPosDelta()：玩家骑乘（矿车/马/船等）时它不会更新，坐骑的值也不可靠。
                    // 统一换算成格/秒（见 kVelocityToPerSecond）。
                    const Vec3 rawVel = player.getVelocity();
                    const Vec3 speed{
                        rawVel.x * kVelocityToPerSecond,
                        rawVel.y * kVelocityToPerSecond,
                        rawVel.z * kVelocityToPerSecond
                    };
                    auto& biome      = blockSource.getBiome(player.getFeetBlockPos());
                    auto  rawName    = utils::removeMinecraftPrefix(biome.mHash->c_str());
                    auto  translated = ll::i18n::getInstance().get("translate.biome." + rawName, {});
                    auto  biomeName  = translated.empty() ? rawName : std::string{translated};
                    msg             += "translate.cfhud.base"_tr(
                        level->getCurrentServerTick().tickID,
                        formatVec3(player.getPosition()),
                        formatVec3(player.getViewVector(1.0f), 3),
                        utils::blockPosToChunkPos(player.getFeetBlockPos()).toString(),
                        hitrst.mType == HitResultType::Tile ? hitrst.mBlock.toString() : "-",
                        hitrst.mType == HitResultType::Tile ? std::to_string(blockSource
                                                                                 ._getRawBrightness(
                                                                                     hitrst.mBlock + BlockPos{0, 1, 0},
                                                                                     player.getDimension().mSkyDarken,
                                                                                     true
                                                                                 )
                                                                                 .mValue)
                                                            : "-",
                        speed.length(),
                        speed.x,
                        speed.y,
                        speed.z,
                        biomeName
                    );
                }
                if (hud & (1 << HudHelper::HudType::redstone)) {
                    if (hitrst.mType == HitResultType::Tile) {
                        auto rst  = showRedstoneComponentsInfo(player.getDimension(), hitrst.mBlock, 2);
                        msg      += rst.first + "\n";
                    }
                }
                if (hud & (1 << HudHelper::HudType::village)) {
                    if (hitrst.mType == HitResultType::Entity) {
                        auto* entity = hitrst.getEntity();
                        if (entity) {
                            auto rst  = CFVillageManager::getInstance().getVillagerInfo(entity->getOrCreateUniqueID());
                            msg      += rst.first + "\n";
                        }
                    }
                }
                if (hud & (1 << HudHelper::HudType::hopper)) {
                    int ch = HopperCounterManager::getViewChannel(blockSource, hitrst);
                    if (ch != -1) msg += HopperCounterManager::getInstance().getChannel(ch).info() + "\n";
                }
                if (hud & (1 << HudHelper::HudType::block)) {
                    if (hitrst.mType == HitResultType::Tile) {
                        auto rst  = getBlockData(blockSource, hitrst.mBlock);
                        msg      += rst + "\n";
                    }
                }
                if (hud & (1 << HudHelper::HudType::container)) {
                    if (hitrst.mType == HitResultType::Tile) {
                        const auto& bl = blockSource.getBlock(hitrst.mBlock);
                        auto*       ba = blockSource.getBlockEntity(hitrst.mBlock);
                        if (bl.mBlockType->isContainerBlock() && ba) {
                            auto* container = static_cast<VanillaBlockActor*>(ba)->getContainer();
                            if (container) {
                                std::map<std::string, int> items;
                                for (const auto& item : container->getSlots()) {
                                    if (*item == ItemStack::EMPTY_ITEM()) continue;
                                    std::string name = itemNameToken(*item);
                                    if (auto it = items.find(name); it != items.end()) it->second += item->mCount;
                                    else items.emplace(name, item->mCount);
                                }
                                std::vector<std::pair<std::string, int>> vec(items.begin(), items.end());
                                std::sort(
                                    vec.begin(),
                                    vec.end(),
                                    [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                                        if (a.second == b.second) return a.first < b.first;
                                        else return a.second > b.second;
                                    }
                                );
                                for (const auto& it : vec) msg += std::format("{} : {}\n", it.first, it.second);
                            }
                        }
                    }
                }
                if (msg.ends_with('\n')) msg = msg.substr(0, msg.length() - 1);
                if (!msg.empty()) {
                    if (hud & (1 << HudHelper::HudType::container)) {
                        // 开启了 container 时整个 HUD 都走 rawtext，物品名由客户端本地化
                        // （始终使用同一种包，避免 Tip 与 Actionbar 两种文本叠在一起）
                        sendActionbarRawtext(player, toRawtextJson(msg));
                    } else {
                        utils::segmentAndSendToPlayer(msg, &player, TextPacketType::Tip);
                    }
                }
                return true;
            });
        }
    }
    static int interval = std::max(1, CoralFans::getInstance().getConfig().functions.hud.refreshInterval);
    gt                  = (gt + 1) % interval;
}

} // namespace coral_fans::functions