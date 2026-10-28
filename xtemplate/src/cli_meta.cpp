#include "xtemplate/cli.hpp"
#include "xtemplate/nodes.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

std::string trim(const std::string &s) {
    auto start = std::find_if_not(s.begin(), s.end(), ::isspace);
    auto end = std::find_if_not(s.rbegin(), s.rend(), ::isspace).base();
    if (start >= end)
        return std::string{};
    return std::string(start, end);
}

std::vector<std::string> split_by_comma(const std::string &s) {
    std::vector<std::string> result;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, ','))
        result.push_back(trim(token));
    return result;
}

// Controllers after '=' use '-' so VARS commas stay record separators,
// and so XVARIANT:a-b-c=ctrl1-!ctrl2 keeps options and deps distinct.
static std::vector<std::string> split_by_dash(const std::string &s) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= s.size()) {
        size_t dash = s.find('-', start);
        if (dash == std::string::npos) {
            std::string tok = trim(s.substr(start));
            if (!tok.empty())
                result.push_back(tok);
            break;
        }
        std::string tok = trim(s.substr(start, dash - start));
        if (!tok.empty())
            result.push_back(tok);
        start = dash + 1;
    }
    return result;
}

// Tags are always a trailing [...] suffix so they never collide with
// XVARIANT:a-b options or =deps (which are stripped afterward).
void strip_trailing_var_tags(std::string &var_type,
                             std::vector<std::string> &tags,
                             const std::string &var_name, std::string &errors) {
    tags.clear();
    var_type = trim(var_type);
    size_t open = var_type.rfind('[');
    if (open == std::string::npos)
        return;

    size_t close = var_type.find(']', open + 1);
    if (close == std::string::npos) {
        errors += std::format(
            "- Variable {} has '[' without closing ']' in tags\n", var_name);
        // Drop the broken suffix so depends-on parsing is not poisoned
        var_type = trim(var_type.substr(0, open));
        return;
    }

    std::string after = trim(var_type.substr(close + 1));
    if (!after.empty()) {
        errors += std::format(
            "- Variable {} tags [...] must be at the end of the type "
            "(after options and depends-on)\n",
            var_name);
        // Still extract what we can between the brackets
    }

    std::string inner = trim(var_type.substr(open + 1, close - open - 1));
    if (!inner.empty())
        tags = split_by_dash(inner);
    var_type = trim(var_type.substr(0, open));
}

// First ';' separates type from name — not rfind — so names stay simple and
// type may freely contain =deps / [tags] without looking for a last ';'.
void parse_var_record(const std::string &var_record, std::string &var_type,
                      std::string &var_name, std::vector<std::string> &var_tags,
                      std::string &errors) {
    var_type.clear();
    var_name.clear();
    var_tags.clear();
    size_t pos = var_record.find(';');
    if (pos != std::string::npos) {
        var_type = trim(var_record.substr(0, pos));
        var_name = trim(var_record.substr(pos + 1));
    } else {
        var_name = trim(var_record);
    }
    if (!var_type.empty())
        strip_trailing_var_tags(var_type, var_tags, var_name, errors);
}

