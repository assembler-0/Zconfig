// ── zconfig/ast.hpp ───────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace zconfig {

// ── Source location (attach to every node for TUI error reporting) ─────────

struct Loc {
    std::string_view file;
    uint32_t line, col;
};

// ── Expression tree ────────────────────────────────────────────────────────
// Represents: when, implies, computed, conditional defaults

struct Expr;

struct ExprIdent  { std::string name; };
struct ExprLit    { bool value; };            // true / false literal
struct ExprIntLit { int64_t value; };
struct ExprStrLit { std::string value; };
struct ExprEq     { std::unique_ptr<Expr> lhs, rhs; };
struct ExprNeq    { std::unique_ptr<Expr> lhs, rhs; };
struct ExprAnd    { std::unique_ptr<Expr> lhs, rhs; };
struct ExprOr     { std::unique_ptr<Expr> lhs, rhs; };
struct ExprNot    { std::unique_ptr<Expr> operand; };

struct Expr {
    using Inner = std::variant<
        ExprIdent, ExprLit, ExprIntLit, ExprStrLit,
        ExprEq, ExprNeq, ExprAnd, ExprOr, ExprNot
    >;
    Inner inner;
    Loc loc;
};

// ── Value variants ─────────────────────────────────────────────────────────
// Runtime values — also used for AST literals

using Value = std::variant<
    bool,
    int64_t,
    std::string,
    std::vector<std::string>   // set<> — multi-select active choices
>;

// ── Conditional default entry ──────────────────────────────────────────────
// One arm of:  default { 1000 when PREEMPT_MODEL == realtime; 100 }

struct DefaultEntry {
    Value                value;
    std::optional<Expr>  when;   // nullopt = unconditional fallback
};

// ── Per-choice metadata (for enum/set options) ─────────────────────────────

struct ChoiceEntry {
    std::string          name;
    std::string          label;
    std::string          help;
    std::optional<Expr>  when;   // hidden unless condition is met
    Loc                  loc;
};

// ── TUI / display metadata (attached to every OptionNode) ─────────────────

enum class DangerLevel { None, Warning, Critical };

struct RangeConstraint {
    int64_t lo, hi;              // inclusive on both ends
};

struct OptionMeta {
    std::string                      label;
    std::string                      help;
    std::vector<std::string>         tags;
    DangerLevel                      danger    = DangerLevel::None;
    bool                             collapsed = false;
    std::optional<RangeConstraint>   range;     // int only
    std::optional<std::string>       pattern;   // string only — regex source
    std::vector<ChoiceEntry>         choices;   // enum/set only
};

// ── Option type tag ────────────────────────────────────────────────────────

enum class OptionType { Bool, Int, String, Enum, Set };

// ── Core option node ───────────────────────────────────────────────────────

struct OptionNode {
    std::string                  name;
    OptionType                   type;
    std::vector<DefaultEntry>    defaults;    // evaluated top-to-bottom
    std::optional<Expr>          when;        // visibility / enable condition
    std::vector<Expr>            implies;     // soft select — one per target
    bool                         computed = false;   // read-only derived value
    OptionMeta                   meta;
    Loc                          loc;
};

// ── Forward-declare Node so MenuNode can hold Vec<Node> ───────────────────

struct Node;

// ── Menu node ─────────────────────────────────────────────────────────────

struct MenuNode {
    std::string              label;
    std::optional<Expr>      when;
    std::vector<Node>        children;
    Loc                      loc;
};

// ── Include node ──────────────────────────────────────────────────────────

struct IncludeNode {
    std::string              path;
    std::optional<std::string> ns;   // "as net" -> ns = "net"
    std::optional<Expr>      when;
    Loc                      loc;
};

// ── Top-level node variant ────────────────────────────────────────────────

struct Node {
    using Inner = std::variant<OptionNode, MenuNode, IncludeNode>;
    Inner inner;
};

// ── Root of one parsed file ────────────────────────────────────────────────

struct ZconfigFile {
    std::string        path;
    std::vector<Node>  children;
};

} // namespace zconfig