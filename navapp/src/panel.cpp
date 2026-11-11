#include "panel.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

// ============================================================================
//                                  Layout
// ============================================================================

struct LayoutStream {
        const std::string &data;
        size_t pos = 0;

        explicit LayoutStream(const std::string &s) : data(s) {}

        bool eof() const { return pos >= data.size(); }
        char peek(size_t n = 0) const {
            return pos + n < data.size() ? data[pos + n] : '\0';
        }
        bool advance(size_t n = 1) {
            if (pos >= data.size())
                return false;
            pos = std::min(pos + n, data.size());
            return true;
        }
};

struct LayoutNode {
        enum Kind { Leaf, HSplit, VSplit } kind = Leaf;
        std::string leaf;
        std::vector<std::unique_ptr<LayoutNode>> children;
        int pane_index = -1;
};

static std::string trim_copy(const std::string &s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

static void skip_ws(LayoutStream &s) {
    while (!s.eof() && std::isspace(static_cast<unsigned char>(s.peek())))
        s.advance();
}

// Reads a leaf until a top-level ',' or ')'. Quotes and balanced parentheses
// are respected, so commands/queries can contain both.
static std::string parse_leaf_text(LayoutStream &s) {
    const size_t start = s.pos;
    int depth = 0;
    char quote = '\0';
    bool escape = false;
    while (!s.eof()) {
        const char c = s.peek();
        if (escape) {
            escape = false;
            s.advance();
            continue;
        }
        if (c == '\\') {
            escape = true;
            s.advance();
            continue;
        }
        if (quote) {
            if (c == quote)
                quote = '\0';
            s.advance();
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            s.advance();
            continue;
        }
        if (c == '(') {
            ++depth;
            s.advance();
            continue;
        }
        if (c == ')') {
            if (depth == 0)
                break;
            --depth;
            s.advance();
            continue;
        }
        if (c == ',' && depth == 0)
            break;
        s.advance();
    }
    return trim_copy(s.data.substr(start, s.pos - start));
}

static std::unique_ptr<LayoutNode> parse_layout_node(LayoutStream &s,
                                                     std::string &error);

static std::vector<std::unique_ptr<LayoutNode>>
parse_layout_list(LayoutStream &s, std::string &error) {
    std::vector<std::unique_ptr<LayoutNode>> out;
    while (true) {
        skip_ws(s);
        if (s.eof() || s.peek() == ')')
            break;
        auto node = parse_layout_node(s, error);
        if (!node) {
            if (error.empty())
                error = "empty pane in layout";
            break;
        }
        out.push_back(std::move(node));
        skip_ws(s);
        if (s.peek() == ',') {
            s.advance();
            continue;
        }
        break;
    }
    return out;
}

static std::unique_ptr<LayoutNode> parse_layout_node(LayoutStream &s,
                                                     std::string &error) {
    skip_ws(s);
    if (s.eof())
        return nullptr;

    if ((s.peek() == 'H' || s.peek() == 'V') && s.peek(1) == '(') {
        const bool horizontal = s.peek() == 'H';
        s.advance(2);
        auto node = std::make_unique<LayoutNode>();
        node->kind = horizontal ? LayoutNode::HSplit : LayoutNode::VSplit;
        node->children = parse_layout_list(s, error);
        skip_ws(s);
        if (s.peek() != ')') {
            error = "missing ')' in layout";
            return nullptr;
        }
        s.advance();
        return node;
    }

    auto node = std::make_unique<LayoutNode>();
    node->kind = LayoutNode::Leaf;
    node->leaf = parse_leaf_text(s);
    if (node->leaf.empty()) {
        error = "empty pane in layout";
        return nullptr;
    }
    return node;
}

static std::unique_ptr<LayoutNode> parse_layout(const std::string &text,
                                                std::string &error) {
    LayoutStream s(text);
    auto root = parse_layout_node(s, error);
    skip_ws(s);
    if (!s.eof() && error.empty())
        error = "unexpected character in layout";
    return root;
}

// ============================================================================
//                                  ANSI
// ============================================================================

static size_t visible_width(const std::string &s) {
    size_t width = 0;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && s[i] != 'm')
                ++i;
            if (i < s.size())
                ++i;
            continue;
        }
        ++width;
        ++i;
    }
    return width;
}

static std::string strip_ansi(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && s[i] != 'm')
                ++i;
            if (i < s.size())
                ++i;
            continue;
        }
        out.push_back(s[i++]);
    }
    return out;
}

static std::string clip_visible(const std::string &s, size_t width) {
    std::string out;
    size_t used = 0;
    bool had_escape = false;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() && s[j] != 'm')
                ++j;
            if (j < s.size())
                ++j;
            out.append(s, i, j - i);
            had_escape = true;
            i = j;
            continue;
        }
        if (used >= width)
            break;
        out.push_back(s[i++]);
        ++used;
    }
    if (had_escape)
        out += "\x1b[0m";
    return out;
}

static std::string pad_visible(std::string s, size_t width) {
    const size_t used = visible_width(s);
    if (used < width)
        s.append(width - used, ' ');
    return s;
}

