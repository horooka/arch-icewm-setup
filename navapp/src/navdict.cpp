#include "stack_machine.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

const char *const OPCODE_PATH_POSTFIX = ".cache/nav/opcode.bin";

void trim(std::string &str) {
    str.erase(str.find_last_not_of(" \t\n\r\f\v") + 1);
    str.erase(0, str.find_first_not_of(" \t\n\r\f\v"));
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

unsigned char go(const Dest &dest, std::string &field_out,
                 const std::array<std::string, 4> &fields_priority) {
    for (const std::string &field : fields_priority) {
        if (field == "command") {
            if (!dest.command.empty()) {
                field_out = dest.command;
                return 4;
            }
        } else if (field == "path") {
            if (!dest.path.empty()) {
                field_out = dest.path;
                return 0;
            }
        } else if (field == "note") {
            if (!dest.note_path.empty()) {
                field_out = dest.note_path;
                return 2;
            }
        } else if (field == "brief") {
            if (!dest.brief.empty()) {
                field_out = dest.brief;
                return 3;
            }
        }
    }
    return 1;
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
    // Successful open does not clear errno; ignore stale values from prior
    // fails.
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
                         return dests[a].priority > dests[b].priority;
                     });
}

struct DestTag {
        unsigned char id;
        size_t pos;
};

std::vector<DestTag> get_specifiers(const std::string &line) {
    std::vector<DestTag> specifiers;
    for (size_t i = 0; i + 1 < line.size();) {
        if (line[i] != '$') {
            ++i;
            continue;
        }
        const char kind = line[i + 1];
        unsigned char id = 7;
        if (kind == 'P')
            id = 0;
        else if (kind == 'B')
            id = 1;
        else if (kind == 'N')
            id = 2;
        else if (kind == 'C')
            id = 3;
        else if (kind == 'L')
            id = 4;
        else if (kind == 'F')
            id = 5;
        else if (kind == 'T')
            id = 6;
        else if (kind == 'S')
            id = 7;
        if (id != 8) {
            specifiers.push_back({id, i});
            i += 2;
        } else {
            ++i;
        }
    }
    specifiers.push_back({8, line.size()});
    return specifiers;
}

// Returns {formatting, priority}
std::pair<unsigned char, unsigned char>
parse_dest_line(const std::string &line, std::string &dest_name,
                std::string &path, std::string &brief, std::string &note_path,
                std::string &task_status, std::string &command,
                std::string &errors) {
    std::pair<unsigned char, unsigned char> ret = {0, 0};
    dest_name.clear();
    path.clear();
    brief.clear();
    note_path.clear();
    command.clear();
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
            path = content;
            break;
        case 1:
            brief = content;
            break;
        case 2:
            note_path = content;
            break;
        case 3:
            command = content;
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
        case 6:
            task_status = content;
            break;
        case 7:
            path = content;
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
    out = std::string(query) + "\n";
    out += std::string(' ', strlen(query)) + "^\n";
    out += std::string(' ', strlen(query)) + err + " at " +
           std::to_string(symbol) + "\n";
}

