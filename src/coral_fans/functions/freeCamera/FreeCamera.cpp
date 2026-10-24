// from https://github.com/GroupMountain/FreeCamera
#include "FreeCamera.h"
#include "coral_fans/base/Macros.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/deps/vanilla_components/StateVectorComponent.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/AddPlayerPacket.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/network/packet/PlayerSkinPacket.h"
#include "mc/network/packet/RemoveActorPacket.h"
#include "mc/network/packet/UpdateAbilitiesPacket.h"
#include "mc/network/packet/UpdatePlayerGameTypePacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/Actor.h "
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"


PlayerInputTick::PlayerInputTick() = default;

namespace coral_fans::functions {

void EnableFreeCameraPacket(Player* pl) {
    auto pkt            = UpdatePlayerGameTypePacket();
    pkt.mPlayerGameType = GameType::Spectator;
    pkt.mTargetPlayer   = pl->getOrCreateUniqueID();
    pkt.mTick->mValue   = ll::service::getLevel()->getCurrentTick().tickID;
    pkt.sendTo(*pl);
    // ((gmlib::world::actor::GMPlayer*)pl)->setClientGamemode(GameType::Spectator);
}

void SendFakePlayerPacket(Player* pl) {
    // Client Player
    auto packet = pl->tryCreateAddActorPacket();
    if (!packet || packet->getId() != MinecraftPacketIds::AddPlayer) return;

    auto& pkt1                                 = static_cast<AddPlayerPacket&>(*packet);
    pkt1.mAbilitiesData->mTargetPlayer->rawID += 114514;
    auto randomUuid                            = mce::UUID::random();
    pkt1.mUuid                                 = randomUuid;
    pl->sendNetworkPacket(pkt1);
    // Update Skin

    auto skinPkt                  = PlayerSkinPacket();
    skinPkt.mUUID                 = randomUuid;
    skinPkt.mSkin                 = *pl->mSkin;
    skinPkt.mLocalizedNewSkinName = "";
    skinPkt.mLocalizedOldSkinName = "";
    skinPkt.sendTo(*pl);
}

void DisableFreeCameraPacket(Player* pl) {
    auto pkt            = UpdatePlayerGameTypePacket();
    pkt.mPlayerGameType = pl->getPlayerGameType();
    pkt.mTargetPlayer   = pl->getOrCreateUniqueID();
    pkt.mTick->mValue   = ll::service::getLevel()->getCurrentTick().tickID;
    pkt.sendTo(*pl);
    // ((gmlib::world::actor::GMPlayer*)pl)->setClientGamemode(pl->getPlayerGameType());
    auto uniqueId  = pl->getOrCreateUniqueID();
    uniqueId.rawID = uniqueId.rawID + 114514;
    // RemoveActorPacket(uniqueId).sendTo(*pl);
    auto pkt2      = RemoveActorPacket();
    pkt2.mEntityId = uniqueId;
    pkt2.sendTo(*pl);
    UpdateAbilitiesPacket(pl->getOrCreateUniqueID(), pl->getAbilities()).sendTo(*pl);
}

/*
void SendActorLinkPacket(Player* pl) {
    auto links = pl->getLinks();
    for (auto& link : links) {
        GMLIB_BinaryStream bs;
        if (ll::service::getLevel()->getPlayer(link.A)) {
            bs.writeVarInt64(link.A.id + 114514);
        } else {
            bs.writeVarInt64(link.A.id);
        }
        if (ll::service::getLevel()->getPlayer(link.B)) {
            bs.writeVarInt64(link.B.id + 114514);
        } else {
            bs.writeVarInt64(link.B.id);
        }
        bs.writeUnsignedChar((uchar)link.type);
        bs.writeBool(link.mImmediate);
        bs.writeBool(link.mPassengerInitiated);
        GMLIB::Server::NetworkPacket<(int)MinecraftPacketIds::SetActorLink> pkt(bs.getAndReleaseData());
        pl->sendNetworkPacket(pkt);
    }
}
*/

void FreeCameraManager::EnableFreeCamera(Player* pl) {
    auto& mgr = FreeCameraManager::getInstance();
    mgr.FreeCamList.insert(pl->getUuid().asString());
    // 灵魂的初始状态 = 当前肉身状态，之后由 PlayerAuthInputPacket 持续更新
    mgr.SoulStates[pl->getUuid().asString()] = {pl->getPosition(), Vec3::ZERO(), pl->getRotation()};
    EnableFreeCameraPacket(pl);
    SendFakePlayerPacket(pl);
    // SendActorLinkPacket(pl);
}

void FreeCameraManager::DisableFreeCamera(Player* pl) {
    auto pos   = pl->getFeetPos();
    auto dimid = pl->getDimensionId();
    // auto links = pl->getLinks();
    FreeCameraManager::getInstance().FreeCamList.erase(pl->getUuid().asString());
    FreeCameraManager::getInstance().SoulStates.erase(pl->getUuid().asString());
    DisableFreeCameraPacket(pl);
    pl->teleport(pos, dimid);
    // for (auto& link : links) {
    //     auto ride  = ll::service::getLevel()->fetchEntity(link.A);
    //     auto rider = ll::service::getLevel()->fetchEntity(link.B);
    //     if (ride && rider) {
    //        rider->startRiding(*ride);
    //    }
    //}
}

LL_TYPE_INSTANCE_HOOK(
    ServerPlayerMoveHandleEvent,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&     source,
    PlayerAuthInputPacket const& packet
) {
    RETURN_IF_NOT_MAIN_THREAD(origin(source, packet));
    auto player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId);
    auto& mgr  = FreeCameraManager::getInstance();
    auto  uuid = player->getUuid().asString();
    if (!mgr.FreeCamList.contains(uuid)) [[likely]] {
        origin(source, packet);
    } else {
        // freecam 中：丢弃该包（肉身不动），但记录灵魂的位置/朝向供 HUD 等使用
        auto& soul    = mgr.SoulStates[uuid];
        soul.pos      = *packet.mPos;
        soul.posDelta = *packet.mPosDelta;
        soul.rot      = *packet.mRot;
    }
}