static std::string sgr_params(unsigned char style) {
    if (style == 0)
        return "";
    const int fg = style & 0x0F;
    const int bg = (style >> 4) & 0x0F;
    std::string params;
    if (fg != 0)
        params += std::to_string(fg < 8 ? 30 + fg : 90 + (fg - 8));
    if (bg != 0) {
        if (!params.empty())
            params += ';';
        params += std::to_string(bg < 8 ? 40 + bg : 100 + (bg - 8));
    }
    return params;
}

static std::string colorize(const std::string &text, unsigned char style,
                            bool force) {
    if (!force && !isatty(STDOUT_FILENO))
        return text;
    const std::string params = sgr_params(style);
    if (params.empty())
        return text;
    return "\x1b[" + params + "m" + text + "\x1b[0m";
}

static char kind_symbol(const Settings &settings, const Dest &dest) {
    if (dest.kind.empty())
        return '-';
    for (const auto &entry : settings.kind_to_symbol_map) {
        if (entry.first == dest.kind)
            return entry.second;
    }
    if (dest.kind == "dest") {
        for (const std::string &field : settings.fields_priority) {
            if (field == "cmd" && !dest.cmd.empty())
                return 'C';
            if (field == "dir" && !dest.dir.empty())
                return 'D';
            if (field == "file" && !dest.file.empty())
                return 'F';
            if (field == "msg" && !dest.msg.empty())
                return 'M';
        }
    }
    return '?';
}

static unsigned char group_style(const Settings &settings,
                                 const std::string &group_name) {
    for (const auto &entry : settings.group_style_map) {
        if (entry.first == group_name)
            return entry.second;
    }
    return 0;
}

// ============================================================================
//                              Data helpers
// ============================================================================

static std::string expand_home(const std::string &path) {
    if (path.empty() || path[0] != '~')
        return path;
    const char *home = std::getenv("HOME");
    if (!home)
        return path;
    if (path.size() == 1)
        return home;
    if (path[1] == '/')
        return std::string(home) + path.substr(1);
    return path;
}

static bool looks_like_path(const std::string &path) {
    return !path.empty() && (path[0] == '/' || path[0] == '~');
}

static bool path_exists(const std::string &path) {
    std::error_code ec;
    return std::filesystem::exists(expand_home(path), ec);
}

static const Dest *find_dest(const std::vector<Dest> &dests,
                             const std::string &name) {
    for (const Dest &dest : dests) {
        if (dest.dest_name == name)
            return &dest;
    }
    return nullptr;
}

static std::string strip_outer_quotes(std::string value) {
    value = trim_copy(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\'')))
        return value.substr(1, value.size() - 2);
    return value;
}

static std::vector<std::string> split_lines(const std::string &s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        const size_t j = s.find('\n', i);
        if (j == std::string::npos) {
            out.push_back(s.substr(i));
            break;
        }
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    if (!out.empty() && out.back().empty())
        out.pop_back();
    return out;
}

static std::vector<std::string> run_shell_lines(const std::string &cmd) {
    if (cmd.empty())
        return {"(no command)"};

    // Keep stderr inside the pane instead of corrupting the TUI.
    const std::string full = cmd + " 2>&1";
    FILE *pipe = popen(full.c_str(), "r");
    if (!pipe)
        return {"! cannot run command: " + cmd};

    std::string out;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, pipe)) > 0)
        out.append(buf, n);
    const int status = pclose(pipe);

    std::vector<std::string> lines = split_lines(out);
    for (std::string &line : lines) {
        // Progress meters use '\r'; keeping it would corrupt the pane.
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
    }
    if (lines.empty())
        lines.push_back("(no output)");
    if (status != -1 && WIFEXITED(status) && WEXITSTATUS(status) != 0)
        lines.push_back("! exit " + std::to_string(WEXITSTATUS(status)));
    return lines;
}

static bool filter_navdict(const std::string &path, const std::string &expr,
                           std::vector<Dest> &out, std::string &error) {
    out.clear();
    Settings settings;
    const int rc = parse_navdict(path, out, settings, error, expr.c_str(),
                                 MCOMPOSE(M_NONE, 0, 1));
    return rc == 0 && error.empty();
}

// ============================================================================
//                                  Panes
// ============================================================================

enum class PaneKind {
    Groups,
    List,
    Detail,
    Stats,
    Attention,
    Dates,
    Cmd,
    Unknown
};

struct PaneSpec {
        PaneKind kind = PaneKind::Unknown;
        std::string arg;
        bool legacy_cmd = false;
};

struct Rect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
};

struct Pane {
        PaneSpec spec;
        Rect rect;
        std::string title;
        std::vector<std::string> lines;
        std::vector<std::string> keys;
        std::vector<bool> keyed;
        bool selectable = false;
        int cursor = 0;
        int scroll = 0;
};

struct Model {
        std::unique_ptr<LayoutNode> root;
        std::vector<Pane> panes;
};

struct State {
        std::string navdict_path;
        std::vector<Dest> dests;
        Settings settings;
        std::string selected_group;
        std::string selected_dest;
        int focus = 0;
        std::string status;
        std::unordered_map<std::string, std::vector<std::string>> cmd_cache;
        std::unordered_map<std::string, std::vector<Dest>> filter_cache;
};

