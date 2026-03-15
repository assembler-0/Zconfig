// ── include/zconfig/ast.hpp ───────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <zconfig/types.hpp>
#include <memory>
#include <vector>
#include <set>
#include <map>

namespace zconfig {
    class Symbol; 

    struct Expression {
        virtual ~Expression() = default;
        virtual Value evaluate() const = 0;
        virtual void collect_dependencies(std::set<Symbol*>& deps) = 0;
    };

    struct DefaultCase {
        std::unique_ptr<Expression> value_expr;
        std::unique_ptr<Expression> condition; // nullptr for fallback
    };

    struct Implication {
        std::unique_ptr<Expression> condition; 
        std::unique_ptr<Expression> target_expr; 
    };

    struct ChoiceInfo {
        std::string label;
        std::string help;
        std::unique_ptr<Expression> when_condition;
    };

    enum class DangerLevel { None, Warning, Critical };

    struct Metadata {
        std::string label;
        std::string help;
        std::vector<std::string> tags;
        DangerLevel danger{DangerLevel::None};
        bool collapsed{false};
        std::optional<Range> range;
        std::string pattern; 
        
        std::map<std::string, ChoiceInfo> choices_meta;
        std::vector<std::string> choices; 
    };

    class Node {
    public:
        virtual ~Node() = default;
        virtual void reevaluate() = 0;
        
        bool is_visible() const { return visible; }
        
        bool is_effectively_visible() const {
            if (!visible) return false;
            if (parent) return parent->is_effectively_visible();
            return true;
        }

        void add_dependent(Node* dep) { 
            for (auto* d : dependents) if (d == dep) return;
            dependents.push_back(dep); 
        }

        std::unique_ptr<Expression> when_condition;
        std::vector<Node*> dependents;
        Node*       parent{nullptr};
        bool        visible{true};
        std::string source_file;
    };

    class ValidationNode : public Node {
    public:
        std::string message;
        std::unique_ptr<Expression> condition;
        bool satisfied{true};
        
        void reevaluate() override; 
    };

    class Symbol : public Node {
    public:
        std::string name;
        Type type;
        Value user_value;
        Value computed_value;
        Metadata meta;
        std::vector<DefaultCase> defaults;
        std::vector<Implication> implications; 

        void reevaluate() override; 
        bool validate(const Value& val) const;
    };

    class Menu : public Node {
    public:
        std::string label;
        std::vector<Node*> children;
        void reevaluate() override; 
    };

    class ComputedSymbol : public Node {
    public:
        std::string name;
        Type type;
        Value value;
        std::unique_ptr<Expression> expr;

        void reevaluate() override; 
    };

    struct MacroDefinition {
        std::string name;
        std::vector<std::string> arg_names;
        std::string body;
    };

    enum class GeneratorBackend { Header, Makefile, JSON };

    class GenerateNode : public Node {
    public:
        GeneratorBackend backend;
        std::string output_path;
        void reevaluate() override {} // Generators are static
    };
}
