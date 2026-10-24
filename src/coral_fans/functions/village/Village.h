#pragma once

#include "bsci/GeometryGroup.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/ai/village/Village.h"


#include "mc/world/actor/player/Player.h"

#include <array>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>


namespace coral_fans::functions {


enum class VillageShowType : size_t {
    Bounds     = 0,
    RaidBounds = 1,
    IronSpawn  = 2,
    Center     = 3,
    PoiQuery   = 4,
    Bind       = 5,
};
constexpr size_t villageShowTypeCount = 6;

// 持久化使用的键名
constexpr char const* villageShowKey(VillageShowType type) {
    switch (type) {
    case VillageShowType::Bounds:
        return "village.bounds";
    case VillageShowType::RaidBounds:
        return "village.raid";
    case VillageShowType::IronSpawn:
        return "village.ironSpawn";
    case VillageShowType::Center:
        return "village.center";
    case VillageShowType::PoiQuery:
        return "village.poi";
    case VillageShowType::Bind:
        return "village.bind";
    }
    return "village.unknown";
}

class CFTickingVillageData {
public:
    using GeoIds = std::array<bsci::GeometryGroup::GeoId, villageShowTypeCount>;

public:
    std::weak_ptr<Village> mVillagePtr;
    int                    mDimId = -1;
    Tick                   mLastTick;
    AABB                   mBounds;
    AABB                   mRaidBounds;
    // 每个玩家各自持有一份图形 id（图形只发送给对应的玩家）
    std::unordered_map<mce::UUID, GeoIds> mGeoIds;

public:
    CFTickingVillageData(const Village&, Tick&);

public:
    // 为 viewer 绘制（会先移除该玩家已有的同类图形）
    void show(VillageShowType, mce::UUID const& viewer, bsci::GeometryGroup& group);
    // 移除 viewer 的某类图形
    void hide(VillageShowType, mce::UUID const& viewer, bsci::GeometryGroup& group);
    // 移除 viewer 的全部图形
    void hideAll(mce::UUID const& viewer, bsci::GeometryGroup& group);
};

class CFVillageManager {
private:
    std::vector<std::optional<std::weak_ptr<Village>>>                   mVillageList;
    std::unordered_map<mce::UUID, std::unique_ptr<CFTickingVillageData>> mTickingList;

public:
    bool                     mayCreatingVillage = false;
    std::optional<mce::UUID> newVillageId       = std::nullopt;

private:
    // 每个玩家独立的显示状态：开关以及只发送给该玩家的 GeometryGroup
    struct PlayerShowState {
        std::unique_ptr<bsci::GeometryGroup>      group;
        std::array<bool, villageShowTypeCount>    show{};
        [[nodiscard]] bool                        any() const {
            for (bool b : show)
                if (b) return true;
            return false;
        }
    };
    std::unordered_map<mce::UUID, PlayerShowState> mPlayerStates;

private:
    void removeOfflinePlayers();

public:
    void setShow(Player&, VillageShowType, bool, bool persist = true);
    bool getShow(Player const&, VillageShowType);

public:
    void                         addVillage(std::weak_ptr<Village>);
    void                         handleVillageTick(const Village&, Tick&);
    void                         tick(const Tick&);
    std::vector<std::string>     listVillages();
    std::string                  listTickingVillages();
    std::pair<std::string, bool> getVillageInfo(int);
    int                          getVillageId(std::weak_ptr<Village>);

    std::pair<std::string, bool> getVillagerInfo(ActorUniqueID);
    void                         clear();

private:
    CFVillageManager() = default;

public:
    [[nodiscard]] static CFVillageManager& getInstance() {
        static CFVillageManager instance;
        return instance;
    }
    static void hookVillage(bool);

    [[nodiscard]] static int getBedPOICount(std::shared_ptr<Village> villagePtr);
};
} // namespace coral_fans::functions