static PaneSpec parse_pane_spec(const std::string &leaf) {
    const std::string text = trim_copy(leaf);
    if (text.rfind("pane:", 0) != 0)
        return PaneSpec{PaneKind::Cmd, text, true};

    const std::string rest = text.substr(5);
    if (rest == "groups")
        return PaneSpec{PaneKind::Groups, "", false};
    if (rest == "list")
        return PaneSpec{PaneKind::List, "", false};
    if (rest.rfind("list:", 0) == 0)
        return PaneSpec{PaneKind::List, trim_copy(rest.substr(5)), false};
    if (rest == "detail")
        return PaneSpec{PaneKind::Detail, "", false};
    if (rest == "stats")
        return PaneSpec{PaneKind::Stats, "", false};
    if (rest == "dates")
        return PaneSpec{PaneKind::Dates, "", false};
    if (rest == "attention")
        return PaneSpec{PaneKind::Attention, "7", false};
    if (rest.rfind("attention:", 0) == 0)
        return PaneSpec{PaneKind::Attention, trim_copy(rest.substr(10)), false};
    if (rest.rfind("cmd:", 0) == 0)
        return PaneSpec{PaneKind::Cmd, trim_copy(rest.substr(4)), false};
    return PaneSpec{PaneKind::Unknown, text, false};
}

static void collect_panes(LayoutNode *node, std::vector<Pane> &panes) {
    if (!node)
        return;
    if (node->kind == LayoutNode::Leaf) {
        node->pane_index = static_cast<int>(panes.size());
        Pane pane;
        pane.spec = parse_pane_spec(node->leaf);
        panes.push_back(std::move(pane));
        return;
    }
    for (auto &child : node->children)
        collect_panes(child.get(), panes);
}

static void compute_rects(LayoutNode *node, const Rect &rect,
                          std::vector<Pane> &panes) {
    if (!node)
        return;
    if (node->kind == LayoutNode::Leaf) {
        if (node->pane_index >= 0 &&
            node->pane_index < static_cast<int>(panes.size()))
            panes[node->pane_index].rect = rect;
        return;
    }

    const int count = static_cast<int>(node->children.size());
    if (count == 0)
        return;

    if (node->kind == LayoutNode::HSplit) {
        const int gap = count > 1 ? 1 : 0;
        const int usable = std::max(0, rect.w - gap * (count - 1));
        const int base = usable / count;
        const int rem = usable % count;
        int x = rect.x;
        for (int i = 0; i < count; ++i) {
            const int w = base + (i == count - 1 ? rem : 0);
            compute_rects(node->children[i].get(), Rect{x, rect.y, w, rect.h},
                          panes);
            x += w + gap;
        }
    } else {
        const int base = rect.h / count;
        const int rem = rect.h % count;
        int y = rect.y;
        for (int i = 0; i < count; ++i) {
            const int h = base + (i == count - 1 ? rem : 0);
            compute_rects(node->children[i].get(), Rect{rect.x, y, rect.w, h},
                          panes);
            y += h;
        }
    }
}

static std::string dest_line(const Dest &dest, const Settings &settings,
                             bool include_group, bool include_date,
                             bool force_color) {
    std::string line =
        (dest.kind.empty()
             ? " -  "
             : std::string("[") + kind_symbol(settings, dest) + "] ") +
        dest.dest_name;
    if (include_group && !dest.group.empty())
        line += " (" + dest.group + ")";
    if (include_date && !dest.date.empty())
        line += "  " + dest.date;
    if (!dest.msg.empty())
        line += "  " + dest.msg;
    return colorize(line, dest.style, force_color);
}

static int attention_days(const std::string &arg) {
    try {
        return std::max(0, std::stoi(arg));
    } catch (...) {
        return 7;
    }
}

static void build_groups(Pane &pane, const State &state, bool force_color) {
    pane.title = "Groups";
    pane.lines.push_back(
        colorize("(all)  " + std::to_string(state.dests.size()), 0, false));
    pane.keys.push_back("");
    pane.keyed.push_back(true);

    std::vector<std::pair<std::string, int>> groups;
    for (const Dest &dest : state.dests) {
        auto it =
            std::find_if(groups.begin(), groups.end(),
                         [&](const auto &g) { return g.first == dest.group; });
        if (it == groups.end())
            groups.push_back({dest.group, 1});
        else
            ++it->second;
    }
    for (const auto &group : groups) {
        const std::string line =
            "[" + group.first + "]  " + std::to_string(group.second);
        pane.lines.push_back(colorize(
            line, group_style(state.settings, group.first), force_color));
        pane.keys.push_back(group.first);
        pane.keyed.push_back(true);
    }
    pane.selectable = true;
}

