// ── src/ast.cpp ───────────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#include <zconfig/ast.hpp>
#include <zconfig/expressions.hpp>
#include <zconfig/registry.hpp>
#include <zconfig/log.hpp>
#include <regex>
#include <cstdlib>
#include <array>

namespace zconfig {

// --- Expression Implementation ---

Symbol* SymbolRefExpr::resolve() const {
    if (!resolved_sym) {
        auto res = reg->find_symbol(name);
        if (res) resolved_sym = *res;
    }
    return resolved_sym;
}

Value SymbolRefExpr::evaluate() const {
    auto* s = resolve();
    return s ? s->computed_value : Value{std::string(name)};
}

void SymbolRefExpr::collect_dependencies(std::set<Symbol*>& deps) {
    auto* s = resolve();
    if (s) deps.insert(s);
}

Value EnvExpr::evaluate() const {
    const char* val = std::getenv(var.c_str());
    return val ? std::string(val) : std::string("");
}

Value ShellExpr::evaluate() const {
    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return std::string("");
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    // Trim newline
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

Value InterpolatedStringExpr::evaluate() const {
    std::string result;
    for (const auto& part : parts) {
        if (part.expr) {
            auto val = part.expr->evaluate();
            if (std::holds_alternative<std::string>(val)) result += std::get<std::string>(val);
            else if (std::holds_alternative<int64_t>(val)) result += std::to_string(std::get<int64_t>(val));
            else if (std::holds_alternative<bool>(val)) result += std::get<bool>(val) ? "true" : "false";
        } else {
            result += part.text;
        }
    }
    return result;
}

void InterpolatedStringExpr::collect_dependencies(std::set<Symbol*>& deps) {
    for (const auto& part : parts) {
        if (part.expr) part.expr->collect_dependencies(deps);
    }
}

Value IsDefinedExpr::evaluate() const {
    auto res = reg->find_symbol(sym_name);
    return res.has_value();
}

Value AbsExpr::evaluate() const {
    auto val = expr->evaluate();
    if (std::holds_alternative<int64_t>(val)) {
        int64_t v = std::get<int64_t>(val);
        return v < 0 ? -v : v;
    }
    return val;
}

Value SetExpr::evaluate() const {
    std::vector<std::string> result;
    for (auto& e : elements) {
        auto val = e->evaluate();
        if (std::holds_alternative<std::string>(val)) result.push_back(std::get<std::string>(val));
        else if (std::holds_alternative<int64_t>(val)) result.push_back(std::to_string(std::get<int64_t>(val)));
        else if (std::holds_alternative<bool>(val)) result.push_back(std::get<bool>(val) ? "true" : "false");
    }
    return result;
}

Value BinaryExpr::evaluate() const {
    auto l = left->evaluate();
    auto r = right->evaluate();

    auto as_int = [](const Value& v) -> int64_t {
        if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v);
        if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? 1 : 0;
        return 0;
    };

    auto as_bool = [](const Value& v) -> bool {
        if (std::holds_alternative<bool>(v)) return std::get<bool>(v);
        if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v) != 0;
        return false;
    };

    switch (op) {
        case BinOp::And: return as_bool(l) && as_bool(r);
        case BinOp::Or:  return as_bool(l) || as_bool(r);
        case BinOp::Eq:  return l == r;
        case BinOp::Neq: return l != r;
        case BinOp::Lt:  return as_int(l) < as_int(r);
        case BinOp::Gt:  return as_int(l) > as_int(r);
        case BinOp::Lte: return as_int(l) <= as_int(r);
        case BinOp::Gte: return as_int(l) >= as_int(r);
        
        case BinOp::Add: return as_int(l) + as_int(r);
        case BinOp::Sub: return as_int(l) - as_int(r);
        case BinOp::Mul: return as_int(l) * as_int(r);
        case BinOp::Div: return as_int(r) != 0 ? as_int(l) / as_int(r) : (int64_t)0;
        case BinOp::Mod: return as_int(r) != 0 ? as_int(l) % as_int(r) : (int64_t)0;
        
        case BinOp::BitAnd: return as_int(l) & as_int(r);
        case BinOp::BitOr:  return as_int(l) | as_int(r);
        case BinOp::BitXor: return as_int(l) ^ as_int(r);
        case BinOp::Shl:    return as_int(l) << as_int(r);
        case BinOp::Shr:    return as_int(l) >> as_int(r);
        case BinOp::Implies: return !as_bool(l) || as_bool(r);
    }
    return false;
}

Value UnaryExpr::evaluate() const {
    auto v = operand->evaluate();
    if (op == Op::Not) {
        if (std::holds_alternative<bool>(v)) return !std::get<bool>(v);
        if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v) == 0;
        if (std::holds_alternative<std::monostate>(v)) return true; // !false == true
        return false;
    } else if (op == Op::Neg) {
        if (std::holds_alternative<int64_t>(v)) return -std::get<int64_t>(v);
        return v;
    }
    return v;
}

