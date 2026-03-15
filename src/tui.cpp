// ── src/tui.cpp ───────────────────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#include <zconfig/tui.hpp>
#include <zconfig/generator.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <algorithm>
#include <map>
#include <set>

namespace zconfig::tui {

using namespace ftxui;

// ─── helpers ─────────────────────────────────────────────────────────────────

static std::string value_str(const Symbol* sym) {
    const Value& v = std::holds_alternative<std::monostate>(sym->user_value)
                   ? sym->computed_value : sym->user_value;
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v) ? "true" : "false";
    if (std::holds_alternative<int64_t>(v))
        return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<std::string>(v))
        return "\"" + std::get<std::string>(v) + "\"";
    if (std::holds_alternative<std::vector<std::string>>(v)) {
        const auto& vec = std::get<std::vector<std::string>>(v);
        if (vec.empty()) return "(none)";
        std::string s;
        for (size_t i = 0; i < vec.size(); ++i) { if (i) s += ", "; s += vec[i]; }
        return s;
    }
    return "(unset)";
}

static bool bool_val(const Symbol* sym) {
    const Value& v = std::holds_alternative<std::monostate>(sym->user_value)
                   ? sym->computed_value : sym->user_value;
    return std::holds_alternative<bool>(v) && std::get<bool>(v);
}

static bool node_visible(const Node* n) {
    return n && n->is_effectively_visible();
}

// ─── Engine construction ──────────────────────────────────────────────────────

Engine::Engine(Registry& r, std::string path)
    : reg(r), config_path(std::move(path)),
      screen(ScreenInteractive::Fullscreen())
{
    searcher = std::make_unique<FuzzySearcher>(reg);
    if (!reg.get_include_tree().path.empty())
        state.expanded_inc.insert(reg.get_include_tree().path);
    rebuild_inc_flat();
    rebuild_rows();
    root_comp = make_root();

    marquee_thread = std::thread([this] {
        while (!marquee_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            marquee_offset++;
            screen.PostEvent(Event::Custom);
        }
    });
}

Engine::~Engine() {
    marquee_stop = true;
    if (marquee_thread.joinable())
        marquee_thread.join();
}

void Engine::run() {
    screen.Loop(root_comp);
}

// ─── flat list builders ───────────────────────────────────────────────────────

void Engine::rebuild_inc_flat() {
    flat_inc.clear();
    std::function<void(const IncludeNode&, int)> walk = [&](const IncludeNode& n, int d) {
        bool exp = state.expanded_inc.count(n.path) > 0;
        flat_inc.push_back({&n, d, exp});
        if (exp)
            for (const auto& c : n.children) walk(c, d + 1);
    };
    walk(reg.get_include_tree(), 0);
    state.inc_sel = std::clamp(state.inc_sel, 0, std::max(0, (int)flat_inc.size() - 1));
}

void Engine::rebuild_rows() {
    flat_rows.clear();

    std::string filter;
    if (!flat_inc.empty() && state.inc_sel > 0)
        filter = flat_inc[state.inc_sel].node->path;

    std::set<Node*> emitted;

    std::function<void(Menu*, int)> walk = [&](Menu* m, int depth) {
        bool pass = filter.empty() || m->source_file == filter;
        if (pass) {
            FlatRow hdr;
            hdr.is_header    = true;
            hdr.header_label  = m->label;
            hdr.header_source = m->source_file;
            hdr.depth         = depth;
            flat_rows.push_back(hdr);
            for (auto* c : m->children) {
                if (dynamic_cast<Menu*>(c)) continue;
                emitted.insert(c);
                flat_rows.push_back({false, {}, {}, c, depth});
            }
        } else {
            for (auto* c : m->children)
                if (!dynamic_cast<Menu*>(c)) emitted.insert(c);
        }
        for (auto* c : m->children)
            if (auto* sub = dynamic_cast<Menu*>(c)) walk(sub, depth + 1);
    };

    for (auto* m : reg.get_menus()) walk(m, 0);

    // Top-level symbols not attached to any menu (from included flat files).
    std::map<std::string, std::vector<Symbol*>> orphans;
    for (const auto& [name, sym] : reg.get_symbols()) {
        if (emitted.count(sym.get()) == 0) {
            if (filter.empty() || sym->source_file == filter)
                orphans[sym->source_file].push_back(sym.get());
        }
    }
    for (auto& [src, syms] : orphans) {
        std::sort(syms.begin(), syms.end(), [](const Symbol* a, const Symbol* b) {
            return a->name < b->name;
        });
        std::string lbl = src;
        if (auto p = src.rfind('/'); p != std::string::npos) lbl = src.substr(p + 1);
        flat_rows.push_back({true, lbl, src, nullptr, 0});
        for (auto* s : syms)
            flat_rows.push_back({false, {}, {}, s, 0});
    }

    state.node_sel = std::clamp(state.node_sel, 0,
                                std::max(0, (int)flat_rows.size() - 1));
}

// ─── accessors ───────────────────────────────────────────────────────────────

FlatRow* Engine::focused_row() {
    if (flat_rows.empty()) return nullptr;
    return &flat_rows[state.node_sel];
}

Symbol* Engine::focused_sym() {
    auto* fr = focused_row();
    if (!fr || fr->is_header) return nullptr;
    return dynamic_cast<Symbol*>(fr->node);
}

// ─── navigation ──────────────────────────────────────────────────────────────

void Engine::nav_inc_up() {
    if (state.inc_sel > 0) {
        state.inc_sel--;
        rebuild_rows();
    }
}
void Engine::nav_inc_down() {
    if (state.inc_sel < (int)flat_inc.size() - 1) {
        state.inc_sel++;
        rebuild_rows();
    }
}
void Engine::toggle_inc_expand() {
    if (flat_inc.empty()) return;
    const auto& fi = flat_inc[state.inc_sel];
    if (fi.expanded)
        state.expanded_inc.erase(fi.node->path);
    else
        state.expanded_inc.insert(fi.node->path);
    rebuild_inc_flat();
    rebuild_rows();
}

