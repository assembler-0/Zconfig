// ── zconfig/error.hpp ─────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <cstdint>
#include <string>
#include <expected>
#include <source_location>

namespace zconfig {
    enum class StatusCode : uint8_t {
        Success = 0,
        NotFound,
        AccessDenied,
        EmptyFile,
        ParseError,
        SchemaMismatch,
        InternalError
    };

    struct OperationError {
        StatusCode code;
        std::string_view message;
        std::source_location location = std::source_location::current();

        static OperationError build(StatusCode c, std::string_view msg) {
            return { .code = c, .message = msg };
        }
    };

    // Result<T>
    template <typename T>
    using Result = std::expected<T, OperationError>;

    struct Nil {}; 
    using Status = Result<Nil>;
}