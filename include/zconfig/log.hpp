// ── include/zconfig/log.hpp ──────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <print>
#include <string_view>
#include <format>

namespace zconfig::log {
    enum class Level { Debug, Info, Warning, Error };
    extern bool debug_enabled;

    template<typename... Args>
    void message(Level lvl, std::format_string<Args...> fmt, Args&&... args) {
        if (lvl == Level::Debug && !debug_enabled) return;
        std::string_view prefix;
        switch(lvl) {
            case Level::Debug:   prefix = "[\033[36mdbg  \033[0m] "; break;
            case Level::Info:    prefix = "[\033[32minfo \033[0m] "; break;
            case Level::Warning: prefix = "[\033[33mwarn \033[0m] "; break;
            case Level::Error:   prefix = "[\033[31mfatal\033[0m] "; break;
        }
        std::print(stderr, "{}", prefix);
        std::println(stderr, fmt, std::forward<Args>(args)...);
    }

    #define ZLOG_DEBUG(fmt, ...) zconfig::log::message(zconfig::log::Level::Debug, fmt, ##__VA_ARGS__)
    #define ZLOG_INFO(fmt, ...)  zconfig::log::message(zconfig::log::Level::Info,  fmt, ##__VA_ARGS__)
    #define ZLOG_WARN(fmt, ...)  zconfig::log::message(zconfig::log::Level::Warning, fmt, ##__VA_ARGS__)
    #define ZLOG_ERROR(fmt, ...) zconfig::log::message(zconfig::log::Level::Error, fmt, ##__VA_ARGS__)
}
