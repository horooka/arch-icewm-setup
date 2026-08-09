#include "xtemplate/xtemplate.hpp"
#include "xtemplate/block_canvas.hpp"
#include "xtemplate/nodes.hpp"
#include "xtemplate/xtemplate_hardcoded.h"
#include <gdkmm.h>
#include <glibmm.h>
#include <gtkmm.h>
#include <iostream>
#include <unordered_map>

class XGtkmm3Template : public Gtk::Window {
    public:
        XGtkmm3Template(const Glib::RefPtr<Gio::Application> &app_ref)
            : config_path(std::string(getenv("HOME")) +
                          "/.config/xtemplate.ini"),
              app(app_ref) {
            config.default_path =
                std::string(getenv("HOME")) + "/.config/xtemplate.txt";

            setup_gresources();
            setup_ui();
            setup_toolbar();
            setup_signals();
            setup_data();

            show_all_children();
        }

    protected:
        void show_notification(const Glib::ustring &title,
                               const Glib::ustring &message, int timeout) {
            Glib::ustring id = Glib::ustring::format(
                "notif-%lu-%d", static_cast<unsigned long>(time(nullptr)),
                rand() % 1000);
            auto notification = Gio::Notification::create(title);
            notification->set_body(message);
            notification->set_priority(Gio::NotificationPriority(0));
            Glib::signal_idle().connect([this, notification, id]() {
                if (app) {
                    app->send_notification(id, notification);
                }
                return false;
            });
            Glib::signal_timeout().connect_seconds(
                [this, id]() {
                    if (app) {
                        app->withdraw_notification(id);
                    }
                    return false;
                },
                timeout);
        }

        bool on_exit_clicked() {
            if (no_changes)
                return false;
            int ret = write_xfile(config.active_path, liststore_xtemplates,
                                  xtemplate_cols);
            if (ret == 0)
                return false;
            Gtk::MessageDialog dialog(
                *this,
                std::string("Error with saving xtemplates: ") + strerror(ret),
                false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, true);
            dialog.run();
            return true;
        }

        void on_xtemplate_selection_changed() {
            Gtk::TreeModel::iterator curr_xtemplate =
                treeview_xtemplates.get_selection()->get_selected();
            if (!curr_xtemplate)
                return;
            Gtk::TreeModel::iterator true_curr_xtemplate =
                filter_xtemplates->convert_iter_to_child_iter(*curr_xtemplate);

            std::vector<std::string> tags =
                (*true_curr_xtemplate)[xtemplate_cols.tags];
            Glib::ustring tags_str;
            for (size_t i = 0; i < tags.size(); ++i) {
                if (i > 0)
                    tags_str += ", ";
                tags_str += tags[i];
            }
            label_tags.set_text(tags_str);
        }