bool finalize_vars_dependencies(
    std::vector<std::string> &vars_types,
    const std::vector<std::string> &vars_names,
    std::vector<std::vector<std::tuple<unsigned short, std::string, bool>>>
        &vars_dependent,
    std::string &errors) {
    vars_dependent.assign(vars_names.size(), {});
    if (vars_types.size() != vars_names.size()) {
        errors += "- vars types/names size mismatch\n";
        return false;
    }

    std::unordered_map<std::string, unsigned short> var_to_idx_map;
    for (size_t i = 0; i < vars_names.size(); ++i)
        var_to_idx_map[vars_names[i]] = static_cast<unsigned short>(i);

    // controller_name -> list of (dependent_idx, expected_value, inverted)
    std::unordered_map<
        std::string, std::vector<std::tuple<unsigned short, std::string, bool>>>
        controller_to_deps;

    for (size_t i = 0; i < vars_types.size(); ++i) {
        std::string &var_type = vars_types[i];
        const std::string &var_name = vars_names[i];
        size_t eq = var_type.find('=');
        if (eq == std::string::npos)
            continue;

        std::vector<std::string> deps = split_by_dash(var_type.substr(eq + 1));
        var_type = trim(var_type.substr(0, eq));

        for (std::string dep : deps) {
            dep = trim(dep);
            if (dep.empty())
                continue;
            bool inverted = false;
            if (dep[0] == '!') {
                inverted = true;
                dep = trim(dep.substr(1));
            }
            std::string name, value;
            size_t value_pos = dep.find('(');
            if (value_pos != std::string::npos) {
                size_t value_end_pos = dep.find(')', value_pos + 1);
                if (value_end_pos == std::string::npos) {
                    errors += std::format(
                        "- Variable {} has invalid value in depends-on\n",
                        var_name);
                    continue;
                }
                name = trim(dep.substr(0, value_pos));
                value = trim(
                    dep.substr(value_pos + 1, value_end_pos - value_pos - 1));
            } else {
                name = trim(dep);
                value = "";
            }
            if (name.empty()) {
                errors += std::format("- Variable {} has empty name after "
                                      "'!' in depends-on\n",
                                      var_name);
                continue;
            }
            if (name == var_name) {
                errors += std::format(
                    "- Variable {} cannot be dependent on itself\n", var_name);
                continue;
            }
            if (var_to_idx_map.find(name) == var_to_idx_map.end()) {
                errors += std::format(
                    "- Variable {} is dependent on undefined variable {}\n",
                    var_name, name);
                continue;
            }
            controller_to_deps[name].push_back(
                {static_cast<unsigned short>(i), value, inverted});
        }
    }

    for (const auto &kv : controller_to_deps) {
        unsigned short ctrl = var_to_idx_map[kv.first];
        for (const auto &dep : kv.second)
            vars_dependent[ctrl].push_back(dep);
    }
    return true;
}

std::string format_var_type_with_deps(
    const std::string &clean_type, size_t var_idx,
    const std::vector<std::string> &vars_names,
    const std::vector<
        std::vector<std::tuple<unsigned short, std::string, bool>>>
        &vars_dependent,
    const std::vector<std::string> &var_tags) {
    std::vector<std::string> dep_tokens;
    for (size_t ctrl = 0; ctrl < vars_dependent.size(); ++ctrl) {
        for (const auto &dep : vars_dependent[ctrl]) {
            if (std::get<0>(dep) != var_idx)
                continue;
            if (ctrl >= vars_names.size())
                continue;
            dep_tokens.push_back(
                (std::get<2>(dep) ? "!" : "") + vars_names[ctrl] +
                (std::get<1>(dep).empty() ? "" : "(" + std::get<1>(dep) + ")"));
        }
    }
    std::string out = clean_type;
    if (!dep_tokens.empty()) {
        out += "=";
        for (size_t i = 0; i < dep_tokens.size(); ++i) {
            if (i)
                out += "-";
            out += dep_tokens[i];
        }
    }
    if (!var_tags.empty()) {
        out += "[";
        for (size_t i = 0; i < var_tags.size(); ++i) {
            if (i)
                out += "-";
            out += var_tags[i];
        }
        out += "]";
    }
    return out;
}

int read_file_ate(const std::string &path, std::string &out) {
    errno = 0;
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    const int open_err = errno;
    if (!ifs.is_open())
        return open_err != 0 ? open_err : ENOENT;
    errno = 0;
    const std::streamsize sz = ifs.tellg();
    if (sz < 0)
        return errno != 0 ? errno : EIO;
    ifs.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(sz));
    if (sz > 0 && !ifs.read(out.data(), sz)) {
        out.clear();
        return errno != 0 ? errno : EIO;
    }
    return 0;
}

