#include "luigi.h"
#include <X11/keysym.h>
#include <iostream>
#include <nav.h>

UIWindow *window;

UISplitPane *split_leftright;
UICode *code_notes;
Dest *note_dest_shown;
UILabel *label_typed_dest;

std::vector<Dest> dests;
Settings settings;
std::string typed_dest = "Type dest: ";

static char *alloc_format(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int size = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (size < 0) {
        va_end(copy);
        return NULL;
    }

    char *result = (char *)malloc((size_t)size + 1);
    if (result != NULL)
        vsnprintf(result, (size_t)size + 1, fmt, copy);
    va_end(copy);
    return result;
}

char status_to_symbol(const std::string &status) {
    for (const auto status_to_symbol_map : settings.status_to_symbol_map) {
        if (status_to_symbol_map.first == status)
            return status_to_symbol_map.second;
    }
    return '?';
}

static void apply_label_formatting(UILabel *label, unsigned char formatting) {
    unsigned char foreground = formatting & 0x0F;
    unsigned char background = (formatting & 0xF0) >> 4;
    label->colorText = palette[foreground];
    label->colorBackground = palette[background];
}

int on_key_pressed(UIElement *element, UIMessage message, int di, void *dp) {
    (void)element;
    (void)di;
    (void)dp;
    if (message != UI_MSG_KEY_TYPED || !dp)
        return 0;
    UIKeyTyped *m = (UIKeyTyped *)dp;
    if (m->code == UI_KEYCODE_BACKSPACE || m->code == UI_KEYCODE_DELETE) {
        if (typed_dest.size() > 11)
            typed_dest.pop_back();
    } else if (m->code == UI_KEYCODE_ENTER) {
        Dest dest;
        bool found = find_dest(dests, typed_dest.substr(11), dest);
        if (!found)
            return 0;
        unsigned char on_click = settings.on_click;
        std::cout << dest.dest_name;
        std::exit(100 + on_click);
    } else if (m->code == UI_KEYCODE_ESCAPE) {
        std::exit(0);
    } else {
        int code = m->code;
        if (!(code == XK_Control_L || code == XK_Control_R ||
              code == XK_Alt_L || code == XK_Alt_R || code == XK_Shift_L ||
              code == XK_Shift_R || code == XK_Left || code == XK_Right ||
              code == XK_Up || code == XK_Down || code == XK_Home ||
              code == XK_End || code == XK_Page_Up || code == XK_Page_Down ||
              code == XK_Insert || code == XK_Delete || code == XK_Tab ||
              code == XK_Return || code == XK_KP_Enter))
            typed_dest += code;
    }
    Dest dest;
    bool found = find_dest(dests, typed_dest.substr(11), dest);
    if (found) {
        note_dest_shown = nullptr;
        std::string note;
        append_dest_note(&dest, note, false, settings.fields_priority);
        UICodeInsertContent(code_notes, note.c_str(), -1, true);
    }
    UILabelSetContent(label_typed_dest, typed_dest.c_str(), -1);
    return 1;
}

