#include "MineruleManager.h"

#include "coral_fans/CoralFans.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
//#include "mc/deps/shared_types/v1_26_50/actor/ProjectileComponentDefinition.h"
#include "mc/entity/components_json_legacy/ArrowEffectSubcomponent.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/HurtEffectsSettings.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/projectile/Arrow.h"
#include "mc/world/phys/HitResult.h"
#include "mc/entity/components/ShulkerPeekAmountComponent.h"
#include "mc/world/actor/monster/Shulker.h"


#ifdef LL_PLAT_C

#include "mc/server/ServerInstance.h"
#include <thread>

#endif


#include <format>
#include <vector>
#include <string>


namespace coral_fans::functions {


namespace {

// 伤害原因：3 = 弹射物伤害，14 = 魔法伤害
constexpr int kCauseProjectile = 3;

// 当前正在执行 onHit 的箭矢
struct HitScope {
    int64_t arrowUid         = -1;
    bool    hasTarget        = false;
    bool    projectileDamage = false; // 本次命中中是否出现了弹射物伤害
    bool    kept             = false; // 本次命中中移除被拦截（箭矢保留）
    bool    removed          = false; // 本次命中中箭矢被真正移除
    ProjectileComponent* pc  = nullptr; // 当前箭矢的组件，用于在移除的瞬间修改 shouldBounce
    Actor* target            = nullptr; // 本次命中的目标
    int  prevHitCount = 0;     // 命中前的状态，拦截移除时还原
};

HitScope gScope;


bool onServerThread() {
#ifdef LL_PLAT_C
    auto serverInstance = ll::service::getServerInstance();
    return serverInstance
        && std::this_thread::get_id() == serverInstance->mServerInstanceThread->get_id();
#else
    return true;
#endif
}


int causeOf(ActorDamageSource const& source) {
    SharedTypes::Legacy::ActorDamageCause causeEnum = source.mCause;
    return static_cast<int>(causeEnum);
}


bool isCurrentArrow(Actor& actor) {
    return gScope.arrowUid != -1 && actor.getOrCreateUniqueID().rawID == gScope.arrowUid;
}


// 当前箭矢在本次命中中没有弹射物伤害，且确实命中了目标 -> 保留
bool shouldKeep(Actor& self) {
    if (!isCurrentArrow(self) || !gScope.hasTarget || gScope.projectileDamage) return false;
    gScope.kept = true;
    if (gScope.pc) {
        // 拦截移除的瞬间，把命中前的 hitCount 还原（实验：若弹回判定在移除之后，这里就能让箭弹回）
        gScope.pc->mActorHitCount = gScope.prevHitCount;
    }
    return true;
}


// 把箭矢的弹回 / 同tick多目标标记设置为 26.32 的行为
/*void applyVanillaArrowFlags(ProjectileComponent& pc) {
    pc.mMultipleHits = true;
    pc.mShouldBounce = ::SharedTypes::v1_26_50::ProjectileComponentDefinition::ShouldBounce::IfNoDamageDealt;
}*/


// 判断潜影贝是否完全闭壳
bool isClosedShulker(Actor& target) {
    if (!target.isType(ActorType::Shulker)) return false;
    auto& shulker = static_cast<Shulker&>(target);
    auto& context = shulker.getEntityContext();
    auto component = context.tryGetComponent<ShulkerPeekAmountComponent>();
    if (!component) return false;
    std::uint32_t currentInterpolation =
        component->mPeekAmount.get().mUnk41f2e3.as<std::uint32_t>();
    return currentInterpolation == 0;
}


} // namespace



//hooks
LL_TYPE_INSTANCE_HOOK(
    FuckArrowDisappearOnHit,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    ::Actor&           owner,
    ::HitResult const& hitResult
) {
    if (!onServerThread() || !owner.isType(ActorType::Arrow)) return origin(owner, hitResult);
    mShouldBounce = ::SharedTypes::v1_26_50::ProjectileComponentDefinition::ShouldBounce::IfNoDamageDealt;

    // 命中前的状态：保留箭矢时需要还原，否则 hitCount 会被扣到 0，箭矢之后再也无法弹回
    // 在 FuckArrowDisappearOnHit 钩子内部，origin 调用之前
    int                 prevHitCount = mActorHitCount;
    HitScope prev = gScope;
    gScope        = HitScope{owner.getOrCreateUniqueID().rawID, hitResult.getEntity() != nullptr, false, false, false, this};
    gScope.target       = hitResult.getEntity();
    gScope.prevHitCount = prevHitCount;

    // 特殊实体直接视为弹射物伤害，使箭矢照常被移除
    if (gScope.target
        && (!gScope.target->hasCategory(ActorCategory::Mob)
        || gScope.target->isType(ActorType::ArmorStand)
        || gScope.target->isType(ActorType::TripodCamera))) {
        gScope.projectileDamage = true;
    }
    origin(owner, hitResult);
    if (gScope.kept && !gScope.removed) {
        mActorHitCount              = prevHitCount;
    }
    if (gScope.removed) {
        mShouldBounce = ::SharedTypes::v1_26_50::ProjectileComponentDefinition::ShouldBounce::No;
    }
    gScope = prev;
}


LL_TYPE_INSTANCE_HOOK(
    FuckArrowDisappearInit,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::initFromDefinition,
    void,
    ::Actor& owner
) {
    origin(owner);
    if (owner.isType(ActorType::Arrow)) {
        mMultipleHits = true;
        //mShouldBounce = ::SharedTypes::v1_26_50::ProjectileComponentDefinition::ShouldBounce::IfNoDamageDealt;
    }
}

//Mob::Hurt/Die的hook没有校验箭矢，存在一定的bug隐患，需要玩家实测
LL_TYPE_INSTANCE_HOOK(
    FuckArrowDisappearHurt,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::$hurtEffects,
    void,
    ::ActorDamageSource const&   source,
    float                        damage,
    ::HurtEffectsSettings const& settings
) {
    if (gScope.arrowUid != -1 && causeOf(source) == kCauseProjectile) gScope.projectileDamage = true;
    origin(source, damage, settings);
}


LL_TYPE_INSTANCE_HOOK(
    FuckArrowDisappearDie,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::$die,
    void,
    ::ActorDamageSource const& source
) {
    if (gScope.arrowUid != -1 && causeOf(source) == kCauseProjectile) gScope.projectileDamage = true;
    origin(source);
}


LL_TYPE_INSTANCE_HOOK(
    FuckArrowRemove,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::$remove,
    void
) {
    if (shouldKeep(*this)) return;
    if (isCurrentArrow(*this)) {
        gScope.removed = true;
    }
    origin();
}


LL_TYPE_INSTANCE_HOOK(
    FuckArrowDisappearEffect,
    ll::memory::HookPriority::Normal,
    ArrowEffectSubcomponent,
    &ArrowEffectSubcomponent::$doOnHitEffect,
    void,
    ::Actor&              owner,
    ::ProjectileComponent& component
) {
    if (!onServerThread() || gScope.arrowUid == -1 || !gScope.target || !owner.isType(ActorType::Arrow)
        || owner.getOrCreateUniqueID().rawID != gScope.arrowUid || !gScope.target->isType(ActorType::Shulker)) {
        return origin(owner, component);
    }


    Actor* target = gScope.target;
    if (!isClosedShulker(*target)) {
        return origin(owner, component);
    }
    
    // 闭壳潜影贝：跳过原版的 doOnHitEffect（原版在此施加效果并随即移除箭矢），
    // 箭矢保持弹回路径，效果随后由我们施加。
    Arrow&                         arrow   = static_cast<Arrow&>(owner);
    std::vector<MobEffectInstance> effects = arrow.mMobEffects;
    for (auto const& effect : effects) target->addEffect(effect);
}


void FuckArrowDisappearHook(bool enable) {
    if (enable) {
        FuckArrowDisappearOnHit::hook();
        FuckArrowDisappearInit::hook();
        FuckArrowDisappearHurt::hook();
        FuckArrowDisappearDie::hook();
        FuckArrowRemove::hook();
        FuckArrowDisappearEffect::hook();
    } else {
        FuckArrowDisappearOnHit::unhook();
        FuckArrowDisappearInit::unhook();
        FuckArrowDisappearHurt::unhook();
        FuckArrowDisappearDie::unhook();
        FuckArrowRemove::unhook();
        FuckArrowDisappearEffect::unhook();
        gScope = HitScope{};
    }
}


} // namespace coral_fans::functions
