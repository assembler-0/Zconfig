// ── src/registry.cpp ──────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#include <zconfig/registry.hpp>
#include <zconfig/log.hpp>
#include <fstream>
#include <sstream>
#include <functional>

namespace zconfig {

Result<Symbol*> Registry::find_symbol(const std::string& name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return it->second.get();
    }
    return std::unexpected(Diagnostic::build(ErrorCode::InternalError, "Symbol not found: " + name));
}

Result<void> Registry::register_symbol(std::unique_ptr<Symbol> sym, const std::string& prefix) {
    if (!prefix.empty()) {
        sym->name = prefix + "." + sym->name;
    }
    if (symbols.contains(sym->name) || computed_symbols.contains(sym->name)) {
        return std::unexpected(Diagnostic::build(ErrorCode::NamespaceCollision, "Symbol already defined: " + sym->name));
    }
    sym->reevaluate();
    symbols[sym->name] = std::move(sym);
    return {};
}

Result<void> Registry::register_computed(std::unique_ptr<ComputedSymbol> sym, const std::string& prefix) {
    if (!prefix.empty()) {
        sym->name = prefix + "." + sym->name;
    }
    if (symbols.contains(sym->name) || computed_symbols.contains(sym->name)) {
        return std::unexpected(Diagnostic::build(ErrorCode::NamespaceCollision, "Computed symbol already defined: " + sym->name));
    }
    sym->reevaluate();
    computed_symbols[sym->name] = std::move(sym);
    return {};
}

void Registry::register_validation(ValidationNode* node) {
    validations.push_back(node);
}

void Registry::register_macro(MacroDefinition macro) {
    macros[macro.name] = std::move(macro);
}

const MacroDefinition* Registry::find_macro(const std::string& name) const {
    auto it = macros.find(name);
    return (it != macros.end()) ? &it->second : nullptr;
}

void Registry::add_generator(std::unique_ptr<GenerateNode> gen) {
    generators.push_back(std::move(gen));
}

void Registry::link() {
    auto collect_all = [&](Node* n, Expression* e) {
        if (!e) return;
        std::set<Symbol*> deps;
        e->collect_dependencies(deps);
        for (auto* d : deps) {
            d->add_dependent(n);
        }
    };

    for (auto& [name, sym] : symbols) {
        collect_all(sym.get(), sym->when_condition.get());
        for (auto& def : sym->defaults) {
            collect_all(sym.get(), def.value_expr.get());
            collect_all(sym.get(), def.condition.get());
        }
        for (auto& imp : sym->implications) {
            collect_all(sym.get(), imp.target_expr.get());
            if (imp.condition) collect_all(sym.get(), imp.condition.get());
        }
    }

    for (auto& [name, sym] : computed_symbols) {
        collect_all(sym.get(), sym->when_condition.get());
        collect_all(sym.get(), sym->expr.get());
    }

    for (auto* val : validations) {
        collect_all(val, val->condition.get());
    }

    // After linking, do one final pass of reevaluation for ALL nodes
    // to build initial state and set up values.
    for (auto& node : all_nodes) {
        node->reevaluate();
    }
    for (auto* val : validations) {
        val->reevaluate();
    }
    
    // Do one final sweep for validations and symbols in case their dependencies 
    // initialized out-of-order and pushed updates that didn't settle.
    for (auto& node : all_nodes) node->reevaluate();
    for (auto* val : validations) val->reevaluate();
}