        void on_select_xfile_clicked() {
            Gtk::FileChooserDialog dialog("Select xtemplates file",
                                          Gtk::FILE_CHOOSER_ACTION_OPEN);
            dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
            dialog.add_button("Open", Gtk::RESPONSE_ACCEPT);
            dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
            if (dialog.run() == Gtk::RESPONSE_ACCEPT) {
                std::string select = dialog.get_filename();
                config.active_path = select;
                std::vector<std::string> cache_list = config.cache_list;
                if (const auto &last_path_idx =
                        std::find(cache_list.begin(), cache_list.end(), select);
                    last_path_idx != cache_list.end()) {
                    config.last_path_idx =
                        std::distance(cache_list.begin(), last_path_idx);
                } else {
                    config.last_path_idx = cache_list.size();
                    cache_list.push_back(select);
                }
                config.active_path = select;
                liststore_xtemplates->clear();
                (void)parse_xtemplate_content_hardcoded(
                    XTEMPLATE_CONTENT_HARDCODED, liststore_xtemplates,
                    xtemplate_cols);
                std::string errors;
                (void)parse_xfile(config.active_path, liststore_xtemplates,
                                  xtemplate_cols, errors);
                if (!errors.empty()) {
                    Gtk::MessageDialog dialog(
                        "Error parsing xtemplates file:\n\n" + errors, false,
                        Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
                    dialog.run();
                }
                label_xfile.set_text(config.active_path);

                errors.clear();
                int ret_update = update_config(config_path, config, errors);
                if (ret_update != 0) {
                    Gtk::MessageDialog dialog(
                        "Error writing config file:\n\n" + errors, false,
                        Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
                    dialog.run();
                }
            }
        }

        void on_open_xtemplate_clicked() {
            Gtk::TreeModel::iterator curr_xtemplate =
                treeview_xtemplates.get_selection()->get_selected();
            if (!curr_xtemplate)
                return;
            if (app_state == STATE_XTEMPLATE_OPENED) {
                stack_main.set_visible_child(frame_xtemplates);
                app_state = STATE_SEARCH;
                return;
            }
            app_state = STATE_XTEMPLATE_OPENED;

            if (textview_xtemplate_result.get_parent())
                textview_xtemplate_result.unparent();
            for (Gtk::Widget *child : vbox_xtemplate_form.get_children()) {
                vbox_xtemplate_form.remove(*child);
            }
            Gtk::TreeModel::Row true_curr_row =
                *filter_xtemplates->convert_iter_to_child_iter(*curr_xtemplate);
            std::vector<std::string> vars_types =
                true_curr_row[xtemplate_cols.vars_types];
            std::vector<std::string> vars_names =
                true_curr_row[xtemplate_cols.vars_names];
            std::vector<
                std::vector<std::tuple<unsigned short, std::string, bool>>>
                vars_dependent = true_curr_row[xtemplate_cols.vars_dependent];
            // Older / no-deps templates may leave this empty
            if (vars_dependent.size() < vars_names.size())
                vars_dependent.resize(vars_names.size());
            std::vector<Gtk::Widget *> input_widgets;
            Gtk::Grid *grid_vars = Gtk::make_managed<Gtk::Grid>();
            grid_vars->set_column_spacing(10);
            grid_vars->set_row_spacing(10);
            vbox_xtemplate_form.pack_start(*grid_vars, Gtk::PACK_SHRINK);
            size_t row_idx = 0;
            for (size_t i = 0; i < vars_names.size(); ++i) {
                Gtk::Image *image_var_type = Gtk::make_managed<Gtk::Image>();
                grid_vars->attach(*image_var_type, 0, row_idx, 1, 1);
                Gtk::Frame *frame_var_type = Gtk::make_managed<Gtk::Frame>();
                Gtk::Label *label_var_type = Gtk::make_managed<Gtk::Label>();
                set_margin(*label_var_type, 5, 0);
                frame_var_type->add(*label_var_type);
                grid_vars->attach(*frame_var_type, 1, row_idx, 1, 1);
                label_var_type->set_halign(Gtk::ALIGN_START);
                std::string var_type = vars_types[i];
                if (var_type == "PlainText")
                    label_var_type->set_name("darkgray");
                label_var_type->set_text(display_var_type(var_type));
                Gtk::Label *label_var_name = Gtk::make_managed<Gtk::Label>();
                grid_vars->attach(*label_var_name, 2, row_idx, 1, 1);
                label_var_name->set_halign(Gtk::ALIGN_START);
                label_var_name->set_text(vars_names[i]);

                image_var_type->set(pixbuf_variable);
                Gtk::Widget *input_widget = nullptr;
                if (is_xcheckbox(var_type)) {
                    image_var_type->set(pixbuf_xcheckbox_variable);
                    auto *check = Gtk::make_managed<Gtk::CheckButton>();
                    check->set_active(false);
                    input_widget = check;
                } else if (is_xvariant(var_type)) {
                    std::vector<std::string> options =
                        parse_xvariant_options(var_type);
                    if (options.empty()) {
                        auto *entry = Gtk::make_managed<Gtk::Entry>();
                        entry->set_text("");
                        input_widget = entry;
                    } else {
                        image_var_type->set(pixbuf_xvariant_variable);
                        auto *combo = Gtk::make_managed<Gtk::ComboBoxText>();
                        for (const std::string &opt : options)
                            combo->append(opt);
                        combo->set_active(0);
                        input_widget = combo;
                    }
                } else {
                    if (var_type == "PlainText")
                        image_var_type->set(pixbuf_plaintext_variable);
                    auto *entry = Gtk::make_managed<Gtk::Entry>();
                    entry->set_text("");
                    input_widget = entry;
                }
                grid_vars->attach(*input_widget, 3, row_idx, 1, 1);
                input_widgets.push_back(input_widget);
                row_idx++;
            }
            // Depends-on: AND across all controllers via contribution vectors.
            // contrib[dep_row][slot] = whether that controller condition holds.
            using DepLink =
                std::tuple<unsigned short, std::string, bool>; // dep, val, !
            struct CtrlEffect {
                    unsigned short dep_row;
                    size_t slot;
                    std::string expected;
                    bool inverted;
            };

            const size_t nvars = vars_names.size();
            auto contrib =
                std::make_shared<std::vector<std::vector<bool>>>(nvars);
            std::vector<std::vector<CtrlEffect>> effects(nvars);

            for (size_t ctrl = 0; ctrl < nvars; ++ctrl) {
                for (const DepLink &link : vars_dependent[ctrl]) {
                    unsigned short dep_row = std::get<0>(link);
                    if (dep_row >= nvars)
                        continue;
                    size_t slot = (*contrib)[dep_row].size();
                    (*contrib)[dep_row].push_back(false);
                    effects[ctrl].push_back(CtrlEffect{
                        dep_row, slot, std::get<1>(link), std::get<2>(link)});
                }
            }

            auto set_row_sensitive = [grid_vars](unsigned short row,
                                                 bool sensitive) {
                for (int col = 0; col <= 3; ++col) {
                    if (Gtk::Widget *w = grid_vars->get_child_at(col, row))
                        w->set_sensitive(sensitive);
                }
            };

            auto refresh_dep_row = [contrib,
                                    set_row_sensitive](unsigned short dep_row) {
                bool all_ok = true;
                for (bool ok : (*contrib)[dep_row])
                    all_ok = all_ok && ok;
                set_row_sensitive(dep_row, all_ok);
            };

            auto condition_ok = [](const std::string &ctrl_value,
                                   bool is_checkbox,
                                   const std::string &expected, bool inverted) {
                bool ok = false;
                if (is_checkbox) {
                    // Checkbox: active when ON; (value) is ignored
                    ok = ctrl_value == "ON";
                } else if (expected.empty()) {
                    ok = !ctrl_value.empty();
                } else {
                    ok = ctrl_value == expected;
                }
                return inverted ? !ok : ok;
            };

            auto apply_controller = [input_widgets, effects, contrib,
                                     condition_ok,
                                     refresh_dep_row](size_t ctrl) {
                if (ctrl >= effects.size() || effects[ctrl].empty())
                    return;
                Gtk::Widget *src = input_widgets[ctrl];
                std::string value;
                bool is_checkbox = false;
                if (auto *check = dynamic_cast<Gtk::CheckButton *>(src)) {
                    is_checkbox = true;
                    value = check->get_active() ? "ON" : "OFF";
                } else if (auto *combo =
                               dynamic_cast<Gtk::ComboBoxText *>(src)) {
                    value = combo->get_active_text();
                } else if (auto *entry = dynamic_cast<Gtk::Entry *>(src)) {
                    value = entry->get_text();
                }
                for (const CtrlEffect &eff : effects[ctrl]) {
                    (*contrib)[eff.dep_row][eff.slot] = condition_ok(
                        value, is_checkbox, eff.expected, eff.inverted);
                    refresh_dep_row(eff.dep_row);
                }
            };

            for (size_t ctrl = 0; ctrl < nvars; ++ctrl) {
                if (effects[ctrl].empty())
                    continue;
                apply_controller(ctrl);
                Gtk::Widget *src = input_widgets[ctrl];
                if (auto *check = dynamic_cast<Gtk::CheckButton *>(src)) {
                    check->signal_toggled().connect(
                        [ctrl, apply_controller]() { apply_controller(ctrl); });
                } else if (auto *combo =
                               dynamic_cast<Gtk::ComboBoxText *>(src)) {
                    combo->signal_changed().connect(
                        [ctrl, apply_controller]() { apply_controller(ctrl); });
                } else if (auto *entry = dynamic_cast<Gtk::Entry *>(src)) {
                    entry->signal_changed().connect(
                        [ctrl, apply_controller]() { apply_controller(ctrl); });
                }
            }

            Gtk::Box *hbox_render =
                Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
            Gtk::Entry *entry_indent = Gtk::make_managed<Gtk::Entry>();
            entry_indent->set_placeholder_text("Indent");
            hbox_render->pack_end(*entry_indent, Gtk::PACK_SHRINK);
            Gtk::Button *button_result = Gtk::make_managed<Gtk::Button>();
            button_result->set_label("Copy");
            button_result->set_hexpand(true);
            hbox_render->pack_start(*button_result, Gtk::PACK_EXPAND_WIDGET);
            vbox_xtemplate_form.pack_start(*hbox_render, Gtk::PACK_SHRINK);
            Gtk::Frame *frame_result = Gtk::make_managed<Gtk::Frame>();
            vbox_xtemplate_form.pack_start(*frame_result,
                                           Gtk::PACK_EXPAND_WIDGET);
            Gtk::ScrolledWindow *scrolled_result =
                Gtk::make_managed<Gtk::ScrolledWindow>();
            frame_result->add(*scrolled_result);
            scrolled_result->set_name("white_background");
            scrolled_result->add(textview_xtemplate_result);

            Glib::RefPtr<Gtk::TextBuffer> buffer =
                textview_xtemplate_result.get_buffer();
            std::vector<std::vector<std::string>> vars_tags =
                true_curr_row[xtemplate_cols.vars_tags];
            if (vars_tags.size() < vars_names.size())
                vars_tags.resize(vars_names.size());
            // Copy body by value — do not capture TreeModel::Row in lambdas
            const std::string body =
                static_cast<Glib::ustring>(true_curr_row[xtemplate_cols.body]);
            std::unordered_map<std::string, std::string> bodies_hardcoded;
            std::unordered_map<std::string, std::string> bodies_file;
            for (const Gtk::TreeModel::Row &row :
                 liststore_xtemplates->children()) {
                const std::string name =
                    static_cast<Glib::ustring>(row[xtemplate_cols.name]);
                const std::string xb =
                    static_cast<Glib::ustring>(row[xtemplate_cols.body]);
                if (row[xtemplate_cols.is_hardcoded])
                    bodies_hardcoded[name] = xb;
                else
                    bodies_file[name] = xb;
            }
            const std::string current_xfile =
                true_curr_row[xtemplate_cols.is_hardcoded] ? "__hardcoded__"
                                                           : config.active_path;
            buffer->set_text(body);
            button_result->signal_clicked().connect([this, buffer,
                                                     input_widgets, vars_names,
                                                     vars_tags, body,
                                                     bodies_hardcoded,
                                                     bodies_file, current_xfile,
                                                     entry_indent]() {
                std::unordered_map<std::string, std::string> vars;
                std::string required_errors;
                for (size_t i = 0; i < vars_names.size(); ++i) {
                    Gtk::Widget *w = input_widgets[i];
                    std::string value;
                    if (auto *check = dynamic_cast<Gtk::CheckButton *>(w)) {
                        value = check->get_active() ? "ON" : "OFF";
                    } else if (auto *combo =
                                   dynamic_cast<Gtk::ComboBoxText *>(w)) {
                        value = combo->get_active_text();
                    } else if (auto *entry = dynamic_cast<Gtk::Entry *>(w)) {
                        value = entry->get_text();
                    }
                    vars[vars_names[i]] = value;
                    for (const std::string &tag : vars_tags[i]) {
                        if (tag == "REQUIRED" && value.empty()) {
                            required_errors += "- Variable " + vars_names[i] +
                                               " is REQUIRED but empty\n";
                        }
                    }
                }
                if (!required_errors.empty()) {
                    show_copyable_mess(
                        Gtk::MESSAGE_ERROR,
                        "Cannot render — required vars missing:\n\n" +
                            required_errors);
                    return;
                }
                ParseDiagnostics diag;
                std::string result;
                std::string render_error;
                XTemplateRenderContext xctx;
                xctx.sub_indent = 0;
                xctx.current_xfile = current_xfile;
                xctx.depth = 0;
                xctx.max_depth = 12;
                xctx.diag = &diag;
                xctx.resolve = [bodies_hardcoded,
                                bodies_file](const std::string &name,
                                             const std::string &xfile,
                                             std::string &out_body) -> bool {
                    if (xfile == "__hardcoded__") {
                        auto it = bodies_hardcoded.find(name);
                        if (it == bodies_hardcoded.end())
                            return false;
                        out_body = it->second;
                        return true;
                    }
                    auto it = bodies_file.find(name);
                    if (it == bodies_file.end())
                        return false;
                    out_body = it->second;
                    return true;
                };

                unsigned char indent = 0;
                if (!entry_indent->get_text().empty()) {
                    try {
                        indent = std::stoi(entry_indent->get_text());
                    } catch (...) {
                        diag.add("Given indent is invalid: " +
                                 entry_indent->get_text());
                    }
                }
                if (!render_xtemplate(
                        body, vars, result, config.render_empty_vals, &diag,
                        &render_error, indent /*top-level indent*/, &xctx)) {
                    show_copyable_mess(Gtk::MESSAGE_ERROR,
                                       "Template ##ERROR:\n\n" + render_error);
                    return;
                }
                buffer->set_text(result);
                if (!diag.empty()) {
                    show_copyable_mess(
                        Gtk::MESSAGE_WARNING,
                        "Template parse issues (rendering continued with "
                        "fallbacks):\n\n" +
                            diag.join());
                }
                std::string cmd = "xclip -selection clipboard";
                FILE *pipe = popen(cmd.c_str(), "w");
                if (pipe) {
                    fwrite(result.c_str(), 1, result.size(), pipe);
                    pclose(pipe);
                    show_notification("Success",
                                      "Successfylly copied to clipboard", 2);
                }
            });
            set_margin(textview_xtemplate_result, 10, 10);
            vbox_xtemplate_form.show_all();
            stack_main.set_visible_child(vbox_xtemplate_form);
        }

        void on_new_xtemplate_clicked() {
            if (app_state == STATE_XTEMPLATE_CREATION) {
                stack_main.set_visible_child(frame_xtemplates);
                app_state = STATE_SEARCH;
                return;
            }
            app_state = STATE_XTEMPLATE_CREATION;
            for (Gtk::Widget *child : vbox_new_xtemplate_form.get_children()) {
                vbox_new_xtemplate_form.remove(*child);
            }

            Gtk::Grid *grid_name_tags = Gtk::make_managed<Gtk::Grid>();
            grid_name_tags->set_column_spacing(10);
            grid_name_tags->set_row_spacing(10);
            vbox_new_xtemplate_form.pack_start(*grid_name_tags,
                                               Gtk::PACK_SHRINK);
            Gtk::Label *label_new_name = Gtk::make_managed<Gtk::Label>();
            grid_name_tags->attach(*label_new_name, 0, 0, 1, 1);
            label_new_name->set_text("Name");
            Gtk::Entry *entry_new_name = Gtk::make_managed<Gtk::Entry>();
            grid_name_tags->attach(*entry_new_name, 1, 0, 1, 1);
            entry_new_name->set_text("");
            Gtk::Label *label_new_tags = Gtk::make_managed<Gtk::Label>();
            grid_name_tags->attach(*label_new_tags, 0, 1, 1, 1);
            label_new_tags->set_text("Tags");
            Gtk::Entry *entry_new_tags = Gtk::make_managed<Gtk::Entry>();
            grid_name_tags->attach(*entry_new_tags, 1, 1, 1, 1);
            entry_new_tags->set_tooltip_text("Tags comma-separated");
            entry_new_tags->set_text("");
            Gtk::Box *hbox_new_vars =
                Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
            vbox_new_xtemplate_form.pack_start(*hbox_new_vars,
                                               Gtk::PACK_SHRINK);
            hbox_new_vars->set_halign(Gtk::ALIGN_CENTER);
            Gtk::Label *label_new_vars = Gtk::make_managed<Gtk::Label>();
            hbox_new_vars->pack_start(*label_new_vars, Gtk::PACK_SHRINK);
            label_new_vars->set_text("Template's variables");
            Gtk::Grid *grid_new_vars = Gtk::make_managed<Gtk::Grid>();
            grid_new_vars->set_column_spacing(10);
            grid_new_vars->set_row_spacing(10);
            vbox_new_xtemplate_form.pack_start(*grid_new_vars,
                                               Gtk::PACK_SHRINK);
            Gtk::Button *button_new_var = Gtk::make_managed<Gtk::Button>();
            hbox_new_vars->pack_start(*button_new_var, Gtk::PACK_SHRINK);
            image_new_variable.set(pixbuf_new_variable);
            button_new_var->set_image(image_new_variable);
            button_new_var->set_always_show_image(true);
            button_new_var->signal_clicked().connect([this, grid_new_vars]() {
                int next_var_num = 0;
                while (grid_new_vars->get_child_at(1, next_var_num)) {
                    Gtk::Widget *child =
                        grid_new_vars->get_child_at(1, next_var_num);
                    if (!child)
                        break;
                    next_var_num++;
                }

                Gtk::Image *image_var = Gtk::make_managed<Gtk::Image>();
                grid_new_vars->attach(*image_var, 0, next_var_num, 1, 1);
                image_var->set_halign(Gtk::ALIGN_START);
                image_var->set(pixbuf_variable);
                Gtk::Entry *entry_type = Gtk::make_managed<Gtk::Entry>();
                entry_type->set_placeholder_text("type[=dep-!dep]");
                entry_type->set_text("");
                entry_type->signal_changed().connect(
                    [this, entry_type, image_var]() {
                        std::string type = entry_type->get_text();
                        size_t eq = type.find('=');
                        if (eq != std::string::npos)
                            type = type.substr(0, eq);
                        type = trim(type);
                        if (type.empty())
                            image_var->set(pixbuf_variable);
                        else if (type == "XCHECKBOX")
                            image_var->set(pixbuf_xcheckbox_variable);
                        else if (type.compare(0, 8, "XVARIANT") == 0)
                            image_var->set(pixbuf_xvariant_variable);
                        else if (type == "PlainText")
                            image_var->set(pixbuf_plaintext_variable);
                        else
                            image_var->set(pixbuf_variable);
                    });
                grid_new_vars->attach(*entry_type, 1, next_var_num, 1, 1);
                Gtk::Entry *entry_name = Gtk::make_managed<Gtk::Entry>();
                entry_name->set_placeholder_text("name");
                entry_name->set_text("");
                grid_new_vars->attach(*entry_name, 2, next_var_num, 1, 1);
                Gtk::Button *button_delete_var =
                    Gtk::make_managed<Gtk::Button>();
                Gtk::Image *image_delete = Gtk::make_managed<Gtk::Image>();
                image_delete->set(pixbuf_delete);
                button_delete_var->set_image(*image_delete);
                button_delete_var->set_always_show_image(true);
                button_delete_var->signal_clicked().connect(
                    [grid_new_vars, next_var_num]() {
                        grid_new_vars->remove_row(next_var_num);
                    });
                grid_new_vars->attach(*button_delete_var, 3, next_var_num, 1,
                                      1);
                grid_new_vars->show_all();
            });
            Gtk::Label *label_new_body = Gtk::make_managed<Gtk::Label>();
            label_new_body->set_text("Body (nodes)");
            vbox_new_xtemplate_form.pack_start(*label_new_body,
                                               Gtk::PACK_SHRINK);

            Gtk::Box *hbox_body =
                Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            vbox_new_xtemplate_form.pack_start(*hbox_body,
                                               Gtk::PACK_EXPAND_WIDGET);

            Gtk::Box *vbox_palette =
                Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
            hbox_body->pack_start(*vbox_palette, Gtk::PACK_SHRINK);
            auto *label_palette = Gtk::make_managed<Gtk::Label>("Palette");
            label_palette->set_halign(Gtk::ALIGN_START);
            vbox_palette->pack_start(*label_palette, Gtk::PACK_SHRINK);

            Gtk::Frame *frame_new_body = Gtk::make_managed<Gtk::Frame>();
            hbox_body->pack_start(*frame_new_body, Gtk::PACK_EXPAND_WIDGET);
            Gtk::ScrolledWindow *scrolled_new_body =
                Gtk::make_managed<Gtk::ScrolledWindow>();
            frame_new_body->add(*scrolled_new_body);
            scrolled_new_body->set_name("white_background");
            scrolled_new_body->set_policy(Gtk::POLICY_AUTOMATIC,
                                          Gtk::POLICY_AUTOMATIC);
            auto *block_canvas = Gtk::make_managed<BlockCanvas>();
            scrolled_new_body->add(*block_canvas);

            auto add_palette_btn = [vbox_palette, block_canvas](
                                       const char *label, BlockKind kind) {
                auto *btn = Gtk::make_managed<Gtk::Button>(label);
                vbox_palette->pack_start(*btn, Gtk::PACK_SHRINK);
                btn->signal_clicked().connect(
                    [block_canvas, kind]() { block_canvas->add_block(kind); });
            };
            add_palette_btn("TEXT", BlockKind::Text);
            add_palette_btn("IF_ON", BlockKind::IfOn);
            add_palette_btn("IF_OFF", BlockKind::IfOff);
            add_palette_btn("IF_EQ", BlockKind::IfEq);
            add_palette_btn("IF_NEQ", BlockKind::IfNeq);

            Gtk::Button *button_create_xtemplate =
                Gtk::make_managed<Gtk::Button>();
            vbox_new_xtemplate_form.pack_start(*button_create_xtemplate,
                                               Gtk::PACK_SHRINK);
            button_create_xtemplate->set_label("Create");
            button_create_xtemplate->set_sensitive(false);

            auto refresh_create_sensitive = [button_create_xtemplate,
                                             entry_new_name, block_canvas]() {
                button_create_xtemplate->set_sensitive(
                    !entry_new_name->get_text().empty() &&
                    !block_canvas->empty());
            };
            entry_new_name->signal_changed().connect(refresh_create_sensitive);
            block_canvas->signal_changed().connect(refresh_create_sensitive);

            button_create_xtemplate->signal_clicked().connect([this,
                                                               grid_new_vars,
                                                               entry_new_name,
                                                               entry_new_tags,
                                                               block_canvas]() {
                std::vector<std::string> vars_types, vars_names;
                std::vector<std::vector<std::string>> vars_tags;
                int row = 0;
                while (grid_new_vars->get_child_at(1, row) &&
                       grid_new_vars->get_child_at(2, row)) {
                    Gtk::Widget *child_type =
                        grid_new_vars->get_child_at(1, row);
                    Gtk::Widget *child_name =
                        grid_new_vars->get_child_at(2, row);
                    if (child_type && child_name) {
                        Gtk::Entry *entry_type =
                            dynamic_cast<Gtk::Entry *>(child_type);
                        Gtk::Entry *entry_name =
                            dynamic_cast<Gtk::Entry *>(child_name);
                        if (entry_type && entry_name) {
                            if (entry_name->get_text().empty()) {
                                show_copyable_mess(
                                    Gtk::MESSAGE_ERROR,
                                    "Variable's name is required");
                                return;
                            }
                            std::string var_type = entry_type->get_text();
                            std::string var_name = entry_name->get_text();
                            std::vector<std::string> var_tags;
                            std::string tag_errors;
                            strip_trailing_var_tags(var_type, var_tags,
                                                    var_name, tag_errors);
                            if (!tag_errors.empty()) {
                                show_copyable_mess(Gtk::MESSAGE_WARNING,
                                                   tag_errors);
                            }
                            vars_types.push_back(var_type);
                            vars_names.push_back(var_name);
                            vars_tags.push_back(var_tags);
                        }
                    }
                    ++row;
                }
                std::vector<
                    std::vector<std::tuple<unsigned short, std::string, bool>>>
                    vars_dependent;
                std::string dep_errors;
                if (!finalize_vars_dependencies(vars_types, vars_names,
                                                vars_dependent, dep_errors)) {
                    vars_dependent.assign(vars_names.size(), {});
                }
                if (!dep_errors.empty()) {
                    show_copyable_mess(
                        Gtk::MESSAGE_WARNING,
                        "Depends-on issues (invalid links skipped):\n\n" +
                            dep_errors);
                }
                Gtk::TreeModel::Row new_row = *(liststore_xtemplates->append());
                new_row[xtemplate_cols.name] = entry_new_name->get_text();
                new_row[xtemplate_cols.vars_types] = vars_types;
                new_row[xtemplate_cols.vars_names] = vars_names;
                new_row[xtemplate_cols.vars_dependent] = vars_dependent;
                new_row[xtemplate_cols.vars_tags] = vars_tags;
                new_row[xtemplate_cols.tags] =
                    split_by_comma(entry_new_tags->get_text());
                std::string body = block_canvas->serialize();
                ParseDiagnostics body_diag;
                Node *root = Node::parse(body, &body_diag);
                Node::destroy(root);
                if (!body_diag.empty()) {
                    show_copyable_mess(
                        Gtk::MESSAGE_WARNING,
                        "Body parse issues (template still saved):\n\n" +
                            body_diag.join());
                }
                new_row[xtemplate_cols.body] = body;
                treeview_xtemplates.get_selection()->select(
                    filter_xtemplates->children().begin());
                no_changes = false;
                app_state = STATE_SEARCH;
                stack_main.set_visible_child(frame_xtemplates);
            });
            label_new_vars->set_margin_top(20);
            label_new_body->set_margin_top(20);
            vbox_new_xtemplate_form.show_all();
            stack_main.set_visible_child(vbox_new_xtemplate_form);
            button_new_var->clicked();
        }

        void on_delete_xtemplate_clicked() {
            if (app_state != STATE_SEARCH) {
                stack_main.set_visible_child(frame_xtemplates);
                app_state = STATE_SEARCH;
                return;
            }
            Gtk::TreeModel::iterator curr_xtemplate =
                treeview_xtemplates.get_selection()->get_selected();
            if (!curr_xtemplate)
                return;
            Gtk::MessageDialog dialog("Are u sure?", false,
                                      Gtk::MESSAGE_QUESTION,
                                      Gtk::BUTTONS_YES_NO, true);
            dialog.set_default_response(Gtk::RESPONSE_NO);
            if (dialog.run() == Gtk::RESPONSE_YES) {
                Gtk::TreeModel::iterator true_curr_xtemplate =
                    filter_xtemplates->convert_iter_to_child_iter(
                        *curr_xtemplate);
                liststore_xtemplates->erase(true_curr_xtemplate);
                if (liststore_xtemplates->children())
                    treeview_xtemplates.get_selection()->select(
                        filter_xtemplates->children().begin());
            }
        }

        void on_display_hardcoded_toggled() {
            display_hardcoded_xtemplates = !display_hardcoded_xtemplates;
            image_display_hardcoded_xtemplates.set(
                display_hardcoded_xtemplates
                    ? pixbuf_display_hardcoded_xtemplates
                    : pixbuf_display_hardcoded_xtemplates_disabled);
            filter_xtemplates->refilter();
        }

        void on_clear_filtering_clicked() {
            entry_search_tag.set_text("");
            if (!display_hardcoded_xtemplates)
                on_display_hardcoded_toggled();
            filter_xtemplates->refilter();
        }

        void on_xfiles_cache_list_changed() {
            std::string cached_selected =
                combobox_xfiles_cache_list.get_active_text();
            const std::vector<std::string> &cache_list = config.cache_list;
            if (const auto &last_path_idx = std::find(
                    cache_list.begin(), cache_list.end(), cached_selected);
                last_path_idx != cache_list.end()) {
                config.last_path_idx =
                    std::distance(cache_list.begin(), last_path_idx);
                config.active_path = cached_selected;
            } else
                return;
            liststore_xtemplates->clear();
            (void)parse_xtemplate_content_hardcoded(XTEMPLATE_CONTENT_HARDCODED,
                                                    liststore_xtemplates,
                                                    xtemplate_cols);
            std::string errors;
            (void)parse_xfile(config.active_path, liststore_xtemplates,
                              xtemplate_cols, errors);
            if (!errors.empty()) {
                Gtk::MessageDialog dialog(
                    "Error parsing xtemplates file:\n\n" + errors, false,
                    Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
                dialog.run();
            }
            label_xfile.set_text(config.active_path);

            errors.clear();
            int ret_update = update_config(config_path, config, errors);
            if (ret_update != 0) {
                Gtk::MessageDialog dialog(
                    "Error writing config file:\n\n" + errors, false,
                    Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
                dialog.run();
            }
        }

        bool filter_xtemplates_by_tag_or_hardcoded(
            const Gtk::TreeModel::const_iterator &iter) {
            if (!iter)
                return false;
            if (app_state != STATE_SEARCH)
                on_open_xtemplate_clicked();

            // Tag filtering
            const std::vector<std::string> &tags = (*iter)[xtemplate_cols.tags];
            Glib::ustring search_text = entry_search_tag.get_text();
            if (search_text.empty() && !display_hardcoded_xtemplates)
                return !iter->get_value(xtemplate_cols.is_hardcoded);
            auto regex = Glib::Regex::create(",\\s*");
            std::vector<Glib::ustring> searched_tags =
                regex->split(search_text);
            for (auto &search_tag : searched_tags) {
                Glib::ustring trimmed_tag = trim(search_tag);
                if (trimmed_tag.empty())
                    continue;
                bool found = false;
                for (const auto &tag : tags) {
                    if (Glib::ustring(tag).find(trimmed_tag) !=
                        Glib::ustring::npos) {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            }

            // Hardcoded filtering
            if (!display_hardcoded_xtemplates)
                return !iter->get_value(xtemplate_cols.is_hardcoded);

            return true;
        }

        void show_copyable_mess(Gtk::MessageType type,
                                const Glib::ustring &text) {
            Gtk::MessageDialog dlg(*this, text, false, type, Gtk::BUTTONS_OK,
                                   true);
            Gtk::Box *area = dlg.get_message_area();
            if (!area)
                return;
            const auto children = area->get_children();
            std::vector<Gtk::Widget *> pending(children.begin(),
                                               children.end());
            while (!pending.empty()) {
                Gtk::Widget *w = pending.back();
                pending.pop_back();
                if (auto *label = dynamic_cast<Gtk::Label *>(w)) {
                    label->set_selectable(true);
                    label->set_can_focus(true);
                    continue;
                }
                if (auto *box = dynamic_cast<Gtk::Box *>(w)) {
                    const auto children = box->get_children();
                    pending.insert(pending.end(), children.begin(),
                                   children.end());
                }
            }
            Gtk::Button *copy_btn = Gtk::manage(new Gtk::Button());
            copy_btn->set_relief(Gtk::RELIEF_NONE);
            copy_btn->set_focus_on_click(false);
            copy_btn->set_tooltip_text("Скопировать текст в буфер");
            auto *img = Gtk::manage(new Gtk::Image());
            img->set_from_icon_name("edit-copy", Gtk::ICON_SIZE_BUTTON);
            copy_btn->add(*img);
            auto *corner_box =
                Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));
            corner_box->set_halign(Gtk::ALIGN_CENTER);
            corner_box->pack_end(*copy_btn, false, false, 0);
            copy_btn->signal_clicked().connect([text]() {
                if (const Glib::RefPtr<Gtk::Clipboard> cb =
                        Gtk::Clipboard::get(GDK_SELECTION_CLIPBOARD))
                    cb->set_text(text);
            });
            area->pack_start(*corner_box, false, false, 0);
            area->reorder_child(*corner_box, 0);
            area->show_all();
            dlg.run();
        }

    private:
        void setup_gresources() {
            try {
                pixbuf_select_xfile = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/select-xtemplates-file.png");
                pixbuf_open_xtemplate = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/open-xtemplate.png");
                pixbuf_new_xtemplate = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/new-xtemplate.png");
                pixbuf_delete_xtemplate = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/delete-xtemplate.png");
                pixbuf_display_hardcoded_xtemplates =
                    Gdk::Pixbuf::create_from_resource(
                        "/org/icons/display-hardcoded-xtemplates.png");
                pixbuf_display_hardcoded_xtemplates_disabled =
                    Gdk::Pixbuf::create_from_resource(
                        "/org/icons/display-hardcoded-xtemplates-disabled.png");
                pixbuf_clear_filtering = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/clear-filtering.png");
                pixbuf_new_variable = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/new-variable.png");
                pixbuf_variable = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/variable.png");
                pixbuf_xcheckbox_variable = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/xcheckbox-variable.png");
                pixbuf_xvariant_variable = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/xvariant-variable.png");
                pixbuf_plaintext_variable = Gdk::Pixbuf::create_from_resource(
                    "/org/icons/plaintext-variable.png");
                pixbuf_delete =
                    Gdk::Pixbuf::create_from_resource("/org/icons/delete.png");
            } catch (const Glib::Error &ex) {
                std::cerr << "Error loading pixbufs: " << ex.what()
                          << std::endl;
                Glib::signal_idle().connect_once([this] { app->quit(); });
            }
        }

        void setup_ui() {
            auto css = Gtk::CssProvider::create();
            css->load_from_data(R"(
                * {
                    font-family: Sans;
                    font-size: 14px;
                }
                #white_background{
                    background-image: none;
                    background-color: white;
                }
                #darkgray {
                    color: darkgray;
                }
            )");
            auto screen = get_screen();
            Gtk::StyleContext::add_provider_for_screen(
                screen, css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

            add(vbox_main);
            vbox_main.set_hexpand(true);
            vbox_main.set_vexpand(true);
            set_size_request(800, 600);
            vbox_main.pack_start(label_xfile, Gtk::PACK_SHRINK);
            label_xfile.set_halign(Gtk::ALIGN_CENTER);
            vbox_main.pack_start(toolbar_main, Gtk::PACK_SHRINK);

            vbox_main.pack_start(stack_main, Gtk::PACK_EXPAND_WIDGET);
            stack_main.add(frame_xtemplates);
            stack_main.set_homogeneous(true);
            frame_xtemplates.add(scrolled_xtemplates);
            scrolled_xtemplates.add(treeview_xtemplates);
            treeview_xtemplates.set_headers_visible(false);
            treeview_xtemplates.append_column(treecolumn_xtemplate_name);
            treecolumn_xtemplate_name.pack_start(renderer_xtemplate_name, true);
            treecolumn_xtemplate_name.add_attribute(
                renderer_xtemplate_name.property_text(), xtemplate_cols.name);
            treecolumn_xtemplate_name.set_cell_data_func(
                renderer_xtemplate_name,
                [this](Gtk::CellRenderer *renderer,
                       const Gtk::TreeModel::iterator &iter) {
                    auto cell_text =
                        static_cast<Gtk::CellRendererText *>(renderer);
                    if (!cell_text)
                        return;

                    bool is_hardcoded = (*iter)[xtemplate_cols.is_hardcoded];
                    cell_text->property_foreground_rgba() =
                        is_hardcoded ? Gdk::RGBA("gray") : Gdk::RGBA("black");
                });

            scrolled_xtemplates.set_name("white_background");
            scrolled_xtemplates.set_hexpand(true);
            scrolled_xtemplates.set_vexpand(true);

            stack_main.add(vbox_xtemplate_form);
            textview_xtemplate_result.set_editable(false);
            stack_main.add(vbox_new_xtemplate_form);
            frame_xtemplates.show_all();
            stack_main.set_visible_child(frame_xtemplates);

            vbox_main.pack_start(hbox_bottom, false, true);
            hbox_bottom.pack_start(label_tag, false, true);
            label_tag.set_text("Tags: ");
            hbox_bottom.pack_start(label_tags, false, true);
            hbox_bottom.pack_start(entry_search_tag, true, true);
            entry_search_tag.set_placeholder_text(
                "Searched tags comma-separated");
            hbox_bottom.pack_start(button_search_tag, false, true);
            button_search_tag.set_label("Search");
            set_margin(vbox_main, 10, 10);
            set_margin(frame_xtemplates, 10, 0);
            set_margin(treeview_xtemplates, 10, 10);
            set_margin(vbox_xtemplate_form, 10, 10);
            set_margin(hbox_bottom, 10, 5);
        }

        void setup_toolbar() {
            toolbar_main.set_toolbar_style(Gtk::TOOLBAR_BOTH_HORIZ);
            toolbar_main.set_icon_size(Gtk::ICON_SIZE_SMALL_TOOLBAR);

            auto set_tool_icon = [](Gtk::ToolButton &btn, Gtk::Image &img,
                                    const Glib::RefPtr<Gdk::Pixbuf> &pb) {
                if (pb)
                    img.set(pb);
                btn.set_icon_widget(img);
                img.show();
            };

            toolbar_main.insert(toolbutton_select_xfile, -1);
            toolbutton_select_xfile.set_tooltip_text("Select xtemplates file");
            set_tool_icon(toolbutton_select_xfile, image_select_xfile,
                          pixbuf_select_xfile);

            toolbar_main.insert(toolitem_xfiles_cache_list, -1);
            toolitem_xfiles_cache_list.set_tooltip_text(
                "Select xfile from cache list");
            toolitem_xfiles_cache_list.add(combobox_xfiles_cache_list);

            toolbar_main.insert(separator1, -1);

            toolbar_main.insert(toolbutton_open_xtemplate, -1);
            toolbutton_open_xtemplate.set_tooltip_text("Open xtemplate");
            set_tool_icon(toolbutton_open_xtemplate, image_open_xtemplate,
                          pixbuf_open_xtemplate);

            toolbar_main.insert(toolbutton_new_xtemplate, -1);
            toolbutton_new_xtemplate.set_tooltip_text("New xtemplate");
            set_tool_icon(toolbutton_new_xtemplate, image_new_xtemplate,
                          pixbuf_new_xtemplate);

            toolbar_main.insert(toolbutton_delete_xtemplate, -1);
            toolbutton_delete_xtemplate.set_tooltip_text("Delete xtemplate");
            set_tool_icon(toolbutton_delete_xtemplate, image_delete_xtemplate,
                          pixbuf_delete_xtemplate);
            toolbar_main.insert(separator2, -1);

            toolbar_main.insert(toolbutton_display_hardcoded_xtemplates, -1);
            toolbutton_display_hardcoded_xtemplates.set_tooltip_text(
                "Display hardcoded xtemplates");
            set_tool_icon(toolbutton_display_hardcoded_xtemplates,
                          image_display_hardcoded_xtemplates,
                          pixbuf_display_hardcoded_xtemplates);

            toolbar_main.insert(toolbutton_clear_filtering, -1);
            toolbutton_clear_filtering.set_tooltip_text("Clear filtering");
            set_tool_icon(toolbutton_clear_filtering, image_clear_filtering,
                          pixbuf_clear_filtering);
        }

        void setup_signals() {
            signal_delete_event().connect([this](GdkEventAny *event) {
                (void)event;
                bool ret = on_exit_clicked();
                return ret;
            });
            treeview_xtemplates.get_selection()->signal_changed().connect(
                sigc::mem_fun(
                    *this, &XGtkmm3Template::on_xtemplate_selection_changed));
            treeview_xtemplates.signal_row_activated().connect(
                [this](const Gtk::TreePath &path, Gtk::TreeViewColumn *column) {
                    (void)path;
                    (void)column;
                    on_open_xtemplate_clicked();
                });
            button_search_tag.signal_clicked().connect(
                [this]() { filter_xtemplates->refilter(); });
            toolbutton_select_xfile.signal_clicked().connect(
                [this]() { on_select_xfile_clicked(); });
            toolbutton_open_xtemplate.signal_clicked().connect(
                [this]() { on_open_xtemplate_clicked(); });
            toolbutton_new_xtemplate.signal_clicked().connect(
                [this]() { on_new_xtemplate_clicked(); });
            toolbutton_delete_xtemplate.signal_clicked().connect(
                [this]() { on_delete_xtemplate_clicked(); });
            toolbutton_display_hardcoded_xtemplates.signal_clicked().connect(
                [this]() { on_display_hardcoded_toggled(); });
            toolbutton_clear_filtering.signal_clicked().connect(
                [this]() { on_clear_filtering_clicked(); });
            combobox_xfiles_cache_list.signal_changed().connect(
                [this]() { on_xfiles_cache_list_changed(); });
        }

        void setup_data() {
            liststore_xtemplates = Gtk::ListStore::create(xtemplate_cols);
            filter_xtemplates =
                Gtk::TreeModelFilter::create(liststore_xtemplates);
            filter_xtemplates->set_visible_func(sigc::mem_fun(
                *this,
                &XGtkmm3Template::filter_xtemplates_by_tag_or_hardcoded));
            treeview_xtemplates.set_model(filter_xtemplates);

            std::string errors;
            (void)parse_and_apply_config(config_path, config, errors);
            if (!errors.empty()) {
                Gtk::MessageDialog dialog(
                    "Error parsing \"" + config_path + "\":\n\n" + errors,
                    false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
                dialog.run();
            }
            const std::vector<std::string> &cache_list = config.cache_list;
            for (const auto &cached : cache_list)
                combobox_xfiles_cache_list.append(cached);
            combobox_xfiles_cache_list.set_active(config.last_path_idx);

            if (!filter_xtemplates->children().empty())
                treeview_xtemplates.get_selection()->select(
                    filter_xtemplates->children().begin());
            on_display_hardcoded_toggled();
        }

        Gtk::Box vbox_main{Gtk::ORIENTATION_VERTICAL, 10};
        Gtk::Toolbar toolbar_main;
        Gtk::ToolButton toolbutton_select_xfile, toolbutton_open_xtemplate,
            toolbutton_new_xtemplate, toolbutton_delete_xtemplate,
            toolbutton_display_hardcoded_xtemplates, toolbutton_clear_filtering;
        Gtk::ToolItem toolitem_xfiles_cache_list;
        Gtk::ComboBoxText combobox_xfiles_cache_list;
        Gtk::Stack stack_main;
        Gtk::Frame frame_xtemplates;
        Gtk::ScrolledWindow scrolled_xtemplates;
        Gtk::TreeView treeview_xtemplates;
        Gtk::TreeViewColumn treecolumn_xtemplate_name;
        Gtk::CellRendererText renderer_xtemplate_name;
        Gtk::Box vbox_xtemplate_form{Gtk::ORIENTATION_VERTICAL, 10};
        Gtk::Box vbox_new_xtemplate_form{Gtk::ORIENTATION_VERTICAL, 10};
        Gtk::TextView textview_xtemplate_result;
        Gtk::Box hbox_bottom{Gtk::ORIENTATION_HORIZONTAL, 10};
        Gtk::Button button_search_tag;
        Gtk::Entry entry_search_tag;
        Gtk::Label label_tag;
        Gtk::Label label_tags;
        Gtk::Label label_xfile;
        int app_state = STATE_SEARCH;
        std::string config_path;
        XTemplateConfig config;
        Glib::RefPtr<Gdk::Pixbuf> pixbuf_select_xfile, pixbuf_open_xtemplate,
            pixbuf_new_xtemplate, pixbuf_delete_xtemplate,
            pixbuf_display_hardcoded_xtemplates,
            pixbuf_display_hardcoded_xtemplates_disabled,
            pixbuf_clear_filtering, pixbuf_new_variable, pixbuf_variable,
            pixbuf_xcheckbox_variable, pixbuf_xvariant_variable,
            pixbuf_plaintext_variable, pixbuf_delete;
        Gtk::Image image_select_xfile, image_open_xtemplate,
            image_new_xtemplate, image_delete_xtemplate,
            image_display_hardcoded_xtemplates, image_clear_filtering,
            image_new_variable, image_delete;
        Gtk::SeparatorToolItem separator1, separator2;
        Glib::RefPtr<Gio::Application> app;
        bool no_changes = true;
        bool display_hardcoded_xtemplates = false;

        Glib::RefPtr<Gtk::TreeModelFilter> filter_xtemplates;
        Glib::RefPtr<Gtk::ListStore> liststore_xtemplates;
        XTemplateCols xtemplate_cols;
};

int run(int argc, char *argv[]) {
    auto app =
        Gtk::Application::create(argc, argv, "github.x-gtkmm3-xtemplate");
    XGtkmm3Template window(app);
    return app->run(window);
}
