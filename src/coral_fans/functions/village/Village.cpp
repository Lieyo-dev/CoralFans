#include "coral_fans/functions/village/Village.h"
#include "Village.h"
#include "coral_fans/CoralFans.h"
#include "coral_fans/base/PlayerShowStore.h"
#include "coral_fans/base/Macros.h"


#include "ll/api/i18n/I18n.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/ai/village/POIInstance.h"
#include "mc/world/actor/ai/village/Village.h"
#include "mc/world/actor/ai/village/VillageManager.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/phys/AABB.h"


#include <cstddef>
#include <math.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace coral_fans::functions {
float getApproximateRadius(AABB& bound) {
    float x = bound.max.x - bound.min.x;
    float z = bound.max.z - bound.min.z;
    if (fabs(x - z) >= 0.00000011920929f) return (x + z) * 0.3f;
    else return 1.414213562373f * x;
}

CFTickingVillageData::CFTickingVillageData(const Village& village, Tick& tick) {
    if (auto level = ll::service::getLevel()) [[likely]] {
        if (auto dim = level->getDimension(village.mDimension.getDimensionId()).lock()) [[likely]] {
            mVillagePtr = dim->mVillageManager->getVillageByID(village.mUniqueID);
        }
    }
    mDimId      = village.mDimension.getDimensionId();
    mLastTick   = tick;
    mBounds     = village.mBounds;
    mRaidBounds = village.mStaticRaidBounds;
}

void CFTickingVillageData::show(VillageShowType type, mce::UUID const& viewer, bsci::GeometryGroup& group) {
    this->hide(type, viewer, group);
    auto&       mod    = CoralFans::getInstance();
    auto const& config = mod.getConfig().functions.village;
    auto&       geoId  = this->mGeoIds[viewer][static_cast<size_t>(type)];
    switch (type) {
    case VillageShowType::Bounds:
        geoId = group.box(mDimId, mBounds, mce::Color(config.boundsColor));
        break;
    case VillageShowType::RaidBounds:
        geoId = group.box(mDimId, mRaidBounds, mce::Color(config.raidBoundsColor));
        break;
    case VillageShowType::IronSpawn:
        geoId = group.box(
            mDimId,
            {
                mBounds.center() - Vec3{8, 6, 8},
                mBounds.center() + Vec3{9, 7, 9}
        },
            mce::Color(config.ironSpawnBoundsColor)
        );
        break;
    case VillageShowType::Center:
        geoId = group.box(
            mDimId,
            {
                mBounds.center(),
                mBounds.center() + Vec3{1, 1, 1}
        },
            mce::Color(config.centerColor)
        );
        break;
    case VillageShowType::PoiQuery:
        geoId = group.box(
            mDimId,
            {
                mBounds.min - Vec3{64, 64, 64},
                mBounds.max + Vec3{64, 64, 64}
        },
            mce::Color(config.poiBoundsColor)
        );
        break;
    case VillageShowType::Bind: {
        auto level = ll::service::getLevel();
        if (!level) break;
        auto villagePtr = mVillagePtr.lock();
        if (!villagePtr) [[unlikely]]
            break;
        std::vector<bsci::GeometryGroup::GeoId> bindIds;
        const mce::Color                        colors[3] = {
            mce::Color(config.bedBindColor),
            mce::Color(config.ringBindColor),
            mce::Color(config.workBindColor)
        };
        bindIds.reserve(3 * villagePtr->mClaimedPOIs->size());
        for (auto& [actorUniqueId, poiArray] : *villagePtr->mClaimedPOIs) {
            auto villager = level->fetchEntity(actorUniqueId, false);
            if (!villager) continue;
            for (int i = 0; i < 3; i++) {
                if (auto poi = poiArray[i].lock()) {
                    bindIds.emplace_back(group.line(
                        villager->getDimensionId(),
                        villager->getHeadPos(),
                        poi->mPosition->center(),
                        colors[i]
                    ));
                }
            }
        }
        geoId = group.merge(bindIds);
        break;
    }
    default:
        break;
    }
}

