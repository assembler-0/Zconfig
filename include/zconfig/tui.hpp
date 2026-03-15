// ── include/zconfig/tui.hpp ───────────────────────────────────────────────
// SPDX-License-Identifier: Apache-2.0
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <zconfig/ast.hpp>
#include <zconfig/registry.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>
#include <zconfig/fs.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <atomic>
#include <thread>

namespace zconfig::tui {

enum class ModalKind {
    None, Help, SaveOk, SaveFail, Validation, ExitConfirm,
    EnumEdit, IntEdit, StringEdit, SetEdit, FullHelp,
    Search
};

struct ModalContext {
    ModalKind                kind{ModalKind::None};
    Symbol*                  target_sym{nullptr};
    std::string              title;
    std::vector<std::string> lines;
    std::string              str_buf;
    int                      int_val{0};
    int                      enum_idx{0};
    std::map<std::string, bool> set_selections;
    std::vector<SearchResult> search_results;
};

// One row in the left include-tree panel.
struct FlatInc {
    const IncludeNode* node;
    int                depth;
    bool               expanded;
};

// One row in the right config panel (either a menu header or a node row).
struct FlatRow {
    bool        is_header{false};
    std::string header_label;
    std::string header_source;
    Node*       node{nullptr};
    int         depth{0};
};

struct ConfigState {
    int  inc_sel{0};     // selected row in left panel
    int  node_sel{0};    // selected row in right panel
    bool focus_right{false};
    int  inc_scroll{0};
    int  node_scroll{0};
    std::set<std::string> expanded_inc; // by resolved path
};

class Engine {
public:
    explicit Engine(Registry& reg, std::string config_path);
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

private:
    Registry&                reg;
    std::string              config_path;
    ftxui::ScreenInteractive screen;

    ConfigState  state;
    ModalContext modal;
    std::unique_ptr<FuzzySearcher> searcher;

    std::vector<FlatInc> flat_inc;
    std::vector<FlatRow> flat_rows;

    ftxui::Box left_box;
    ftxui::Box right_box;

    ftxui::Component root_comp;

    bool dirty{false}; // true when user has made changes not yet saved
    
    std::atomic<int> marquee_offset{0};
    std::atomic<bool> marquee_stop{false};
    std::thread marquee_thread;

    void rebuild_inc_flat();
    void rebuild_rows();

    FlatRow* focused_row();
    Symbol*  focused_sym();

    void nav_inc_up();
    void nav_inc_down();
    void toggle_inc_expand();

    void nav_row_up();
    void nav_row_down();
    void activate_row();
    void toggle_row();

    void open_modal(ModalKind kind, Symbol* sym = nullptr);
    void close_modal();
    void apply_modal();
    void try_save();
    void ensure_row_visible(int panel_h);
    void ensure_inc_visible(int panel_h);

    ftxui::Component make_root();
    ftxui::Element   render_left(int h);
    ftxui::Element   render_right(int h);
    ftxui::Element   render_tab_bar();
    ftxui::Element   render_banner();
    ftxui::Element   render_status();
    ftxui::Element   render_modal();
    ftxui::Element   render_full_help(Node* node);
    ftxui::Element   render_inc_row(const FlatInc& fi, bool sel);
    ftxui::Element   render_config_row(const FlatRow& fr, bool sel);
    ftxui::Element   render_help(Node* node);
};

} // namespace zconfig::tui
