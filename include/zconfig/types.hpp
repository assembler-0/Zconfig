// ── include/zconfig/types.hpp ─────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <variant>
#include <string>
#include <vector>
#include <cstdint>
#include <zconfig/error.hpp>

namespace zconfig {

enum class Type { Bool, Int, String, Enum, Set, Void };

using Value = std::variant<
    std::monostate, // Void/Unset
    bool,
    int64_t,
    std::string,
    std::vector<std::string> // Set
>;

struct Range { int64_t min; int64_t max; };

}
