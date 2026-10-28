#pragma once

#include "xtemplate/cli.hpp"

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

const int STATE_SEARCH = 0;
const int STATE_XTEMPLATE_OPENED = 1;
const int STATE_XTEMPLATE_CREATION = 2;

void set_margin(Gtk::Widget& widget, int margin_horizontal, int margin_vertical);
int update_config(const std::string &file_path,
                          const XTemplateConfig &config,
                          std::string &errors);
int parse_xtemplate_content_hardcoded(const std::string &content, Glib::RefPtr<Gtk::ListStore> liststore_xtemplates, const XTemplateCols &xtemplate_cols);
int parse_xfile(const std::string &file_path, Glib::RefPtr<Gtk::ListStore> liststore_xtemplates, const XTemplateCols& xtemplate_cols, std::string &errors);
int write_xfile(const std::string &file_path, const Glib::RefPtr<Gtk::ListStore> &liststore_xtemplates, const XTemplateCols& xtemplate_cols);
int run(int argc, char *argv[]);
