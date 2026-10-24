#pragma once

#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"

#include <string_view>

// 玩家个人的显示开关（hsa / hsa structure / slime / village 各类）的持久化。
// 存储在 CoralFans 的 configDb 中，键为 functions.players.<uuid>.<key>，值为 "true"/"false"。
namespace coral_fans::show_store {

// 读取；未保存过则返回 false
[[nodiscard]] bool get(mce::UUID const& uuid, std::string_view key);
// 保存
void set(mce::UUID const& uuid, std::string_view key, bool value);

// 玩家进服时，按保存的开关恢复其 hsa / slime / village 的显示
void restoreAll(Player& player);

} // namespace coral_fans::show_store