void Engine::nav_row_up() {
    if (state.node_sel > 0) state.node_sel--;
    while (state.node_sel > 0 && flat_rows[state.node_sel].is_header)
        state.node_sel--;
}
void Engine::nav_row_down() {
    if (state.node_sel < (int)flat_rows.size() - 1) state.node_sel++;
    while (state.node_sel < (int)flat_rows.size() - 1 && flat_rows[state.node_sel].is_header)
        state.node_sel++;
}

void Engine::toggle_row() {
    auto* sym = focused_sym();
    if (!sym || sym->type != Type::Bool) return;
    sym->user_value = !bool_val(sym);
    reg.notify_change(sym);
    dirty = true;
}

void Engine::activate_row() {
    auto* sym = focused_sym();
    if (!sym) return;
    switch (sym->type) {
        case Type::Bool:   toggle_row(); break;
        case Type::Enum:   open_modal(ModalKind::EnumEdit,   sym); break;
        case Type::Int:    open_modal(ModalKind::IntEdit,    sym); break;
        case Type::String: open_modal(ModalKind::StringEdit, sym); break;
        case Type::Set:    open_modal(ModalKind::SetEdit,    sym); break;
        default: break;
    }
}

void Engine::ensure_row_visible(int panel_h) {
    int content_h = std::max(1, panel_h - 5); // title + sep + help block
    if (state.node_sel < state.node_scroll)
        state.node_scroll = state.node_sel;
    if (state.node_sel >= state.node_scroll + content_h)
        state.node_scroll = state.node_sel - content_h + 1;
    state.node_scroll = std::max(0, state.node_scroll);
}

void Engine::ensure_inc_visible(int panel_h) {
    int content_h = std::max(1, panel_h - 1);
    if (state.inc_sel < state.inc_scroll)
        state.inc_scroll = state.inc_sel;
    if (state.inc_sel >= state.inc_scroll + content_h)
        state.inc_scroll = state.inc_sel - content_h + 1;
    state.inc_scroll = std::max(0, state.inc_scroll);
}

// ─── modal ────────────────────────────────────────────────────────────────────

void Engine::open_modal(ModalKind kind, Symbol* sym) {
    modal.kind       = kind;
    modal.target_sym = sym;
    modal.lines.clear();
    modal.str_buf    = "";
    modal.int_val    = 0;
    modal.enum_idx   = 0;
    modal.set_selections.clear();

    if (!sym) return;
    const Value& v = std::holds_alternative<std::monostate>(sym->user_value)
                   ? sym->computed_value : sym->user_value;

    if (kind == ModalKind::StringEdit && std::holds_alternative<std::string>(v))
        modal.str_buf = std::get<std::string>(v);
    if (kind == ModalKind::IntEdit && std::holds_alternative<int64_t>(v))
        modal.int_val = (int)std::get<int64_t>(v);
    if (kind == ModalKind::EnumEdit && std::holds_alternative<std::string>(v)) {
        const auto& cur = std::get<std::string>(v);
        for (size_t i = 0; i < sym->meta.choices.size(); ++i)
            if (sym->meta.choices[i] == cur) { modal.enum_idx = (int)i; break; }
    }
    if (kind == ModalKind::SetEdit && std::holds_alternative<std::vector<std::string>>(v))
        for (const auto& s : std::get<std::vector<std::string>>(v))
            modal.set_selections[s] = true;
}

void Engine::close_modal() { modal.kind = ModalKind::None; }

void Engine::apply_modal() {
    if (!modal.target_sym) { close_modal(); return; }
    Symbol* s = modal.target_sym;
    switch (modal.kind) {
        case ModalKind::StringEdit: s->user_value = modal.str_buf; break;
        case ModalKind::IntEdit:    s->user_value = (int64_t)modal.int_val; break;
        case ModalKind::EnumEdit:
            if (modal.enum_idx >= 0 && modal.enum_idx < (int)s->meta.choices.size())
                s->user_value = s->meta.choices[modal.enum_idx];
            break;
        case ModalKind::SetEdit: {
            std::vector<std::string> res;
            for (const auto& [k, v] : modal.set_selections) if (v) res.push_back(k);
            s->user_value = res;
        } break;
        default: break;
    }
    reg.notify_change(s);
    dirty = true;
    close_modal();
}

void Engine::try_save() {
    for (const auto& [name, sym] : reg.get_symbols()) {
        if (std::holds_alternative<std::monostate>(sym->user_value)) {
            sym->user_value = sym->computed_value;
        }
    }

    auto res = reg.save_cache(config_path);
    if (!res) {
        modal.kind  = ModalKind::SaveFail;
        modal.title = "error";
        modal.lines = std::vector<std::string>{"failed to save cache: " + res.error().message};
        return;
    }

    auto gen = zconfig::Generator::run(reg);
    if (!gen) {
        modal.kind  = ModalKind::SaveFail;
        modal.title = "error";
        modal.lines = std::vector<std::string>{"failed to export outputs: " + gen.error().message};
        return;
    }

    dirty = false;
    modal.kind  = ModalKind::SaveOk;
    modal.title = "saved";
    modal.lines = std::vector<std::string>{"configuration saved to " + config_path + " and exported successfully."};
}

// ─── rendering ───────────────────────────────────────────────────────────────

