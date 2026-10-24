#pragma once

#include "bsci/GeometryGroup.h"

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/ChunkPos.h"

#include <memory>
#include <unordered_map>

namespace coral_fans::functions {

class SlimeManager {
private:
    // 每个玩家独立的显示状态：图形数据以及只发送给该玩家的 GeometryGroup
    struct PlayerSlimeState {
        std::unique_ptr<bsci::GeometryGroup>                                     group;
        std::unordered_map<ChunkPos, std::pair<bsci::GeometryGroup::GeoId, int>> particleMap;
    };

private:
    std::unordered_map<mce::UUID, PlayerSlimeState> mPlayerStates;

    int tickCounter              = 0;
    int runtimeRemoveTickCounter = 1;

public:
    void setShow(Player&, bool, bool persist = true);
    bool getShow(Player const&);
    // 移除所有玩家的显示（插件禁用/服务器关闭时调用）
    void clear();

public:
    void tick();

private:
    void draw();
    void drawForPlayer(PlayerSlimeState&, Player&);
    void removeAll(PlayerSlimeState&);
    void runtimeRemove(PlayerSlimeState&);

private:
    SlimeManager() = default;

public:
    [[nodiscard]] static SlimeManager& getInstance() {
        static SlimeManager instance;
        return instance;
    }
};

} // namespace coral_fans::functions