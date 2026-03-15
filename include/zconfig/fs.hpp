// ── include/zconfig/fs.hpp ────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <error.hpp>
#include <string>
#include <string_view>

namespace zconfig::fs {
    Result<std::string> read_file(const std::string& path);
    std::string resolve_path(const std::string& path, const std::string& base);
}

#include <zconfig/ast.hpp>
#include <zconfig/registry.hpp>
#include <vector>
#include <cstdint>

namespace zconfig {

    struct SearchResult {
        Node* node;
        int32_t score;

        bool operator<(const SearchResult& other) const {
            return score > other.score;
        }
    };

    class FuzzySearcher {
    public:
        explicit FuzzySearcher(const Registry& registry);

        std::vector<SearchResult> search(std::string_view query, size_t max_results = 50) const;

        static int32_t score_match(std::string_view query, std::string_view target) noexcept;

    private:
        struct IndexEntry {
            Node*       node;           
            std::string search_target;  
        };

        std::vector<IndexEntry> index;
    };

} // namespace zconfig