Element Engine::render_inc_row(const FlatInc& fi, bool sel) {
    bool has_ch = !fi.node->children.empty();
    auto indent = text(std::string(fi.depth * 2, ' '));
    auto toggle = text(has_ch ? (fi.expanded ? "▾ " : "▸ ") : "  ") | dim;
    auto icon   = text("◈ ") | (fi.depth == 0 ? bold : dim);
    auto name   = text(fi.node->display.empty() ? fi.node->path : fi.node->display);

    Element ns_badge   = text("");
    Element cond_badge = text("");
    if (!fi.node->ns.empty())
        ns_badge = hbox({ text(" "), text("as " + fi.node->ns) | dim });
    if (fi.node->conditional) {
        if (fi.node->active)
            cond_badge = hbox({ text(" "), text("when ✓") | dim });
        else
            cond_badge = hbox({ text(" "), text("when ✗") | color(Color::Yellow) | dim });
    }

    auto row = hbox({ indent, toggle, icon, name | flex, ns_badge, cond_badge });
    if (sel) return row | inverted;
    if (!fi.node->active) return row | dim;
    return row;
}

Element Engine::render_config_row(const FlatRow& fr, bool sel) {
    if (fr.is_header) {
        return hbox({
            text(std::string(fr.depth * 2, ' ')),
            text("↳ ") | dim,
            text(fr.header_label) | bold,
            filler() | dim
        }) | dim;
    }

    Node* n = fr.node;
    bool visible = node_visible(n);

    Element icon;
    std::string val_text;
    bool is_computed = (dynamic_cast<ComputedSymbol*>(n) != nullptr);

    if (auto* sym = dynamic_cast<Symbol*>(n)) {
        if (sym->type == Type::Bool) {
            bool on = bool_val(sym);
            icon = text(on ? "[✓]" : "[ ]") | (on ? bold : dim);
        } else if (sym->type == Type::Enum || sym->type == Type::Set) {
            icon = text("[·]") | dim;
        } else {
            icon = text("[~]") | dim;
        }
        val_text = value_str(sym);
    } else if (auto* cs = dynamic_cast<ComputedSymbol*>(n)) {
        icon = text("[=]") | dim;
        const Value& v = cs->value;
        if      (std::holds_alternative<bool>(v))        val_text = std::get<bool>(v) ? "true" : "false";
        else if (std::holds_alternative<int64_t>(v))     val_text = std::to_string(std::get<int64_t>(v));
        else if (std::holds_alternative<std::string>(v)) val_text = std::get<std::string>(v);
        else val_text = "(unset)";
    } else {
        return text("(unknown node)") | dim;
    }

    std::string label, sym_name;
    bool has_when = (n->when_condition != nullptr);
    int  depth    = fr.depth;

    if (auto* sym = dynamic_cast<Symbol*>(n)) {
        label    = sym->meta.label.empty() ? sym->name : sym->meta.label;
        sym_name = sym->name;
    } else if (auto* cs = dynamic_cast<ComputedSymbol*>(n)) {
        label    = cs->name;
        sym_name = cs->name;
    }

    auto dep_badge = has_when
        ? hbox({ text(" "), text("when …") | dim })
        : text("");

    // Red ⚠ if the current value fails range/pattern validation.
    Element valid_badge = text("");
    if (auto* sym = dynamic_cast<Symbol*>(n)) {
        const Value& cur = (sym->user_value != Value{}) ? sym->user_value : sym->computed_value;
        if (!sym->validate(cur))
            valid_badge = text(" ⚠") | color(Color::Red);
    }

    auto row = hbox({
        text(std::string(depth * 2, ' ')),
        text(" "),
        icon | size(WIDTH, EQUAL, 4),
        text(label) | flex,
        text(" "),
        text(sym_name) | dim,
        dep_badge,
        valid_badge,
        text("  "),
        text(val_text) | dim,
        is_computed ? text("") : text(" ›") | dim
    });

    if (!visible) row = row | dim;
    if (sel)      return row | inverted;
    return row;
}

Element Engine::render_help(Node* node) {
    if (!node) return text("  select an option to see its description.") | dim;

    Elements lines;
    auto add = [&](std::string s) { lines.push_back(text("  " + s) | dim); };

    if (auto* sym = dynamic_cast<Symbol*>(node)) {
        static const char* type_names[] = {"bool","int","string","enum","set","void"};
        add(sym->name + "  ·  " + type_names[(int)sym->type]
            + (!sym->source_file.empty() ? "  ·  " + sym->source_file : ""));

        if (!sym->meta.help.empty())
            lines.push_back(text("  " + sym->meta.help) | bold);

        if (sym->meta.range)
            add("range: " + std::to_string(sym->meta.range->min)
                          + " .. " + std::to_string(sym->meta.range->max));
        if (!sym->meta.pattern.empty())
            add("pattern: " + sym->meta.pattern);

        if (!sym->meta.choices.empty()) {
            std::string c = "choices: ";
            for (size_t i = 0; i < sym->meta.choices.size(); ++i) {
                if (i) c += " | ";
                c += sym->meta.choices[i];
                auto it = sym->meta.choices_meta.find(sym->meta.choices[i]);
                if (it != sym->meta.choices_meta.end() && !it->second.label.empty())
                    c += " (" + it->second.label + ")";
            }
            add(c);
        }

        if (sym->when_condition)
            add(sym->is_effectively_visible()
                ? "dependency: satisfied"
                : "dependency: not satisfied (option inactive)");

        if (sym->meta.danger == DangerLevel::Warning)
            lines.push_back(text("  ⚠ warning: use with care") | color(Color::Yellow));
        else if (sym->meta.danger == DangerLevel::Critical)
            lines.push_back(text("  ✗ critical: expert only") | color(Color::Red));

        if (!sym->meta.tags.empty()) {
            std::string t = "tags:";
            for (const auto& tag : sym->meta.tags) t += " #" + tag;
            add(t);
        }

    } else if (auto* cs = dynamic_cast<ComputedSymbol*>(node)) {
        add(cs->name + "  ·  computed (read-only)"
            + (!cs->source_file.empty() ? "  ·  " + cs->source_file : ""));
        add("this value is derived automatically and cannot be edited.");
    }

    if (lines.empty()) add("(no description available)");
    return vbox(std::move(lines));
}

