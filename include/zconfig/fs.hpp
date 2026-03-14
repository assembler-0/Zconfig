// ── zconfig/fs.hpp ────────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <error.hpp>

namespace zconfig::fs {
    // Reads a file to string, returns the string on success or OperationError on failure
    Result<std::string> read_file_raw(const std::string_view path);
}