#include "stack_machine.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

const char *const OPCODE_PATH_POSTFIX = ".cache/nav/opcode.bin";

void trim(std::string &str) {
    str.erase(str.find_last_not_of(" \t\n\r\f\v") + 1);
    str.erase(0, str.find_first_not_of(" \t\n\r\f\v"));
}

// --- date helpers -----------------------------------------------------------
// Dates are ISO `YYYY-MM-DD` strings; day 0 is 1970-01-01. The civil
// conversion is Howard Hinnant's algorithm (proleptic Gregorian).

static int32_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2u;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const int mp = static_cast<int>(m) + (m > 2u ? -3 : 9);
    const unsigned doy = (153u * static_cast<unsigned>(mp) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

static void civil_from_days(int32_t z, int &y, unsigned &m, unsigned &d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe =
        (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int yy = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    d = doy - (153u * mp + 2u) / 5u + 1u;
    const int m_int = static_cast<int>(mp) + (mp < 10u ? 3 : -9);
    m = static_cast<unsigned>(m_int);
    y = yy + (m <= 2u ? 1 : 0);
}

bool date_to_days(const std::string &in, int32_t &days_out) {
    int y = 0;
    unsigned mo = 0, da = 0;
    char sep1 = 0, sep2 = 0;
    int consumed = 0;
    const int n = sscanf(in.c_str(), "%d%c%u%c%u%n", &y, &sep1, &mo, &sep2, &da,
                         &consumed);
    if (n != 5 || consumed != static_cast<int>(in.size()))
        return false;
    if ((sep1 != '-' && sep1 != '/') || sep2 != sep1)
        return false;
    if (y < 1 || y > 9999 || mo < 1 || mo > 12 || da < 1 || da > 31)
        return false;
    const int32_t days = days_from_civil(y, mo, da);
    int ry;
    unsigned rm, rd;
    civil_from_days(days, ry, rm, rd);
    if (ry != y || rm != mo || rd != da)
        return false;
    days_out = days;
    return true;
}

std::string days_to_date(int32_t days) {
    int y;
    unsigned m, d;
    civil_from_days(days, y, m, d);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, m, d);
    return std::string(buf);
}

std::string today_date() {
    std::time_t t = std::time(nullptr);
    std::tm *lt = std::localtime(&t);
    if (!lt)
        return "";
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", lt->tm_year + 1900,
             lt->tm_mon + 1, lt->tm_mday);
    return std::string(buf);
}

int32_t today_days() {
    static int32_t cached = INT32_MIN;
    if (cached == INT32_MIN) {
        int32_t days = 0;
        cached = date_to_days(today_date(), days) ? days : 0;
    }
    return cached;
}

bool find_dest(const std::vector<Dest> &dests, const std::string &dest_name,
               Dest &dest_out) {
    if (const auto dest_it = std::find_if(dests.begin(), dests.end(),
                                          [&dest_name](const Dest &dest) {
                                              return dest.dest_name ==
                                                     dest_name;
                                          });
        dest_it != dests.end()) {
        dest_out = *dest_it;
        return true;
    }
    return false;
}

std::string expand_args(const std::string &str,
                        const std::vector<std::string> &args) {
    std::string out;
    size_t pos = 0;
    while (pos < str.size()) {
        if (str[pos] != '@' || pos + 1 >= str.size()) {
            out += str[pos++];
            continue;
        }
        const char next = str[pos + 1];
        if (next == '@') {
            out += '@';
            pos += 2;
            continue;
        }
        if (next >= '0' && next <= '9') {
            const size_t arg_idx = static_cast<size_t>(next - '0');
            if (arg_idx < args.size()) {
                out += args[arg_idx];
            }
            pos += 2;
            continue;
        }
        out += str[pos++];
    }
    return out;
}

