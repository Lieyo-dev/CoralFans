#include "coral_fans/functions/hsa/Hsa.h"
#include "coral_fans/CoralFans.h"
#include "coral_fans/base/PlayerShowStore.h"


#include "ll/api/service/Bedrock.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/chunk/ChunkSource.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/LevelChunkVolumeData.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/v2/SpawnerData.h"
#include "mc/world/level/levelgen/v2/StructureSpawnOverride.h"
#include <memory>
#include <vector>


namespace coral_fans::functions {
HsaManager::PlayerHsaState& HsaManager::getOrCreateState(Player const& player) {
    auto [it, inserted] = this->mPlayerStates.try_emplace(player.getUuid());
    if (inserted) it->second.group = bsci::GeometryGroup::createDefault(player.getUuid());
    return it->second;
}

void HsaManager::eraseStateIfUnused(mce::UUID const& uuid) {
    auto it = this->mPlayerStates.find(uuid);
    if (it == this->mPlayerStates.end()) return;
    if (it->second.hsaShow || it->second.structureShow) return;
    this->removeAll(it->second);
    this->mPlayerStates.erase(it);
}

void HsaManager::setHsaShow(Player& player, bool show, bool persist) {
    if (persist) show_store::set(player.getUuid(), "hsa.show", show);
    if (this->getHsaShow(player) == show) return;
    auto& state   = this->getOrCreateState(player);
    state.hsaShow = show;
    if (show) {
        this->tickCounter              = 0;
        this->runtimeRemoveTickCounter = 1;
    } else {
        this->remove(state);
        this->eraseStateIfUnused(player.getUuid());
    }
}

void HsaManager::setStructureShow(Player& player, bool show, bool persist) {
    if (persist) show_store::set(player.getUuid(), "hsa.structure", show);
    if (this->getStructureShow(player) == show) return;
    auto& state         = this->getOrCreateState(player);
    state.structureShow = show;
    if (show) {
        this->tickCounter              = 0;
        this->runtimeRemoveTickCounter = 1;
    } else {
        this->remove(state);
        this->eraseStateIfUnused(player.getUuid());
    }
}

bool HsaManager::getHsaShow(Player const& player) {
    auto it = this->mPlayerStates.find(player.getUuid());
    return it != this->mPlayerStates.end() && it->second.hsaShow;
}

bool HsaManager::getStructureShow(Player const& player) {
    auto it = this->mPlayerStates.find(player.getUuid());
    return it != this->mPlayerStates.end() && it->second.structureShow;
}

void HsaManager::clear() {
    for (auto& [uuid, state] : this->mPlayerStates) this->removeAll(state);
    this->mPlayerStates.clear();
}

void HsaManager::drawChunkHsa(
    bsci::GeometryGroup&     geometryGroup,
    std::vector<::BlockPos>& hsa,
    DimensionType            dim,
    HsaChunkData&            chunkData
) {
    std::unordered_map<BlockPos, int> hsaCount;
    auto&                             hsaConfig = CoralFans::getInstance().getConfig().functions.hsa;
    for (auto& pos : hsa) {
        auto [it, inserted2] = hsaCount.try_emplace(pos);
        if (inserted2) it->second = 1;
        else it->second++;
    }
    std::vector<bsci::GeometryGroup::GeoId> geoIdList;
    geoIdList.reserve(8 * hsaCount.size() + 1);
    for (auto& [pos, count] : hsaCount) {
        geoIdList.emplace_back(
            geometryGroup
                .line(dim, {pos.x, pos.y, pos.z}, {pos.x, pos.y + 1, pos.z}, mce::Color(hsaConfig.hsaNorthWestColor))
        );
        geoIdList.emplace_back(
            geometryGroup
                .line(dim, {pos.x + 1, pos.y, pos.z}, {pos.x + 1, pos.y + 1, pos.z}, mce::Color(hsaConfig.hsaColor))
        );
        geoIdList.emplace_back(
            geometryGroup
                .line(dim, {pos.x, pos.y, pos.z + 1}, {pos.x, pos.y + 1, pos.z + 1}, mce::Color(hsaConfig.hsaColor))
        );
        geoIdList.emplace_back(geometryGroup.line(
            dim,
            {pos.x + 1, pos.y, pos.z + 1},
            {pos.x + 1, pos.y + 1, pos.z + 1},
            mce::Color(hsaConfig.hsaColor)
        ));
        geoIdList.emplace_back(
            geometryGroup
                .line(dim, {pos.x, pos.y + 1, pos.z}, {pos.x + 1, pos.y + 1, pos.z}, mce::Color(hsaConfig.hsaColor))
        );
        geoIdList.emplace_back(
            geometryGroup
                .line(dim, {pos.x, pos.y + 1, pos.z}, {pos.x, pos.y + 1, pos.z + 1}, mce::Color(hsaConfig.hsaColor))
        );
        geoIdList.emplace_back(geometryGroup.line(
            dim,
            {pos.x + 1, pos.y + 1, pos.z},
            {pos.x + 1, pos.y + 1, pos.z + 1},
            mce::Color(hsaConfig.hsaColor)
        ));
        geoIdList.emplace_back(geometryGroup.line(
            dim,
            {pos.x, pos.y + 1, pos.z + 1},
            {pos.x + 1, pos.y + 1, pos.z + 1},
            mce::Color(hsaConfig.hsaColor)
        ));
        if (count > 1) {
            geoIdList.emplace_back(geometryGroup.text(
                dim,
                {pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f},
                "x" + std::to_string(count),
                mce::Color::WHITE(),
                1.0f
            ));
        }
    }
    chunkData.hsaGeoId = geometryGroup.merge(geoIdList);
}

void HsaManager::drawChunkStructure(
    bsci::GeometryGroup& geometryGroup,
    ::entt::basic_storage<::br::ChunkBoundingBox, ::br::ChunkEntity>&
                  chunkBoundingBoxes,
    DimensionType dim,
    HsaChunkData& chunkData,
    ChunkPos      chunkPos
) {
    std::vector<bsci::GeometryGroup::GeoId> geoIdList;
    auto&                                   hsaConfig = CoralFans::getInstance().getConfig().functions.hsa;
    geoIdList.reserve(chunkBoundingBoxes.size());
    for (auto [entity, component] : chunkBoundingBoxes.each()) {
        if (ChunkPos(component.box->min) != chunkPos || ChunkPos(component.box->max) != chunkPos
            || component.box->min.y < -64 || component.box->max.y > 320) {
            continue;
        }
        geoIdList.emplace_back(geometryGroup.box(
            dim,
            AABB(component.box->min, component.box->max + BlockPos(1, 1, 1)),
            mce::Color(hsaConfig.structureColor)
        ));
    }
    chunkData.structureGeoId = geometryGroup.merge(geoIdList);
}

void HsaManager::draw() {
    auto level = ll::service::getLevel();
    if (!level) [[unlikely]]
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

void HsaManager::drawForPlayer(PlayerHsaState& state, Player& player) {
    static int radius          = std::max(0, CoralFans::getInstance().getConfig().functions.hsa.drawRadius);
    ChunkPos   originChunkPos = ChunkPos(player.getFeetBlockPos());
    auto&      dim            = player.getDimension();
    int        dimId          = player.getDimensionId();
    for (int i = -radius; i <= radius; ++i) {
        int maxJ = radius - abs(i);
        for (int j = -maxJ; j <= maxJ; ++j) {
            ChunkPos chunkPos = ChunkPos(originChunkPos.x + i, originChunkPos.z + j);
            auto     chunk    = (*dim.mBlockSource)->getChunk(chunkPos);
            if (chunk && *chunk->mLoadState == ChunkState::Loaded) {
                std::vector<::BlockPos> hsa;
                auto& chunkBoundingBoxes = std::get<1>(*chunk->mLevelChunkVolumeData->mDataRegistry->mData);
                if (state.hsaShow) hsa = chunk->mLevelChunkVolumeData->structureSpawnPos();

                if (hsa.size() || (state.structureShow && chunkBoundingBoxes.size())) {

                    auto [iter, inserted] = state.chunkDataMap.try_emplace(std::make_pair(chunkPos, dimId));
                    if (hsa.size() && !iter->second.hsaGeoId.value)
                        this->drawChunkHsa(*state.group, hsa, dimId, iter->second);
                    if ((hsa.size() || state.structureShow) && chunkBoundingBoxes.size()
                        && !iter->second.structureGeoId.value)
                        this->drawChunkStructure(*state.group, chunkBoundingBoxes, dimId, iter->second, chunkPos);
                    iter->second.freshed = true;
                }
            }
        }
    }
}

void HsaManager::tick() {
    if (this->mPlayerStates.empty()) return;
    if (!this->tickCounter) {
        this->draw();
        if (!this->runtimeRemoveTickCounter) {
            for (auto& [uuid, state] : this->mPlayerStates) this->runtimeRemove(state);
        }
        static int removeInterval = std::max(1, CoralFans::getInstance().getConfig().functions.hsa.runtimeRemoveScale);
        this->runtimeRemoveTickCounter = (this->runtimeRemoveTickCounter + 1) % removeInterval;
    }
    static int interval = std::max(1, CoralFans::getInstance().getConfig().functions.hsa.drawInterval);
    this->tickCounter   = (this->tickCounter + 1) % interval;
}

void HsaManager::removeAll(PlayerHsaState& state) {
    if (!state.group) return;
    for (auto& [_, chunkData] : state.chunkDataMap) {
        if (chunkData.hsaGeoId.value) state.group->remove(chunkData.hsaGeoId);
        if (chunkData.structureGeoId.value) state.group->remove(chunkData.structureGeoId);
    }
    state.chunkDataMap.clear();
}

void HsaManager::remove(PlayerHsaState& state) {
    auto& geometryGroup = *state.group;
    if (!state.hsaShow) {
        if (!state.structureShow) {
            this->removeAll(state);
        } else {
            std::erase_if(state.chunkDataMap, [&geometryGroup](auto& data) {
                if (data.second.hsaGeoId.value) {
                    geometryGroup.remove(data.second.hsaGeoId);
                    data.second.hsaGeoId.value = 0;
                }
                return !data.second.structureGeoId.value;
            });
        }
    } else {
        std::erase_if(state.chunkDataMap, [&geometryGroup](auto& data) {
            if (!data.second.hsaGeoId.value) {
                geometryGroup.remove(data.second.structureGeoId);
                return true;
            }
            return false;
        });
    }
}

void HsaManager::runtimeRemove(PlayerHsaState& state) {
    auto& geometryGroup = *state.group;
    std::erase_if(state.chunkDataMap, [&geometryGroup](auto& data) {
        if (data.second.freshed) {
            data.second.freshed = false;
            return false;
        }
        if (data.second.hsaGeoId.value) geometryGroup.remove(data.second.hsaGeoId);
        if (data.second.structureGeoId.value) geometryGroup.remove(data.second.structureGeoId);
        return true;
    });
}

std::vector<BlockPos> HsaManager::listChunkHsa(BlockSource& region, ChunkPos chunkPos) {
    auto chunk = region.getChunk(chunkPos);
    if (chunk && *chunk->mLoadState == ChunkState::Loaded) {
        return chunk->mLevelChunkVolumeData->structureSpawnPos();
    }
    return {};
}

std::vector<AABB> HsaManager::listChunkStructure(BlockSource& region, ChunkPos chunkPos) {
    auto chunk = region.getChunk(chunkPos);
    if (chunk && *chunk->mLoadState == ChunkState::Loaded) {
        std::vector<AABB> result;
        auto&             chunkBoundingBoxes = std::get<1>(*chunk->mLevelChunkVolumeData->mDataRegistry->mData);
        result.reserve(chunkBoundingBoxes.size());
        for (auto [entity, component] : chunkBoundingBoxes.each()) {
            if (ChunkPos(component.box->min) != chunkPos || ChunkPos(component.box->max) != chunkPos
                || component.box->min.y < -64 || component.box->max.y > 320) {
                continue;
            }
            result.emplace_back(AABB(component.box->min, component.box->max + BlockPos(1, 1, 1)));
        }
        return result;
    }
    return {};
}
} // namespace coral_fans::functions