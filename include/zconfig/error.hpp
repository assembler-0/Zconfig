// ── include/zconfig/error.hpp ─────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <string>
#include <expected>
#include <source_location>
#include <cstdint>

namespace zconfig {
    enum class ErrorCode : uint8_t {
        Success = 0,
        ParseError,
        TypeMismatch,
        CircularDependency,
        FileNotFound,
        NamespaceCollision,
        InvalidExpression,
        InternalError
    };

    struct Diagnostic {
        ErrorCode code;
        std::string message;
        std::string file;
        uint32_t line;
        uint32_t column;
        std::source_location loc = std::source_location::current();
        
        static Diagnostic build(ErrorCode c, std::string msg, 
                              std::string f = "", uint32_t l = 0, uint32_t col = 0,
                              std::source_location sl = std::source_location::current()) {
            return { c, std::move(msg), std::move(f), l, col, sl };
        }
    };

    template<typename T>
    using Result = std::expected<T, Diagnostic>;
}