static std::vector<Dest> list_view(const Pane &pane, State &state,
                                   bool &include_group, std::string &error) {
    include_group = false;
    const std::string &arg = pane.spec.arg;
    if (arg.empty()) {
        if (state.selected_group.empty()) {
            include_group = true;
            return state.dests;
        }
        std::vector<Dest> out;
        for (const Dest &dest : state.dests) {
            if (dest.group == state.selected_group)
                out.push_back(dest);
        }
        return out;
    }
    if (arg == "all") {
        include_group = true;
        return state.dests;
    }
    if (arg.rfind("group=", 0) == 0) {
        const std::string group = strip_outer_quotes(arg.substr(6));
        std::vector<Dest> out;
        for (const Dest &dest : state.dests) {
            if (dest.group == group)
                out.push_back(dest);
        }
        return out;
    }
    if (arg.rfind("filter=", 0) == 0) {
        const std::string expr = strip_outer_quotes(arg.substr(7));
        if (expr.empty()) {
            include_group = true;
            return state.dests;
        }
        auto cached = state.filter_cache.find(expr);
        if (cached != state.filter_cache.end()) {
            include_group = true;
            return cached->second;
        }
        std::vector<Dest> out;
        if (!filter_navdict(state.navdict_path, expr, out, error))
            return {};
        state.filter_cache[expr] = out;
        include_group = true;
        return out;
    }
    const std::string group = strip_outer_quotes(arg);
    std::vector<Dest> out;
    for (const Dest &dest : state.dests) {
        if (dest.group == group)
            out.push_back(dest);
    }
    return out;
}

static void build_list(Pane &pane, State &state, bool force_color) {
    if (pane.spec.arg.empty()) {
        pane.title = state.selected_group.empty()
                         ? "Dests"
                         : "Dests: " + state.selected_group;
    } else if (pane.spec.arg == "all") {
        pane.title = "All dests";
    } else if (pane.spec.arg.rfind("group=", 0) == 0) {
        pane.title = "Dests: " + strip_outer_quotes(pane.spec.arg.substr(6));
    } else if (pane.spec.arg.rfind("filter=", 0) == 0) {
        pane.title = "Dests (filtered)";
    } else {
        pane.title = "Dests: " + strip_outer_quotes(pane.spec.arg);
    }

    bool include_group = false;
    std::string error;
    const std::vector<Dest> view = list_view(pane, state, include_group, error);
    if (!error.empty()) {
        pane.lines.push_back("! " + error);
        pane.keys.push_back("");
        pane.keyed.push_back(false);
        return;
    }
    for (const Dest &dest : view) {
        pane.lines.push_back(
            dest_line(dest, state.settings, include_group, true, force_color));
        pane.keys.push_back(dest.dest_name);
        pane.keyed.push_back(true);
    }
    if (pane.lines.empty()) {
        pane.lines.push_back("(no dests)");
        pane.keys.push_back("");
        pane.keyed.push_back(false);
    }
    pane.selectable = true;
}

static void build_dates(Pane &pane, const State &state, bool force_color) {
    pane.title = "Dates";
    std::vector<std::pair<int32_t, const Dest *>> dated;
    for (const Dest &dest : state.dests) {
        int32_t days = 0;
        if (date_to_days(dest.date, days))
            dated.push_back({days, &dest});
    }
    std::sort(dated.begin(), dated.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first)
            return a.first < b.first;
        return a.second->dest_name < b.second->dest_name;
    });
    const int32_t today = today_days();
    for (const auto &entry : dated) {
        const Dest &dest = *entry.second;
        const int32_t diff = entry.first - today;
        std::string line = dest.date + "  ";
        if (diff < 0)
            line += "! ";
        line += dest_line(dest, state.settings, false, false, force_color);
        pane.lines.push_back(line);
        pane.keys.push_back(dest.dest_name);
        pane.keyed.push_back(true);
    }
    if (pane.lines.empty()) {
        pane.lines.push_back("(no dated dests)");
        pane.keys.push_back("");
        pane.keyed.push_back(false);
    }
    pane.selectable = true;
}

static void build_attention(Pane &pane, const State &state, bool force_color) {
    const int limit = attention_days(pane.spec.arg);
    pane.title = "Attention " + std::to_string(limit) + "d";

    struct Issue {
            int64_t order;
            std::string line;
            std::string key;
    };
    std::vector<Issue> issues;
    const int32_t today = today_days();

    for (const Dest &dest : state.dests) {
        int32_t days = 0;
        if (date_to_days(dest.date, days)) {
            const int32_t diff = days - today;
            if (diff <= limit) {
                std::string reason;
                if (diff < 0)
                    reason = "overdue " + std::to_string(-diff) + "d";
                else if (diff == 0)
                    reason = "today";
                else
                    reason = "in " + std::to_string(diff) + "d";
                issues.push_back({diff,
                                  "! " + reason + "  " +
                                      dest_line(dest, state.settings, false,
                                                false, force_color),
                                  dest.dest_name});
            }
        }
        if (looks_like_path(dest.dir) && !path_exists(dest.dir)) {
            issues.push_back({1000000,
                              "! missing  " +
                                  dest_line(dest, state.settings, false, false,
                                            force_color) +
                                  "  " + dest.dir,
                              dest.dest_name});
        }
    }

    std::sort(issues.begin(), issues.end(), [](const Issue &a, const Issue &b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.line < b.line;
    });
    for (const Issue &issue : issues) {
        pane.lines.push_back(issue.line);
        pane.keys.push_back(issue.key);
        pane.keyed.push_back(true);
    }
    if (pane.lines.empty()) {
        pane.lines.push_back("(nothing needs attention)");
        pane.keys.push_back("");
        pane.keyed.push_back(false);
    }
    pane.selectable = true;
}