void CFTickingVillageData::hide(VillageShowType type, mce::UUID const& viewer, bsci::GeometryGroup& group) {
    auto it = this->mGeoIds.find(viewer);
    if (it == this->mGeoIds.end()) return;
    auto& geoId = it->second[static_cast<size_t>(type)];
    if (geoId.value) group.remove(geoId);
    geoId = {0};
}

void CFTickingVillageData::hideAll(mce::UUID const& viewer, bsci::GeometryGroup& group) {
    auto it = this->mGeoIds.find(viewer);
    if (it == this->mGeoIds.end()) return;
    for (auto& geoId : it->second)
        if (geoId.value) group.remove(geoId);
    this->mGeoIds.erase(it);
}

int CFVillageManager::getVillageId(std::weak_ptr<Village> villagePtr) {
    if (auto village = villagePtr.lock()) [[likely]] {
        auto size = mVillageList.size();
        for (size_t i = 0; i < size; i++) {
            if (mVillageList[i]) {
                if (auto lockedVillage = mVillageList[i].value().lock()) {
                    if (lockedVillage == village) return static_cast<int>(i);
                } else mVillageList[i] = std::nullopt;
            }
        }
    }
    return -1;
}

void CFVillageManager::addVillage(std::weak_ptr<Village> villagePtr) { mVillageList.emplace_back(villagePtr); }

void CFVillageManager::handleVillageTick(const Village& village, Tick& tick) {
    auto [it, isInserted] = this->mTickingList.try_emplace(village.mUniqueID);
    if (isInserted) {
        it->second = std::make_unique<CFTickingVillageData>(village, tick);
        for (auto& [uuid, state] : this->mPlayerStates) {
            for (size_t i = 0; i < villageShowTypeCount; i++) {
                if (state.show[i]) it->second->show(static_cast<VillageShowType>(i), uuid, *state.group);
            }
        }
    } else {
        it->second->mLastTick = tick;
    }
}

void CFVillageManager::removeOfflinePlayers() {
    auto level = ll::service::getLevel();
    if (!level) [[unlikely]]
        return;
    for (auto it = this->mPlayerStates.begin(); it != this->mPlayerStates.end();) {
        if (!level->getPlayer(it->first)) {
            for (auto& [villageUuid, villageData] : this->mTickingList) villageData->hideAll(it->first, *it->second.group);
            it = this->mPlayerStates.erase(it);
        } else ++it;
    }
}

void CFVillageManager::tick(const Tick& currentTick) {
    auto&      mod = CoralFans::getInstance();
    static int gt  = 0;
    if (!gt) this->removeOfflinePlayers();

    // 对所有开启了 type 的玩家重新绘制 villageData 的 type 类型图形
    auto reshow = [this](CFTickingVillageData& villageData, VillageShowType type) {
        for (auto& [uuid, state] : this->mPlayerStates)
            if (state.show[static_cast<size_t>(type)]) villageData.show(type, uuid, *state.group);
    };
    auto hideAll = [this](CFTickingVillageData& villageData) {
        for (auto& [uuid, state] : this->mPlayerStates) villageData.hideAll(uuid, *state.group);
    };

    std::erase_if(
        mTickingList,
        [this, &currentTick, &reshow, &hideAll](std::pair<const mce::UUID, std::unique_ptr<CFTickingVillageData>>& item) {
            auto& [uuid, villageData] = item;
            if (currentTick.tickID - villageData->mLastTick.tickID > 5) {
                hideAll(*villageData);
                return true;
            }
            auto village = villageData->mVillagePtr.lock();
            if (!village) [[unlikely]] {
                hideAll(*villageData);
                return true;
            }
            if (villageData->mBounds != village->mBounds) {
                villageData->mBounds = village->mBounds;
                reshow(*villageData, VillageShowType::Bounds);
                reshow(*villageData, VillageShowType::IronSpawn);
                reshow(*villageData, VillageShowType::Center);
                reshow(*villageData, VillageShowType::PoiQuery);
            }
            if (villageData->mRaidBounds != village->mStaticRaidBounds) {
                villageData->mRaidBounds = village->mStaticRaidBounds;
                reshow(*villageData, VillageShowType::RaidBounds);
            }
            if (!gt) reshow(*villageData, VillageShowType::Bind);
            return false;
        }
    );
    const static int interval = std::max(1, mod.getConfig().functions.village.drawInterval);
    gt                        = (gt + 1) % interval;
}

