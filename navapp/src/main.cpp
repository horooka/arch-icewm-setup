#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <nav.h>
#include <string>
#include <unistd.h>
#include "panel.h"

struct Ctx {
        int argc = 0;
        char **argv = nullptr;
        std::string navdict; // $HOME/.config/navdict.ini
        std::string errors;
        std::vector<Dest> dests;
        Settings settings;
};

using Handler = int (*)(Ctx &);

struct Command {
        const char *name;
        Handler run;
};

// ============================================================================
//                               DISPLAY HELPERS
// ============================================================================

char kind_to_symbol(const Settings &settings, const Dest &dest) {
    if (dest.kind.empty())
        return '-';
    for (const auto &kind_to_symbol_map : settings.kind_to_symbol_map) {
        if (kind_to_symbol_map.first == dest.kind)
            return kind_to_symbol_map.second;
    }
    if (dest.kind == "dest") {
        for (const std::string &field : settings.fields_priority) {
            if (field == "cmd") {
                if (!dest.cmd.empty())
                    return 'C';
            } else if (field == "dir") {
                if (!dest.dir.empty())
                    return 'D';
            } else if (field == "file") {
                if (!dest.file.empty())
                    return 'F';
            } else if (field == "msg") {
                if (!dest.msg.empty())
                    return 'M';
            }
        }
    }
    return '?';
}

/*
 * Packed 8-bit format: foreground in the low nibble, background in the high
 * nibble. Nibbles are ANSI-16 color numbers (0 = terminal default, 1-7 normal,
 * 8-15 bright). Escapes are only emitted when stdout is a TTY.
 */
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

static std::string ansi_color(unsigned char style, bool force) {
    static const int color_enabled = isatty(STDOUT_FILENO) ? 1 : 0;
    if (!force && !color_enabled)
        return "";
    const std::string params = sgr_params(style);
    if (params.empty())
        return "";
    return "\033[" + params + "m";
}

