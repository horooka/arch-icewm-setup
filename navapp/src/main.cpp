#include <iostream>
#include <nav.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

Settings settings;
std::vector<Dest> dests;

static char *alloc_format(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int size = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (size < 0) {
        va_end(copy);
        return NULL;
    }

    char *result = (char *)malloc((size_t)size + 1);
    if (result != NULL)
        vsnprintf(result, (size_t)size + 1, fmt, copy);
    va_end(copy);
    return result;
}

char mark_to_symbol(const Dest &dest) {
    for (const auto &mark_to_symbol_map : settings.mark_to_symbol_map) {
        if (mark_to_symbol_map.first == dest.dest_mark)
            return mark_to_symbol_map.second;
    }
    if (dest.dest_mark == "dest") {
        for (const std::string &field : settings.fields_priority) {
            if (field == "command") {
                if (!dest.command.empty())
                    return 'C';
            } else if (field == "path") {
                if (!dest.path.empty())
                    return 'P';
            } else if (field == "note") {
                if (!dest.note_path.empty())
                    return 'N';
            } else if (field == "brief") {
                if (!dest.brief.empty())
                    return 'B';
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
/* SGR parameter list (e.g. "31;42"), or "" when there is no format. */
static std::string sgr_params(unsigned char formatting) {
    if (formatting == 0)
        return "";
    const int fg = formatting & 0x0F;
    const int bg = (formatting >> 4) & 0x0F;
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

static std::string ansi_color(unsigned char formatting, bool force) {
    static const int color_enabled = isatty(STDOUT_FILENO) ? 1 : 0;
    if (!force && !color_enabled)
        return "";
    const std::string params = sgr_params(formatting);
    if (params.empty())
        return "";
    return "\033[" + params + "m";
}

static std::string colorize(const std::string &text, unsigned char formatting,
                            bool force = false) {
    const std::string color = ansi_color(formatting, force);
    if (color.empty())
        return text;
    return color + text + "\033[0m";
}

static unsigned char group_format(const std::string &group_name) {
    for (const auto &entry : settings.group_format_map) {
        if (entry.first == group_name)
            return entry.second;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        std::cout
            << "Usage: navapp <command> <arg>\n\n"
               "Commands:\n"
               "  list                     lists dests\n"
               "  list <group>             lists dests of the group\n"
               "  list-filter <cond>       lists dests satisfying "
               "condition\n"
               "  get <dest> [args]        prints the path of the dest with"
               " args expanded\n"
               "  get-filter <cond>        prints dest_name of the first"
               " dest satisfying condition\n"
               "  brief-get <dest> [args]  prints the brief of the "
               "dest\n"
               "  note-get <dest> [args]   prints the note of the dest\n"
               "  <com above> -c           performs command with the filter "
               "compiled previously\n"
               "  <com above> <..> -s      performs command without caching "
               "compiled filter\n"
               "  query <query> [-f]       allows to query the navdict "
               "into stdout ([-f] adds the field header)\n"
               "  get-startup              prints the startup command "
               "from config\n"
               "  dates [--upcoming [N]]   lists dests with dates sorted by "
               "date\n"
               "                           (also --past, --reverse, --all, "
               "--limit N)\n"
            << std::endl;
        return 0;
    }
#ifdef DEBUG
    std::cout << "DEBUG MODE IS ON\n";
#endif
    const std::string command = argv[1];
    std::string errors;
    char *home = std::getenv("HOME");
    if (!home) {
        std::cerr << "$HOME not set\n";
        return 1;
    }
    if (command != "list") {
        if (command == "dests-list") {
            parse_navdict(std::string(home) + "/.config/navdict.ini", dests,
                          settings, errors, NULL, M_NONE);
            if (!errors.empty()) {
                std::cerr << "Parse errors 4:\n" << errors << std::endl;
                return 1;
            }
            // Feed for shell completion: <sgr params>\t<display>\t<value>.
            // The shell maps the SGR params to its own color mechanism.
            for (const auto &dest : dests) {
                const std::string display =
                    dest.dest_mark.empty()
                        ? std::string(" -  ") + dest.dest_name
                        : std::string("[") + mark_to_symbol(dest) + "] " +
                              dest.dest_name;
                // "-" marks "no color" so the first tab-separated field is
                // never empty (a leading tab would be eaten by shell `read`).
                const std::string sgr = sgr_params(dest.formatting);
                printf("%s\t%s\t%s\n", sgr.empty() ? "-" : sgr.c_str(),
                       display.c_str(), dest.dest_name.c_str());
            }
            return 0;
        } else if (command == "get-startup") {
            parse_navdict(std::string(home) + "/.config/navdict.ini", dests,
                          settings, errors, NULL, M_STARTUP);
            if (!errors.empty()) {
                std::cerr << "Parse errors 3:\n" << errors << std::endl;
                return 1;
            }
            std::string on_startup = settings.on_startup;
            if (on_startup.empty()) {
                return 1;
            } else {
                std::cout << on_startup;
                return 0;
            }
        } else if (command == "dates") {
            bool reverse = false, only_past = false, all = false;
            bool upcoming = false;
            long upcoming_n = -1;
            long limit = -1;
            for (int i = 2; i < argc; i++) {
                if (strcmp(argv[i], "--reverse") == 0)
                    reverse = true;
                else if (strcmp(argv[i], "--past") == 0)
                    only_past = true;
                else if (strcmp(argv[i], "--all") == 0)
                    all = true;
                else if (strcmp(argv[i], "--upcoming") == 0) {
                    upcoming = true;
                    if (i + 1 < argc && argv[i + 1][0] != '-')
                        upcoming_n = atol(argv[++i]);
                } else if (strcmp(argv[i], "--limit") == 0) {
                    if (i + 1 < argc)
                        limit = atol(argv[++i]);
                } else {
                    std::cerr << "Unknown dates option: " << argv[i] << "\n";
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
                    cond +=
                        " && DAYS_UNTIL(date) <= " + std::to_string(upcoming_n);
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
            if (run_query(q, dests, selected, errors, &settings) != 0 ||
                !errors.empty()) {
                std::cerr << "Parse errors 5:\n" << errors << std::endl;
                return 1;
            }
            const int32_t today = today_days();
            for (const auto &dest : dests) {
                std::string date_col =
                    dest.date.empty() ? "----------" : dest.date;
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
                std::string mark_prefix = "    ";
                if (!dest.dest_mark.empty())
                    mark_prefix =
                        std::string("[") + mark_to_symbol(dest) + "] ";
                std::string rel_padded = rel;
                if (rel_padded.size() < 12)
                    rel_padded.append(12 - rel_padded.size(), ' ');
                std::string line = date_col + "  " + rel_padded + " " +
                                   mark_prefix + dest.dest_name;
                std::cout << colorize(line, dest.formatting) << std::endl;
            }
            return 0;
        } else if (argc == 2) {
            std::cerr << "Incorrect args count\n";
            return 1;
        }
        if (command == "query") {
            bool show_fields = false;
            const char *query_line = NULL;
            for (int i = 2; i < argc; i++) {
                if (strcmp(argv[i], "-f") == 0 ||
                    strcmp(argv[i], "--fields") == 0)
                    show_fields = true;
                else if (!query_line)
                    query_line = argv[i];
            }
            if (!query_line) {
                std::cerr << "Incorrect args count\n";
                return 1;
            }
            query(query_line, dests, errors, show_fields);
            if (!errors.empty()) {
                std::cerr << "Query error:\n" << errors << std::endl;
                return 1;
            }
            return 0;
        }
        const char *filter = NULL;
        if (command == "get-filter")
            filter = alloc_format("%s", argv[2]);
        else
            filter = alloc_format("dest = '%s'", argv[2]);
        if (command != "list-filter") {
            bool cached = false;
            bool silent = false;
            std::vector<std::string> args;
            for (int i = 2; i < argc; i++) {
                if (strcmp(argv[i], "-s") == 0)
                    silent = true;
                else if (strcmp(argv[i], "-c") == 0)
                    cached = true;
                else if (i > 2)
                    args.push_back(argv[i]);
            }
            parse_navdict(std::string(home) + "/.config/navdict.ini", dests,
                          settings, errors, filter,
                          MCOMPOSE(M_FIRST, cached, silent));
            if (!errors.empty()) {
                std::cerr << "Parse errors 1:\n" << errors << std::endl;
                return 1;
            }
            Dest dest = dests[0];
            if (command == "get" || command == "get-filter") {
                std::string field;
                int ret = go(dest, field, settings.fields_priority, args);
                std::cout << field;
                return ret;
            } else if (command == "brief-get") {
                if (dest.brief.empty()) {
                    std::cerr << "Dest " << argv[2] << " has no brief"
                              << std::endl;
                    return 1;
                }
                if (args.size() > 0) {
                    std::cout << expand_args(dest.brief, args);
                } else {
                    std::cout << dest.brief;
                }
                return 0;
            } else if (command == "note-get") {
                if (dest.note_path.empty()) {
                    std::cerr << "Dest " << argv[2] << " has no note"
                              << std::endl;
                    return 1;
                }
                if (args.size() > 0) {
                    std::cout << expand_args(dest.note_path, args);
                } else {
                    std::cout << dest.note_path;
                }
                return 0;
            } else {
                std::cerr << "Unknown command: \"" << command << "\""
                          << std::endl;
                return 1;
            }
        }
    }

    // list / list-filter: flags may appear anywhere; the first non-flag
    // argument is the group (list) or the condition (list-filter).
    const char *filter_arg = NULL;
    bool cached = false;
    bool silent = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0)
            silent = true;
        else if (strcmp(argv[i], "-c") == 0)
            cached = true;
        else if (!filter_arg)
            filter_arg = argv[i];
    }
    const char *filter = NULL;
    if (filter_arg) {
        if (command == "list-filter")
            filter = alloc_format("%s", filter_arg);
        else
            filter = alloc_format("group = '%s'", filter_arg);
    } else if (cached) {
        // No explicit filter: pass a non-NULL placeholder so parse_navdict
        // takes the cached-opcode path (its contents are unused when cached).
        filter = alloc_format("");
    }
    parse_navdict(std::string(home) + "/.config/navdict.ini", dests, settings,
                  errors, filter, MCOMPOSE(M_NONE, cached, silent));
    if (!errors.empty()) {
        std::cout << "Parse errors 0:\n" << errors << std::endl;
        return 1;
    }
    std::string last_group_name;
    for (Dest &dest : dests) {
        if (last_group_name != dest.group_name) {
            last_group_name = dest.group_name;
            std::string group_name_displayed =
                std::string("[") + last_group_name + "]";
            std::cout << colorize(group_name_displayed,
                                  group_format(last_group_name))
                      << std::endl;
        }
        char *dest_str = NULL;
        std::string fields;
        fields_used(dest, fields);
        if (dest.dest_mark.empty()) {
            dest_str = alloc_format("   -  %s %s", dest.dest_name.c_str(),
                                    fields.c_str());
        } else {
            dest_str = alloc_format("  [%c] %s %s", mark_to_symbol(dest),
                                    dest.dest_name.c_str(), fields.c_str());
        }
        std::cout << colorize(dest_str, dest.formatting) << std::endl;
    }
}