void CFVillageManager::setShow(Player& player, VillageShowType type, bool show, bool persist) {
    if (persist) show_store::set(player.getUuid(), villageShowKey(type), show);
    if (this->getShow(player, type) == show) return;
    auto const& uuid = player.getUuid();
    auto [it, inserted] = this->mPlayerStates.try_emplace(uuid);
    auto& state         = it->second;
    if (inserted) state.group = bsci::GeometryGroup::createDefault(uuid);
    state.show[static_cast<size_t>(type)] = show;
    if (show) {
        for (auto& [villageUuid, villageData] : this->mTickingList) villageData->show(type, uuid, *state.group);
    } else {
        for (auto& [villageUuid, villageData] : this->mTickingList) villageData->hide(type, uuid, *state.group);
        if (!state.any()) {
            for (auto& [villageUuid, villageData] : this->mTickingList) villageData->hideAll(uuid, *state.group);
            this->mPlayerStates.erase(it);
        }
    }
}

bool CFVillageManager::getShow(Player const& player, VillageShowType type) {
    auto it = this->mPlayerStates.find(player.getUuid());
    return it != this->mPlayerStates.end() && it->second.show[static_cast<size_t>(type)];
}

int CFVillageManager::getBedPOICount(std::shared_ptr<Village> villagePtr) {
    if (!villagePtr) return 0;

    int count = 0;
    for (auto& [villagerId, poiList] : *villagePtr->mClaimedPOIs) {
        for (auto& poi : poiList) {
            auto poiPtr = poi.lock();
            if (poiPtr && poiPtr->mType == POIType::Bed) {
                count++;
            }
        }
    }
    for (auto& poiStack : *villagePtr->mUnclaimedPOIStacks) {
        for (auto& poi : poiStack) {
            auto poiPtr = poi.lock();
            if (poiPtr && poiPtr->mType == POIType::Bed) {
                count++;
            }
        }
    }
    return count;
}

std::vector<std::string> CFVillageManager::listVillages() {
    using ll::i18n_literals::operator""_tr;
    size_t                   size = mVillageList.size();
    std::vector<std::string> retstrs;
    for (size_t i = 0; i < size; i++) {
        std::string retstr = "";
        if (!mVillageList[i]) retstr += "translate.village.villageHasBeenRemoved"_tr(i);
        else {
            auto villagePtr = mVillageList[i].value().lock();
            if (!villagePtr) {
                retstr          += "translate.village.villageHasBeenRemoved"_tr(i);
                mVillageList[i]  = std::nullopt;
                continue;
            }
            std::string dimensionText = "";
            switch (villagePtr->mDimension.getDimensionId()) {
            case 0:
                dimensionText = "translate.dimension.overworld"_tr();
                break;
            case 1:
                dimensionText = "translate.dimension.nether"_tr();
                break;
            case 2:
                dimensionText = "translate.dimension.the_end"_tr();
                break;
            default:
                dimensionText = "translate.dimension.unknown"_tr();
            }
            retstr += "translate.village.villageInfo"_tr(
                i,
                villagePtr->mBounds->center().toString(),
                dimensionText,
                getApproximateRadius(*villagePtr->mBounds),
                (*villagePtr->mDwellers)[0].size(), // Villager
                (*villagePtr->mDwellers)[1].size(), // IronGolem
                CFVillageManager::getBedPOICount(villagePtr),
                villagePtr->mBounds->min.toString(),
                villagePtr->mBounds->max.toString()
            );
        }
        retstrs.emplace_back(std::move(retstr));
    }
    return retstrs;
}

