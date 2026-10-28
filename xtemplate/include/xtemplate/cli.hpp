#pragma once

#include <string>
#include <tuple>
#include <vector>

// GTK-free types and helpers shared by the CLI and the GUI build.
// Nothing in this header may include gtkmm/glibmm.

// Used in CLI mode
struct XTemplateColsCLI {
    XTemplateColsCLI() {}

    std::string name;
    bool is_hardcoded;
    std::vector<std::string> vars_types;
    std::vector<std::string> vars_names;
    // std::vector<std::vector<std::tuple<unsigned short, std::string, bool>>> vars_dependent;
    std::vector<std::vector<std::string>> vars_tags;
    std::vector<std::string> tags;
    std::string body;
};

struct XTemplateConfig {
    std::string active_path;
    std::string default_path;
    short last_path_idx = 0;
    bool render_empty_vals = false;
    std::vector<std::string> cache_list;
};

// CLI entry point (implemented in src/cli.cpp).
int run_cli(int argc, char *argv[]);

std::string trim(const std::string &s);
std::vector<std::string> split_by_comma(const std::string &s);

// Type grammar: base[=deps][tags]  (tags are a trailing [...] suffix).
// Strips trailing [tag-...] into tags and leaves base[=deps] in var_type.
// Soft errors are appended; always leaves var_type usable for depends-on parse.
void strip_trailing_var_tags(std::string &var_type,
                             std::vector<std::string> &tags,
                             const std::string &var_name, std::string &errors);

// Split one VARS record: "type; name" (first ';') or bare name.
void parse_var_record(const std::string &var_record, std::string &var_type,
                      std::string &var_name,
                      std::vector<std::string> &var_tags, std::string &errors);

// Strip Type=deps from vars_types and fill vars_dependent.
// Soft: invalid deps are skipped and appended to errors; parsing continues.
// Returns false only on hard size mismatch.
bool finalize_vars_dependencies(
    std::vector<std::string> &vars_types,
    const std::vector<std::string> &vars_names,
    std::vector<std::vector<std::tuple<unsigned short, std::string, bool>>>
        &vars_dependent,
    std::string &errors);

// Rebuild "Type=dep-!other(value)[tags]" from dependent + tags tables.
std::string format_var_type_with_deps(
    const std::string &clean_type, size_t var_idx,
    const std::vector<std::string> &vars_names,
    const std::vector<
        std::vector<std::tuple<unsigned short, std::string, bool>>>
        &vars_dependent,
    const std::vector<std::string> &var_tags = {});

int read_file_ate(const std::string &path, std::string &out);

int parse_content_cli(const std::string &content,
                      std::vector<XTemplateColsCLI> &xtemplates,
                      bool is_hardcoded, std::string &errors);

int parse_and_apply_config(const std::string &file_path,
                           XTemplateConfig &config, std::string &errors);

int parse_xfile_cli(const std::string &file_path,
                    std::vector<XTemplateColsCLI> &xtemplates,
                    std::string &errors);

int parse_xtemplate_content_hardcoded_cli(
    const std::string &content, std::vector<XTemplateColsCLI> &xtemplates);
