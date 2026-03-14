// ── src/main.cpp ──────────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#include <print>
#include <zconfig/fs.hpp>

int main(const int argc, char* argv[]) {
    auto default_path = zconfig::fs::read_file_raw("Zconfig");
    if (!default_path) {
        std::println("{}", default_path.error().message);
    }

    std::println("{}", *default_path);

    return 0;
}