LL_TYPE_INSTANCE_HOOK(
    PlayerGamemodeChangeEvent,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::$setPlayerGameType,
    void,
    ::GameType gamemode
) {
    RETURN_IF_NOT_MAIN_THREAD(origin(gamemode));
    origin(gamemode);
    if (FreeCameraManager::getInstance().FreeCamList.contains(getUuid().asString())) {
        FreeCameraManager::DisableFreeCamera(this);
    }
}

LL_TYPE_INSTANCE_HOOK(
    PlayerHurtEvent,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$_hurt,
    ActorHurtResult,
    ::ActorDamageSource const& source,
    float                      damage,
    ::HurtParameters const&    hurtParameters
) {
    RETURN_IF_NOT_MAIN_THREAD(origin(source, damage, hurtParameters));
    auto res = origin(source, damage, hurtParameters);
    if ((this->isSurvival() || this->isAdventure())
        && FreeCameraManager::getInstance().FreeCamList.contains(getUuid().asString())) {
        FreeCameraManager::DisableFreeCamera(this);
    }
    return res;
}

LL_TYPE_INSTANCE_HOOK(
    PlayerDieEvent,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$die,
    void,
    class ActorDamageSource const& a1
) {
    RETURN_IF_NOT_MAIN_THREAD(origin(a1));
    if (FreeCameraManager::getInstance().FreeCamList.contains(getUuid().asString())) {
        FreeCameraManager::DisableFreeCamera(this);
    }
    return origin(a1);
}

LL_TYPE_INSTANCE_HOOK(
    PlayerLeftEvent,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::disconnect,
    void
) {
    RETURN_IF_NOT_MAIN_THREAD(origin());
    FreeCameraManager::getInstance().FreeCamList.erase(getUuid().asString());
    FreeCameraManager::getInstance().SoulStates.erase(getUuid().asString());
    return origin();
}

struct Impl {
    ll::memory::HookRegistrar<
        ServerPlayerMoveHandleEvent,
        PlayerGamemodeChangeEvent,
        PlayerHurtEvent,
        PlayerDieEvent,
        PlayerLeftEvent>
        r;
};

std::unique_ptr<Impl> impl;

SoulViewGuard::SoulViewGuard(Player& player) {
    auto& mgr  = FreeCameraManager::getInstance();
    auto  uuid = player.getUuid().asString();
    if (!mgr.FreeCamList.contains(uuid)) return;
    auto it = mgr.SoulStates.find(uuid);
    if (it == mgr.SoulStates.end()) return;
    auto const& soul = it->second;

    mPlayer      = &player;
    mOldPos      = player.mBuiltInComponents->mStateVectorComponent->mPos;
    mOldPosPrev  = player.mBuiltInComponents->mStateVectorComponent->mPosPrev;
    mOldPosDelta = player.mBuiltInComponents->mStateVectorComponent->mPosDelta;
    mOldRot      = player.mBuiltInComponents->mActorRotationComponent->mRot;
    mOldRotPrev  = player.mBuiltInComponents->mActorRotationComponent->mRotPrev;

    player.mBuiltInComponents->mStateVectorComponent->mPos      = soul.pos;
    player.mBuiltInComponents->mStateVectorComponent->mPosPrev  = soul.pos;
    player.mBuiltInComponents->mStateVectorComponent->mPosDelta = soul.posDelta;
    player.mBuiltInComponents->mActorRotationComponent->mRot     = soul.rot;
    player.mBuiltInComponents->mActorRotationComponent->mRotPrev = soul.rot;
}

SoulViewGuard::~SoulViewGuard() {
    if (!mPlayer) return;
    mPlayer->mBuiltInComponents->mStateVectorComponent->mPos      = mOldPos;
    mPlayer->mBuiltInComponents->mStateVectorComponent->mPosPrev  = mOldPosPrev;
    mPlayer->mBuiltInComponents->mStateVectorComponent->mPosDelta = mOldPosDelta;
    mPlayer->mBuiltInComponents->mActorRotationComponent->mRot     = mOldRot;
    mPlayer->mBuiltInComponents->mActorRotationComponent->mRotPrev = mOldRotPrev;
}

void FreeCameraManager::freecameraHook(bool enable) {
    if (enable) {
        if (!impl) impl = std::make_unique<Impl>();
    } else {
        impl.reset();
    }
};

} // namespace coral_fans::functions