int parse_content_cli(const std::string &content,
                      std::vector<XTemplateColsCLI> &xtemplates,
                      bool is_hardcoded, std::string &errors) {
    std::stringstream ss_content(content);
    std::string line;
    bool xtemplate_body_parsing = false;
    while (std::getline(ss_content, line)) {
        if (line.empty())
            continue;

        XTemplateColsCLI new_xtemplate;
        new_xtemplate.is_hardcoded = is_hardcoded;
        new_xtemplate.name = line;
        const std::string template_name = line;
        while (std::getline(ss_content, line)) {
            if (line == "<TEMPLATE_BODY>") {
                xtemplate_body_parsing = true;
                break;
            }
            size_t tags_specifier_pos = line.find("TAGS:");
            if (tags_specifier_pos != std::string::npos) {
                std::vector<std::string> tags =
                    split_by_comma(line.substr(tags_specifier_pos + 5));
                new_xtemplate.tags = tags;
            }

            size_t vars_specifier_pos = line.find("VARS:");
            if (vars_specifier_pos != std::string::npos) {
                std::vector<std::string> vars_records =
                    split_by_comma(line.substr(vars_specifier_pos + 5));
                std::vector<std::string> vars_names, vars_types;
                std::vector<std::vector<std::string>> vars_tags;
                std::string vars_annot_errors;
                for (const std::string &var_record : vars_records) {
                    std::string var_type, var_name;
                    std::vector<std::string> var_tags;
                    parse_var_record(var_record, var_type, var_name, var_tags,
                                     vars_annot_errors);
                    vars_types.push_back(var_type);
                    vars_names.push_back(var_name);
                    vars_tags.push_back(var_tags);
                }
                if (!vars_annot_errors.empty())
                    errors += "In template '" + template_name + "' (VARS):\n" +
                              vars_annot_errors;
                std::vector<
                    std::vector<std::tuple<unsigned short, std::string, bool>>>
                    vars_dependent;
                std::string dep_errors;
                if (!finalize_vars_dependencies(vars_types, vars_names,
                                                vars_dependent, dep_errors))
                    vars_dependent.assign(vars_names.size(), {});
                if (!dep_errors.empty())
                    errors += "In template '" + template_name + "' (VARS):\n" +
                              dep_errors;
                new_xtemplate.vars_types = vars_types;
                new_xtemplate.vars_names = vars_names;
                new_xtemplate.vars_tags = vars_tags;
            }
        }
        if (!xtemplate_body_parsing)
            continue;

        std::string body;
        while (std::getline(ss_content, line)) {
            if (line.empty() && !xtemplate_body_parsing)
                break;
            if (line == "</TEMPLATE_BODY>") {
                xtemplate_body_parsing = false;
                continue;
            }
            if (!body.empty())
                body += "\n";
            body += line;
        }
        new_xtemplate.body = body;

        ParseDiagnostics body_diag;
        Node *root = Node::parse(body, &body_diag);
        Node::destroy(root);
        if (!body_diag.empty())
            errors += "In template '" + template_name + "' (body):\n" +
                      body_diag.join();

        xtemplates.push_back(new_xtemplate);
    }
    return 0;
}

namespace {

struct IniEntry {
    std::string key;
    std::string value;
};

struct IniGroup {
    std::string name;
    std::vector<IniEntry> entries;
};

std::string strip_ini_quotes(const std::string &value) {
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\'')))
        return value.substr(1, value.size() - 2);
    return value;
}

struct IniFile {
    std::vector<IniGroup> groups;

    const IniGroup *find_group(const std::string &name) const {
        for (const IniGroup &group : groups) {
            if (group.name == name)
                return &group;
        }
        return nullptr;
    }

    std::string get_string(const std::string &group,
                           const std::string &key) const {
        const IniGroup *g = find_group(group);
        if (!g)
            return {};
        for (const IniEntry &entry : g->entries) {
            if (entry.key == key)
                return entry.value;
        }
        return {};
    }

