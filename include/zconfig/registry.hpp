#pragma once

#include <zconfig/ast.hpp>
#include <error.hpp>
#include <unordered_map>
#include <string>
#include <memory>

namespace zconfig {

    // Recursive include-file tree captured at parse time.
    struct IncludeNode {
        std::string path;         // resolved absolute path
        std::string display;      // original path as written in source
        std::string ns;           // "net" from `as net`, empty otherwise
        bool        conditional{false};
        bool        active{true};
        std::vector<IncludeNode> children;
    };

    struct ValidationError {
        std::string message;
        Node* source{nullptr};
    };

    struct ValidationResult {
        bool valid{true};
        std::vector<ValidationError> errors;

        explicit operator bool() const { return valid; }
    };

    class Registry {
    public:
        Result<Symbol*> find_symbol(const std::string& name);
        Result<void> register_symbol(std::unique_ptr<Symbol> sym, const std::string& prefix = "");
        Result<void> register_computed(std::unique_ptr<ComputedSymbol> sym, const std::string& prefix = "");
        void register_validation(ValidationNode* node);
        void register_node(std::unique_ptr<Node> node);
        void add_root_menu(Menu* menu);
        
        // Finalize dependencies after parsing
        void link();

        // [NEW] Global validation routine
        ValidationResult validate_all() const;

        const std::unordered_map<std::string, std::unique_ptr<Symbol>>& get_symbols() const { return symbols; }
        const std::unordered_map<std::string, std::unique_ptr<ComputedSymbol>>& get_computed() const { return computed_symbols; }
        const std::vector<ValidationNode*>& get_validations() const { return validations; }
        const std::vector<Menu*>& get_menus() const { return root_menus; }

        void notify_change(Node* changed_node);

        void set_title(std::string t) { title = std::move(t); }
        const std::string& get_title() const { return title; }

        // Include tree
        void set_include_root(std::string path, std::string display);
        void record_include(const std::string& parent, const std::string& child,
                            const std::string& display, const std::string& ns,
                            bool conditional, bool active);
        const IncludeNode& get_include_tree() const { return include_root; }

        // Meta-programming
        void register_macro(MacroDefinition macro);
        const MacroDefinition* find_macro(const std::string& name) const;

        // Generators
        void add_generator(std::unique_ptr<GenerateNode> gen);
        const std::vector<std::unique_ptr<GenerateNode>>& get_generators() const { return generators; }

        // Persistence
        Result<void> load_cache(const std::string& path);
        Result<void> save_cache(const std::string& path);

    private:
        std::string title = "Zconfig Project";
        std::unordered_map<std::string, std::unique_ptr<Symbol>> symbols;
        std::unordered_map<std::string, std::unique_ptr<ComputedSymbol>> computed_symbols;
        std::unordered_map<std::string, MacroDefinition> macros;
        std::vector<std::unique_ptr<GenerateNode>> generators;
        std::vector<ValidationNode*> validations;
        std::vector<std::unique_ptr<Node>> all_nodes;
        std::vector<Menu*>     root_menus;
        IncludeNode             include_root;
    };
}