Element Engine::render_tab_bar() {
    return hbox({
        text(" zconfig v0.0.1 ") | bold,
        separator(),
        filler(),
        text("[?] help  [s] save  [d] default  [q] quit  [v] validate  [/] search") | dim,
        text(" "),
    }) | border;
}

Element Engine::render_banner() {
    auto v = reg.validate_all();
    bool all_ok = v.valid;

    int modified = 0;
    for (const auto& [n, s] : reg.get_symbols())
        if (!std::holds_alternative<std::monostate>(s->user_value)) modified++;

    std::string val_summary;
    if (!v.valid && !v.errors.empty()) {
        if (v.errors.size() == 1) {
            val_summary = "  ✗ " + v.errors.front().message;
        } else {
            std::string combined;
            for (const auto& err : v.errors) combined += " ✗ " + err.message + "   ·  ";
            int len = (int)combined.size();
            int off = marquee_offset % len;
            val_summary = (combined + combined).substr(off, 120);
        }
    }

    auto dot = text("●") | (all_ok ? bold : color(Color::Red));
    auto lbl = text(all_ok ? " valid" : " invalid")
             | (all_ok ? Decorator(bold) : color(Color::Red));
    auto sep = text("  │  ") | dim;
    auto mods = text(std::to_string(modified) + " modified") | dim;
    auto vsum = val_summary.empty() ? text("") : (text(val_summary) | color(Color::Red));

    return hbox({ text(" "), dot, lbl, sep, mods,
                  text("  "), vsum | flex, text(" ") }) | border;
}

Element Engine::render_status() {
    bool right = state.focus_right;
    auto hint = [](std::string k, std::string a) {
        return hbox({ text("[" + k + "]") | dim, text(" " + a + "  ") | dim });
    };
    return hbox({
        text(" ") | dim,
        text(right ? "options" : "includes") | dim,
        text("  ") | dim,
        hint("↑↓", "nav"),
        hint("space", right ? "toggle" : "expand"),
        hint("enter", right ? "edit" : "select"),
        hint(right ? "←" : "→", right ? "back to tree" : "into options"),
        filler(),
        hint("d", "default"),
        hint("s", "save"),
        hint("?", "help"),
    }) | border;
}

Element Engine::render_left(int h) {
    ensure_inc_visible(h);
    Elements rows;
    rows.push_back(hbox({ text(" ◈ ") | dim, text("includes") | bold }) | inverted);

    int start = state.inc_scroll;
    int end   = std::min((int)flat_inc.size(), start + h - 1);
    for (int i = start; i < end; ++i)
        rows.push_back(render_inc_row(flat_inc[i], i == state.inc_sel && !state.focus_right));

    if ((int)flat_inc.size() > h - 1) {
        rows.push_back(
            text(std::to_string(state.inc_scroll + 1) + "/" + std::to_string(flat_inc.size()))
            | dim | align_right);
    }

    return vbox(std::move(rows)) | reflect(left_box) | flex;
}

Element Engine::render_right(int h) {
    ensure_row_visible(h);
    Elements rows;

    std::string title = "options";
    if (!flat_inc.empty() && state.inc_sel > 0)
        title = flat_inc[state.inc_sel].node->display;
    rows.push_back(hbox({ text(" ◈ ") | dim, text(title) | bold }) | inverted);

    int start  = state.node_scroll;
    int help_h = 4;
    int list_h = std::max(1, h - 1 - help_h - 1);
    int end    = std::min((int)flat_rows.size(), start + list_h);

    for (int i = start; i < end; ++i)
        rows.push_back(render_config_row(flat_rows[i], i == state.node_sel && state.focus_right));

    rows.push_back(separator() | dim);

    Node* focused_node = nullptr;
    if (!flat_rows.empty() && !flat_rows[state.node_sel].is_header)
        focused_node = flat_rows[state.node_sel].node;
    rows.push_back(render_help(focused_node));

    return vbox(std::move(rows)) | reflect(right_box) | flex;
}

