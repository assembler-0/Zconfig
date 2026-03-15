// ── src/fs.hpp ────────────────────────────────────────────────────────────
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

    // Represents a single matched item.
    struct SearchResult {
        Node* node;
        int32_t score;

        // Sort descending by score
        bool operator<(const SearchResult& other) const {
            return score > other.score;
        }
    };

    class FuzzySearcher {
    public:
        // Core constructor: Builds the dense index from the registry.
        // O(N*L) time complexity, minimal allocations.
        explicit FuzzySearcher(const Registry& registry);

        // Perform a fuzzy search against the internal index.
        // Returns the top N results, sorted by relevance score.
        // O(N*M) time complexity, where N is total nodes, M is query length.
        // Fast-fails if query is empty or whitespace-only.
        std::vector<SearchResult> search(std::string_view query, size_t max_results = 50) const;

    private:
        // Dense, cache-aligned structure for L1/L2 friendly linear scans
        struct IndexEntry {
            Node*       node;           // Non-owning pointer back to AST
            std::string search_target;  // Pre-lowercased string
        };

        // The contiguous block of memory we linearly scan.
        std::vector<IndexEntry> index;

        // Internal scoring function: scores `query` against `target`.
        // Returns <= 0 if no structural match.
        static int32_t score_match(std::string_view query, std::string_view target) noexcept;
    };

} // namespace zconfig
