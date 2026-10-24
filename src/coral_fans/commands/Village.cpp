#include "coral_fans/functions/village/Village.h"
#include "coral_fans/Config.h"
#include "coral_fans/base/Macros.h"
#include "coral_fans/base/Utils.h"


#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"
#include "ll/api/i18n/I18n.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/phys/HitResult.h"


namespace coral_fans::commands {

void registerVillageCommand(config::CommandConfigStruct& config) {
    if (config.enabled) {
        using ll::i18n_literals::operator""_tr;

        // reg cmd
        auto& villageCommand = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand(
            config.command,
            "command.village.description"_tr(),
            config.permission
        );

        // village show <bounds|raid|spawn|center|poi|bind> <bool>
        ll::command::CommandRegistrar::getInstance(false).tryRegisterRuntimeEnum(
            "villageShowType",
            {
                {"bounds", 0},
                {"raid",   1},
                {"spawn",  2},
                {"center", 3},
                {"poi",    4},
                {"bind",   5}
        }
        );
        villageCommand.runtimeOverload()
            .text("show")
            .required("type", ll::command::ParamKind::Enum, "villageShowType")
            .optional("enable", ll::command::ParamKind::Bool)
            .execute([](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& self) {
                using ll::i18n_literals::operator""_tr;
                COMMAND_CHECK_PLAYER
                auto& villageManager = functions::CFVillageManager::getInstance();
                auto  type = static_cast<functions::VillageShowType>(self["type"].get<ll::command::ParamKind::Enum>().index);
                if (self["enable"].has_value())
                    villageManager.setShow(*player, type, self["enable"].get<ll::command::ParamKind::Bool>());
                else villageManager.setShow(*player, type, !villageManager.getShow(*player, type));
                output.success(
                    "command.village.show.output"_tr(
                        self["type"].get<ll::command::ParamKind::Enum>().name,
                        villageManager.getShow(*player, type) ? "true" : "false"
                    )
                );
            });

        // village list
        villageCommand.overload().text("list").execute([](CommandOrigin const& origin, CommandOutput& output) {
            using ll::i18n_literals::operator""_tr;
            auto  entity         = origin.getEntity();
            auto& villageManager = functions::CFVillageManager::getInstance();
            if (entity == nullptr || !entity->isType(ActorType::Player)) {
                for (auto& str : villageManager.listVillages()) {
                    output.success(str);
                }
                return;
            }
            auto* player = static_cast<Player*>(entity);
            for (auto& str : villageManager.listVillages()) {
                utils::segmentAndSendToPlayer(str, player);
            }
        });

        // village tickinglist
        villageCommand.overload().text("tickinglist").execute([](CommandOrigin const& origin, CommandOutput& output) {
            using ll::i18n_literals::operator""_tr;
            auto res    = functions::CFVillageManager::getInstance().listTickingVillages();
            auto entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player)) {
                output.success(res);
                return;
            }
            utils::segmentAndSendToPlayer(res, static_cast<Player*>(entity));
        });


        // village info <id: int>
        villageCommand.runtimeOverload()
            .text("info")
            .required("id", ll::command::ParamKind::Int)
            .execute([](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& self) {
                using ll::i18n_literals::operator""_tr;
                auto rst = functions::CFVillageManager::getInstance().getVillageInfo(
                    self["id"].get<ll::command::ParamKind::Int>()
                );
                if (rst.second) {
                    auto entity = origin.getEntity();
                    if (entity == nullptr || !entity->isType(ActorType::Player)) {
                        output.success(rst.first);
                        return;
                    }
                    utils::segmentAndSendToPlayer(rst.first, static_cast<Player*>(entity));
                } else return output.error(rst.first);
            });

        // village dweller
        villageCommand.overload().text("dweller").execute([](CommandOrigin const& origin, CommandOutput& output) {
            using ll::i18n_literals::operator""_tr;
            COMMAND_CHECK_PLAYER
            auto hitrst = player->traceRay(5.25f, true, false);
            if (!hitrst) return output.error("command.village.dweller.noactor"_tr());
            auto* actor = hitrst.getEntity();
            if (!actor) return output.error("command.village.dweller.noactor"_tr());
            else {
                auto rst = functions::CFVillageManager::getInstance().getVillagerInfo(actor->getOrCreateUniqueID());
                if (rst.second) {
                    // return output.success(rst.first);
                    utils::segmentAndSendToPlayer(rst.first, player);
                } else return output.error(rst.first);
            }
        });
    }

    coral_fans::functions::CFVillageManager::hookVillage(config.enabled);
}

} // namespace coral_fans::commands