static void build_detail(Pane &pane, const State &state) {
    pane.title = "Detail";
    const Dest *dest = find_dest(state.dests, state.selected_dest);
    if (!dest) {
        pane.lines.push_back("(no dest selected)");
        return;
    }
    std::string fields;
    fields_used(*dest, fields);
    pane.lines.push_back("dest:    " + dest->dest_name);
    pane.lines.push_back("group:   " + dest->group);
    const std::string kind =
        dest->kind.empty()
            ? "-"
            : std::string(1, kind_symbol(state.settings, *dest)) + " (" +
                  dest->kind + ")";
    pane.lines.push_back("kind:    " + kind);
    pane.lines.push_back("date:    " + dest->date);
    pane.lines.push_back("level:   " + std::to_string(dest->level));
    pane.lines.push_back("fields:  " + fields);
    pane.lines.push_back("dir:     " + dest->dir);
    pane.lines.push_back("msg:     " + dest->msg);
    pane.lines.push_back("file:    " + dest->file);
    pane.lines.push_back("cmd:     " + dest->cmd);
}

static void build_stats(Pane &pane, const State &state) {
    pane.title = "Stats";
    const int32_t today = today_days();
    std::unordered_set<std::string> groups;
    std::map<char, int> kinds;
    int dated = 0;
    int due = 0;
    int overdue = 0;
    int missing = 0;

    for (const Dest &dest : state.dests) {
        groups.insert(dest.group);
        ++kinds[dest.kind.empty() ? '-' : kind_symbol(state.settings, dest)];
        int32_t days = 0;
        if (date_to_days(dest.date, days)) {
            ++dated;
            const int32_t diff = days - today;
            if (diff < 0)
                ++overdue;
            else if (diff <= 7)
                ++due;
        }
        if (looks_like_path(dest.dir) && !path_exists(dest.dir))
            ++missing;
    }

    pane.lines.push_back("dests     " + std::to_string(state.dests.size()));
    pane.lines.push_back("groups    " + std::to_string(groups.size()));
    pane.lines.push_back("dated     " + std::to_string(dated));
    pane.lines.push_back("due <=7d  " + std::to_string(due));
    pane.lines.push_back("overdue   " + std::to_string(overdue));
    pane.lines.push_back("missing   " + std::to_string(missing));
    std::string kind_line = "kinds     ";
    for (const auto &entry : kinds) {
        if (kind_line.size() > 10)
            kind_line += "  ";
        kind_line +=
            std::string(1, entry.first) + ":" + std::to_string(entry.second);
    }
    if (kinds.empty())
        kind_line += "-";
    pane.lines.push_back(kind_line);
}

static void build_cmd(Pane &pane, State &state, bool force_cmds) {
    pane.title = pane.spec.legacy_cmd ? "" : "Cmd";
    const std::string &cmd = pane.spec.arg;
    auto it = state.cmd_cache.find(cmd);
    if (force_cmds || it == state.cmd_cache.end()) {
        state.cmd_cache[cmd] = run_shell_lines(cmd);
        it = state.cmd_cache.find(cmd);
    }
    pane.lines = it->second;
    if (pane.lines.empty())
        pane.lines.push_back("");
}

static void build_unknown(Pane &pane) {
    pane.title = "Unknown";
    pane.lines.push_back("unknown pane: " + pane.spec.arg);
}

static void refresh_panes(Model &model, State &state, bool force_cmds) {
    // Validate the current selection.
    bool group_ok = state.selected_group.empty();
    if (!group_ok) {
        for (const Dest &dest : state.dests) {
            if (dest.group == state.selected_group) {
                group_ok = true;
                break;
            }
        }
    }
    if (!group_ok)
        state.selected_group.clear();

    const Dest *selected = find_dest(state.dests, state.selected_dest);
    const bool selection_ok =
        selected && (state.selected_group.empty() ||
                     selected->group == state.selected_group);
    if (!selection_ok) {
        state.selected_dest.clear();
        if (!state.selected_group.empty()) {
            for (const Dest &dest : state.dests) {
                if (dest.group == state.selected_group) {
                    state.selected_dest = dest.dest_name;
                    break;
                }
            }
        } else if (!state.dests.empty()) {
            state.selected_dest = state.dests.front().dest_name;
        }
    }

    const bool force_color = true;
    for (Pane &pane : model.panes) {
        pane.lines.clear();
        pane.keys.clear();
        pane.keyed.clear();
        pane.selectable = false;
        switch (pane.spec.kind) {
        case PaneKind::Groups:
            build_groups(pane, state, force_color);
            break;
        case PaneKind::List:
            build_list(pane, state, force_color);
            break;
        case PaneKind::Detail:
            build_detail(pane, state);
            break;
        case PaneKind::Stats:
            build_stats(pane, state);
            break;
        case PaneKind::Attention:
            build_attention(pane, state, force_color);
            break;
        case PaneKind::Dates:
            build_dates(pane, state, force_color);
            break;
        case PaneKind::Cmd:
            build_cmd(pane, state, force_cmds);
            break;
        case PaneKind::Unknown:
            build_unknown(pane);
            break;
        }
    }

    for (Pane &pane : model.panes) {
        if (!pane.selectable) {
            pane.cursor = 0;
            continue;
        }
        const std::string want = pane.spec.kind == PaneKind::Groups
                                     ? state.selected_group
                                     : state.selected_dest;
        int cursor = 0;
        bool found = false;
        for (size_t i = 0; i < pane.keys.size(); ++i) {
            if (pane.keyed[i] && pane.keys[i] == want) {
                cursor = static_cast<int>(i);
                found = true;
                break;
            }
        }
        if (!found) {
            for (size_t i = 0; i < pane.keyed.size(); ++i) {
                if (pane.keyed[i]) {
                    cursor = static_cast<int>(i);
                    break;
                }
            }
        }
        pane.cursor = cursor;
    }

    if (model.panes.empty())
        state.focus = 0;
    else
        state.focus = std::clamp(state.focus, 0,
                                 static_cast<int>(model.panes.size()) - 1);
}

