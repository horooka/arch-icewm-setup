#include "nav.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

void trim(std::string &str) {
    str.erase(str.find_last_not_of(" \t\n\r\f\v") + 1);
    str.erase(0, str.find_first_not_of(" \t\n\r\f\v"));
}

bool find_dest(const std::vector<DestGroup> &dests_groups,
               const std::string &dest_name, Dest &dest) {
    for (const auto &dest_group : dests_groups) {
        const auto &dests = dest_group.dests;
        if (const auto dest_it = std::find_if(dests.begin(), dests.end(),
                                              [&dest_name](const auto &dest) {
                                                  return dest.first ==
                                                         dest_name;
                                              });
            dest_it != dests.end()) {
            dest = dest_it->second;
            return true;
        }
    }
    return false;
}

size_t find_group(const std::vector<DestGroup> &dests_groups,
                  const std::string &group_name) {
    size_t group_idx = 0;
    for (const auto &group : dests_groups) {
        if (group.name == group_name) {
            return group_idx;
        }
        ++group_idx;
    }
    return std::string::npos;
}

void append_destgroup_note(const DestGroup *dest_group, std::string &output) {
    unsigned char group_len = dest_group->name.size();
    std::string group_border(group_len, '=');
    output += group_border + "\n";
    output += dest_group->name + "\n";
    output += group_border + "\n";
    std::vector<std::pair<std::string, Dest>> dests = dest_group->dests;
    if (dests.empty())
        return;
    for (const auto &dest : dests) {
        append_dest_note(&dest.second, output, true);
    }
    output.pop_back();
    output.pop_back();
    output.pop_back();
}

void append_dest_note(const Dest *dest, std::string &output, bool in_group) {
    unsigned short path_len = dest->path.size();
    std::string path_border(path_len, '=');
    if (in_group)
        output += "|    \n";
    if (in_group)
        output += "|    ";
    output += path_border + "\n";
    if (in_group)
        output += "+----";
    output += dest->path + "\n";
    if (in_group)
        output += "|    ";
    output += path_border + "\n";
    if (dest->note_lines.empty())
        return;
    for (const auto &line : dest->note_lines) {
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

void sort_by_priority(std::vector<std::pair<std::string, Dest>> &dests) {
    std::stable_sort(dests.begin(), dests.end(),
                     [](const std::pair<std::string, Dest> &a,
                        const std::pair<std::string, Dest> &b) {
                         return a.second.priority < b.second.priority;
                     });
}

// Tag ids: 0=$P path, 1=$B brief note text, 2=$N note file, 3=$L priority,
// 4=$F formatting.
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
        unsigned char id = 5;
        if (kind == 'P')
            id = 0;
        else if (kind == 'B')
            id = 1;
        else if (kind == 'N')
            id = 2;
        else if (kind == 'L')
            id = 3;
        else if (kind == 'F')
            id = 4;
        if (id != 5) {
            specifiers.push_back({id, i});
            i += 2;
        } else {
            ++i;
        }
    }
    specifiers.push_back({5, line.size()});
    return specifiers;
}

static void strip_trailing_semicolon(std::string &value) {
    if (!value.empty() && value.back() == ';') {
        value.pop_back();
        trim(value);
    }
}

// Returns {formatting, priority}. Sets dest/path/note/note_path out-params.
std::pair<unsigned char, unsigned char>
parse_dest_line(const std::string &line, std::string &dest_name,
                std::string &path, std::string &note, std::string &note_path,
                std::string &errors) {
    std::pair<unsigned char, unsigned char> ret = {0, 0};
    dest_name.clear();
    path.clear();
    note.clear();
    note_path.clear();
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
        errors += "Line without any specifier: \"" + line + "\"\n";
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
            strip_trailing_semicolon(path);
            break;
        case 1:
            note = content;
            break;
        case 2:
            note_path = content;
            strip_trailing_semicolon(note_path);
            break;
        case 3:
            try {
                ret.second = static_cast<unsigned char>(std::stoi(content));
            } catch (...) {
                errors += "Invalid priority value: \"" + content + "\"\n";
            }
            break;
        case 4:
            try {
                ret.first = static_cast<unsigned char>(std::stoi(content));
            } catch (...) {
                errors += "Invalid formatting value: \"" + content + "\"\n";
            }
            break;
        }
    }
    return ret;
}

static void load_note_lines(const std::string &note, std::string &note_ret,
                            std::vector<std::string> &note_lines,
                            std::string &errors) {
    if (note_ret.empty())
        note_ret = note;
    // Prefer inline note text over note file.
    if (!note.empty()) {
        note_lines.push_back(note);
        return;
    }
    if (note_ret.empty())
        return;
    if (std::filesystem::exists(note_ret) &&
        std::filesystem::is_regular_file(note_ret)) {
        std::string file_output;
        if (read_ate(note_ret, file_output, errors) == 0)
            note_lines = split_by_newline(file_output);
        else
            note_lines.push_back(note_ret);
        return;
    }
    // Missing note file: show the path as plain text.
    note_lines.push_back(note_ret);
}

int parse_navdict(const std::string &path, std::vector<DestGroup> &dests_groups,
                  std::string &errors, const std::string &arg) {
    std::string output;
    if (int ret = read_ate(path, output, errors); ret != 0)
        return ret;
    std::istringstream ss(output);
    std::string curr_group_name;
    DestGroup curr_group;
    unsigned char curr_formatting = 0;
    std::vector<std::pair<std::string, Dest>> curr_dests = {};
    bool have_group = false;
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] == '[') {
            if (have_group) {
                sort_by_priority(curr_dests);
                curr_group.dests = curr_dests;
                dests_groups.push_back(curr_group);
                if (curr_group_name == arg)
                    return 0;
            }
            curr_dests.clear();
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
            if (size_t group_idx = find_group(dests_groups, curr_group_name);
                group_idx != std::string::npos) {
                errors += "Duplicate group: \"" + curr_group_name + "\"\n";
                continue;
            }
            curr_group.clear();
            curr_group.name = curr_group_name;
            curr_group.formatting = curr_formatting;
            have_group = true;
            continue;
        }
        std::string dest, dest_path, note, note_ret;
        const auto formatting_priority =
            parse_dest_line(line, dest, dest_path, note, note_ret, errors);
        if (dest.empty())
            continue;
        if (std::find_if(curr_dests.begin(), curr_dests.end(),
                         [&dest](const auto &entry) {
                             return entry.first == dest;
                         }) != curr_dests.end()) {
            errors +=
                "Duplicate dest: \"" + dest + "\" in " + curr_group_name + "\n";
            continue;
        }
        std::vector<std::string> note_lines;
        load_note_lines(note, note_ret, note_lines, errors);
        const unsigned char formatting =
            curr_formatting != 0 ? curr_formatting : formatting_priority.first;
        Dest destination = {formatting, dest_path, note_lines, note_ret,
                            formatting_priority.second};
        curr_dests.push_back(std::make_pair(dest, destination));
    }
    if (have_group || !curr_dests.empty()) {
        sort_by_priority(curr_dests);
        curr_group.dests = curr_dests;
        dests_groups.push_back(curr_group);
    }
    if (curr_group_name == arg)
        return 0;
    if (!arg.empty()) {
        errors += "Group \"" + arg + "\" not found\n";
    }

    return 0;
}
