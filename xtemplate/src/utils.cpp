#include "xtemplate/cli.hpp"
#include "xtemplate/nodes.hpp"
#include "xtemplate/xtemplate.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <gtkmm.h>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

void set_margin(Gtk::Widget &widget, int margin_horizontal,
                int margin_vertical) {
    widget.set_margin_top(margin_vertical);
    widget.set_margin_left(margin_horizontal);
    widget.set_margin_right(margin_horizontal);
    widget.set_margin_bottom(margin_vertical);
};

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
