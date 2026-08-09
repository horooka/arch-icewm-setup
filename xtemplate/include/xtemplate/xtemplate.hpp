#include <glibmm.h>
#include <gtkmm.h>

class XTemplateCols : public Gtk::TreeModel::ColumnRecord {
    public:
        XTemplateCols() {
            add(name);
            add(is_hardcoded);
            add(vars_types);
            add(vars_names);
            add(vars_dependent);
            add(vars_tags);
            add(tags);
            add(body);
        }

    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<bool> is_hardcoded;
    Gtk::TreeModelColumn<std::vector<std::string>> vars_types;
    Gtk::TreeModelColumn<std::vector<std::string>> vars_names;
    // Per-controller outgoing deps: (dependent_row, expected_value, inverted).
    // A dependent is sensitive only when ALL of its controllers satisfy.
    Gtk::TreeModelColumn<std::vector<std::vector<std::tuple<unsigned short, std::string, bool>>>> vars_dependent;
    Gtk::TreeModelColumn<std::vector<std::vector<std::string>>> vars_tags;
    Gtk::TreeModelColumn<std::vector<std::string>> tags;
    Gtk::TreeModelColumn<Glib::ustring> body;
};

// Used in cli mode
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
    short last_path_idx;
    bool render_empty_vals = false;
    std::vector<std::string> cache_list;
};

const int STATE_SEARCH = 0;
const int STATE_XTEMPLATE_OPENED = 1;
const int STATE_XTEMPLATE_CREATION = 2;

void set_margin(Gtk::Widget& widget, int margin_horizontal, int margin_vertical);
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
int parse_and_apply_config(const std::string &file_path,
                                     XTemplateConfig &config,
                                     std::string &errors);
int update_config(const std::string &file_path,
                          const XTemplateConfig &config,
                          std::string &errors);
int parse_xtemplate_content_hardcoded(const std::string &content, Glib::RefPtr<Gtk::ListStore> liststore_xtemplates, const XTemplateCols &xtemplate_cols);
int parse_xfile(const std::string &file_path, Glib::RefPtr<Gtk::ListStore> liststore_xtemplates, const XTemplateCols& xtemplate_cols, std::string &errors);
int parse_xtemplate_content_hardcoded_cli(const std::string &content, std::vector<XTemplateColsCLI> &xtemplates);
int parse_xfile_cli(const std::string &file_path, std::vector<XTemplateColsCLI> &xtemplates, std::string &errors);
int write_xfile(const std::string &file_path, const Glib::RefPtr<Gtk::ListStore> &liststore_xtemplates, const XTemplateCols& xtemplate_cols);
int run(int argc, char *argv[]);