// --- Node Implementation ---

void ValidationNode::reevaluate() {
    if (condition) {
        auto val = condition->evaluate();
        if (std::holds_alternative<bool>(val)) satisfied = std::get<bool>(val);
        else if (std::holds_alternative<int64_t>(val)) satisfied = std::get<int64_t>(val) != 0;
        else satisfied = true;
    }
}

bool Symbol::validate(const Value& val) const {
    if (std::holds_alternative<std::monostate>(val)) return true;

    if (type == Type::Int && std::holds_alternative<int64_t>(val)) {
        if (meta.range) {
            int64_t v = std::get<int64_t>(val);
            return v >= meta.range->min && v <= meta.range->max;
        }
        return true;
    }
    
    if (type == Type::Enum && std::holds_alternative<std::string>(val)) {
        if (meta.choices.empty()) return true;
        std::string v = std::get<std::string>(val);
        for (const auto& c : meta.choices) {
            if (c == v) {
                auto it = meta.choices_meta.find(c);
                if (it != meta.choices_meta.end() && it->second.when_condition) {
                    auto res = it->second.when_condition->evaluate();
                    if (std::holds_alternative<bool>(res) && !std::get<bool>(res)) return false;
                }
                return true;
            }
        }
        return false;
    }

    if (type == Type::String && std::holds_alternative<std::string>(val)) {
        if (!meta.pattern.empty()) {
            try {
                std::regex re(meta.pattern);
                return std::regex_search(std::get<std::string>(val), re);
            } catch (...) { return true; }
        }
        return true;
    }

    return true; 
}

void Symbol::reevaluate() {
    bool old_visible = visible;
    Value old_computed = computed_value;

    if (when_condition) {
        auto val = when_condition->evaluate();
        if (std::holds_alternative<bool>(val)) visible = std::get<bool>(val);
        else visible = false;
    } else {
        visible = true;
    }

    if (parent && !parent->is_effectively_visible()) {
        visible = false;
    }

    if (visible) {
        bool value_set = false;
        for (const auto& def : defaults) {
            bool cond_met = true;
            if (def.condition) {
                auto c_val = def.condition->evaluate();
                if (std::holds_alternative<bool>(c_val)) cond_met = std::get<bool>(c_val);
                else cond_met = false;
            }
            if (cond_met && def.value_expr) {
                computed_value = def.value_expr->evaluate();
                value_set = true;
                break;
            }
        }
        
        if (std::holds_alternative<std::monostate>(user_value) && type == Type::Bool && !value_set) {
            bool implied_true = false;
            for (const auto& imp : implications) {
                auto res = imp.target_expr->evaluate();
                if (std::holds_alternative<bool>(res) && std::get<bool>(res)) {
                    implied_true = true;
                    break;
                }
            }
            if (implied_true) {
                computed_value = true;
                value_set = true;
            }
        }

        if (!std::holds_alternative<std::monostate>(user_value)) {
             computed_value = user_value;
        } else if (!value_set) {
             if (type == Type::Bool) computed_value = false;
             else if (type == Type::Int) {
                 if (meta.range) computed_value = meta.range->min;
                 else computed_value = (int64_t)0;
             }
             else if (type == Type::String) computed_value = std::string("");
             else if (type == Type::Enum) {
                 bool found = false;
                 for (const auto& c : meta.choices) {
                     auto it = meta.choices_meta.find(c);
                     if (it != meta.choices_meta.end() && it->second.when_condition) {
                         auto res = it->second.when_condition->evaluate();
                         if (std::holds_alternative<bool>(res) && !std::get<bool>(res)) continue;
                     }
                     computed_value = c;
                     found = true;
                     break;
                 }
                 if (!found) computed_value = std::string("");
             }
             else if (type == Type::Set) computed_value = std::vector<std::string>{};
        }
    } else {
        computed_value = std::monostate{};
    }

    if (visible != old_visible || computed_value != old_computed) {
        for (auto* dep : dependents) {
            dep->reevaluate();
        }
    }
}

void Menu::reevaluate() {
    if (when_condition) {
        auto val = when_condition->evaluate();
        if (std::holds_alternative<bool>(val)) visible = std::get<bool>(val);
        else visible = false;
    } else {
        visible = true;
    }

    // Children re-evaluation is always needed as they depend on parent visibility
    for (auto& child : children) {
        child->reevaluate();
    }

}

void ComputedSymbol::reevaluate() {
    bool old_visible = visible;
    Value old_value = value;

    if (when_condition) {
        auto val = when_condition->evaluate();
        if (std::holds_alternative<bool>(val)) visible = std::get<bool>(val);
        else visible = false;
    } else {
        visible = true;
    }

    if (visible && expr) {
        value = expr->evaluate();
    } else {
        value = std::monostate{};
    }

    if (visible != old_visible || value != old_value) {
        for (auto* dep : dependents) {
            dep->reevaluate();
        }
    }
}

}
