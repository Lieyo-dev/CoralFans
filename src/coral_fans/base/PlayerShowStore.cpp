#include "coral_fans/base/PlayerShowStore.h"

#include "coral_fans/CoralFans.h"
#include "coral_fans/functions/hsa/Hsa.h"
#include "coral_fans/functions/slime/Slime.h"
#include "coral_fans/functions/village/Village.h"

#include <string>

namespace coral_fans::show_store {

namespace {
std::string makeKey(mce::UUID const& uuid, std::string_view key) {
    return "functions.players." + uuid.asString() + "." + std::string(key);
}
} // namespace

bool get(mce::UUID const& uuid, std::string_view key) {
    auto& db = CoralFans::getInstance().getConfigDb();
    if (!db) return false;
    auto value = db->get(makeKey(uuid, key));
    return value.has_value() && *value == "true";
}

void set(mce::UUID const& uuid, std::string_view key, bool value) {
    auto& db = CoralFans::getInstance().getConfigDb();
    if (!db) return;
    db->set(makeKey(uuid, key), value ? "true" : "false");
}

void restoreAll(Player& player) {
    auto const& uuid = player.getUuid();

    auto& hsa = functions::HsaManager::getInstance();
    if (get(uuid, "hsa.show")) hsa.setHsaShow(player, true, false);
    if (get(uuid, "hsa.structure")) hsa.setStructureShow(player, true, false);

    if (get(uuid, "slime.show")) functions::SlimeManager::getInstance().setShow(player, true, false);

    auto& village = functions::CFVillageManager::getInstance();
    for (size_t i = 0; i < functions::villageShowTypeCount; ++i) {
        auto type = static_cast<functions::VillageShowType>(i);
        if (get(uuid, functions::villageShowKey(type))) village.setShow(player, type, true, false);
    }
}

} // namespace coral_fans::show_store