unsigned char go(const Dest &dest, std::string &field_out,
                 const std::array<std::string, 4> &fields_priority,
                 const std::vector<std::string> &args) {
    for (const std::string &field : fields_priority) {
        if (field == "command") {
            if (!dest.command.empty()) {
                field_out = expand_args(dest.command, args);
                return 4;
            }
        } else if (field == "path") {
            if (!dest.path.empty()) {
                field_out = expand_args(dest.path, args);
                return 0;
            }
        } else if (field == "note") {
            if (!dest.note_path.empty()) {
                field_out = expand_args(dest.note_path, args);
                return 2;
            }
        } else if (field == "brief") {
            if (!dest.brief.empty()) {
                field_out = expand_args(dest.brief, args);
                return 3;
            }
        }
    }
    return 1;
}

unsigned char go(const Dest &dest, std::string &field_out,
                 const std::array<std::string, 4> &fields_priority) {
    return go(dest, field_out, fields_priority, {});
}

void append_dest_note(const Dest *dest, std::string &output, bool in_group,
                      const std::array<std::string, 4> &fields_priority) {
    std::string dest_displayed;
    (void)go(*dest, dest_displayed, fields_priority);
    unsigned short path_len = dest_displayed.size();
    std::string path_border(path_len, '=');
    if (in_group)
        output += "|    \n";
    if (in_group)
        output += "|    ";
    output += path_border + "\n";
    if (in_group)
        output += "+----";
    output += dest_displayed + "\n";
    if (in_group)
        output += "|    ";
    output += path_border + "\n";
    if (dest->displayed_lines.empty())
        return;
    for (const auto &line : dest->displayed_lines) {
        if (in_group)
            output += "|  ";
        output += line;
    }
    if (in_group)
        output += "\n|\n";
}

int read_ate(const std::string &path, std::string &output,
             std::string &errors) {
    errno = 0;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        int ret = errno != 0 ? errno : ENOENT;
        errors +=
            std::string("While reading \"") + path + "\": " + strerror(ret);
        return ret;
    }
    errno = 0;
    const std::streamsize sz = file.tellg();
    if (sz < 0) {
        int ret = errno != 0 ? errno : EIO;
        errors +=
            std::string("While reading \"") + path + "\": " + strerror(ret);
        return ret;
    }
    file.seekg(0, std::ios::beg);
    output.resize(sz);
    if (sz > 0 && !file.read(output.data(), sz)) {
        output.clear();
        int ret = errno != 0 ? errno : EIO;
        errors +=
            std::string("While reading \"") + path + "\": " + strerror(ret);
        return ret;
    }
    return 0;
}