// ============================================================================
//                          Static composition
// ============================================================================

struct Block {
        std::vector<std::string> lines;
        size_t width = 0;
        size_t height = 0;
};

static Block make_block(std::vector<std::string> lines) {
    Block block;
    block.lines = std::move(lines);
    block.height = block.lines.size();
    for (const std::string &line : block.lines)
        block.width = std::max(block.width, line.size());
    for (std::string &line : block.lines) {
        if (line.size() < block.width)
            line.append(block.width - line.size(), ' ');
    }
    return block;
}

static Block compose_h(const std::vector<Block> &kids, size_t gap) {
    Block out;
    if (kids.empty())
        return out;
    size_t total_w = gap * (kids.size() - 1);
    size_t max_h = 0;
    for (const Block &kid : kids) {
        total_w += kid.width;
        max_h = std::max(max_h, kid.height);
    }
    out.lines.assign(max_h, std::string(total_w, ' '));
    out.width = total_w;
    out.height = max_h;
    size_t x = 0;
    for (const Block &kid : kids) {
        for (size_t y = 0; y < kid.height; ++y) {
            for (size_t col = 0; col < kid.width; ++col)
                out.lines[y][x + col] = kid.lines[y][col];
        }
        x += kid.width + gap;
    }
    return out;
}

static Block compose_v(const std::vector<Block> &kids, size_t gap) {
    Block out;
    if (kids.empty())
        return out;
    size_t total_h = gap * (kids.size() - 1);
    size_t max_w = 0;
    for (const Block &kid : kids) {
        total_h += kid.height;
        max_w = std::max(max_w, kid.width);
    }
    out.lines.assign(total_h, std::string(max_w, ' '));
    out.width = max_w;
    out.height = total_h;
    size_t y = 0;
    for (const Block &kid : kids) {
        for (size_t row = 0; row < kid.height; ++row) {
            for (size_t col = 0; col < kid.width; ++col)
                out.lines[y + row][col] = kid.lines[row][col];
        }
        y += kid.height + gap;
    }
    return out;
}

static Block pane_block(const Pane &pane) {
    std::vector<std::string> lines;
    if (!pane.title.empty())
        lines.push_back(strip_ansi(pane.title));
    for (const std::string &line : pane.lines)
        lines.push_back(strip_ansi(line));
    return make_block(std::move(lines));
}

static Block compose_static(const LayoutNode *node, const Model &model) {
    if (!node)
        return {};
    if (node->kind == LayoutNode::Leaf) {
        if (node->pane_index < 0 ||
            node->pane_index >= static_cast<int>(model.panes.size()))
            return {};
        return pane_block(model.panes[node->pane_index]);
    }
    std::vector<Block> kids;
    kids.reserve(node->children.size());
    for (const auto &child : node->children)
        kids.push_back(compose_static(child.get(), model));
    return node->kind == LayoutNode::HSplit ? compose_h(kids, 2)
                                            : compose_v(kids, 0);
}

static void render_static(const Model &model) {
    const Block out = compose_static(model.root.get(), model);
    for (const std::string &line : out.lines)
        std::cout << line << '\n';
}

// ============================================================================
//                                  TUI
// ============================================================================

struct RawTerminal {
        int fd;
        termios saved{};
        bool active = false;

        explicit RawTerminal(int fd_) : fd(fd_) {
            if (tcgetattr(fd, &saved) != 0)
                return;
            termios raw = saved;
            raw.c_lflag &= ~(ICANON | ECHO | ISIG);
            raw.c_iflag &= ~(IXON | ICRNL);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            active = tcsetattr(fd, TCSANOW, &raw) == 0;
        }

        ~RawTerminal() {
            if (active)
                tcsetattr(fd, TCSANOW, &saved);
        }
};

enum class Key {
    Quit,
    Up,
    Down,
    NextPane,
    PrevPane,
    Go,
    Msg,
    File,
    Edit,
    Rm,
    Refresh,
    Help,
    Unknown,
};

static bool write_all(int fd, const std::string &data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = write(fd, data.data() + offset, data.size() - offset);
        if (n <= 0)
            return false;
        offset += static_cast<size_t>(n);
    }
    return true;
}

static std::string move_to(int y, int x) {
    return "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
}

