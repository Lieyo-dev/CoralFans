#pragma once

#include "bsci/GeometryGroup.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/chunk/ChunkBoundingBox.h"
#include "mc/world/level/chunk/ChunkEntity.h"


#include <memory>
#include <unordered_map>
#include <vector>

namespace coral_fans::functions {

struct PairHash {
    std::size_t operator()(const std::pair<ChunkPos, DimensionType>& p) const {
        auto hash1 = std::hash<ChunkPos>{}(p.first);
        auto hash2 = std::hash<DimensionType>{}(p.second);

        return hash1 ^ (hash2 << 1);
    }
};

class HsaManager {
private:
    struct HsaChunkData {
        bsci::GeometryGroup::GeoId hsaGeoId       = {0};
        bsci::GeometryGroup::GeoId structureGeoId = {0};
        bool                       freshed        = true;
    };

    // 每个玩家独立的显示状态：开关、图形数据以及只发送给该玩家的 GeometryGroup
    struct PlayerHsaState {
        std::unique_ptr<bsci::GeometryGroup>                                           group;
        bool                                                                           hsaShow       = false;
        bool                                                                           structureShow = false;
        std::unordered_map<std::pair<ChunkPos, DimensionType>, HsaChunkData, PairHash> chunkDataMap;
    };

private:
    std::unordered_map<mce::UUID, PlayerHsaState> mPlayerStates;

private:
    int tickCounter              = 1;
    int runtimeRemoveTickCounter = 1;

public:
    void setHsaShow(Player&, bool show, bool persist = true);
    void setStructureShow(Player&, bool show, bool persist = true);
    bool getHsaShow(Player const&);
    bool getStructureShow(Player const&);
    // 移除所有玩家的显示（插件禁用/服务器关闭时调用）
    void clear();

public:
    void                  tick();
    std::vector<BlockPos> listChunkHsa(BlockSource&, ChunkPos);
    std::vector<AABB>     listChunkStructure(BlockSource&, ChunkPos);

private:
    PlayerHsaState& getOrCreateState(Player const&);
    void            eraseStateIfUnused(mce::UUID const&);

    void drawChunkHsa(bsci::GeometryGroup&, std::vector<::BlockPos>&, DimensionType, HsaChunkData&);
    void drawChunkStructure(
        bsci::GeometryGroup&,
        ::entt::basic_storage<::br::ChunkBoundingBox, ::br::ChunkEntity>&,
        DimensionType,
        HsaChunkData&,
        ChunkPos
    );
    void draw();
    void drawForPlayer(PlayerHsaState&, Player&);
    void remove(PlayerHsaState&);        // 某个开关关闭后，移除不再需要的图形
    void removeAll(PlayerHsaState&);     // 移除该玩家的全部图形
    void runtimeRemove(PlayerHsaState&); // 移除不再刷新的区块图形

private:
    HsaManager() = default;

public:
    [[nodiscard]] static HsaManager& getInstance() {
        static HsaManager instance;
        return instance;
    }
};
} // namespace coral_fans::functions