#include "xtemplate/nodes.hpp"
#include "xtemplate/xtemplate.hpp"
#include <format>
#include <fstream>
#include <gtkmm.h>
#include <iostream>
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

void set_margin(Gtk::Widget &widget, int margin_horizontal,
                int margin_vertical) {
    widget.set_margin_top(margin_vertical);
    widget.set_margin_left(margin_horizontal);
    widget.set_margin_right(margin_horizontal);
    widget.set_margin_bottom(margin_vertical);
};

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

int parse_content(const std::string &content,
                  Glib::RefPtr<Gtk::ListStore> liststore_xtemplates,
                  const XTemplateCols &xtemplate_cols, bool is_hardcoded,
                  std::string &errors) {
    std::stringstream ss_content(content);
    std::string line;
    bool xtemplate_body_parsing = false;
    while (std::getline(ss_content, line)) {
        if (line.empty())
            continue;

        Gtk::TreeModel::Row new_row = *liststore_xtemplates->append();
        new_row[xtemplate_cols.is_hardcoded] = is_hardcoded;
        new_row[xtemplate_cols.name] = line;
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
                new_row[xtemplate_cols.tags] = tags;
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

                new_row[xtemplate_cols.vars_types] = vars_types;
                new_row[xtemplate_cols.vars_names] = vars_names;
                new_row[xtemplate_cols.vars_dependent] = vars_dependent;
                new_row[xtemplate_cols.vars_tags] = vars_tags;
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
        new_row[xtemplate_cols.body] = body;

        ParseDiagnostics body_diag;
        Node *root = Node::parse(body, &body_diag);
        Node::destroy(root);
        if (!body_diag.empty())
            errors += "In template '" + template_name + "' (body):\n" +
                      body_diag.join();
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

int parse_and_apply_config(const std::string &file_path,
                           XTemplateConfig &config, std::string &errors) {
    Glib::KeyFile key_file;
    try {
        key_file.load_from_file(file_path);
    } catch (const Glib::FileError &ex) {
        errors += "- While parsing \"" + file_path + "\": " + ex.what() + "\n";
        return ex.code();
    }
    auto try_get_str = [&](const std::string &group_name,
                           const std::string &key) {
        try {
            if (!key_file.has_key(group_name, key))
                return std::string();
            return std::string(key_file.get_string(group_name, key));
        } catch (...) {
            return std::string();
        }
    };
    auto try_get_bool = [&](const std::string group_name,
                            const std::string &key) {
        try {
            if (!key_file.has_key(group_name, key))
                return false;
            return key_file.get_boolean(group_name, key);
        } catch (...) {
            return false;
        }
    };
    auto try_get_int = [&](const std::string group_name,
                           const std::string &key) {
        try {
            if (!key_file.has_key(group_name, key))
                return -1;
            return key_file.get_integer(group_name, key);
        } catch (...) {
            return -1;
        }
    };
    auto try_get_keys =
        [&](const std::string group_name) -> std::vector<std::string> {
        try {
            return key_file.get_keys(group_name);
        } catch (...) {
            return std::vector<std::string>();
        }
    };

    std::string default_path = try_get_str("Main", "DefaultPath");
    int last_path_idx = try_get_int("Main", "LastPathIdx");
    bool render_empty_vals = try_get_bool("Main", "RenderEmptyVals");
    std::vector<std::string> cache_list = try_get_keys("CacheList");

    if (!default_path.empty()) {
        config.default_path = default_path;
        config.active_path = default_path;
    }
    bool valid_last_path = false;
    if (last_path_idx >= 0 &&
        last_path_idx < static_cast<int>(cache_list.size())) {
        const std::string &last_path = cache_list[last_path_idx];
        config.last_path_idx = last_path_idx;
        config.active_path = last_path;
        valid_last_path = true;
    }
    if (const auto &default_path_idx =
            std::find(cache_list.begin(), cache_list.end(), default_path);
        default_path_idx != cache_list.end()) {
        if (!valid_last_path)
            config.last_path_idx =
                std::distance(cache_list.begin(), default_path_idx);
    } else {
        if (!valid_last_path)
            config.last_path_idx = cache_list.size();
        cache_list.push_back(default_path);
    }
    config.render_empty_vals = render_empty_vals;
    config.cache_list = cache_list;
    return 0;
}

int update_config(const std::string &file_path, const XTemplateConfig &config,
                  std::string &errors) {
    Glib::KeyFile key_file;
    try {
        key_file.set_string("Main", "DefaultPath", config.default_path);
        key_file.set_integer("Main", "LastPathIdx", config.last_path_idx);
        key_file.set_boolean("Main", "RenderEmptyVals",
                             config.render_empty_vals);
        const std::vector<std::string> &cache_list = config.cache_list;
        for (const std::string &cached : cache_list) {
            key_file.set_string("CacheList", cached, "");
        }
    } catch (const Glib::KeyFileError &ex) {
        errors += "- While updating \"" + file_path + "\": " + ex.what() + "\n";
        return 1;
    }

    try {
        key_file.save_to_file(file_path);
    } catch (const Glib::FileError &ex) {
        errors += "- While saving \"" + file_path + "\": " + ex.what() + "\n";
        return ex.code();
    }
    return 0;
}

int parse_xfile(const std::string &file_path,
                Glib::RefPtr<Gtk::ListStore> liststore_xtemplates,
                const XTemplateCols &xtemplate_cols, std::string &errors) {
    std::string content;
    if (const int err = read_file_ate(file_path, content); err != 0) {
        errors += std::string("- Error during opening xtemplate file: ") +
                  strerror(err) + "\n";
        return err;
    }
    return parse_content(content, liststore_xtemplates, xtemplate_cols, false,
                         errors);
}

int parse_xtemplate_content_hardcoded(
    const std::string &content,
    Glib::RefPtr<Gtk::ListStore> liststore_xtemplates,
    const XTemplateCols &xtemplate_cols) {
    std::string ignored;
    return parse_content(content, liststore_xtemplates, xtemplate_cols, true,
                         ignored);
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

int write_xfile(const std::string &file_path,
                const Glib::RefPtr<Gtk::ListStore> &liststore_xtemplates,
                const XTemplateCols &xtemplate_cols) {
    errno = 0;
    std::ofstream file(file_path);
    int err = errno;
    if (!file.is_open() || err != 0)
        return err;
    for (const Gtk::TreeModel::Row &row : liststore_xtemplates->children()) {
        if (row[xtemplate_cols.is_hardcoded])
            continue;
        std::string name = static_cast<Glib::ustring>(row[xtemplate_cols.name]);
        file << trim(name) << "\n";

        std::vector<std::string> tags = row[xtemplate_cols.tags];
        if (!tags.empty()) {
            file << "TAGS: ";
            for (size_t i = 0; i < tags.size(); ++i) {
                file << tags[i];
                if (i + 1 < tags.size())
                    file << ", ";
            }
            file << "\n";
        }

        std::vector<std::string> vars_names = row[xtemplate_cols.vars_names];
        if (!vars_names.empty()) {
            file << "VARS: ";
            std::vector<std::string> vars_types =
                row[xtemplate_cols.vars_types];
            std::vector<
                std::vector<std::tuple<short unsigned, std::string, bool>>>
                vars_dependent = row[xtemplate_cols.vars_dependent];
            std::vector<std::vector<std::string>> vars_tags =
                row[xtemplate_cols.vars_tags];
            if (vars_dependent.size() < vars_names.size())
                vars_dependent.resize(vars_names.size());
            if (vars_tags.size() < vars_names.size())
                vars_tags.resize(vars_names.size());
            for (size_t i = 0; i < vars_names.size(); ++i) {
                std::string type_out = format_var_type_with_deps(
                    i < vars_types.size() ? vars_types[i] : "", i, vars_names,
                    vars_dependent, vars_tags[i]);
                file << type_out << "; ";
                file << vars_names[i];
                if (i + 1 < vars_names.size())
                    file << ", ";
            }
            file << "\n";
        }

        std::string body = static_cast<Glib::ustring>(row[xtemplate_cols.body]);
        file << "<TEMPLATE_BODY>\n" << trim(body) << "\n</TEMPLATE_BODY>\n\n";
    }
    return 0;
}