static void get_size(int fd, int &width, int &height) {
    winsize ws{};
    if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        width = ws.ws_col;
        height = ws.ws_row;
    } else {
        width = 80;
        height = 24;
    }
}

static Key read_key(int fd) {
    unsigned char c = 0;
    if (read(fd, &c, 1) != 1)
        return Key::Quit;
    if (c == 3)
        return Key::Quit;
    if (c == 27) {
        struct pollfd pfd = {fd, POLLIN, 0};
        if (poll(&pfd, 1, 30) > 0) {
            unsigned char seq[2] = {0, 0};
            if (read(fd, &seq[0], 1) == 1 && (seq[0] == '[' || seq[0] == 'O') &&
                read(fd, &seq[1], 1) == 1) {
                switch (seq[1]) {
                case 'A':
                    return Key::Up;
                case 'B':
                    return Key::Down;
                case 'C':
                    return Key::NextPane;
                case 'D':
                    return Key::PrevPane;
                case 'Z':
                    return Key::PrevPane;
                }
            }
        }
        return Key::Unknown;
    }
    switch (c) {
    case 'q':
        return Key::Quit;
    case 'j':
        return Key::Down;
    case 'k':
        return Key::Up;
    case '\t':
        return Key::NextPane;
    case '\n':
    case '\r':
        return Key::Go;
    case 'm':
        return Key::Msg;
    case 'f':
        return Key::File;
    case 'e':
        return Key::Edit;
    case 'r':
        return Key::Rm;
    case 'R':
        return Key::Refresh;
    case '?':
        return Key::Help;
    default:
        return Key::Unknown;
    }
}

static bool move_selection(Model &model, State &state, int delta) {
    if (model.panes.empty())
        return false;
    Pane &pane = model.panes[state.focus];
    if (!pane.selectable) {
        const int content_h = std::max(0, pane.rect.h - 1);
        const int max_scroll =
            std::max(0, static_cast<int>(pane.lines.size()) - content_h);
        const int next = std::clamp(pane.scroll + delta, 0, max_scroll);
        if (next == pane.scroll)
            return false;
        pane.scroll = next;
        return true;
    }

    int next = pane.cursor;
    for (int i = pane.cursor + delta;
         i >= 0 && i < static_cast<int>(pane.keyed.size()); i += delta) {
        if (pane.keyed[i]) {
            next = i;
            break;
        }
    }
    if (next == pane.cursor)
        return false;
    pane.cursor = next;
    const std::string &key = pane.keys[next];
    if (pane.spec.kind == PaneKind::Groups) {
        state.selected_group = key;
        state.selected_dest.clear();
    } else {
        state.selected_dest = key;
        for (const Dest &dest : state.dests) {
            if (dest.dest_name == key) {
                state.selected_group = dest.group;
                break;
            }
        }
    }
    return true;
}

static void adjust_scroll(std::vector<Pane> &panes) {
    for (Pane &pane : panes) {
        const int content_h = std::max(0, pane.rect.h - 1);
        if (content_h <= 0) {
            pane.scroll = 0;
            continue;
        }
        const int max_scroll =
            std::max(0, static_cast<int>(pane.lines.size()) - content_h);
        pane.scroll = std::clamp(pane.scroll, 0, max_scroll);
        if (pane.selectable) {
            if (pane.cursor < pane.scroll)
                pane.scroll = pane.cursor;
            if (pane.cursor >= pane.scroll + content_h)
                pane.scroll = pane.cursor - content_h + 1;
            pane.scroll = std::clamp(pane.scroll, 0, max_scroll);
        }
    }
}

static const char *HELP_TEXT =
    "Tab focus  j/k move  Enter go  m msg  f file  e edit  r rm  R reload  "
    "q quit";

// Action is returned via exit status; only the selected dest goes to stdout.
enum PanelAction {
    PANEL_NONE = 0,
    PANEL_GO = 10,
    PANEL_MSG = 11,
    PANEL_FILE = 12,
    PANEL_EDIT = 13,
    PANEL_RM = 14,
};

static void draw_frame(int fd, const Model &model, const State &state,
                       int width, int height) {
    std::string frame;
    frame.reserve(static_cast<size_t>(width) * height + 256);
    frame += "\x1b[H\x1b[2J";

    for (size_t i = 0; i < model.panes.size(); ++i) {
        const Pane &pane = model.panes[i];
        if (pane.rect.w <= 0 || pane.rect.h <= 0)
            continue;

        const int content_h = std::max(0, pane.rect.h - 1);
        std::string title = pane.title.empty() ? "Cmd" : pane.title;
        if (content_h > 0 &&
            pane.lines.size() > static_cast<size_t>(content_h)) {
            const size_t first = static_cast<size_t>(pane.scroll);
            const size_t last = std::min(
                pane.lines.size(), first + static_cast<size_t>(content_h));
            title += " [" + std::to_string(first + 1) + "-" +
                     std::to_string(last) + "/" +
                     std::to_string(pane.lines.size()) + "]";
        }
        title = pad_visible(clip_visible(title, pane.rect.w), pane.rect.w);
        frame += move_to(pane.rect.y, pane.rect.x);
        frame += (static_cast<int>(i) == state.focus) ? "\x1b[7m" : "\x1b[1m";
        frame += title;
        frame += "\x1b[0m";

        for (int row = 0; row < content_h; ++row) {
            const size_t index = static_cast<size_t>(pane.scroll + row);
            std::string line =
                index < pane.lines.size() ? pane.lines[index] : "";
            line = pad_visible(clip_visible(line, pane.rect.w), pane.rect.w);
            if (pane.selectable && static_cast<int>(index) == pane.cursor &&
                static_cast<int>(i) == state.focus)
                line = "\x1b[7m" + line + "\x1b[0m";
            frame += move_to(pane.rect.y + 1 + row, pane.rect.x);
            frame += line;
        }
    }

    if (height > 0) {
        std::string footer = state.status.empty() ? HELP_TEXT : state.status;
        footer = pad_visible(clip_visible(footer, width), width);
        frame += move_to(height - 1, 0);
        frame += state.status.empty() ? "\x1b[2m" : "\x1b[7m";
        frame += footer;
        frame += "\x1b[0m";
    }

    write_all(fd, frame);
}

