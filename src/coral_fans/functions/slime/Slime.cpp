#include "coral_fans/functions/slime/Slime.h"
#include "coral_fans/CoralFans.h"
#include "coral_fans/base/PlayerShowStore.h"
#include "coral_fans/base/Utils.h"


#include "ll/api/service/Bedrock.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/chunk/ChunkSource.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"

namespace coral_fans::functions {
void SlimeManager::setShow(Player& player, bool show, bool persist) {
    if (persist) show_store::set(player.getUuid(), "slime.show", show);
    if (this->getShow(player) == show) return;
    if (show) {
        auto [it, inserted] = this->mPlayerStates.try_emplace(player.getUuid());
        it->second.group    = bsci::GeometryGroup::createDefault(player.getUuid());
        this->tickCounter              = 0;
        this->runtimeRemoveTickCounter = 1;
    } else {
        auto it = this->mPlayerStates.find(player.getUuid());
        if (it == this->mPlayerStates.end()) return;
        this->removeAll(it->second);
        this->mPlayerStates.erase(it);
    }
}

bool SlimeManager::getShow(Player const& player) { return this->mPlayerStates.contains(player.getUuid()); }

void SlimeManager::clear() {
    for (auto& [uuid, state] : this->mPlayerStates) this->removeAll(state);
    this->mPlayerStates.clear();
}

void SlimeManager::draw() {
    auto level = ll::service::getLevel();
    if (!level.has_value()) [[unlikely]]
        return;
    for (auto it = this->mPlayerStates.begin(); it != this->mPlayerStates.end();) {
        auto* player = level->getPlayer(it->first);
        if (!player) {
            // 玩家已离线，清理其显示数据
            this->removeAll(it->second);
            it = this->mPlayerStates.erase(it);
            continue;
        }
        this->drawForPlayer(it->second, *player);
        ++it;
    }
}

void SlimeManager::drawForPlayer(PlayerSlimeState& state, Player& player) {
    static int radius = std::max(0, CoralFans::getInstance().getConfig().functions.slime.drawRadius);
    auto       originChunkPos = utils::blockPosToChunkPos(player.getFeetBlockPos());
    for (int i = -radius; i <= radius; ++i) {
        int maxJ = radius - abs(i);
        for (int j = -maxJ; j <= maxJ; ++j) {
            auto         seed = ((originChunkPos.x + i) * 0x1f1f1f1fu) ^ (uint32_t)(originChunkPos.z + j);
            std::mt19937 mt(seed);
            if (mt() % 10 == 0) {
                auto [it, inserted] =
                    state.particleMap.try_emplace(ChunkPos(originChunkPos.x + i, originChunkPos.z + j));
                if (inserted) {
                    it->second.first = state.group->box(
                        0,
                        AABB{
                            {(originChunkPos.x + i) * 16 + 0.1,  -64, (originChunkPos.z + j) * 16 + 0.1 },
                            {(originChunkPos.x + i) * 16 + 15.9, 320, (originChunkPos.z + j) * 16 + 15.9}
                    },
                        mce::Color::GREEN()
                    );
                }
                it->second.second = 0;
            }
        }
    }
}

void SlimeManager::tick() {
    if (this->mPlayerStates.empty()) return;
    if (!this->tickCounter) {
        this->draw();
        if (!this->runtimeRemoveTickCounter) {
            for (auto& [uuid, state] : this->mPlayerStates) this->runtimeRemove(state);
        }
        static int removeInterval =
            std::max(1, CoralFans::getInstance().getConfig().functions.slime.runtimeRemoveScale);
        this->runtimeRemoveTickCounter = (this->runtimeRemoveTickCounter + 1) % removeInterval;
    }
    static int interval = std::max(1, CoralFans::getInstance().getConfig().functions.slime.drawInterval);
    this->tickCounter   = (this->tickCounter + 1) % interval;
}

void SlimeManager::removeAll(PlayerSlimeState& state) {
    if (!state.group) return;
    for (auto& [_, data] : state.particleMap) state.group->remove(data.first);
    state.particleMap.clear();
}

void SlimeManager::runtimeRemove(PlayerSlimeState& state) {
    auto& geometryGroup = *state.group;
    auto& slimeConfig   = CoralFans::getInstance().getConfig().functions.slime;
    std::erase_if(state.particleMap, [&geometryGroup, &slimeConfig](auto& data) {
        if (data.second.second == slimeConfig.runtimeRemoveScale) {
            geometryGroup.remove(data.second.first);
            return true;
        }
        return false;
    });
}
} // namespace coral_fans::functions