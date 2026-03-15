// ── include/zconfig/search.hpp ────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <zconfig/ast.hpp>
#include <zconfig/registry.hpp>
#include <vector>
#include <string>
#include <string_view>
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

private:
    struct IndexEntry {
        Node*       node;       
        std::string search_target;
    };

    std::vector<IndexEntry> index;
    static int32_t score_match(std::string_view query, std::string_view target) noexcept;
};

} // namespace zconfig
