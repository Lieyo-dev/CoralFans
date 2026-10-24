// from https://github.com/GroupMountain/FreeCamera
#pragma once

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
namespace coral_fans::functions {
class FreeCameraManager {
private:
    FreeCameraManager() = default;

public:
    // “灵魂”（客户端侧的自由视角）的状态。freecam 期间服务端会丢弃该玩家的 PlayerAuthInputPacket，
    // 因此“肉身”的位置/朝向不会变化，这里在丢弃前把灵魂的位置/朝向记录下来。
    struct SoulState {
        Vec3 pos;      // 与 Actor::getPosition() 同一坐标系（玩家为眼睛位置）
        Vec3 posDelta; // 灵魂的速度
        Vec2 rot;      // 俯仰/偏航，与 Actor::getRotation() 一致
    };

    std::unordered_set<std::string>             FreeCamList;  // 玩家 UUID 字符串
    std::unordered_map<std::string, SoulState> SoulStates;   // 玩家 UUID 字符串 -> 灵魂状态
    [[nodiscard]] static FreeCameraManager& getInstance() {
        static FreeCameraManager instance;
        return instance;
    }
    static void DisableFreeCamera(Player* pl);
    static void EnableFreeCamera(Player* pl);
    static void freecameraHook(bool enable);
};

// RAII：若玩家正处于 freecam，则在作用域内临时把其“肉身”的位置/速度/朝向替换为“灵魂”的，
// 这样 traceRay、getPosition、getViewVector、getPosDelta 等基于玩家状态的函数得到的都是灵魂视角的结果。
// 析构时恢复。玩家不在 freecam 时什么都不做。只在服务器主线程、同步使用，不要跨 tick 持有。
class SoulViewGuard {
public:
    explicit SoulViewGuard(Player& player);
    ~SoulViewGuard();
    SoulViewGuard(SoulViewGuard const&)            = delete;
    SoulViewGuard& operator=(SoulViewGuard const&) = delete;

private:
    Player* mPlayer = nullptr; // 非空表示已替换，需要在析构时恢复
    Vec3    mOldPos, mOldPosPrev, mOldPosDelta;
    Vec2    mOldRot, mOldRotPrev;
};
} // namespace coral_fans::functions