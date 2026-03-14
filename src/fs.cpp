// ── src/fs.cpp ────────────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#include <error.hpp>
#include <system_error>
#include <filesystem>
#include <fstream>

namespace zconfig::fs {
    Result<std::string> read_file_raw(const std::string_view path) {
        std::error_code ec;

        if (!std::filesystem::exists(path, ec)) {
            return std::unexpected(OperationError::build(
                StatusCode::NotFound, "File does not exist on disk"
            ));
        }

        std::ifstream file(path.data(), std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return std::unexpected(OperationError::build(
                StatusCode::AccessDenied, "Failed to open file stream (check permissions)"
            ));
        }

        auto size = file.tellg();
        if (size == 0) {
            return std::unexpected(OperationError::build(
                StatusCode::EmptyFile, "Configuration file is 0 bytes"
            ));
        }

        std::string buffer;
        buffer.resize(static_cast<std::size_t>(size));
        file.seekg(0);
        
        if (!file.read(buffer.data(), size)) {
            return std::unexpected(OperationError::build(
                StatusCode::InternalError, "Hardware/Stream read failure"
            ));
        }

        return buffer;
    }
}