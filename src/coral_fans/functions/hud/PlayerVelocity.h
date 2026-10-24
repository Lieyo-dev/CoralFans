#pragma once

#include "mc/deps/core/math/Vec3.h"
#include <optional>

class Player;

namespace coral_fans::functions {

std::optional<Vec3> getReportedVelocity(Player& player);

} // namespace coral_fans::functions