static bool reload_state(State &state) {
    std::vector<Dest> dests;
    Settings settings;
    std::string errors;
    const int rc = parse_navdict(state.navdict_path, dests, settings, errors,
                                 nullptr, M_NONE);
    if (rc != 0 || !errors.empty()) {
        state.status = errors.empty() ? "reload failed" : errors;
        const size_t nl = state.status.find('\n');
        if (nl != std::string::npos)
            state.status.resize(nl);
        return false;
    }
    state.dests = std::move(dests);
    state.settings = std::move(settings);
    state.cmd_cache.clear();
    state.filter_cache.clear();
    return true;
}

static int run_tui(Model &model, State &state) {
    const int fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (fd < 0 || !isatty(fd)) {
        if (fd >= 0)
            close(fd);
        refresh_panes(model, state, true);
        render_static(model);
        return 0;
    }

    int action = PANEL_NONE;
    std::string action_dest;
    {
        RawTerminal raw(fd);
        write_all(fd, "\x1b[?1049h\x1b[?25l");

        bool done = false;
        while (!done) {
            int width = 80;
            int height = 24;
            get_size(fd, width, height);
            const Rect body{0, 0, std::max(1, width), std::max(1, height - 1)};
            compute_rects(model.root.get(), body, model.panes);
            refresh_panes(model, state, false);
            adjust_scroll(model.panes);
            draw_frame(fd, model, state, width, height);
            state.status.clear();

            const Key key = read_key(fd);
            switch (key) {
            case Key::Quit:
                done = true;
                break;
            case Key::Up:
                move_selection(model, state, -1);
                break;
            case Key::Down:
                move_selection(model, state, 1);
                break;
            case Key::NextPane:
                if (!model.panes.empty())
                    state.focus = (state.focus + 1) %
                                  static_cast<int>(model.panes.size());
                break;
            case Key::PrevPane:
                if (!model.panes.empty())
                    state.focus = (state.focus - 1 +
                                   static_cast<int>(model.panes.size())) %
                                  static_cast<int>(model.panes.size());
                break;
            case Key::Refresh:
                state.status = reload_state(state) ? "reloaded" : state.status;
                break;
            case Key::Help:
                state.status = HELP_TEXT;
                break;
            case Key::Go:
            case Key::Msg:
            case Key::File:
            case Key::Edit:
            case Key::Rm:
                if (state.selected_dest.empty()) {
                    state.status = "no dest selected";
                    break;
                }
                action = key == Key::Go     ? PANEL_GO
                         : key == Key::Msg  ? PANEL_MSG
                         : key == Key::File ? PANEL_FILE
                         : key == Key::Edit ? PANEL_EDIT
                                            : PANEL_RM;
                action_dest = state.selected_dest;
                done = true;
                break;
            case Key::Unknown:
                break;
            }
        }

        write_all(fd, "\x1b[?25h\x1b[?1049l");
    } // Restore termios before closing the tty fd.
    close(fd);

    if (action != PANEL_NONE && !action_dest.empty()) {
        std::cout << action_dest << '\n';
        std::cout.flush();
    }
    return action;
}

} // namespace

int eval_panel(const std::string &layout, const std::string &navdict_path,
               const std::vector<Dest> &dests, const Settings &settings,
               bool interactive) {
    const std::string effective = trim_copy(layout);
    if (effective.empty()) {
        std::cerr << "Panel layout is not configured; set `panel=` in "
                     "[@SETTINGS]\n";
        return 1;
    }

    std::string error;
    Model model;
    model.root = parse_layout(effective, error);
    if (!model.root) {
        std::cerr << "Panel layout error: "
                  << (error.empty() ? "invalid layout" : error) << '\n';
        return 1;
    }
    collect_panes(model.root.get(), model.panes);
    if (model.panes.empty()) {
        std::cerr << "Panel layout error: no panes\n";
        return 1;
    }

    State state;
    state.navdict_path = navdict_path;
    state.dests = dests;
    state.settings = settings;

    if (interactive)
        return run_tui(model, state);

    refresh_panes(model, state, true);
    render_static(model);
    return 0;
}
