// ── include/zconfig/parser.hpp ────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <zconfig/ast.hpp>
#include <zconfig/registry.hpp>
#include <zconfig/types.hpp>
#include <string_view>

namespace zconfig {
class Parser {
public:
    static Result<void> parse(
        std::string_view source, 
        Registry& reg, 
        const std::string& filename = "<string>", 
        const std::string& prefix = "",
        uint32_t depth = 0
    );
};
}