std::vector<std::string> split_by_newline(const std::string &str) {
    std::vector<std::string> lines;
    std::istringstream ss(str);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::array<std::string, 4> split_priorities(const std::string &str) {
    std::array<std::string, 4> fields;
    std::istringstream ss(str);
    std::string field;
    size_t i = 0;
    while (std::getline(ss, field, ',')) {
        fields[i++] = field;
    }
    return fields;
}

std::vector<std::string> split_by_comma(const std::string &str) {
    std::vector<std::string> fields;
    std::istringstream ss(str);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

void sort_by_priority(std::vector<uint16_t> &dests_idxs,
                      const std::vector<Dest> &dests) {
    std::stable_sort(dests_idxs.begin(), dests_idxs.end(),
                     [dests](const uint16_t &a, const uint16_t &b) {
                         return dests[a].level > dests[b].level;
                     });
}

struct DestTag {
        unsigned char id;
        size_t pos;
};

std::vector<DestTag> get_specifiers(const std::string &line) {
    std::vector<DestTag> specifiers;
    for (size_t i = 0; i + 1 < line.size();) {
        if (line[i] == '@' && line[i + 1] == '@') {
            i += 2;
            continue;
        }
        if (line[i] != '@') {
            ++i;
            continue;
        }
        const char spec = line[i + 1];
        unsigned char id = 9;
        if (spec == 'P')
            id = 0;
        else if (spec == 'B')
            id = 1;
        else if (spec == 'N')
            id = 2;
        else if (spec == 'C')
            id = 3;
        else if (spec == 'L')
            id = 4;
        else if (spec == 'F')
            id = 5;
        else if (spec == 'D')
            id = 6;
        else if (spec == 'M')
            id = 7;
        else if (spec == 'S')
            id = 8;
        if (id != 9) {
            specifiers.push_back({id, i});
            i += 2;
        } else {
            ++i;
        }
    }
    specifiers.push_back({9, line.size()});
    return specifiers;
}

std::pair<unsigned char, unsigned char> parse_dest_line(
    const std::string &line, std::string &dest_name, std::string &dest_path,
    std::string &dest_brief, std::string &note_path, std::string &dest_date,
    std::string &dest_mark, std::string &dest_command, std::string &errors) {
    std::pair<unsigned char, unsigned char> ret = {0, 0};
    dest_name.clear();
    dest_path.clear();
    dest_brief.clear();
    note_path.clear();
    dest_date.clear();
    dest_mark.clear();
    dest_command.clear();
    size_t split_pos = line.find('=');
    if (split_pos == std::string::npos) {
        errors += "Invalid line: \"" + line + "\"\n";
        return ret;
    }
    dest_name = line.substr(0, split_pos);
    trim(dest_name);
    std::string rest = line.substr(split_pos + 1);
    trim(rest);
    if (rest.empty())
        return ret;

    const auto specifiers = get_specifiers(rest);
    if (specifiers.size() == 1) {
        errors += "Line without any specifier: \"" + rest + "\"\n";
        return ret;
    }

    for (size_t i = 0; i + 1 < specifiers.size(); ++i) {
        const DestTag &tag = specifiers[i];
        const DestTag &next = specifiers[i + 1];
        std::string content = rest.substr(tag.pos + 2, next.pos - tag.pos - 2);
        trim(content);
        if (content.empty())
            continue;
        switch (tag.id) {
        case 0:
            dest_path = content;
            break;
        case 1:
            dest_brief = content;
            break;
        case 2:
            note_path = content;
            break;
        case 3:
            dest_command = content;
            break;
        case 4:
            try {
                ret.second = static_cast<unsigned char>(std::stoi(content));
            } catch (...) {
                errors += "Invalid priority value: \"" + content + "\"\n";
            }
            break;
        case 5:
            try {
                ret.first = static_cast<unsigned char>(std::stoi(content));
            } catch (...) {
                errors += "Invalid formatting value: \"" + content + "\"\n";
            }
            break;
        case 6: {
            int32_t days = 0;
            if (date_to_days(content, days))
                dest_date = days_to_date(days);
            else
                errors += "Invalid date: \"" + content + "\"\n";
            break;
        }
        case 7:
            dest_mark = content;
            break;
        case 8:
            dest_path = content;
            break;
        }
    }
    return ret;
}

static void load_displayed_lines(const std::string &brief,
                                 std::string &note_path,
                                 std::vector<std::string> &displayed_lines,
                                 std::string &errors) {
    // Prefer inline note text over note file.
    if (!brief.empty()) {
        displayed_lines.push_back(brief);
        return;
    }
    if (note_path.empty())
        return;
    if (std::filesystem::exists(note_path) &&
        std::filesystem::is_regular_file(note_path)) {
        std::string file_output;
        if (read_ate(note_path, file_output, errors) == 0)
            displayed_lines = split_by_newline(file_output);
        else
            displayed_lines.push_back(note_path);
        return;
    }
    // Missing note file: show the path as plain text.
    displayed_lines.push_back(note_path);
}

void sprint_syntax_error(std::string &out, const char *query, const char *err,
                         int symbol) {
    std::string placeholder(symbol, ' ');
    out = std::string(query) + "\n";
    out += placeholder + "^\n";
    out += placeholder + err + " at " + std::to_string(symbol) + "\n";
}

int parse_navdict(const std::string &file_path, std::vector<Dest> &dests,
                  Settings &settings, std::string &errors, const char *filter,
                  unsigned char mode) {
    std::string output;
    if (int ret = read_ate(file_path, output, errors); ret != 0)
        return ret;
    std::istringstream ss(output);
    std::string curr_group_name;
    unsigned char curr_formatting = 0;
    std::string line;
    uint8_t *buffer = NULL;
    size_t buffer_size = 0;
    uint16_t offset;
    char **const_strs = NULL;
    char *home = getenv("HOME");
    char *expanded_opcode_path =
        alloc_format("%s/%s", home, OPCODE_PATH_POSTFIX);
    CompileCtx *ctx = NULL;
#ifdef DEBUG
    printf("MODE %d: base=%d, read_cached=%d, do_cache=%d\n", mode, MBASE(mode),
           MCACHED(mode), MSILENT(mode));
#endif
    if (filter) {
        if (!MCACHED(mode)) {
            TokenStream ts;
            if (!tokenize_into(&ts, filter, strlen(filter))) {
                errors += "Tokenize failed";
                return 1;
            }
            ctx = compile_ctx_new(&ts);
            char *compile_err = NULL;
            int compile_symbol = 0;
            compile(ctx, expanded_opcode_path, !MSILENT(mode), &compile_err,
                    &compile_symbol);
            if (compile_err) {
                sprint_syntax_error(errors, filter, compile_err,
                                    compile_symbol);
                return 1;
            }
            Token *tail = ts_tok_peek(&ts);
            if (tail->kind != TOK_EOF && tail->kind != TOK_SEMI) {
                errors += std::string("Unexpected token in filter: ") +
                          tok_lexeme(tail) + "\n";
                return 1;
            }
        }
        if (MCACHED(mode)) {
            FILE *file = fopen(expanded_opcode_path, "rb");
            if (!file) {
                printf("Error opening opcode file\n");
                return 1;
            }
            if (fseek(file, 0, SEEK_END) != 0) {
                fclose(file);
                return -1;
            }
            buffer_size = ftell(file);
            rewind(file);
            buffer = (uint8_t *)malloc(buffer_size);
            fread(buffer, 1, buffer_size, file);
            memcpy(&offset, buffer, 2);
#ifdef DEBUG
            printf("\n========================\n");
            printf("       EXECUTION\n");
            printf("========================\n");
            printf("Program (%d bytes): ", buffer_size);
            for (size_t i = 0; i < offset; ++i) {
                printf("%02X ", buffer[i]);
            }
            printf("| ");
            for (size_t i = 0; i < buffer_size - offset; ++i) {
                printf("%02X ", buffer[offset + i]);
            }
            printf("\n");
#endif
            const_strs = (char **)malloc(5 * sizeof(char *));
            decode_const_pool(&const_strs, buffer, offset);
            buffer += offset;
            buffer_size -= offset;
        } else {
            buffer_size = ctx->bytecode_size;
            buffer = (uint8_t *)malloc(ctx->bytecode_size);
            memcpy(buffer, ctx->bytecode, ctx->bytecode_size);
            memcpy(&offset, ctx->const_pool, 2);
            const_strs = (char **)malloc(5 * sizeof(char *));
#ifdef DEBUG
            printf("\n========================\n");
            printf("       EXECUTION\n");
            printf("========================\n");
            printf("Program (%d bytes): ", ctx->bytecode_size + offset);
            for (size_t i = 0; i < offset; ++i) {
                printf("%02X ", ctx->const_pool[i]);
            }
            printf("| ");
            for (size_t i = 0; i < ctx->bytecode_size; ++i) {
                printf("%02X ", buffer[i]);
            }
            printf("\n");
#endif
            decode_const_pool(&const_strs, (uint8_t *)ctx->const_pool, offset);
        }
    }
    std::string dest_name, dest_path, dest_brief, note_path, dest_date,
        dest_mark, dest_command;
    std::optional<std::streampos> prev_line;
    bool settings_found = false;
    // Util groups
    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] == '[') {
            if (settings_found) {
                ss.seekg(prev_line.value());
                break;
            }
            curr_group_name = line.substr(1, line.size() - 2);
            if (size_t comma_pos = curr_group_name.find(',');
                comma_pos != std::string::npos) {
                try {
                    curr_formatting = static_cast<unsigned char>(
                        std::stoi(curr_group_name.substr(comma_pos + 1)));
                } catch (...) {
                    errors += "Invalid formatting: \"" +
                              curr_group_name.substr(comma_pos + 1) + "\"\n";
                    curr_formatting = 1;
                    continue;
                }
                curr_group_name = curr_group_name.substr(0, comma_pos);
            } else {
                curr_formatting = 0;
            }
            trim(curr_group_name);
            continue;
        }
        parse_dest_line(line, dest_name, dest_path, dest_brief, note_path,
                        dest_date, dest_mark, dest_command, errors);
        if (dest_name.empty())
            continue;
        if (curr_group_name == "@SETTINGS") {
            settings_found = true;
            prev_line = ss.tellg();
            if (dest_name == "priority")
                settings.fields_priority = split_priorities(dest_path);
            else if (dest_name == "on_click")
                settings.on_click = dest_path == "brief-go"  ? 1
                                    : dest_path == "note-go" ? 2
                                                             : 0;
            else if (dest_name == "on_startup") {
                settings.on_startup = dest_path;
                if (MBASE(mode) == M_STARTUP)
                    return 0;
            } else if (dest_name == "marks_map") {
                std::vector<std::string> mappings = split_by_comma(dest_path);
                for (const std::string &map_pair : mappings) {
                    const auto iter =
                        std::find(map_pair.begin(), map_pair.end(), '=');
                    if (iter == map_pair.end()) {
                        errors +=
                            "Invalid mark mapping: \"" + map_pair + "\"\n";
                        continue;
                    }
                    std::string mark =
                        map_pair.substr(0, iter - map_pair.begin());
                    trim(mark);
                    std::string mark_symbol =
                        map_pair.substr(iter - map_pair.begin() + 1);
                    trim(mark_symbol);
                    if (mark_symbol.size() > 1) {
                        if (mark_symbol.size() == 3 && mark_symbol[0] == '\'' &&
                            mark_symbol[2] == '\'') {
                            mark_symbol = mark_symbol.substr(1, 2);
                        } else {
                            errors += "Map is longer than 1 symbol: \"" +
                                      map_pair + "\"\n";
                            continue;
                        }
                    }
                    for (size_t i = 0; i < settings.mark_to_symbol_map.size();
                         ++i) {
                        if (settings.mark_to_symbol_map[i].first == mark) {
                            const std::string &existing_symbol =
                                settings.mark_to_symbol_map[i].first;
                            if (existing_symbol != "opened" &&
                                existing_symbol != "closed" &&
                                existing_symbol != "finished" &&
                                existing_symbol != "ongoing") {
                                errors += "Duplicate mark mapping: \"" + mark +
                                          "\"\n";
                                continue;
                            }
                            settings.mark_to_symbol_map[i].second =
                                mark_symbol[0];
                            continue;
                        }
                    }
                    settings.mark_to_symbol_map.push_back(
                        std::make_pair(mark, mark_symbol[0]));
                }
            }
            continue;
        } else {
            errors += "First group must be \"@SETTINGS\"\n";
            return 1;
        }
    }
    // Dest groups
    do {
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] == '[') {
            curr_group_name = line.substr(1, line.size() - 2);
            if (size_t comma_pos = curr_group_name.find(',');
                comma_pos != std::string::npos) {
                try {
                    curr_formatting = static_cast<unsigned char>(
                        std::stoi(curr_group_name.substr(comma_pos + 1)));
                } catch (...) {
                    errors += "Invalid formatting: \"" +
                              curr_group_name.substr(comma_pos + 1) + "\"\n";
                    curr_formatting = 1;
                    continue;
                }
                curr_group_name = curr_group_name.substr(0, comma_pos);
            } else {
                curr_formatting = 0;
            }
            trim(curr_group_name);
            continue;
        }
        const auto formatting_priority =
            parse_dest_line(line, dest_name, dest_path, dest_brief, note_path,
                            dest_date, dest_mark, dest_command, errors);
        if (dest_name.empty())
            continue;
        std::vector<std::string> displayed_lines;
        load_displayed_lines(dest_brief, note_path, displayed_lines, errors);
        const unsigned char formatting =
            curr_formatting != 0 ? curr_formatting : formatting_priority.first;
        Dest destination = {dest_name,
                            formatting,
                            dest_path,
                            dest_brief,
                            note_path,
                            dest_date,
                            dest_command,
                            dest_mark,
                            curr_group_name,
                            displayed_lines,
                            formatting_priority.second};
        if (filter) {
            if (execute(buffer, buffer_size, const_strs, &destination) == 1) {
                dests.push_back(destination);
                if (MBASE(mode) == M_FIRST)
                    return 0;
            }
        } else
            dests.push_back(destination);
    } while (std::getline(ss, line));
    if (filter && dests.empty() && MBASE(mode) == M_FIRST) {
        if (MCACHED(mode))
            errors += std::string("No one dest found for cached filter\n");
        else
            errors += std::string("No one dest found for  \"") + filter +
                      "\" filter\n";
    }

    return 0;
}