    bool get_bool(const std::string &group, const std::string &key) const {
        std::string value = get_string(group, key);
        std::transform(value.begin(), value.end(), value.begin(), [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        return value == "true" || value == "1" || value == "yes" ||
               value == "on";
    }

    int get_int(const std::string &group, const std::string &key) const {
        const std::string value = trim(get_string(group, key));
        if (value.empty())
            return -1;
        char *end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        if (errno != 0 || !end || *end != '\0')
            return -1;
        if (parsed < -32768L || parsed > 32767L)
            return -1;
        return static_cast<int>(parsed);
    }

    std::vector<std::string> get_keys(const std::string &group) const {
        std::vector<std::string> keys;
        const IniGroup *g = find_group(group);
        if (!g)
            return keys;
        for (const IniEntry &entry : g->entries)
            keys.push_back(entry.key);
        return keys;
    }
};

int read_ini_file(const std::string &path, IniFile &ini, std::string &error) {
    errno = 0;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        const int err = errno != 0 ? errno : ENOENT;
        error = std::strerror(err);
        return err;
    }

    IniGroup *current = nullptr;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::string stripped = trim(line);
        if (stripped.empty() || stripped[0] == '#' || stripped[0] == ';')
            continue;
        if (stripped.front() == '[' && stripped.back() == ']') {
            const std::string name =
                trim(stripped.substr(1, stripped.size() - 2));
            ini.groups.push_back({name, {}});
            current = &ini.groups.back();
            continue;
        }
        if (!current)
            continue; // keys before the first group are ignored

        IniEntry entry;
        const size_t eq = stripped.find('=');
        if (eq == std::string::npos) {
            entry.key = trim(stripped);
        } else {
            entry.key = trim(stripped.substr(0, eq));
            entry.value = strip_ini_quotes(trim(stripped.substr(eq + 1)));
        }
        if (!entry.key.empty())
            current->entries.push_back(std::move(entry));
    }
    return 0;
}

} // namespace

int parse_and_apply_config(const std::string &file_path,
                           XTemplateConfig &config, std::string &errors) {
    IniFile ini;
    std::string io_error;
    const int ret = read_ini_file(file_path, ini, io_error);
    if (ret != 0) {
        errors += "- While parsing \"" + file_path + "\": " + io_error + "\n";
        return ret;
    }

    const std::string configured_default =
        ini.get_string("Main", "DefaultPath");
    if (!configured_default.empty())
        config.default_path = configured_default;

    const int configured_last_idx = ini.get_int("Main", "LastPathIdx");
    config.render_empty_vals = ini.get_bool("Main", "RenderEmptyVals");
    config.active_path = config.default_path;

    std::vector<std::string> cache_list = ini.get_keys("CacheList");
    bool valid_last_path = false;
    if (configured_last_idx >= 0 &&
        configured_last_idx < static_cast<int>(cache_list.size())) {
        config.last_path_idx = static_cast<short>(configured_last_idx);
        config.active_path = cache_list[configured_last_idx];
        valid_last_path = true;
    }

    const auto default_it =
        std::find(cache_list.begin(), cache_list.end(), config.default_path);
    if (default_it != cache_list.end()) {
        if (!valid_last_path)
            config.last_path_idx =
                static_cast<short>(std::distance(cache_list.begin(), default_it));
    } else {
        if (!valid_last_path)
            config.last_path_idx = static_cast<short>(cache_list.size());
        if (!config.default_path.empty())
            cache_list.push_back(config.default_path);
    }

    config.cache_list = std::move(cache_list);
    return 0;
}

int parse_xfile_cli(const std::string &file_path,
                    std::vector<XTemplateColsCLI> &xtemplates,
                    std::string &errors) {
    std::string content;
    if (const int err = read_file_ate(file_path, content); err != 0) {
        errors += std::string("- Error during opening xtemplate file: ") +
                  strerror(err) + "\n";
        return err;
    }
    return parse_content_cli(content, xtemplates, false, errors);
}

int parse_xtemplate_content_hardcoded_cli(
    const std::string &content, std::vector<XTemplateColsCLI> &xtemplates) {
    std::string ignored;
    return parse_content_cli(content, xtemplates, true, ignored);
}
