#include "coral_fans/functions/hud/PlayerVelocity.h"
#include "ll/api/memory/Hook.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/player/Player.h"
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace coral_fans::functions {

namespace {

// false：用相邻两个包的位置差 / 两个包的客户端 tick 差 计算（推荐，不受服务端批量处理输入的影响）
// true ：直接使用包里客户端上报的 mPosDelta（用于对比测试）
constexpr bool kUsePacketPosDelta = false;

using Clock = std::chrono::steady_clock;

struct Sample {
    bool              valid       = false; // 是否已有上一个包作为基准
    uint64_t          clientTick  = 0;     // 上一个包的客户端 tick
    Vec3              pos{0, 0, 0};        // 上一个包的位置
    int               dim         = -1;
    bool              hasVelocity = false;
    Vec3              perTick{0, 0, 0};    // 每 tick 位移
    Clock::time_point updatedAt{};
};

std::mutex                              gMutex;
std::unordered_map<std::string, Sample> gSamples; // key: 玩家 uuid

void record(Player& player, PlayerAuthInputPacket const& packet) {
    const Vec3 pos = packet.mPos;
    uint64_t   tick;
    {
        PlayerInputTick const& t = packet.mClientTick;
        tick                     = t.mValue;
    }
    const int dim = (int)player.getDimensionId();
    const auto now = Clock::now();

    std::lock_guard lock(gMutex);

    // 清理很久没更新的记录（已下线的玩家）
    if (gSamples.size() > 256) {
        for (auto it = gSamples.begin(); it != gSamples.end();) {
            if (now - it->second.updatedAt > std::chrono::seconds(60)) it = gSamples.erase(it);
            else ++it;
        }
    }

    Sample& s = gSamples[player.getUuid().asString()];

    if (kUsePacketPosDelta) {
        s.perTick     = packet.mPosDelta;
        s.hasVelocity = true;
    } else if (s.valid && s.dim == dim && tick > s.clientTick && tick - s.clientTick <= 10) {
        const float n     = (float)(tick - s.clientTick);
        const Vec3  moved = pos - s.pos;
        const Vec3  v{moved.x / n, moved.y / n, moved.z / n};
        // 平均每 tick 超过 8 格（160 格/秒）视为传送，不计速度
        if (v.length() <= 8.0f) {
            s.perTick     = v;
            s.hasVelocity = true;
        } else {
            s.hasVelocity = false;
        }
    } else if (s.valid && s.dim == dim && tick == s.clientTick) {
        return; // 同一 tick 的重复包，保持不变
    } else {
        s.hasVelocity = false; // 刚上线 / 换维度 / tick 回退或间隔过大：重置基准
    }

    s.valid      = true;
    s.clientTick = tick;
    s.pos        = pos;
    s.dim        = dim;
    s.updatedAt  = now;
}

using HandlePlayerAuthInput = void (ServerNetworkHandler::*)(NetworkIdentifier const&, PlayerAuthInputPacket const&);

} // namespace

LL_AUTO_TYPE_INSTANCE_HOOK(
    PlayerAuthInputVelocityHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    static_cast<HandlePlayerAuthInput>(&ServerNetworkHandler::$handle),
    void,
    NetworkIdentifier const&   source,
    PlayerAuthInputPacket const& packet
) {
    origin(source, packet);
    //if (auto* player = this->_getServerPlayer(source, packet.mSenderSubId)) record(*player, packet);
}

std::optional<Vec3> getReportedVelocity(Player& player) {
    std::lock_guard lock(gMutex);
    auto            it = gSamples.find(player.getUuid().asString());
    if (it == gSamples.end() || !it->second.hasVelocity) return std::nullopt;
    if (Clock::now() - it->second.updatedAt > std::chrono::milliseconds(250)) return std::nullopt;
    return it->second.perTick;
}

} // namespace coral_fans::functions