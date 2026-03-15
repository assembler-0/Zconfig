// ── include/zconfig/expressions.hpp ───────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once
#include <zconfig/ast.hpp>
#include <string>

namespace zconfig {

class Registry;

struct LiteralExpr : public Expression {
    Value val;
    LiteralExpr(Value v) : val(std::move(v)) {}
    Value evaluate() const override { return val; }
    void collect_dependencies(std::set<Symbol*>&) override {}
};

struct SymbolRefExpr : public Expression {
    std::string name;
    Registry* reg;
    mutable Symbol* resolved_sym{nullptr};

    SymbolRefExpr(std::string n, Registry* r) : name(std::move(n)), reg(r) {}

    Symbol* resolve() const;

    Value evaluate() const override;

    void collect_dependencies(std::set<Symbol*>& deps) override;
};

struct EnvExpr : public Expression {
    std::string var;
    EnvExpr(std::string v) : var(std::move(v)) {}
    Value evaluate() const override;
    void collect_dependencies(std::set<Symbol*>&) override {}
};

struct ShellExpr : public Expression {
    std::string command;
    ShellExpr(std::string cmd) : command(std::move(cmd)) {}
    Value evaluate() const override;
    void collect_dependencies(std::set<Symbol*>&) override {}
};

struct InterpolatedStringExpr : public Expression {
    struct Part {
        std::string text;
        std::unique_ptr<Expression> expr; // non-null → dynamic symbol reference
    };
    std::vector<Part> parts;
    Value evaluate() const override;
    void collect_dependencies(std::set<Symbol*>& deps) override;
};

struct IsDefinedExpr : public Expression {
    std::string sym_name;
    Registry* reg;
    IsDefinedExpr(std::string n, Registry* r) : sym_name(std::move(n)), reg(r) {}
    Value evaluate() const override;
    void collect_dependencies(std::set<Symbol*>&) override {}
};

struct AbsExpr : public Expression {
    std::unique_ptr<Expression> expr;
    AbsExpr(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
    Value evaluate() const override;
    void collect_dependencies(std::set<Symbol*>& deps) override { expr->collect_dependencies(deps); }
};

struct SetExpr : public Expression {
    std::vector<std::unique_ptr<Expression>> elements;
    Value evaluate() const override;
    void collect_dependencies(std::set<Symbol*>& deps) override {
        for (auto& e : elements) e->collect_dependencies(deps);
    }
};

enum class BinOp {
    And, Or, Eq, Neq, Lt, Gt, Lte, Gte,
    Add, Sub, Mul, Div, Mod,
    BitAnd, BitOr, BitXor, Shl, Shr,
    Implies
};

struct BinaryExpr : public Expression {
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;
    BinOp op;

    BinaryExpr(std::unique_ptr<Expression> l, BinOp o, std::unique_ptr<Expression> r)
        : left(std::move(l)), right(std::move(r)), op(o) {}

    Value evaluate() const override;
    void collect_dependencies(std::set<Symbol*>& deps) override {
        left->collect_dependencies(deps);
        right->collect_dependencies(deps);
    }
};

struct UnaryExpr : public Expression {
    std::unique_ptr<Expression> operand;
    enum class Op { Not, Neg };
    Op op;

    UnaryExpr(std::unique_ptr<Expression> op, Op o)
        : operand(std::move(op)), op(o) {}

    Value evaluate() const override;
    void collect_dependencies(std::set<Symbol*>& deps) override {
        operand->collect_dependencies(deps);
    }
};

// Ternary conditional:  condition ? then_expr : else_expr
struct TernaryExpr : public Expression {
    std::unique_ptr<Expression> cond;
    std::unique_ptr<Expression> then_expr;
    std::unique_ptr<Expression> else_expr;

    TernaryExpr(std::unique_ptr<Expression> c,
                std::unique_ptr<Expression> t,
                std::unique_ptr<Expression> e)
        : cond(std::move(c)), then_expr(std::move(t)), else_expr(std::move(e)) {}

    Value evaluate() const override {
        auto cv = cond->evaluate();
        bool truth = false;
        if (std::holds_alternative<bool>(cv)) truth = std::get<bool>(cv);
        else if (std::holds_alternative<int64_t>(cv)) truth = (std::get<int64_t>(cv) != 0);
        else if (std::holds_alternative<std::string>(cv)) truth = !std::get<std::string>(cv).empty();
        return truth ? then_expr->evaluate() : else_expr->evaluate();
    }
    void collect_dependencies(std::set<Symbol*>& deps) override {
        cond->collect_dependencies(deps);
        then_expr->collect_dependencies(deps);
        else_expr->collect_dependencies(deps);
    }
};

} // namespace zconfig