std::string CFVillageManager::listTickingVillages() {
    using ll::i18n_literals::operator""_tr;
    std::string retstr = "";
    for (auto& [uuid, villageData] : this->mTickingList) {
        if (auto villagePtr = villageData->mVillagePtr.lock()) {
            size_t      id            = this->getVillageId(villagePtr);
            std::string dimensionText = "";
            switch (villagePtr->mDimension.getDimensionId()) {
            case 0:
                dimensionText = "translate.dimension.overworld"_tr();
                break;
            case 1:
                dimensionText = "translate.dimension.nether"_tr();
                break;
            case 2:
                dimensionText = "translate.dimension.the_end"_tr();
                break;
            default:
                dimensionText = "translate.dimension.unknown"_tr();
            }
            retstr += "translate.village.villageInfo"_tr(
                id,
                villagePtr->mBounds->center().toString(),
                dimensionText,
                getApproximateRadius(*villagePtr->mBounds),
                (*villagePtr->mDwellers)[0].size(), // Villager
                (*villagePtr->mDwellers)[1].size(), // IronGolem
                CFVillageManager::getBedPOICount(villagePtr),
                villagePtr->mBounds->min.toString(),
                villagePtr->mBounds->max.toString()
            );
        }
    }
    return retstr;
}

std::string getPoiInfo(std::array<::std::weak_ptr<::POIInstance>, 3>& poiList) {
    using ll::i18n_literals::operator""_tr;
    std::string retstr  = "";
    retstr             += "translate.village.villagerPOIType1"_tr();
    auto poi1           = poiList[0].lock();
    if (poi1)
        retstr += "translate.village.villagerPOIInfo"_tr(
            *poi1->mPosition,
            poi1->mOwnerCount,
            poi1->mOwnerCapacity,
            poi1->mRadius,
            poi1->mWeight
        );
    else retstr += " §7(x)§r\n";
    retstr    += "translate.village.villagerPOIType2"_tr();
    auto poi2  = poiList[1].lock();
    if (poi2)
        retstr += "translate.village.villagerPOIInfo"_tr(
            *poi2->mPosition,
            poi2->mOwnerCount,
            poi2->mOwnerCapacity,
            poi2->mRadius,
            poi2->mWeight
        );
    else retstr += " §7(x)§r\n";
    retstr    += "translate.village.villagerPOIType3"_tr();
    auto poi3  = poiList[2].lock();
    if (poi3)
        retstr += "translate.village.villagerPOIInfo"_tr(
            *poi3->mPosition,
            poi3->mOwnerCount,
            poi3->mOwnerCapacity,
            poi3->mRadius,
            poi3->mWeight
        );
    else retstr += " §7(x)§r\n";
    return retstr;
}

std::pair<std::string, bool> CFVillageManager::getVillageInfo(int id) {
    using ll::i18n_literals::operator""_tr;
    if (id >= static_cast<int>(mVillageList.size())) return {"translate.village.notExist"_tr(), false};
    if (!mVillageList[id]) return {"translate.village.villageHasBeenRemoved"_tr(id), false};
    auto village = mVillageList[id].value().lock();
    if (!village) {
        mVillageList[id] = std::nullopt;
        return {"translate.village.villageHasBeenRemoved"_tr(id), false};
    }

    std::string dimensionText = "";
    switch (village->mDimension.getDimensionId()) {
    case 0:
        dimensionText = "translate.dimension.overworld"_tr();
        break;
    case 1:
        dimensionText = "translate.dimension.nether"_tr();
        break;
    case 2:
        dimensionText = "translate.dimension.the_end"_tr();
        break;
    default:
        dimensionText = "translate.dimension.unknown"_tr();
    }

    std::string retstr = "translate.village.info"_tr(
        id,
        village->mUniqueID->asString(),
        village->mBounds->center().toString(),
        dimensionText,
        village->mBounds->min.toString(),
        village->mBounds->max.toString(),
        getApproximateRadius(*village->mBounds),
        (*village->mDwellers)[0].size(),
        (*village->mDwellers)[1].size(),
        (*village->mDwellers)[2].size(),
        (*village->mDwellers)[3].size()
    );
    if (auto level = ll::service::getLevel()) {
        int i = 0;
        for (auto& [auid, poiList] : *village->mClaimedPOIs) {
            i++;
            retstr += "translate.village.villagerInfo"_tr(i, level->fetchEntity(auid, true)->getFeetPos().toString());
            retstr += getPoiInfo(poiList);
        }
    }
    return {retstr, true};
}