static std::string colorize(const std::string &text, unsigned char style,
                            bool force = false) {
    const std::string color = ansi_color(style, force);
    if (color.empty())
        return text;
    return color + text + "\033[0m";
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
//                              NAVDICT HELPERS
// ============================================================================

static int load(Ctx &ctx, const char *filter, unsigned char mode,
                const char *label) {
    ctx.dests.clear();
    ctx.settings = Settings{};
    ctx.errors.clear();
    parse_navdict(ctx.navdict, ctx.dests, ctx.settings, ctx.errors, filter,
                  mode);
    if (!ctx.errors.empty()) {
        std::cerr << label << ctx.errors << std::endl;
        return 1;
    }
    return 0;
}

// ============================================================================
//                                    LISTING
// ============================================================================

static void print_selected(const Ctx &ctx) {
    std::string last_group;
    for (const auto &dest : ctx.dests) {
        if (last_group != dest.group) {
            last_group = dest.group;
            std::cout << colorize("[" + last_group + "]",
                                  group_style(ctx.settings, last_group))
                      << '\n';
        }
        std::string fields;
        fields_used(dest, fields);
        std::string line;
        if (dest.kind.empty())
            line = "   -  " + dest.dest_name + " " + fields;
        else
            line = std::string("  [") + kind_to_symbol(ctx.settings, dest) +
                   "] " + dest.dest_name + " " + fields;
        std::cout << colorize(line, dest.style) << '\n';
    }
}

static int run_list(Ctx &ctx, bool is_filter) {
    const char *filter_arg = nullptr;
    bool cached = false;
    bool silent = false;
    for (int i = 2; i < ctx.argc; i++) {
        if (std::strcmp(ctx.argv[i], "-s") == 0)
            silent = true;
        else if (std::strcmp(ctx.argv[i], "-c") == 0)
            cached = true;
        else if (!filter_arg)
            filter_arg = ctx.argv[i];
    }
    if (is_filter && !filter_arg && !cached) {
        std::cerr << "Incorrect args count\n";
        return 1;
    }

    std::string filter_storage;
    const char *filter = nullptr;
    if (filter_arg) {
        filter_storage = is_filter
                             ? std::string(filter_arg)
                             : std::string("group = '") + filter_arg + "'";
        filter = filter_storage.c_str();
    } else if (cached) {
        // Non-NULL empty placeholder: parse_navdict takes the cached-opcode
        // path, where the filter contents are unused.
        filter = "";
    }

    if (load(ctx, filter, MCOMPOSE(M_NONE, cached, silent), "Parse errors:\n"))
        return 1;
    print_selected(ctx);
    return 0;
}

static int cmd_list(Ctx &ctx) { return run_list(ctx, false); }
static int cmd_list_filter(Ctx &ctx) { return run_list(ctx, true); }
static int cmd_panel(Ctx &ctx) {
    bool interactive = true;
    std::string panel_arg;
    if (ctx.argc >= 3) {
        if (std::strcmp(ctx.argv[2], "--static") == 0) {
            interactive = false;
        } else {
            if (ctx.argc >= 4 && std::strcmp(ctx.argv[3], "--static") == 0)
                interactive = false;
            panel_arg = ctx.argv[2];
        }
    }
    if (load(ctx, nullptr, MCOMPOSE(M_NONE, 0, 0), "Parse errors:\n"))
        return 1;
    return eval_panel(panel_arg.empty() ? ctx.settings.panel : panel_arg,
                      ctx.navdict, ctx.dests, ctx.settings, interactive);
}

// ============================================================================
//                            SINGLE-DEST COMMANDS
// ============================================================================

struct DestQuery {
        std::string filter;
        bool cached = false;
        bool silent = false;
        std::vector<std::string> args;
};

static bool parse_dest_query(Ctx &ctx, bool is_filter, DestQuery &q) {
    if (ctx.argc < 3) {
        std::cerr << "Incorrect args count\n";
        return false;
    }
    q.filter = is_filter ? std::string(ctx.argv[2])
                         : std::string("dest = '") + ctx.argv[2] + "'";
    for (int i = 2; i < ctx.argc; i++) {
        if (std::strcmp(ctx.argv[i], "-s") == 0)
            q.silent = true;
        else if (std::strcmp(ctx.argv[i], "-c") == 0)
            q.cached = true;
        else if (i > 2)
            q.args.push_back(ctx.argv[i]);
    }
    return true;
}

static int run_get(Ctx &ctx, bool is_filter) {
    DestQuery q;
    if (!parse_dest_query(ctx, is_filter, q))
        return 1;
    if (load(ctx, q.filter.c_str(), MCOMPOSE(M_FIRST, q.cached, q.silent),
             "Parse errors:\n"))
        return 1;
    const Dest &dest = ctx.dests[0];
    std::string field;
    const int ret = go(dest, field, ctx.settings.fields_priority, q.args);
    std::cout << field;
    return ret;
}

static int run_field(Ctx &ctx, bool file) {
    DestQuery q;
    if (!parse_dest_query(ctx, false, q))
        return 1;
    if (load(ctx, q.filter.c_str(), MCOMPOSE(M_FIRST, q.cached, q.silent),
             "Parse errors:\n"))
        return 1;
    const Dest &dest = ctx.dests[0];
    const std::string &value = file ? dest.file : dest.msg;
    if (value.empty()) {
        std::cerr << "Dest " << ctx.argv[2] << " has no "
                  << (file ? "file" : "msg") << std::endl;
        return 1;
    }
    if (q.args.empty())
        std::cout << value;
    else
        std::cout << expand_args(value, q.args);
    return 0;
}

static int cmd_get(Ctx &ctx) { return run_get(ctx, false); }
static int cmd_get_filter(Ctx &ctx) { return run_get(ctx, true); }
static int cmd_msg_get(Ctx &ctx) { return run_field(ctx, false); }
static int cmd_file_get(Ctx &ctx) { return run_field(ctx, true); }

// ============================================================================
//                              SPECIAL COMMANDS
// ============================================================================

static int cmd_dests_list(Ctx &ctx) {
    if (load(ctx, nullptr, M_NONE, "Parse errors:\n"))
        return 1;
    // Feed for shell completion: <sgr params>\t<display>\t<value>. The shell
    // maps the SGR params to its own color mechanism.
    for (const auto &dest : ctx.dests) {
        const std::string display =
            dest.kind.empty()
                ? std::string(" -  ") + dest.dest_name
                : std::string("[") + kind_to_symbol(ctx.settings, dest) + "] " +
                      dest.dest_name;
        // "-" marks "no color" so the first tab-separated field is never
        // empty (a leading tab would be eaten by shell `read`).
        const std::string sgr = sgr_params(dest.style);
        std::printf("%s\t%s\t%s\n", sgr.empty() ? "-" : sgr.c_str(),
                    display.c_str(), dest.dest_name.c_str());
    }
    return 0;
}

static int cmd_get_startup(Ctx &ctx) {
    if (load(ctx, nullptr, M_STARTUP, "Parse errors:\n"))
        return 1;
    if (ctx.settings.on_startup.empty())
        return 1;
    std::cout << ctx.settings.on_startup;
    return 0;
}

static int cmd_query(Ctx &ctx) {
    bool show_fields = false;
    const char *query_line = nullptr;
    for (int i = 2; i < ctx.argc; i++) {
        if (std::strcmp(ctx.argv[i], "-f") == 0 ||
            std::strcmp(ctx.argv[i], "--fields") == 0)
            show_fields = true;
        else if (!query_line)
            query_line = ctx.argv[i];
    }
    if (!query_line) {
        std::cerr << "Incorrect args count\n";
        return 1;
    }
    query(query_line, ctx.dests, ctx.errors, show_fields);
    if (!ctx.errors.empty()) {
        std::cerr << "Query error:\n" << ctx.errors << std::endl;
        return 1;
    }
    return 0;
}

static int cmd_dates(Ctx &ctx) {
    bool reverse = false, only_past = false, all = false;
    bool upcoming = false;
    long upcoming_n = -1;
    long limit = -1;
    for (int i = 2; i < ctx.argc; i++) {
        if (std::strcmp(ctx.argv[i], "--reverse") == 0)
            reverse = true;
        else if (std::strcmp(ctx.argv[i], "--past") == 0)
            only_past = true;
        else if (std::strcmp(ctx.argv[i], "--all") == 0)
            all = true;
        else if (std::strcmp(ctx.argv[i], "--upcoming") == 0) {
            upcoming = true;
            if (i + 1 < ctx.argc && ctx.argv[i + 1][0] != '-')
                upcoming_n = std::atol(ctx.argv[++i]);
        } else if (std::strcmp(ctx.argv[i], "--limit") == 0) {
            if (i + 1 < ctx.argc)
                limit = std::atol(ctx.argv[++i]);
        } else {
            std::cerr << "Unknown dates option: " << ctx.argv[i] << "\n";
            return 1;
        }
    }
    std::string cond;
    if (!all)
        cond = "date != ''";
    if (upcoming) {
        if (!cond.empty())
            cond += " && ";
        cond += "DAYS_UNTIL(date) >= 0";
        if (upcoming_n >= 0)
            cond += " && DAYS_UNTIL(date) <= " + std::to_string(upcoming_n);
    } else if (only_past) {
        if (!cond.empty())
            cond += " && ";
        cond += "date < TODAY()";
    }
    std::string q = "SELECT dest, date";
    if (!cond.empty())
        q += " WHERE " + cond;
    q += " ORDER BY date";
    if (reverse)
        q += " DESC";
    if (limit >= 0)
        q += " LIMIT " + std::to_string(limit);

    std::vector<unsigned char> selected;
    if (run_query(q, ctx.dests, selected, ctx.errors, &ctx.settings) != 0 ||
        !ctx.errors.empty()) {
        std::cerr << "Parse errors:\n" << ctx.errors << std::endl;
        return 1;
    }
    const int32_t today = today_days();
    for (const auto &dest : ctx.dests) {
        std::string date_col = dest.date.empty() ? "----------" : dest.date;
        std::string rel;
        if (!dest.date.empty()) {
            int32_t d = 0;
            if (date_to_days(dest.date, d)) {
                const int32_t delta = d - today;
                if (delta == 0)
                    rel = "today";
                else if (delta > 0)
                    rel = "in " + std::to_string(delta) + " days";
                else
                    rel = std::to_string(-delta) + " days ago";
            }
        }
        std::string mark_prefix = " -  ";
        if (!dest.kind.empty())
            mark_prefix =
                std::string("[") + kind_to_symbol(ctx.settings, dest) + "] ";
        std::string rel_padded = rel;
        if (rel_padded.size() < 12)
            rel_padded.append(12 - rel_padded.size(), ' ');
        std::string line =
            date_col + "  " + rel_padded + " " + mark_prefix + dest.dest_name;
        std::cout << colorize(line, dest.style) << '\n';
    }
    return 0;
}

// ============================================================================
//                                   EDIT
// ============================================================================

static int cmd_edit_dest(Ctx &ctx) {
    DestQuery q;
    if (!parse_dest_query(ctx, false, q))
        return 1;
    if (load(ctx, q.filter.c_str(), MCOMPOSERAW(M_FIRST, q.cached, q.silent),
             "Parse errors:\n"))
        return 1;
    return edit_dest(ctx.navdict, ctx.dests[0]);
}

static int cmd_edit_group(Ctx &ctx) {
    if (ctx.argc < 3) {
        std::cerr << "Usage: edit-group <group>\n";
        return 1;
    }
    return edit_group(ctx.navdict, ctx.argv[2]);
}

static int cmd_add(Ctx &ctx) {
    if (ctx.argc < 3) {
        std::cerr << "Usage: add <group> [dest line]\n";
        return 1;
    }
    std::string hint;
    for (int i = 3; i < ctx.argc; i++) {
        if (!hint.empty())
            hint += ' ';
        hint += ctx.argv[i];
    }
    return add_dest(ctx.navdict, ctx.argv[2], hint);
}

static int cmd_rm(Ctx &ctx) {
    const char *dest_arg = nullptr;
    bool force = false, cached = false, silent = false;
    for (int i = 2; i < ctx.argc; i++) {
        if (std::strcmp(ctx.argv[i], "-f") == 0 ||
            std::strcmp(ctx.argv[i], "--force") == 0)
            force = true;
        else if (std::strcmp(ctx.argv[i], "-c") == 0)
            cached = true;
        else if (std::strcmp(ctx.argv[i], "-s") == 0)
            silent = true;
        else if (!dest_arg)
            dest_arg = ctx.argv[i];
        else {
            std::cerr << "Unknown option: " << ctx.argv[i] << "\n";
            return 1;
        }
    }
    if (!dest_arg && !cached) {
        std::cerr << "Usage: rm <dest> [-f] [-c|-s]\n";
        return 1;
    }
    std::string filter_storage;
    if (dest_arg)
        filter_storage = std::string("dest = '") + dest_arg + "'";
    // With -c the contents are unused; keep it non-NULL to take the cached
    // path.
    if (!dest_arg && cached)
        filter_storage = "";
    if (load(ctx, filter_storage.c_str(), MCOMPOSE(M_FIRST, cached, silent),
             "Parse errors:\n"))
        return 1;
    return remove_dest(ctx.navdict, ctx.dests[0], force);
}

// ============================================================================
//                                 DISPATCH
// ============================================================================

static const Command commands[] = {
    {"list", cmd_list},
    {"list-filter", cmd_list_filter},
    {"panel", cmd_panel},
    {"get", cmd_get},
    {"get-filter", cmd_get_filter},
    {"msg-get", cmd_msg_get},
    {"file-get", cmd_file_get},
    {"query", cmd_query},
    {"dates", cmd_dates},
    {"dests-list", cmd_dests_list},
    {"get-startup", cmd_get_startup},
    {"edit", cmd_edit_dest},
    {"edit-group", cmd_edit_group},
    {"add", cmd_add},
    {"rm", cmd_rm},
};

static int print_usage() {
    std::cout
        << "Usage: navapp <command> <arg>\n\n"
           "Commands:\n"
           "  list [<group>] [-c|-s]           lists dests (of the "
           "group)\n"
           "  list-filter <cond> [-c|-s]       lists dests satisfying "
           "condition\n"
           "  panel [<panel>] [--static]       interactive panel from config "
           "or provided one (--static prints once)\n"
           "  get <dest> [args] [-c|-s]        prints the resolved target of "
           "the dest with args expanded\n"
           "  get-filter <cond> [-c|-s]        prints dest_name of the "
           "first dest satisfying condition\n"
           "  msg-get <dest> [args] [-c|-s]    prints the message of the "
           "dest\n"
           "  file-get <dest> [args] [-c|-s]   prints the file of the "
           "dest\n"
           "  edit <dest> [-c|-s]              edits a dest line in "
           "place (readline, prefilled)\n"
           "  rm <dest> [-f] [-c|-s]           removes a dest "
           "(confirmation unless -f)\n"
           "  query <query> [-f]               allows to query the "
           "navdict into stdout ([-f] adds the field header)\n"
           "  get-startup                      prints the startup command "
           "from config\n"
           "  dates [--upcoming [N]]           lists dests with dates "
           "sorted by date (also --past, --reverse, --all, --limit N)\n"
           "  edit-group <group>               edits a group body in "
           "$EDITOR\n"
           "  add <group> [line]               appends a dest line to a "
           "group\n"
           "\n"
           "Flags:\n"
           "  -c  reuse the previously compiled filter\n"
           "  -s  compile the filter but do not cache it\n"
        << std::endl;
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1)
        return print_usage();
#ifdef DEBUG
    std::cout << "DEBUG MODE IS ON\n";
#endif
    char *home = std::getenv("HOME");
    if (!home) {
        std::cerr << "$HOME not set\n";
        return 1;
    }

    Ctx ctx;
    ctx.argc = argc;
    ctx.argv = argv;
    ctx.navdict = std::string(home) + "/.config/navdict.ini";

    for (const Command &command : commands) {
        if (std::strcmp(argv[1], command.name) == 0)
            return command.run(ctx);
    }
    std::cerr << "Unknown command: \"" << argv[1] << "\"\n";
    return 1;
}
