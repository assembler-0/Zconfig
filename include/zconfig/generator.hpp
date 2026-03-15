// ── include/zconfig/generator.hpp ─────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <zconfig/registry.hpp>
#include <zconfig/types.hpp>

namespace zconfig {
class Generator {
public:
    static Result<void> run(const Registry& reg);
private:
    static Result<void> emit_header(const Registry& reg, const std::string& path);
    static Result<void> emit_makefile(const Registry& reg, const std::string& path);
    static Result<void> emit_json(const Registry& reg, const std::string& path);
};
}