static void append_csv_field(std::string &out, const std::string &value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        out += value;
        return;
    }
    out += '"';
    for (const char c : value) {
        if (c == '"')
            out += '"';
        out += c;
    }
    out += '"';
}

struct SortKey {
        unsigned char idx;
        Type type;
        bool desc;
};

static int compare_dest_by_keys(const Dest &a, const Dest &b,
                                const std::vector<SortKey> &keys) {
    for (const SortKey &k : keys) {
        ValUnion va = {}, vb = {};
        get_col_ptr(&a, k.idx, &va);
        get_col_ptr(&b, k.idx, &vb);
        int c;
        if (BASE_TYPE(k.type) == STR) {
            const char *sa = va.str ? va.str : "";
            const char *sb = vb.str ? vb.str : "";
            c = strcmp(sa, sb);
            c = c < 0 ? -1 : c > 0 ? 1 : 0;
        } else {
            c = va.i32 < vb.i32 ? -1 : va.i32 > vb.i32 ? 1 : 0;
        }
        if (c != 0)
            return k.desc ? -c : c;
    }
    return 0;
}

int run_query(const std::string &line, std::vector<Dest> &dests,
              std::vector<unsigned char> &selected, std::string &errors,
              Settings *settings_out) {
    TokenStream ts;
    if (!tokenize_into(&ts, line.data(), line.size())) {
        errors += "Tokenize failed\n";
        return false;
    }
    Token *tok = ts_tok_peek(&ts);
    if (tok->kind == TOK_EOF)
        return true;
    unsigned char columns[MAX_SELECT_COLS];
    int column_count = 0;
    char *home = getenv("HOME");

    // SELECT
    if (tok->kind == TOK_KEYWORD && strcmp(tok->text, "SELECT") == 0) {
        char *err = NULL;
        ts_tok_consume(&ts);
        int symbol = parse_cols(&ts, columns, &err, &column_count);
        if (symbol != -1) {
            sprint_syntax_error(errors, line.data(), err, symbol);
            return symbol;
        }
    }
    tok = ts_tok_peek(&ts);

    // WHERE
    std::string filter;
    const bool has_where =
        tok->kind == TOK_KEYWORD && strcmp(tok->text, "WHERE") == 0;
    if (has_where || (tok->kind != TOK_KEYWORD && tok->kind != TOK_SEMI &&
                      tok->kind != TOK_EOF)) {
        ts_tok_consume(&ts);
        tok = ts_tok_peek(&ts);
        while (tok->kind != TOK_EOF && tok->kind != TOK_SEMI &&
               tok->kind != TOK_KEYWORD) {
            filter += " ";
            filter += tok_lexeme(tok);
            ts_tok_consume(&ts);
            tok = ts_tok_peek(&ts);
        }
    }
    if (has_where && filter.empty()) {
        errors += "Expected expression after WHERE\n";
        return 1;
    }

    // ORDER BY <col> [ASC|DESC] (, ...)*
    std::vector<SortKey> sort_keys;
    tok = ts_tok_peek(&ts);
    if (tok->kind == TOK_KEYWORD && strcmp(tok->text, "ORDER") == 0) {
        ts_tok_consume(&ts);
        tok = ts_tok_peek(&ts);
        if (!(tok->kind == TOK_KEYWORD && strcmp(tok->text, "BY") == 0)) {
            errors += "Expected BY after ORDER\n";
            return tok->symbol;
        }
        ts_tok_consume(&ts);
        for (;;) {
            tok = ts_tok_peek(&ts);
            if (tok->kind != TOK_IDENT) {
                errors += "Expected column name in ORDER BY\n";
                return tok->symbol;
            }
            SchemaCol col;
            if (!col_in_schema(tok->text, &col)) {
                errors += std::string("Unknown column in ORDER BY: ") +
                          tok->text + "\n";
                return tok->symbol;
            }
            SortKey key = {col.idx, col.type, false};
            ts_tok_consume(&ts);
            tok = ts_tok_peek(&ts);
            if (tok->kind == TOK_KEYWORD && strcmp(tok->text, "ASC") == 0) {
                ts_tok_consume(&ts);
            } else if (tok->kind == TOK_KEYWORD &&
                       strcmp(tok->text, "DESC") == 0) {
                key.desc = true;
                ts_tok_consume(&ts);
            }
            sort_keys.push_back(key);
            tok = ts_tok_peek(&ts);
            if (tok->kind == TOK_COMMA) {
                ts_tok_consume(&ts);
                continue;
            }
            break;
        }
    }

    // LIMIT <int-expr>
    bool has_limit = false;
    int limit = 0;
    tok = ts_tok_peek(&ts);
    if (tok->kind == TOK_KEYWORD && strcmp(tok->text, "LIMIT") == 0) {
        char *err = NULL;
        ts_tok_consume(&ts);
        int symbol;
        int expression_pos = ts.pos + 1;
        CompileCtx *ctx = compile_ctx_new(&ts);
        Val res = compile(ctx, NULL, false, &err, &symbol);
        if (err) {
            sprint_syntax_error(errors, line.data(), err, symbol);
            return symbol;
        } else if (res.type != I32) {
            errors += "Expected const integer as limit\n";
            return expression_pos;
        }
        limit = res.as.i32 < 0 ? 0 : res.as.i32;
        has_limit = true;
        tok = ts_tok_peek(&ts);
    }

    if (tok->kind != TOK_EOF && tok->kind != TOK_SEMI) {
        errors += std::string("Unexpected token: ") + tok_lexeme(tok) + "\n";
        return tok->symbol;
    }

    // FILTERING
    Settings settings;
    parse_navdict(std::string(home) + "/.config/navdict.ini", dests, settings,
                  errors, filter.empty() ? NULL : filter.c_str(),
                  MCOMPOSE(M_NONE, false, true));
    if (settings_out)
        *settings_out = settings;

    // SORTING
    if (!sort_keys.empty()) {
        std::stable_sort(dests.begin(), dests.end(),
                         [&sort_keys](const Dest &a, const Dest &b) {
                             return compare_dest_by_keys(a, b, sort_keys) < 0;
                         });
    }

    // LIMIT (after sorting)
    if (has_limit && static_cast<size_t>(limit) < dests.size())
        dests.resize(static_cast<size_t>(limit));

    selected.clear();
    for (int i = 0; i < column_count; ++i)
        selected.push_back(columns[i]);

    return 0;
}