Element Engine::render_modal() {
    if (modal.kind == ModalKind::None) return text("");

    Element body;
    Element footer;
    std::string title = modal.title;

    // Subdued hint text: GrayLight on black bg — readable without the SGR dim
    // attribute which would bleed through the clear_under reset.
    auto hint = [](std::string s) -> Element {
        return text(s) | color(Color::GrayLight);
    };

    if (modal.kind == ModalKind::Help) {
        title = "help";
        Elements lines;
        for (const auto& l : modal.lines)
            lines.push_back(text(l) | color(Color::GrayLight));
        body   = vbox(std::move(lines)) | flex;
        footer = hbox({ filler(), hint(" [esc] close ") });

    } else if (modal.kind == ModalKind::SaveOk || modal.kind == ModalKind::SaveFail) {
        Elements lines;
        for (const auto& l : modal.lines)
            lines.push_back(text(l) | (modal.kind == ModalKind::SaveFail ? color(Color::Red) : bold));
        body   = vbox(std::move(lines)) | flex;
        footer = hbox({ filler(), hint(" [esc] close ") });

    } else if (modal.kind == ModalKind::ExitConfirm) {
        title  = "unsaved changes";
        body   = vbox({
            text("you have unsaved changes.") | color(Color::GrayLight),
            text("quit anyway?") | bold,
        }) | flex;
        footer = hbox({
            hint(" [enter] quit without saving "),
            hint("  [s] save & quit "),
            filler(),
            hint(" [esc] cancel "),
        });

    } else if (modal.kind == ModalKind::Validation) {
        title = "validation";
        Elements items;
        auto v = reg.validate_all();
        if (!v.valid) {
            for (int i = 0; i < (int)v.errors.size(); ++i) {
                bool sel = (i == modal.enum_idx);
                auto row = hbox({
                    text(sel ? " ● " : "   ") | bold,
                    text(" ✗ " + v.errors[i].message)
                });
                if (sel) row = row | bgcolor(Color::Red) | color(Color::Black) | bold;
                else row = row | color(Color::Red);
                items.push_back(row);
            }
        } else {
            items.push_back(text("  all validation rules pass.") | bold);
        }
        body   = vbox(std::move(items)) | flex;
        footer = hbox({ hint(" [uparrow/downarrow] select  [enter] jump to source "), filler(), hint(" [esc] close ") });

    } else if (modal.kind == ModalKind::FullHelp) {
        title = "extended help";
        body = render_full_help(modal.target_sym) | flex;
        footer = hbox({ filler(), hint(" [esc/enter] close ") });

    } else if (modal.kind == ModalKind::EnumEdit) {
        title = modal.target_sym
              ? (modal.target_sym->name + " — " + (modal.target_sym->meta.label.empty()
                    ? "choose value" : modal.target_sym->meta.label))
              : "choose value";
        Elements opts;
        if (modal.target_sym) {
            for (int i = 0; i < (int)modal.target_sym->meta.choices.size(); ++i) {
                bool sel = (i == modal.enum_idx);
                const auto& ch = modal.target_sym->meta.choices[i];
                auto it = modal.target_sym->meta.choices_meta.find(ch);
                bool enabled = true;
                if (it != modal.target_sym->meta.choices_meta.end() && it->second.when_condition) {
                    auto v = it->second.when_condition->evaluate();
                    enabled = std::holds_alternative<bool>(v) && std::get<bool>(v);
                }
                std::string ch_label;
                if (it != modal.target_sym->meta.choices_meta.end() && !it->second.label.empty())
                    ch_label = "  " + it->second.label;

                auto row = hbox({
                    text(sel ? " ● " : " ○ ") | (sel ? bold : color(Color::GrayLight)),
                    text(ch)  | (sel ? bold : color(Color::GrayLight)),
                    text(ch_label) | color(Color::GrayLight) | flex,
                });
                if (!enabled) row = row | color(Color::GrayDark);
                if (sel)      row = row | inverted;
                opts.push_back(row);
            }
        }
        body   = vbox(std::move(opts)) | flex;
        footer = hbox({
            hint(" [↑↓] select "),
            filler(),
            text(" [enter] apply ") | bold,
            hint(" [esc] cancel "),
        });

    } else if (modal.kind == ModalKind::IntEdit) {
        title = modal.target_sym
              ? (modal.target_sym->name
                 + (modal.target_sym->meta.label.empty() ? "" : " — " + modal.target_sym->meta.label))
              : "edit value";
        std::string range_hint;
        if (modal.target_sym && modal.target_sym->meta.range) {
            auto& r = *modal.target_sym->meta.range;
            range_hint = "range: " + std::to_string(r.min) + " .. " + std::to_string(r.max);
        }
        body = vbox({
            text("  value") | color(Color::GrayLight),
            hbox({ text("  "), text(std::to_string(modal.int_val)) | bold | underlined }),
            text("  " + range_hint) | color(Color::GrayLight),
        }) | flex;
        footer = hbox({
            hint(" [↑↓] adjust  [0-9] type "),
            filler(),
            text(" [enter] apply ") | bold,
            hint(" [esc] cancel "),
        });

    } else if (modal.kind == ModalKind::StringEdit) {
        title = modal.target_sym ? modal.target_sym->name : "edit value";
        std::string pat_hint;
        if (modal.target_sym && !modal.target_sym->meta.pattern.empty())
            pat_hint = "pattern: " + modal.target_sym->meta.pattern;
        body = vbox({
            text("  value") | color(Color::GrayLight),
            hbox({
                text("  "),
                text(modal.str_buf + "▌") | underlined | bold
            }),
            text("  " + pat_hint) | color(Color::GrayLight),
        }) | flex;
        footer = hbox({
            hint(" [backspace] delete "),
            filler(),
            text(" [enter] apply ") | bold,
            hint(" [esc] cancel "),
        });

    } else if (modal.kind == ModalKind::SetEdit) {
        title = modal.target_sym
              ? modal.target_sym->name + " — select flags"
              : "select flags";
        Elements opts;
        if (modal.target_sym) {
            int cursor = modal.int_val;
            for (int i = 0; i < (int)modal.target_sym->meta.choices.size(); ++i) {
                const auto& ch = modal.target_sym->meta.choices[i];
                bool on  = modal.set_selections.count(ch) && modal.set_selections.at(ch);
                bool cur = (i == cursor);
                auto row = hbox({
                    text(on ? " [✓] " : " [ ] ") | (on ? bold : color(Color::GrayLight)),
                    text(ch) | (on ? bold : color(Color::GrayLight)),
                });
                if (cur) row = row | inverted;
                opts.push_back(row);
            }
        }
        body   = vbox(std::move(opts)) | flex;
        footer = hbox({
            hint(" [↑↓] nav  [space] toggle "),
            filler(),
            text(" [enter] apply ") | bold,
            hint(" [esc] cancel "),
        });
    } else if (modal.kind == ModalKind::Search) {
        title = "fuzzy search: /" + modal.str_buf;
        Elements items;
        if (modal.search_results.empty() && !modal.str_buf.empty()) {
            items.push_back(text("  no matches found.") | color(Color::GrayLight));
        } else {
            for (int i = 0; i < (int)modal.search_results.size(); ++i) {
                bool sel = (i == modal.int_val);
                const auto& res = modal.search_results[i];
                std::string label = "(unknown)";
                if (auto* sym = dynamic_cast<Symbol*>(res.node)) {
                    label = sym->name;
                    if (!sym->meta.label.empty()) label += " — " + sym->meta.label;
                } else if (auto* cs = dynamic_cast<ComputedSymbol*>(res.node)) {
                    label = cs->name + " (computed)";
                }
                
                auto row = hbox({
                    text(sel ? " ▶ " : "   ") | (sel ? bold : color(Color::GrayLight)),
                    text(label) | (sel ? bold : color(Color::GrayLight)),
                    filler(),
                    text("score: " + std::to_string(res.score) + " ") | color(Color::GrayDark)
                });
                if (sel) row = row | inverted;
                items.push_back(row);
                
                if (i >= 15) {
                    items.push_back(text("  ... " + std::to_string(modal.search_results.size() - i - 1) + " more") | color(Color::GrayDark));
                    break;
                }
            }
        }
        body   = vbox(std::move(items)) | flex;
        footer = hbox({ hint(" [uparrow/downarrow] select  [enter] jump to source "), filler(), hint(" [esc] cancel ") });
    }

    // The popup is built as:
    //   borderRounded → wraps all content including border chars with black bgcolor
    //   clear_under   → resets every pixel under the popup to default style FIRST
    //                   (this clears any dim=true pixels from the body dbox layer)
    //   center        → places the cleared+styled popup in the middle of the screen
    //
    // Body uses body | dim for the background — the clear_under ensures none of that
    // dim bleeds into the popup area.
    auto popup_content = vbox({
        hbox({ text(" " + title + " ") | bold, filler() }) | inverted,
        separator(),
        body | flex,
        separator(),
        footer,
    });

    return (popup_content | borderRounded | bgcolor(Color::Black))
         | size(WIDTH,  LESS_THAN, 64)
         | size(HEIGHT, LESS_THAN, 26)
         | clear_under
         | center;
}

