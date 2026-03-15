// ── src/main.cpp ──────────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#include <zconfig/parser.hpp>
#include <zconfig/generator.hpp>
#include <zconfig/fs.hpp>
#include <zconfig/log.hpp>
#include <zconfig/tui.hpp>
#include <print>
#include <filesystem>

namespace zconfig::log { bool debug_enabled = false; }

// Fixed config filename — always "Zconfig" in the working directory.
// Cache is always "Zconfig.cache" alongside it.
static constexpr const char* ZCONFIG_FILE  = "Zconfig";
static constexpr const char* ZCONFIG_CACHE = "Zconfig.cache";

int main(int argc, char** argv) {
    bool parse_only = false;
    bool defconfig  = false;
    bool validate   = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--parse-only") parse_only = true;
        else if (arg == "--debug")    zconfig::log::debug_enabled = true;
        else if (arg == "--defconfig") defconfig = true;
        else if (arg == "--validate") validate = true;
    }

    if (!std::filesystem::exists(ZCONFIG_FILE)) {
        ZLOG_ERROR("no '{}' found in current directory.", ZCONFIG_FILE);
        return 1;
    }

    auto content = zconfig::fs::read_file(ZCONFIG_FILE);
    if (!content) { ZLOG_ERROR("error: {}", content.error().message); return 1; }

    zconfig::Registry reg;
    if (auto res = zconfig::Parser::parse(*content, reg, ZCONFIG_FILE); !res) {
        auto& err = res.error();
        ZLOG_ERROR("Parse error at {}:{}:{}: {}", err.file.empty() ? ZCONFIG_FILE : err.file, err.line, err.column, err.message);
        if (!zconfig::log::debug_enabled) {
            ZLOG_INFO("Tip: rerun with --debug to see detailed token traces and debugging information.");
        }
        return 1;
    }

    if (parse_only) {
        reg.link();
        auto v = reg.validate_all();
        if (!v.valid) {
            for (const auto& err : v.errors) {
                ZLOG_WARN("Validation failed: {}", err.message);
            }
        }
        std::print("Successfully parsed {}\n", ZCONFIG_FILE);
        return v.valid ? 0 : 1;
    }

    if (validate) {
        reg.link();
        auto v = reg.validate_all();
        if (v.valid) {
            std::print("Configuration is valid.\n");
            return 0;
        } else {
            for (const auto& err : v.errors) {
                ZLOG_ERROR("Validation error: {}", err.message);
            }
            return 1;
        }
    }

    if (defconfig) {
        reg.link();
        for (const auto& [name, sym] : reg.get_symbols()) {
            if (std::holds_alternative<std::monostate>(sym->user_value))
                sym->user_value = sym->computed_value;
        }
        if (auto res = reg.save_cache(ZCONFIG_CACHE); !res) {
            ZLOG_ERROR("failed to save defconfig: {}", res.error().message);
            return 1;
        }
        if (auto gen_res = zconfig::Generator::run(reg); !gen_res) {
            ZLOG_ERROR("generation failed: {}", gen_res.error().message);
            return 1;
        }
        std::print("Default configuration written to {}\n", ZCONFIG_CACHE);
        return 0;
    }

    if (auto res = reg.load_cache(ZCONFIG_CACHE); !res) {
        reg.link();
    }

    zconfig::tui::Engine tui_engine(reg, ZCONFIG_CACHE);
    tui_engine.run();

    return 0;
}