ValidationResult Registry::validate_all() const {
    ValidationResult res;
    
    // 1. Check explicit Validation nodes
    for (auto* val : validations) {
        if (!val->satisfied) {
            res.valid = false;
            res.errors.push_back({val->message, val});
        }
    }

    // 2. Check all configurable symbols for range/pattern bounds constraints
    for (const auto& [name, sym] : symbols) {
        if (!sym->is_effectively_visible()) continue;
        const Value& cur = (std::holds_alternative<std::monostate>(sym->user_value)) ? sym->computed_value : sym->user_value;
        if (!sym->validate(cur)) {
            res.valid = false;
            std::string err_msg = "Symbol '" + sym->name + "' has invalid value.";
            // attempt more specific hints
            if (sym->meta.range && std::holds_alternative<int64_t>(cur)) {
                err_msg = "Symbol '" + sym->name + "' value " + std::to_string(std::get<int64_t>(cur)) +
                          " is out of bounds [" + std::to_string(sym->meta.range->min) + ".." + std::to_string(sym->meta.range->max) + "].";
            } else if (!sym->meta.pattern.empty() && std::holds_alternative<std::string>(cur)) {
                err_msg = "Symbol '" + sym->name + "' string '" + std::get<std::string>(cur) +
                          "' does not match required pattern: " + sym->meta.pattern;
            } else if (!sym->meta.choices.empty()) {
                err_msg = "Symbol '" + sym->name + "' value is not in the allowed choices.";
            }
            res.errors.push_back({err_msg, sym.get()});
        }
    }

    return res;
}

void Registry::register_node(std::unique_ptr<Node> node) {
    all_nodes.push_back(std::move(node));
}

void Registry::add_root_menu(Menu* menu) {
    root_menus.push_back(menu);
}

void Registry::set_include_root(std::string path, std::string display) {
    if (include_root.path.empty()) {
        include_root.path    = std::move(path);
        include_root.display = std::move(display);
    }
}

void Registry::record_include(const std::string& parent, const std::string& child,
                               const std::string& display, const std::string& ns,
                               bool conditional, bool active) {
    std::function<IncludeNode*(IncludeNode&)> find = [&](IncludeNode& n) -> IncludeNode* {
        if (n.path == parent) return &n;
        for (auto& c : n.children)
            if (auto* f = find(c)) return f;
        return nullptr;
    };
    auto* p = find(include_root);
    if (!p) return;
    p->children.push_back({child, display, ns, conditional, active, {}});
}

void Registry::notify_change(Node* changed_node) {
    if (changed_node) {
        changed_node->reevaluate();
    }
}

Result<void> Registry::load_cache(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        auto it = symbols.find(key);
        if (it != symbols.end()) {
            Symbol* sym = it->second.get();
            try {
                if (sym->type == Type::Bool) {
                    sym->user_value = (val == "true");
                } else if (sym->type == Type::Int) {
                    if (!val.empty()) sym->user_value = (int64_t)std::stoll(val);
                } else if (sym->type == Type::String || sym->type == Type::Enum) {
                    sym->user_value = val;
                } else if (sym->type == Type::Set) {
                    std::vector<std::string> items;
                    std::stringstream ss(val);
                    std::string item;
                    while (std::getline(ss, item, ','))
                        if (!item.empty()) items.push_back(item);
                    sym->user_value = items;
                }
            } catch (...) {
                ZLOG_WARN("Failed to load cache value for {}: {}", key, val);
            }
        }
    }

    link();
    return {};
}

Result<void> Registry::save_cache(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return std::unexpected(Diagnostic::build(ErrorCode::InternalError, "Could not open cache for writing: " + path));
    
    for (const auto& [name, sym] : symbols) {
        if (std::holds_alternative<std::monostate>(sym->user_value)) continue;
        
        f << name << "=";
        if (std::holds_alternative<bool>(sym->user_value)) {
            f << (std::get<bool>(sym->user_value) ? "true" : "false");
        } else if (std::holds_alternative<int64_t>(sym->user_value)) {
            f << std::get<int64_t>(sym->user_value);
        } else if (std::holds_alternative<std::string>(sym->user_value)) {
            f << std::get<std::string>(sym->user_value);
        } else if (std::holds_alternative<std::vector<std::string>>(sym->user_value)) {
            const auto& vec = std::get<std::vector<std::string>>(sym->user_value);
            for (size_t i = 0; i < vec.size(); ++i) {
                f << vec[i] << (i == vec.size() - 1 ? "" : ",");
            }
        }
        f << "\n";
    }
    return {};
}

}