int on_dest_triggered(UIElement *element, UIMessage message, int di, void *dp) {
    (void)dp;
    Dest *dest = (Dest *)element->cp;
    if (!dest)
        return 0;
    if (message == UI_MSG_CLICKED) {
        unsigned char on_click = settings.on_click;
        std::cout << dest->dest_name;
        std::exit(100 + on_click);
    }
    if (message == UI_MSG_UPDATE && di == UI_UPDATE_HOVERED &&
        element->window->hovered == element) {
        if (dest == note_dest_shown)
            return 0;
        note_dest_shown = dest;
        std::string note;
        append_dest_note(dest, note, false, settings.fields_priority);
        UICodeInsertContent(code_notes, note.c_str(), -1, true);
        return 0;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        std::cout
            << "Usage: navapp <command> <arg>\n\n"
               "Commands:\n"
               "  list                lists dests in interactive "
               "mode, on dest click performs 'go <dest>' logic\n"
               "  list <group>        lists dests of the group "
               "in interactive mode\n"
               "  list-filter <cond>  lists dests satisfying "
               "condition in interactive mode\n"
               "  get <dest>          prints the path of the dest\n"
               "  get-filter <cond>   prints dest_name of the first"
               "dest satisfying condition\n"
               "  query <query>       allows to query the navdict into stdout\n"
               "  brief-get <dest>    prints the brief of the "
               "dest\n"
               "  note-get <dest>     prints the note of the dest\n"
               "  get-startup         prints the startup command from config\n"
            << std::endl;
        return 0;
    }
    const std::string command = argv[1];
    std::string errors;
    char *home = std::getenv("HOME");
    if (!home) {
        std::cerr << "$HOME not set\n";
        return 1;
    }
    if (command == "query") {
        std::cout << "not implemented yet" << std::endl;
        return 1;
    }
    if (command != "list") {
        if (command == "dests-list") {
#ifdef DEBUG
            std::cerr << "dests-list can't be used with debug mode\n";
#endif
            parse_navdict(std::string(home) + "/.config/navdict.ini", dests,
                          settings, errors, NULL, M_NONE);
            if (!errors.empty()) {
                std::cerr << "Parse errors:\n" << errors << std::endl;
                return 1;
            }
            for (const auto &dest : dests) {
                if (dest.task_status.empty()) {
                    printf(" -  %s\n", dest.dest_name.c_str());
                } else {
                    printf("[%c] %s\n", status_to_symbol(dest.task_status),
                           dest.dest_name.c_str());
                }
            }
            return 0;
        }
        if (command == "get-startup") {
            parse_navdict(std::string(home) + "/.config/navdict.ini", dests,
                          settings, errors, NULL, M_STARTUP);
            if (!errors.empty()) {
                std::cerr << "Parse errors:\n" << errors << std::endl;
                return 1;
            }
            std::string on_startup = settings.on_startup;
            if (on_startup.empty()) {
                return 1;
            } else {
                std::cout << on_startup;
                return 0;
            }
        }
        if (argc == 2) {
            std::cerr << "Incorrect args count\n";
            return 1;
        }
        if (command == "get-filter") {
            parse_navdict(std::string(home) + "/.config/navdict.ini", dests,
                          settings, errors, NULL, M_FIRST);
            if (!errors.empty()) {
                std::cerr << "Parse errors:\n" << errors << std::endl;
                return 1;
            }
            Dest dest = dests[0];
            std::string field;
            int ret = go(dest, field, settings.fields_priority);
            std::cout << field;
            return ret;
        }

        if (command != "list-filter") {
            const char *filter = alloc_format("dest = '%s'", argv[2]);
            parse_navdict(std::string(home) + "/.config/navdict.ini", dests,
                          settings, errors, filter, M_FIRST);
            if (!errors.empty()) {
                std::cerr << "Parse errors:\n" << errors << std::endl;
                return 1;
            }
            Dest dest = dests[0];
            if (command == "get") {
                std::string field;
                int ret = go(dest, field, settings.fields_priority);
                std::cout << field;
                return ret;
            } else if (command == "brief-get") {
                if (!dest.brief.empty()) {
                    std::cout << dest.brief;
                    return 0;
                }
                std::cerr << "Dest " << argv[2] << " has no brief" << std::endl;
                return 1;
            } else if (command == "note-get") {
                if (!dest.note_path.empty()) {
                    std::cout << dest.note_path;
                    return 0;
                }
                std::cerr << "Dest " << argv[2] << " has no note" << std::endl;
                return 1;
            } else {
                std::cerr << "Unknown command: \"" << command << "\""
                          << std::endl;
                return 1;
            }
        }
    }

    const char *filter = NULL;
    if (argc == 3) {
        if (command == "list-filter")
            filter = alloc_format("%s", argv[2]);
        else
            filter = alloc_format("group_name = '%s'", argv[2]);
    }
    parse_navdict(std::string(home) + "/.config/navdict.ini", dests, settings,
                  errors, filter, M_NONE);
    if (!errors.empty()) {
        std::cerr << "Parse errors:\n" << errors << std::endl;
        return 1;
    }
    UIInitialise();
    window = UIWindowCreate(0, 0, "Nav", 0, 0);
    window->e.messageUser = on_key_pressed;
    split_leftright = UISplitPaneCreate(&window->e, 0, 0.33f);
    split_leftright->margin = UI_RECT_1(8);
    UIPanel *panel_dests = UIPanelCreate(
        &split_leftright->e, UI_PANEL_COLOR_2 | UI_PANEL_SCROLL |
                                 UI_PANEL_SMALL_SPACING | UI_ELEMENT_H_FILL);
    code_notes = UICodeCreate(&split_leftright->e, 0);
    note_dest_shown = nullptr;
    if (!errors.empty()) {
        UIDialogShow(window, 0, "Parse errors:\n%s\n\n%b", errors.c_str(),
                     "OK");
    }
    label_typed_dest =
        UILabelCreate(&panel_dests->e, UI_ELEMENT_BORDER, "Type dest: ", -1);
    UISpacerCreate(&panel_dests->e, 0, 0, 10);
    std::string last_group_name;
    for (Dest &dest : dests) {
        if (last_group_name != dest.group_name) {
            UISpacerCreate(&panel_dests->e, 0, 0, 5);
            last_group_name = dest.group_name;
            std::string group_name_displayed =
                std::string("[") + last_group_name + "]";
            UILabel *group_label =
                UILabelCreate(&panel_dests->e, UI_ELEMENT_H_FILL,
                              group_name_displayed.c_str(), -1);
            group_label->e.cp = &last_group_name;
            UISpacerCreate(&panel_dests->e, 0, 0, 5);
        }
        char *dest_str = NULL;
        if (dest.task_status.empty()) {
            dest_str = alloc_format("   -  %s", dest.dest_name.c_str());
        } else {
            dest_str =
                alloc_format("  [%c] %s", status_to_symbol(dest.task_status),
                             dest.dest_name.c_str());
        }
        UILabel *dest_label =
            UILabelCreate(&panel_dests->e, UI_ELEMENT_H_FILL, dest_str, -1);
        apply_label_formatting(dest_label, dest.formatting);
        dest_label->e.messageUser = on_dest_triggered;
        dest_label->e.cp = &dest;
    }

    return UIMessageLoop();
}