Element Engine::render_full_help(Node* node) {
    if (!node) return text(" (no node selected)");

    Elements rows;
    auto header = [](std::string s) { 
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return text(s) | bold | color(Color::White); 
    };
    auto field  = [](std::string k, std::string v) {
        std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c){ return std::tolower(c); });
        return hbox({ text("  " + k + ": ") | dim, text(v) });
    };

    if (auto* sym = dynamic_cast<Symbol*>(node)) {
        rows.push_back(hbox({ header("symbol: "), text(sym->name) | bold | color(Color::White) }));
        rows.push_back(field("type", std::string(sym->type == Type::Bool ? "bool" : 
                                       sym->type == Type::Int ? "int" :
                                       sym->type == Type::String ? "string" :
                                       sym->type == Type::Enum ? "enum" : "set")));
        if (!sym->source_file.empty()) rows.push_back(field("source", sym->source_file));
        rows.push_back(separator());

        if (!sym->meta.label.empty()) rows.push_back(field("label", sym->meta.label));
        if (!sym->meta.help.empty()) {
            rows.push_back(text("  help:") | dim);
            rows.push_back(paragraph("    " + sym->meta.help) | color(Color::GrayLight));
        }

        if (!sym->defaults.empty()) {
            rows.push_back(separator() | dim);
            rows.push_back(text("  default values:") | dim);
            for (const auto& d : sym->defaults) {
                std::string s = "    · value expr";
                if (d.condition) s += " (conditional)";
                rows.push_back(text(s) | color(Color::GrayLight));
            }
        }

        if (sym->implications.size() > 0) {
            rows.push_back(separator() | dim);
            rows.push_back(text("  implications/selects:") | dim);
            rows.push_back(text("    · this symbol has " + std::to_string(sym->implications.size()) + " dependency rules.") | color(Color::GrayLight));
        }

        if (sym->meta.range) {
            rows.push_back(field("range", std::to_string(sym->meta.range->min) + " to " + std::to_string(sym->meta.range->max)));
        }
        if (!sym->meta.pattern.empty()) {
            rows.push_back(field("pattern", sym->meta.pattern));
        }

    } else if (auto* cs = dynamic_cast<ComputedSymbol*>(node)) {
        rows.push_back(hbox({ header("computed: "), text(cs->name) | bold | color(Color::White) }));
        rows.push_back(text("  this is a derived value.") | dim);
    }

    return vbox(std::move(rows)) | vscroll_indicator | frame | flex;
}

// ─── root component ───────────────────────────────────────────────────────────

