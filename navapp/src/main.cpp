#include "luigi.h"
#include <X11/keysym.h>
#include <iostream>
#include <nav.h>

UIWindow *window;

UISplitPane *split_leftright;
UICode *code_notes;
Dest *note_dest_shown;
UILabel *label_typed_dest;

std::vector<DestGroup> dests_groups;
std::string typed_dest = "Type dest: ";

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
        bool found = find_dest(dests_groups, typed_dest.substr(11), dest);
        if (!found) {
            std::cerr << "Dest not found: \"" << typed_dest.substr(11) << "\"";
            std::exit(1);
        }
        if (!dest.path.empty()) {
            std::cout << dest.path << std::endl;
            std::exit(0);
        }
        std::cout << dest.note_ret << std::endl;
        std::exit(2);
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
    bool found = find_dest(dests_groups, typed_dest.substr(11), dest);
    if (found) {
        note_dest_shown = nullptr;
        std::string note;
        append_dest_note(&dest, note, false);
        UICodeInsertContent(code_notes, note.c_str(), -1, true);
    }
    UILabelSetContent(label_typed_dest, typed_dest.c_str(), -1);
    return 1;
}

int on_destgroup_triggered(UIElement *element, UIMessage message, int di,
                           void *dp) {
    (void)dp;
    DestGroup *destgroup = (DestGroup *)element->cp;
    if (!destgroup)
        return 0;
    if (message == UI_MSG_UPDATE && di == UI_UPDATE_HOVERED &&
        element->window->hovered == element) {
        note_dest_shown = nullptr;
        std::string note;
        append_destgroup_note(destgroup, note);
        UICodeInsertContent(code_notes, note.c_str(), -1, true);
        return 0;
    }
    return 0;
}

int on_dest_triggered(UIElement *element, UIMessage message, int di, void *dp) {
    (void)dp;
    Dest *dest = (Dest *)element->cp;
    if (!dest)
        return 0;
    if (message == UI_MSG_CLICKED) {
        std::string path = dest->path;
        if (path.empty()) {
            std::cout << dest->note_ret << std::endl;
            std::exit(2);
        }
        std::cout << path << std::endl;
        std::exit(0);
    }
    if (message == UI_MSG_UPDATE && di == UI_UPDATE_HOVERED &&
        element->window->hovered == element) {
        if (dest == note_dest_shown)
            return 0;
        note_dest_shown = dest;
        std::string note;
        append_dest_note(dest, note, false);
        UICodeInsertContent(code_notes, note.c_str(), -1, true);
        return 0;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        std::cout << "Usage: navapp <command> <arg>\n\n"
                     "commands:\n"
                     "  list          lists dests in interactive "
                     "mode\n"
                     "  list <group>  lists dests of the group in "
                     "interactive mode\n"
                     "  get <dest>    prints the path of the dest\n"
                  << std::endl;
        return 0;
    }
    const std::string command = argv[1];
    const std::string arg = argc > 2 && command != "get" ? argv[2] : "";

    std::string errors;
    parse_navdict("/home/snusoed/.config/navdict.ini", dests_groups, errors,
                  arg);
    if (command != "list") {
        if (!errors.empty()) {
            std::cerr << "Parse errors:\n" << errors << std::endl;
            return 1;
        }
        if (command == "get") {
            if (argc != 3) {
                std::cerr << "Usage: navapp get [DEST]" << std::endl;
                return 1;
            }
            Dest dest;
            bool found = find_dest(dests_groups, argv[2], dest);
            if (!found) {
                std::cerr << "Dest not found: " << argv[2] << std::endl;
                return 1;
            }
            if (!dest.path.empty()) {
                std::cout << dest.path << std::endl;
                return 0;
            }
            std::cout << dest.note_ret << std::endl;
            return 2;
        }
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
    for (auto &dest_group : dests_groups) {
        const std::string &group_name = dest_group.name;
        if (!arg.empty() && group_name != arg)
            continue;
        const unsigned char formatting = dest_group.formatting;
        std::string group_name_displayed = std::string("[") + group_name + "]";
        UILabel *group_label = UILabelCreate(&panel_dests->e, UI_ELEMENT_H_FILL,
                                             group_name_displayed.c_str(), -1);
        apply_label_formatting(group_label, formatting);
        group_label->e.messageUser = on_destgroup_triggered;
        group_label->e.cp = &dest_group;
        for (auto &dest : dest_group.dests) {
            Dest &destination = dest.second;
            UILabel *dest_label = UILabelCreate(
                &panel_dests->e, UI_ELEMENT_H_FILL,
                (std::string("    -  ") + dest.first).c_str(), -1);
            apply_label_formatting(dest_label, destination.formatting);
            dest_label->e.messageUser = on_dest_triggered;
            dest_label->e.cp = &destination;
        }
        if (errors.empty()) {
            UISpacerCreate(&panel_dests->e, 0, 0, 10);
        }
    }

    return UIMessageLoop();
}