std::pair<std::string, bool> CFVillageManager::getVillagerInfo(ActorUniqueID auid) {
    using ll::i18n_literals::operator""_tr;
    for (auto& [uuid, villageData] : this->mTickingList) {
        auto village = villageData->mVillagePtr.lock();
        if (!village) continue;
        auto it = village->mClaimedPOIs->find(auid);
        if (it != village->mClaimedPOIs->end()) {
            std::string retstr = "";
            if (auto level = ll::service::getLevel()) {
                retstr = "translate.village.villagerInfo2"_tr(
                    this->getVillageId(villageData->mVillagePtr),
                    level->fetchEntity(auid, false)->getFeetPos().toString()
                );
                retstr += getPoiInfo(it->second);
            }
            return {retstr, true};
        }
    }
    return {"translate.village.cannotgetvillager"_tr(), false};
}

LL_TYPE_INSTANCE_HOOK(
    CoralFansVillageAddHook,
    ll::memory::HookPriority::Normal,
    Dimension,
    &Dimension ::$init,
    void,
    ::br::worldgen::StructureSetRegistry const& structureSetRegistry
) {
    origin(structureSetRegistry);
    if (mVillageManager) {
        for (auto& [uuid, village] : *mVillageManager->mVillages) {
            CFVillageManager::getInstance().addVillage(village);
        }
    }
}

LL_TYPE_INSTANCE_HOOK(
    CoralFansVillageAddHook2,
    ll::memory::HookPriority::Normal,
    VillageManager,
    &VillageManager ::_tryAssignPOIOrCreateVillage,
    void,
    ::std::shared_ptr<::POIInstance>&& pi
) {
    RETURN_IF_NOT_MAIN_THREAD(origin(std::move(pi)));
    auto& manager              = CFVillageManager::getInstance();
    manager.newVillageId       = std::nullopt;
    manager.mayCreatingVillage = true;
    origin(std::move(pi));
    manager.mayCreatingVillage = false;
    if (manager.newVillageId) {
        auto newVillage = getVillageByID(*manager.newVillageId);
        if (!newVillage.expired()) {
            manager.addVillage(newVillage);
        }
    }
}

LL_TYPE_INSTANCE_HOOK(
    CoralFansVillageAddHook3,
    ll::memory::HookPriority::Normal,
    Village,
    &Village::$ctor,
    void*,
    ::Dimension&      dimension,
    ::mce::UUID       id,
    ::BlockPos const& _origin
) {
    RETURN_IF_NOT_MAIN_THREAD(origin(dimension, id, _origin));
    auto  ori     = origin(dimension, id, _origin);
    auto& manager = CFVillageManager::getInstance();
    if (manager.mayCreatingVillage) manager.newVillageId = id;
    return ori;
}

LL_TYPE_INSTANCE_HOOK(
    CoralFansVillageTickHook,
    ll::memory::HookPriority::Normal,
    Village,
    &Village::tick,
    void,
    Tick         tick,
    BlockSource& region
) {
    RETURN_IF_NOT_MAIN_THREAD(origin(tick, region));
    CFVillageManager::getInstance().handleVillageTick(*this, tick);
    origin(tick, region);
}

void CFVillageManager::hookVillage(bool hook) {
    if (hook) {
        CoralFansVillageAddHook::hook();
        CoralFansVillageAddHook2::hook();
        CoralFansVillageAddHook3::hook();
        CoralFansVillageTickHook::hook();
    } else {
        CoralFansVillageAddHook::unhook();
        CoralFansVillageAddHook2::unhook();
        CoralFansVillageAddHook3::unhook();
        CoralFansVillageTickHook::unhook();
    }
}

void CFVillageManager::clear() {
    for (auto& [uuid, state] : mPlayerStates)
        for (auto& [villageUuid, villageData] : mTickingList) villageData->hideAll(uuid, *state.group);
    mPlayerStates.clear();
    mVillageList.clear();
    mTickingList.clear();
}
} // namespace coral_fans::functions