int query(const std::string &line, std::vector<Dest> &dests,
          std::string &errors, bool show_fields) {
    std::vector<unsigned char> selected;
    int rc = run_query(line, dests, selected, errors);
    if (rc != 0)
        return rc;

    std::vector<SchemaCol> out_cols;
    if (!selected.empty()) {
        for (unsigned char idx : selected)
            out_cols.push_back(schema_cols[idx]);
    } else {
        for (const SchemaCol &col : schema_cols)
            out_cols.push_back(col);
    }

    std::string out;
    if (show_fields) {
        for (size_t i = 0; i < out_cols.size(); ++i) {
            if (i > 0)
                out += ',';
            append_csv_field(out, out_cols[i].name);
        }
        out += '\n';
    }
    for (size_t r = 0; r < dests.size(); ++r) {
        for (size_t i = 0; i < out_cols.size(); ++i) {
            if (i > 0)
                out += ',';
            ValUnion val = {};
            get_col_ptr(&dests[r], out_cols[i].idx, &val);
            std::string cell;
            switch (BASE_TYPE(out_cols[i].type)) {
            case STR:
                cell = val.str ? val.str : "";
                break;
            case I32:
                cell = std::to_string(val.i32);
                break;
            case YES:
                cell = val.yes ? "yes" : "no";
                break;
            default:
                break;
            }
            append_csv_field(out, cell);
        }
        out += '\n';
    }
    fputs(out.c_str(), stdout);

    return 0;
}