Component Engine::make_root() {
    return CatchEvent(
        Renderer([this] {
            int tw = screen.dimx();
            int th = screen.dimy();

            int panel_h = std::max(4, th - 9);
            int left_w  = std::max(20, tw / 4);

            auto tab    = render_tab_bar();
            auto banner = render_banner();
            auto left   = render_left(panel_h);
            auto right  = render_right(panel_h);
            auto status = render_status();

            auto panels = hbox({
                left  | size(WIDTH, EQUAL, left_w),
                separator() | dim,
                right | flex,
            }) | flex;

            auto body = vbox({ tab, banner, panels, status });

            if (modal.kind != ModalKind::None) {
                // body | dim provides visual contrast; clear_under inside
                // render_modal() clears the popup area back to undimmed defaults.
                return dbox({ body | dim, render_modal() });
            }
            return body;
        }),
        [this](Event ev) -> bool {
            // ── modal input ──────────────────────────────────────────────
            if (modal.kind != ModalKind::None) {
                if (ev == Event::Escape) { close_modal(); return true; }

                if (ev == Event::Return) {
                    if (modal.kind == ModalKind::Help     ||
                        modal.kind == ModalKind::SaveOk   ||
                        modal.kind == ModalKind::SaveFail)
                        close_modal();
                    else if (modal.kind == ModalKind::Validation) {
                        auto v = reg.validate_all();
                        if (modal.enum_idx >= 0 && modal.enum_idx < (int)v.errors.size()) {
                            Node* src = v.errors[modal.enum_idx].source;
                            if (src && !src->source_file.empty()) {
                                state.expanded_inc.insert(src->source_file);
                                rebuild_inc_flat();
                                for (int i = 0; i < (int)flat_inc.size(); ++i) {
                                    if (flat_inc[i].node->path == src->source_file) {
                                        state.inc_sel = i;
                                        break;
                                    }
                                }
                                rebuild_rows();
                                for (int i = 0; i < (int)flat_rows.size(); ++i) {
                                    if (flat_rows[i].node == src) {
                                        state.node_sel = i;
                                        state.focus_right = true;
                                        ensure_row_visible(right_box.y_max - right_box.y_min);
                                        break;
                                    }
                                }
                            }
                        }
                        close_modal();
                    }
                    else if (modal.kind == ModalKind::ExitConfirm)
                        screen.Exit();
                    else if (modal.kind == ModalKind::Search) {
                        if (modal.int_val >= 0 && modal.int_val < (int)modal.search_results.size()) {
                            Node* src = modal.search_results[modal.int_val].node;
                            if (src && !src->source_file.empty()) {
                                state.expanded_inc.insert(src->source_file);
                                rebuild_inc_flat();
                                for (int i = 0; i < (int)flat_inc.size(); ++i) {
                                    if (flat_inc[i].node->path == src->source_file) {
                                        state.inc_sel = i;
                                        break;
                                    }
                                }
                                rebuild_rows();
                                for (int i = 0; i < (int)flat_rows.size(); ++i) {
                                    if (flat_rows[i].node == src) {
                                        state.node_sel = i;
                                        state.focus_right = true;
                                        ensure_row_visible(right_box.y_max - right_box.y_min);
                                        break;
                                    }
                                }
                            }
                        }
                        close_modal();
                    } else
                        apply_modal();
                    return true;
                }

                if (modal.kind == ModalKind::ExitConfirm &&
                    (ev == Event::Character('s') || ev == Event::Special({0x13}))) {
                    try_save();
                    if (modal.kind == ModalKind::SaveOk) screen.Exit();
                    return true;
                }

                if (modal.kind == ModalKind::Validation) {
                    if (ev == Event::ArrowUp && modal.enum_idx > 0) modal.enum_idx--;
                    if (ev == Event::ArrowDown) {
                        auto v = reg.validate_all();
                        if (modal.enum_idx < (int)v.errors.size() - 1) modal.enum_idx++;
                    }
                    return true;
                }
                if (modal.kind == ModalKind::EnumEdit) {
                    if (ev == Event::ArrowUp && modal.enum_idx > 0) modal.enum_idx--;
                    if (ev == Event::ArrowDown) {
                        int max = modal.target_sym ? (int)modal.target_sym->meta.choices.size()-1 : 0;
                        if (modal.enum_idx < max) modal.enum_idx++;
                    }
                    return true;
                }
                if (modal.kind == ModalKind::FullHelp) {
                    if (ev == Event::Escape || ev == Event::Return) close_modal();
                    return true;
                }
                if (modal.kind == ModalKind::IntEdit) {
                    if (ev == Event::ArrowUp)   modal.int_val++;
                    if (ev == Event::ArrowDown) modal.int_val--;
                    if (modal.target_sym && modal.target_sym->meta.range) {
                        auto& r = *modal.target_sym->meta.range;
                        modal.int_val = (int)std::clamp((int64_t)modal.int_val, r.min, r.max);
                    }
                    if (ev.is_character()) {
                        char c = ev.character()[0];
                        if (c >= '0' && c <= '9') {
                            std::string s = std::to_string(std::abs(modal.int_val));
                            s += c;
                            try { modal.int_val = std::stoi(s); } catch (...) {}
                        } else if (c == '-') {
                            modal.int_val = -modal.int_val;
                        }
                    }
                    return true;
                }
                if (modal.kind == ModalKind::StringEdit) {
                    if (ev == Event::Backspace && !modal.str_buf.empty())
                        modal.str_buf.pop_back();
                    if (ev.is_character()) modal.str_buf += ev.character();
                    return true;
                }
                if (modal.kind == ModalKind::SetEdit) {
                    if (ev == Event::ArrowUp && modal.int_val > 0) modal.int_val--;
                    if (ev == Event::ArrowDown && modal.target_sym) {
                        int max = (int)modal.target_sym->meta.choices.size() - 1;
                        if (modal.int_val < max) modal.int_val++;
                    }
                    if (ev == Event::Character(' ') && modal.target_sym) {
                        int idx = modal.int_val;
                        if (idx >= 0 && idx < (int)modal.target_sym->meta.choices.size()) {
                            const auto& ch = modal.target_sym->meta.choices[idx];
                            modal.set_selections[ch] = !modal.set_selections[ch];
                        }
                    }
                    return true;
                }
                if (modal.kind == ModalKind::Search) {
                    if (ev == Event::ArrowUp && modal.int_val > 0) modal.int_val--;
                    if (ev == Event::ArrowDown && modal.int_val < (int)modal.search_results.size() - 1) modal.int_val++;
                    if (ev == Event::Backspace && !modal.str_buf.empty()) {
                        modal.str_buf.pop_back();
                        if (searcher) modal.search_results = searcher->search(modal.str_buf);
                        modal.int_val = 0;
                    }
                    if (ev.is_character()) {
                        modal.str_buf += ev.character();
                        if (searcher) modal.search_results = searcher->search(modal.str_buf);
                        modal.int_val = 0;
                    }
                    if (ev == Event::Return) {
                        return true; // Handled in global Return block
                    }
                    return true;
                }
                return true;
            }

            // ── mouse events ─────────────────────────────────────────────
            if (ev.is_mouse()) {
                auto& m = ev.mouse();
                int mx = m.x, my = m.y;

                if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
                    int delta = (m.button == Mouse::WheelUp) ? -1 : 1;
                    if (left_box.Contain(mx, my))
                        state.inc_scroll  = std::max(0, state.inc_scroll  + delta);
                    else if (right_box.Contain(mx, my))
                        state.node_scroll = std::max(0, state.node_scroll + delta);
                    return true;
                }

                if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                    if (left_box.Contain(mx, my)) {
                        state.focus_right = false;
                        int clicked = (my - left_box.y_min - 1) + state.inc_scroll;
                        if (clicked >= 0 && clicked < (int)flat_inc.size()) {
                            if (state.inc_sel == clicked)
                                toggle_inc_expand();
                            else {
                                state.inc_sel = clicked;
                                rebuild_rows();
                            }
                        }
                        return true;
                    }
                    if (right_box.Contain(mx, my)) {
                        state.focus_right = true;
                        int clicked = (my - right_box.y_min - 1) + state.node_scroll;
                        if (clicked >= 0 && clicked < (int)flat_rows.size()) {
                            if (!flat_rows[clicked].is_header) {
                                if (state.node_sel == clicked)
                                    activate_row();
                                else
                                    state.node_sel = clicked;
                            } else {
                                state.node_sel = clicked;
                            }
                        }
                        return true;
                    }
                }
                return false;
            }

            // ── global keys ───────────────────────────────────────────────
            if (ev == Event::Character('q') || ev == Event::Escape) {
                if (dirty) {
                    open_modal(ModalKind::ExitConfirm);
                    modal.title = "quit?";
                } else {
                    screen.Exit();
                }
                return true;
            }
            if (ev == Event::Character('s') || ev == Event::Special({0x13})) {
                try_save(); return true;
            }
            if (ev == Event::Character('?') || ev == Event::F1) {
                open_modal(ModalKind::Help);
                modal.title = "help";
                modal.lines = {
                    "navigation",
                    "  ↑ ↓ / jk   move selection within active panel",
                    "  → / l      (includes) expand node or enter options panel",
                    "  ← / h      (options) return to includes panel",
                    "  ← / h      (includes) collapse node",
                    "",
                    "editing",
                    "  space      toggle bool option",
                    "  enter      open editor for current option",
                    "  d          reset option to its default value",
                    "",
                    "general",
                    "  s          save configuration",
                    "  v          show validation report",
                    "  q / esc    quit (prompts if unsaved changes)",
                    "  ?          this help",
                };
                return true;
            }
            if (ev == Event::Character('h') || ev == Event::F2) {
                if (auto* sym = focused_sym()) {
                    open_modal(ModalKind::FullHelp, sym);
                } else if (!flat_rows.empty() && !flat_rows[state.node_sel].is_header) {
                     open_modal(ModalKind::FullHelp, dynamic_cast<Symbol*>(flat_rows[state.node_sel].node));
                }
                return true;
            }
            if (ev == Event::Character('v')) {
                open_modal(ModalKind::Validation);
                modal.title = "validation report";
                return true;
            }
            if (ev == Event::Character('/')) {
                open_modal(ModalKind::Search);
                modal.title = "search";
                if (searcher) {
                    modal.search_results = searcher->search("");
                }
                return true;
            }
            // 'd' = reset focused option to computed default.
            if (ev == Event::Character('d') && state.focus_right) {
                if (auto* sym = focused_sym()) {
                    sym->user_value = std::monostate{};
                    reg.notify_change(sym);
                    dirty = true;
                }
                return true;
            }
            // No Tab — panels switched via ← / → arrow keys (see below).

            // ── panel navigation ──────────────────────────────────────────
            if (!state.focus_right) {
                if (ev == Event::ArrowUp   || ev == Event::Character('k')) { nav_inc_up();   return true; }
                if (ev == Event::ArrowDown || ev == Event::Character('j')) { nav_inc_down(); return true; }
                if (ev == Event::ArrowRight || ev == Event::Character('l') || ev == Event::Return) {
                    bool has_children = !flat_inc.empty() && !flat_inc[state.inc_sel].node->children.empty();
                    bool is_expanded  = !flat_inc.empty() && flat_inc[state.inc_sel].expanded;
                    if (has_children && !is_expanded) {
                        // Node has children but is collapsed → expand it.
                        toggle_inc_expand();
                    } else {
                        // Already expanded, no children, or Return — enter right panel.
                        rebuild_rows();
                        state.focus_right = true;
                    }
                    return true;
                }
                if (ev == Event::ArrowLeft || ev == Event::Character('h')) {
                    // Collapse if expanded, otherwise no-op.
                    if (!flat_inc.empty() && flat_inc[state.inc_sel].expanded)
                        toggle_inc_expand();
                    return true;
                }
                if (ev == Event::Character(' ')) { toggle_inc_expand(); return true; }
            } else {
                // Right panel: ↑↓/jk navigate; ← / h returns to left panel.
                if (ev == Event::ArrowUp   || ev == Event::Character('k')) { nav_row_up();   return true; }
                if (ev == Event::ArrowDown || ev == Event::Character('j')) { nav_row_down(); return true; }
                if (ev == Event::ArrowLeft || ev == Event::Character('h')) {
                    state.focus_right = false;
                    return true;
                }
                if (ev == Event::Character(' '))  { toggle_row();   return true; }
                if (ev == Event::Return)          { activate_row(); return true; }
            }

            return false;
        }
    );
}

} // namespace zconfig::tui
