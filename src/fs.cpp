// ── src/fs.cpp ────────────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#include <zconfig/fs.hpp>
#include <filesystem>
#include <fstream>

namespace zconfig::fs {

Result<std::string> read_file(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(Diagnostic::build(
            ErrorCode::FileNotFound, "File not found: " + path
        ));
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return std::unexpected(Diagnostic::build(
            ErrorCode::FileNotFound, "Unable to open file: " + path
        ));
    }

    auto size = file.tellg();
    if (size == -1) {
         return std::unexpected(Diagnostic::build(
            ErrorCode::InternalError, "Failed to determine file size: " + path
        ));
    }
    std::string buffer(size, '\0');
    file.seekg(0);
    if (!file.read(&buffer[0], size)) {
        return std::unexpected(Diagnostic::build(
            ErrorCode::InternalError, "Failed to read file content: " + path
        ));
    }

    return buffer;
}

std::string resolve_path(const std::string& path, const std::string& base) {
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return p.string();
    }
    std::filesystem::path b(base);
    return (b.parent_path() / p).string();
}

}

#include <algorithm>
#include <cctype>

namespace zconfig {

    static std::string to_lower_ascii(std::string_view s) {
        std::string res;
        res.reserve(s.size());
        for (char c : s) {
            res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return res;
    }

    FuzzySearcher::FuzzySearcher(const Registry& registry) {
        const auto& symbols = registry.get_symbols();
        const auto& computed = registry.get_computed();
        
        index.reserve(symbols.size() + computed.size());

        for (const auto& [name, sym] : symbols) {
            std::string target = name;
            if (!sym->meta.label.empty()) {
                target += " " + sym->meta.label;
            }
            index.push_back({sym.get(), to_lower_ascii(target)});
        }

        for (const auto& [name, sym] : computed) {
            index.push_back({sym.get(), to_lower_ascii(name)});
        }
    }

    std::vector<SearchResult> FuzzySearcher::search(std::string_view query, size_t max_results) const {
        if (query.empty()) return {};

        std::string q_lower = to_lower_ascii(query);
        
        bool all_ws = true;
        for (char c : q_lower) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                all_ws = false;
                break;
            }
        }
        if (all_ws) return {};

        std::vector<SearchResult> results;
        results.reserve(std::min(index.size(), static_cast<size_t>(1000)));

        for (const auto& entry : index) {
            int32_t score = score_match(q_lower, entry.search_target);
            if (score > 0) {
                results.push_back({entry.node, score});
            }
        }

        if (results.size() > max_results) {
            std::partial_sort(results.begin(), results.begin() + max_results, results.end());
            results.resize(max_results);
        } else {
            std::sort(results.begin(), results.end());
        }

        return results;
    }

    int32_t FuzzySearcher::score_match(std::string_view query, std::string_view target) noexcept {
        if (query.empty() || target.empty() || query.size() > target.size()) return 0;

        int32_t score = 0;
        size_t t_idx = 0;
        size_t consecutive_matches = 0;

        for (size_t q_idx = 0; q_idx < query.size(); ++q_idx) {
            char qc = query[q_idx];
            bool found = false;

            while (t_idx < target.size()) {
                char tc = target[t_idx];
                
                if (qc == tc) {
                    found = true;
                    score += 10;
                    
                    if (consecutive_matches > 0) {
                        score += 5 * consecutive_matches;
                    }
                    
                    if (t_idx == 0 || target[t_idx - 1] == '_' || target[t_idx - 1] == ' ' || target[t_idx - 1] == '-') {
                        score += 20;
                    }

                    consecutive_matches++;
                    t_idx++;
                    break;
                } else {
                    consecutive_matches = 0;
                    t_idx++;
                }
            }

            if (!found) {
                return 0; // Subsequence not found
            }
        }

        int32_t remaining = static_cast<int32_t>(target.size() - t_idx);
        score -= remaining;

        return score > 0 ? score : 1;
    }

} // namespace zconfig
