// ── include/zconfig/fs.hpp ────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <zconfig/types.hpp>
#include <string>

namespace zconfig::fs {
    Result<std::string> read_file(const std::string& path);
    std::string resolve_path(const std::string& path, const std::string& base);
}