int parse_navdict(const std::string &file_path, std::vector<Dest> &dests,
                  Settings &settings, std::string &errors, const char *filter,
                  char stop_mode) {
    std::string output;
    if (int ret = read_ate(file_path, output, errors); ret != 0)
        return ret;
    std::istringstream ss(output);
    std::string curr_group_name;
    unsigned char curr_formatting = 0;
    std::string line;
    uint8_t *buffer = NULL;
    size_t buffer_size = 0;
    char **const_strs = NULL;
    if (filter) {
        TokenStream ts;
        if (!tokenize_into(&ts, filter, strlen(filter))) {
            errors += "tokenize failed";
            return 1;
        }
        char *compile_err = NULL;
        int compile_symbol = 0;
        char *home = getenv("HOME");
        if (!home) {
            errors += "HOME environment variable not set\n";
            return 1;
        } else if (home[strlen(home) - 1] == '/') {
            home[strlen(home) - 1] = '\0';
        }
        char *expanded_opcode_path =
            alloc_format("%s/%s", home, OPCODE_PATH_POSTFIX);
        int compile_ret =
            compile(&ts, expanded_opcode_path, compile_err, &compile_symbol)
                .as.yes;
        if (compile_err) {
            sprint_syntax_error(errors, filter, compile_err, compile_symbol);
            return 1;
        }
        FILE *file = fopen(expanded_opcode_path, "rb");
        if (!file) {
            printf("error opening opcode file\n");
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
        const_strs = (char **)malloc(5 * sizeof(char *));
        uint16_t offset = decode_const_pool(const_strs, buffer);
        buffer += offset;
        buffer_size -= offset;
    }
    while (std::getline(ss, line)) {
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
                    errors += "Invalid sqr value: \"" +
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
        std::string dest, dest_path, brief, note_path, task_status, command;
        const auto formatting_priority =
            parse_dest_line(line, dest, dest_path, brief, note_path,
                            task_status, command, errors);
        if (dest.empty())
            continue;
        if (curr_group_name == "$SETTINGS") {
            if (dest == "priority")
                settings.fields_priority = split_priorities(dest_path);
            else if (dest == "on_click")
                settings.on_click = dest_path == "brief-go"  ? 1
                                    : dest_path == "note-go" ? 2
                                                             : 0;
            else if (dest == "on_startup") {
                settings.on_startup = dest_path;
                if (stop_mode == M_STARTUP)
                    return 0;
            } else if (dest == "statuses") {
                std::vector<std::string> mappings = split_by_comma(dest_path);
                for (const std::string &map_pair : mappings) {
                    const auto iter =
                        std::find(map_pair.begin(), map_pair.end(), '=');
                    if (iter == map_pair.end()) {
                        errors +=
                            "Invalid status mapping: \"" + map_pair + "\"\n";
                        continue;
                    }
                    std::string status =
                        map_pair.substr(0, iter - map_pair.begin());
                    trim(status);
                    std::string status_symbol =
                        map_pair.substr(iter - map_pair.begin() + 1);
                    trim(status_symbol);
                    if (status_symbol.size() > 1) {
                        if (status_symbol.size() == 3 &&
                            status_symbol[0] == '\'' &&
                            status_symbol[2] == '\'') {
                            status_symbol = status_symbol.substr(1, 2);
                        } else {
                            errors += "Map is longer than 1 symbol: \"" +
                                      map_pair + "\"\n";
                            continue;
                        }
                    }
                    for (size_t i = 0; i < settings.status_to_symbol_map.size();
                         ++i) {
                        if (settings.status_to_symbol_map[i].first == status) {
                            const std::string &existing_symbol =
                                settings.status_to_symbol_map[i].first;
                            if (existing_symbol != "opened" &&
                                existing_symbol != "closed" &&
                                existing_symbol != "finished" &&
                                existing_symbol != "ongoing") {
                                errors += "Duplicate status mapping: \"" +
                                          status + "\"\n";
                                continue;
                            }
                            settings.status_to_symbol_map[i].second =
                                status_symbol[0];
                            continue;
                        }
                    }
                    settings.status_to_symbol_map.push_back(
                        std::make_pair(status, status_symbol[0]));
                }
            }
            continue;
        }
        std::vector<std::string> displayed_lines;
        load_displayed_lines(brief, note_path, displayed_lines, errors);
        const unsigned char formatting =
            curr_formatting != 0 ? curr_formatting : formatting_priority.first;
        Dest destination = {dest,
                            formatting,
                            dest_path,
                            brief,
                            note_path,
                            command,
                            task_status,
                            curr_group_name,
                            displayed_lines,
                            formatting_priority.second};
        if (filter) {
            if (execute(buffer, buffer_size, const_strs, &destination) == 1)
                dests.push_back(destination);
        } else
            dests.push_back(destination);
    }
    if (filter && dests.empty()) {
        errors +=
            std::string("No one dest found for  \"") + filter + "\" filter\n";
    }

    return 0;
}
