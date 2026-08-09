#include "xtemplate/block_canvas.hpp"

#include <iostream>

namespace {
constexpr double kPad = 16.0;
constexpr double kGap = 8.0;
constexpr double kIndent = 20.0;
constexpr double kMinW = 280.0;
constexpr double kHeaderH = 30.0;
constexpr double kEmptyBodyH = 28.0;
constexpr double kFooterH = 10.0;
constexpr double kSnapDist = 28.0;
constexpr double kCorner = 6.0;
constexpr double kDragThreshold = 6.0;

std::string trim_copy(const std::string &s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}
} // namespace

BlockCanvas::BlockCanvas() {
    add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
               Gdk::POINTER_MOTION_MASK | Gdk::KEY_PRESS_MASK |
               Gdk::SCROLL_MASK);
    set_can_focus(true);
    set_size_request(400, 240);
}

void BlockCanvas::add_block(BlockKind kind) {
    auto b = std::make_unique<Block>();
    b->kind = kind;
    if (kind == BlockKind::Text)
        b->text = "// text (use $var_name)";
    else {
        b->arg1 = "var";
        if (kind == BlockKind::IfEq || kind == BlockKind::IfNeq)
            b->arg2 = "value";
    }

    if (m_selected && m_selected->is_cond()) {
        m_selected->children.push_back(std::move(b));
        m_selected = m_selected->children.back().get();
    } else {
        m_roots.push_back(std::move(b));
        m_selected = m_roots.back().get();
    }
    relayout();
    queue_draw();
    notify_changed();
    grab_focus();
}

void BlockCanvas::clear() {
    m_roots.clear();
    m_selected = nullptr;
    m_dragging = nullptr;
    m_press_target = nullptr;
    m_drag_owned.reset();
    m_snap = {};
    relayout();
    queue_draw();
    notify_changed();
}

bool BlockCanvas::empty() const { return m_roots.empty(); }

std::string BlockCanvas::serialize() const {
    std::string out;
    serialize_list(m_roots, out, 0);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

void BlockCanvas::serialize_list(
    const std::vector<std::unique_ptr<Block>> &list, std::string &out,
    int indent) {
    const std::string pad(indent, ' ');
    for (const auto &b : list) {
        if (!b)
            continue;
        if (b->kind == BlockKind::Text) {
            std::istringstream ss(b->text);
            std::string line;
            bool any = false;
            while (std::getline(ss, line)) {
                out += line + "\n";
                any = true;
            }
            if (!any)
                out += "\n";
        } else {
            out += pad + "##";
            switch (b->kind) {
            case BlockKind::IfOn:
                out += "IF_ON " + b->arg1;
                break;
            case BlockKind::IfOff:
                out += "IF_OFF " + b->arg1;
                break;
            case BlockKind::IfEq:
                out += "IF_EQ " + b->arg1 + " " + b->arg2;
                break;
            case BlockKind::IfNeq:
                out += "IF_NEQ " + b->arg1 + " " + b->arg2;
                break;
            default:
                break;
            }
            out += "\n";
            serialize_list(b->children, out, indent + 2);
            out += pad + "##END\n";
        }
    }
}

const char *BlockCanvas::kind_label(BlockKind kind) {
    switch (kind) {
    case BlockKind::Text:
        return "TEXT";
    case BlockKind::IfOn:
        return "IF_ON";
    case BlockKind::IfOff:
        return "IF_OFF";
    case BlockKind::IfEq:
        return "IF_EQ";
    case BlockKind::IfNeq:
        return "IF_NEQ";
    }
    return "?";
}

void BlockCanvas::color_for(BlockKind kind, double &r, double &g, double &b) {
    switch (kind) {
    case BlockKind::Text:
        r = 0.35;
        g = 0.55;
        b = 0.85;
        break;
    case BlockKind::IfOn:
    case BlockKind::IfOff:
        r = 0.85;
        g = 0.55;
        b = 0.20;
        break;
    case BlockKind::IfEq:
    case BlockKind::IfNeq:
        r = 0.75;
        g = 0.35;
        b = 0.55;
        break;
    }
}

void BlockCanvas::notify_changed() { m_signal_changed.emit(); }

void BlockCanvas::relayout() {
    double max_w =
        std::max(kMinW, static_cast<double>(get_allocated_width()) - 2 * kPad);
    double h = layout_list(m_roots, kPad, kPad, max_w);
    int need_w = static_cast<int>(2 * kPad + max_w);
    int need_h = static_cast<int>(h + 2 * kPad);
    set_size_request(std::max(need_w, 400), std::max(need_h, 240));
}

double BlockCanvas::layout_list(std::vector<std::unique_ptr<Block>> &list,
                                double x, double y, double max_w) {
    double cy = y;
    for (auto &b : list) {
        if (!b)
            continue;
        cy += layout_block(*b, x, cy, max_w) + kGap;
    }
    if (!list.empty())
        cy -= kGap;
    return std::max(0.0, cy - y);
}

double BlockCanvas::layout_block(Block &b, double x, double y, double max_w) {
    b.x = x;
    b.y = y;
    b.w = max_w;
    b.header_h = kHeaderH;

    if (b.kind == BlockKind::Text) {
        // Extra height for multi-line preview (cap)
        int lines = 1;
        for (char c : b.text)
            if (c == '\n')
                ++lines;
        lines = std::min(lines, 6);
        b.h = kHeaderH + std::max(0, lines - 1) * 14.0 + 8.0;
        b.body_h = 0;
        return b.h;
    }

    b.body_x = x + kIndent;
    b.body_y = y + kHeaderH;
    b.body_w = max_w - kIndent;
    if (b.children.empty()) {
        b.body_h = kEmptyBodyH;
    } else {
        b.body_h = layout_list(b.children, b.body_x, b.body_y, b.body_w);
    }
    b.h = kHeaderH + b.body_h + kFooterH;
    return b.h;
}

Block *BlockCanvas::hit_test(double x, double y,
                             std::vector<std::unique_ptr<Block>> &list) const {
    // Top-most: reverse order
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (!*it)
            continue;
        if (Block *hit = hit_test_block(**it, x, y))
            return hit;
    }
    return nullptr;
}

Block *BlockCanvas::hit_test_block(Block &b, double x, double y) const {
    if (x < b.x || x > b.x + b.w || y < b.y || y > b.y + b.h)
        return nullptr;
    if (b.is_cond()) {
        if (Block *child = hit_test(x, y, b.children))
            return child;
    }
    return &b;
}

bool BlockCanvas::detach(Block *target, std::unique_ptr<Block> &out) {
    return detach_from_list(m_roots, target, out);
}

bool BlockCanvas::detach_from_list(std::vector<std::unique_ptr<Block>> &list,
                                   Block *target, std::unique_ptr<Block> &out) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].get() == target) {
            out = std::move(list[i]);
            list.erase(list.begin() + static_cast<long>(i));
            return true;
        }
        if (list[i] && detach_from_list(list[i]->children, target, out))
            return true;
    }
    return false;
}

void BlockCanvas::apply_snap(std::unique_ptr<Block> block) {
    if (!block)
        return;
    Block *raw = block.get();
    if (m_snap.kind == SnapKind::None || !m_snap.anchor) {
        m_roots.push_back(std::move(block));
        m_selected = raw;
        return;
    }

    auto insert_relative = [&](std::vector<std::unique_ptr<Block>> &list,
                               Block *anchor, bool before) {
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].get() == anchor) {
                size_t at = before ? i : i + 1;
                list.insert(list.begin() + static_cast<long>(at),
                            std::move(block));
                return true;
            }
        }
        return false;
    };

    // Find parent list containing anchor
    std::function<bool(std::vector<std::unique_ptr<Block>> &)> place;
    place = [&](std::vector<std::unique_ptr<Block>> &list) -> bool {
        if (m_snap.kind == SnapKind::IntoBody) {
            for (auto &b : list) {
                if (b.get() == m_snap.anchor) {
                    b->children.push_back(std::move(block));
                    return true;
                }
                if (b && place(b->children))
                    return true;
            }
            return false;
        }
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].get() == m_snap.anchor) {
                return insert_relative(list, m_snap.anchor,
                                       m_snap.kind == SnapKind::Before);
            }
        }
        for (auto &b : list) {
            if (b && place(b->children))
                return true;
        }
        return false;
    };

    if (!place(m_roots))
        m_roots.push_back(std::move(block));
    m_selected = raw;
}

SnapTarget BlockCanvas::find_snap(double x, double y, Block *exclude) const {
    SnapTarget best;
    double best_dist = kSnapDist;
    find_snap_in_list(m_roots, x, y, exclude, best, best_dist);

    // Empty canvas: snap as first root
    if (m_roots.empty() && !exclude) {
        best.kind = SnapKind::None;
        return best;
    }

    // Also allow dropping at end of root list
    if (!m_roots.empty()) {
        const Block &last = *m_roots.back();
        double end_y = last.y + last.h + kGap / 2;
        double d = std::abs(y - end_y);
        if (d < best_dist && x >= last.x - 10 && x <= last.x + last.w + 10) {
            best.kind = SnapKind::After;
            best.anchor = m_roots.back().get();
            best.line_y = last.y + last.h + 2;
            best.line_x = last.x;
            best.line_w = last.w;
            best_dist = d;
        }
    }
    return best;
}

void BlockCanvas::find_snap_in_list(
    const std::vector<std::unique_ptr<Block>> &list, double x, double y,
    Block *exclude, SnapTarget &best, double &best_dist) const {
    for (const auto &b : list) {
        if (!b || b.get() == exclude)
            continue;
        find_snap_in_block(*b, x, y, exclude, best, best_dist);
    }
}

void BlockCanvas::find_snap_in_block(const Block &b, double x, double y,
                                     Block *exclude, SnapTarget &best,
                                     double &best_dist) const {
    if (&b == exclude)
        return;

    auto consider = [&](SnapKind kind, Block *anchor, double ly, double lx,
                        double lw, double dist) {
        if (dist < best_dist) {
            best_dist = dist;
            best.kind = kind;
            best.anchor = anchor;
            best.line_y = ly;
            best.line_x = lx;
            best.line_w = lw;
        }
    };

    // Before this block
    if (x >= b.x - 10 && x <= b.x + b.w + 10) {
        consider(SnapKind::Before, const_cast<Block *>(&b), b.y - 2, b.x, b.w,
                 std::abs(y - b.y));
        consider(SnapKind::After, const_cast<Block *>(&b), b.y + b.h + 2, b.x,
                 b.w, std::abs(y - (b.y + b.h)));
    }

    if (b.is_cond()) {
        // Into empty / body top
        double socket_y = b.body_y + (b.children.empty() ? b.body_h / 2 : 0);
        if (x >= b.body_x - 10 && x <= b.body_x + b.body_w + 10) {
            consider(SnapKind::IntoBody, const_cast<Block *>(&b),
                     b.children.empty() ? b.body_y + b.body_h / 2 : b.body_y,
                     b.body_x, b.body_w, std::abs(y - socket_y));
        }
        find_snap_in_list(b.children, x, y, exclude, best, best_dist);
    }
}

void BlockCanvas::draw_list(
    const Cairo::RefPtr<Cairo::Context> &cr,
    const std::vector<std::unique_ptr<Block>> &list) const {
    for (const auto &b : list) {
        if (b)
            draw_block(cr, *b);
    }
}

void BlockCanvas::draw_block(const Cairo::RefPtr<Cairo::Context> &cr,
                             const Block &b) const {
    double r, g, bl;
    color_for(b.kind, r, g, bl);
    const bool selected = (&b == m_selected);

    auto round_rect = [&](double x, double y, double w, double h) {
        cr->begin_new_path();
        cr->arc(x + w - kCorner, y + kCorner, kCorner, -M_PI / 2, 0);
        cr->arc(x + w - kCorner, y + h - kCorner, kCorner, 0, M_PI / 2);
        cr->arc(x + kCorner, y + h - kCorner, kCorner, M_PI / 2, M_PI);
        cr->arc(x + kCorner, y + kCorner, kCorner, M_PI, 3 * M_PI / 2);
        cr->close_path();
    };

    if (b.kind == BlockKind::Text) {
        round_rect(b.x, b.y, b.w, b.h);
        cr->set_source_rgb(r, g, bl);
        cr->fill_preserve();
        if (selected) {
            cr->set_source_rgb(0.1, 0.1, 0.1);
            cr->set_line_width(2.0);
        } else {
            cr->set_source_rgb(r * 0.6, g * 0.6, bl * 0.6);
            cr->set_line_width(1.0);
        }
        cr->stroke();

        cr->set_source_rgb(1, 1, 1);
        cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                             Cairo::FONT_WEIGHT_BOLD);
        cr->set_font_size(12);
        cr->move_to(b.x + 10, b.y + 18);
        cr->show_text(kind_label(b.kind));

        cr->set_font_size(11);
        cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                             Cairo::FONT_WEIGHT_NORMAL);
        double ty = b.y + 34;
        std::istringstream ss(b.text);
        std::string line;
        int shown = 0;
        while (std::getline(ss, line) && shown < 5) {
            if (line.size() > 48)
                line = line.substr(0, 45) + "...";
            cr->move_to(b.x + 10, ty);
            cr->show_text(line);
            ty += 14;
            ++shown;
        }
        return;
    }

    // Condition C-shape: header bar + left rail + footer
    cr->set_source_rgb(r, g, bl);
    // header
    round_rect(b.x, b.y, b.w, b.header_h);
    cr->fill();
    // left rail
    cr->rectangle(b.x, b.y + b.header_h - 1, kIndent - 4, b.body_h + 2);
    cr->fill();
    // footer
    round_rect(b.x, b.y + b.h - kFooterH, b.w, kFooterH);
    cr->fill();

    // empty body well
    cr->set_source_rgb(0.92, 0.92, 0.94);
    cr->rectangle(b.body_x, b.body_y, b.body_w, b.body_h);
    cr->fill();

    if (selected) {
        cr->set_source_rgb(0.1, 0.1, 0.1);
        cr->set_line_width(2.0);
        round_rect(b.x, b.y, b.w, b.h);
        cr->stroke();
    }

    cr->set_source_rgb(1, 1, 1);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_BOLD);
    cr->set_font_size(12);
    std::string label = std::string(kind_label(b.kind)) + " " + b.arg1;
    if (b.kind == BlockKind::IfEq || b.kind == BlockKind::IfNeq)
        label += " " + b.arg2;
    if (label.size() > 50)
        label = label.substr(0, 47) + "...";
    cr->move_to(b.x + 10, b.y + 20);
    cr->show_text(label);

    if (b.children.empty()) {
        cr->set_source_rgb(0.55, 0.55, 0.6);
        cr->set_font_size(11);
        cr->move_to(b.body_x + 8, b.body_y + b.body_h / 2 + 4);
        cr->show_text("drop blocks here");
    } else {
        draw_list(cr, b.children);
    }
}

void BlockCanvas::draw_snap(const Cairo::RefPtr<Cairo::Context> &cr) const {
    if (m_snap.kind == SnapKind::None || !m_snap.anchor)
        return;
    cr->set_source_rgb(0.2, 0.75, 0.35);
    cr->set_line_width(3.0);
    cr->move_to(m_snap.line_x, m_snap.line_y);
    cr->line_to(m_snap.line_x + m_snap.line_w, m_snap.line_y);
    cr->stroke();
}

bool BlockCanvas::on_draw(const Cairo::RefPtr<Cairo::Context> &cr) {
    const Gtk::Allocation alloc = get_allocation();
    cr->set_source_rgb(1, 1, 1);
    cr->paint();

    // subtle grid
    cr->set_source_rgb(0.93, 0.93, 0.95);
    cr->set_line_width(1.0);
    for (int x = 0; x < alloc.get_width(); x += 20) {
        cr->move_to(x + 0.5, 0);
        cr->line_to(x + 0.5, alloc.get_height());
    }
    for (int y = 0; y < alloc.get_height(); y += 20) {
        cr->move_to(0, y + 0.5);
        cr->line_to(alloc.get_width(), y + 0.5);
    }
    cr->stroke();

    relayout();
    draw_list(cr, m_roots);

    if (m_dragging && m_drag_owned) {
        draw_snap(cr);
        cr->save();
        cr->translate(m_pointer_x - m_drag_dx - m_drag_owned->x,
                      m_pointer_y - m_drag_dy - m_drag_owned->y);
        cr->push_group();
        draw_block(cr, *m_drag_owned);
        auto pattern = cr->pop_group();
        cr->set_source(pattern);
        cr->paint_with_alpha(0.75);
        cr->restore();
    } else {
        draw_snap(cr);
    }

    if (m_roots.empty() && !m_dragging) {
        cr->set_source_rgb(0.55, 0.55, 0.6);
        cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                             Cairo::FONT_WEIGHT_NORMAL);
        cr->set_font_size(14);
        cr->move_to(24, 40);
        cr->show_text("Add blocks from the palette. Drag to reorder / nest.");
        cr->move_to(24, 62);
        cr->show_text("Right-click to edit text or condition args.");
    }
    return true;
}

bool BlockCanvas::on_button_press_event(GdkEventButton *event) {
    grab_focus();
    m_pointer_x = event->x;
    m_pointer_y = event->y;

    // Right-click: edit without starting a drag / snap
    if (event->button == 3) {
        if (Block *hit = hit_test(event->x, event->y, m_roots)) {
            m_selected = hit;
            m_press_target = nullptr;
            edit_block(hit);
            queue_draw();
        }
        return true;
    }

    if (event->button == 1) {
        Block *hit = hit_test(event->x, event->y, m_roots);
        m_selected = hit;
        m_press_target = hit;
        m_press_x = event->x;
        m_press_y = event->y;
        if (hit) {
            m_drag_dx = event->x - hit->x;
            m_drag_dy = event->y - hit->y;
        }
        queue_draw();
        return true;
    }
    return false;
}

bool BlockCanvas::on_button_release_event(GdkEventButton *event) {
    if (event->button != 1)
        return false;

    m_pointer_x = event->x;
    m_pointer_y = event->y;

    if (m_dragging) {
        m_snap = find_snap(event->x, event->y, m_dragging);
        apply_snap(std::move(m_drag_owned));
        m_dragging = nullptr;
        m_snap = {};
        relayout();
        queue_draw();
        notify_changed();
    }
    // Click without drag threshold: selection only (no reposition)
    m_press_target = nullptr;
    return true;
}

bool BlockCanvas::on_motion_notify_event(GdkEventMotion *event) {
    m_pointer_x = event->x;
    m_pointer_y = event->y;

    // Start drag only after moving past threshold (avoids accidental snap on
    // click)
    if (!m_dragging && m_press_target && (event->state & Gdk::BUTTON1_MASK)) {
        const double dx = event->x - m_press_x;
        const double dy = event->y - m_press_y;
        if (dx * dx + dy * dy >= kDragThreshold * kDragThreshold) {
            if (detach(m_press_target, m_drag_owned)) {
                m_dragging = m_drag_owned.get();
                m_press_target = nullptr;
                m_snap = find_snap(event->x, event->y, m_dragging);
                relayout();
            } else {
                m_press_target = nullptr;
            }
        }
    }

    if (!m_dragging)
        return false;
    m_snap = find_snap(event->x, event->y, m_dragging);
    queue_draw();
    return true;
}

bool BlockCanvas::on_key_press_event(GdkEventKey *event) {
    if (!m_selected || m_dragging)
        return false;
    if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_BackSpace) {
        std::unique_ptr<Block> removed;
        if (detach(m_selected, removed)) {
            m_selected = nullptr;
            relayout();
            queue_draw();
            notify_changed();
        }
        return true;
    }
    return false;
}

void BlockCanvas::edit_block(Block *b) {
    if (!b)
        return;
    Gtk::Dialog dialog("Edit block", true);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("OK", Gtk::RESPONSE_OK);
    dialog.set_default_response(Gtk::RESPONSE_OK);

    auto *box = dialog.get_content_area();
    box->set_spacing(8);
    box->set_margin_top(10);
    box->set_margin_bottom(10);
    box->set_margin_left(10);
    box->set_margin_right(10);

    Gtk::Entry *entry_arg1 = nullptr;
    Gtk::Entry *entry_arg2 = nullptr;
    Gtk::TextView *text_view = nullptr;

    if (b->kind == BlockKind::Text) {
        auto *label = Gtk::manage(new Gtk::Label("Text (use $var_name)"));
        label->set_halign(Gtk::ALIGN_START);
        box->pack_start(*label, Gtk::PACK_SHRINK);
        auto *scroll = Gtk::manage(new Gtk::ScrolledWindow());
        scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        scroll->set_min_content_height(120);
        text_view = Gtk::manage(new Gtk::TextView());
        text_view->get_buffer()->set_text(b->text);
        scroll->add(*text_view);
        box->pack_start(*scroll, Gtk::PACK_EXPAND_WIDGET);
    } else {
        auto *l1 = Gtk::manage(
            new Gtk::Label("Variable name (no $ needed; $name also accepted)"));
        l1->set_halign(Gtk::ALIGN_START);
        box->pack_start(*l1, Gtk::PACK_SHRINK);
        entry_arg1 = Gtk::manage(new Gtk::Entry());
        entry_arg1->set_text(b->arg1);
        box->pack_start(*entry_arg1, Gtk::PACK_SHRINK);
        if (b->kind == BlockKind::IfEq || b->kind == BlockKind::IfNeq) {
            auto *l2 = Gtk::manage(new Gtk::Label("Compare value"));
            l2->set_halign(Gtk::ALIGN_START);
            box->pack_start(*l2, Gtk::PACK_SHRINK);
            entry_arg2 = Gtk::manage(new Gtk::Entry());
            entry_arg2->set_text(b->arg2);
            box->pack_start(*entry_arg2, Gtk::PACK_SHRINK);
        }
    }

    dialog.show_all();
    if (dialog.run() != Gtk::RESPONSE_OK)
        return;

    if (b->kind == BlockKind::Text && text_view) {
        b->text = text_view->get_buffer()->get_text();
    } else {
        if (entry_arg1) {
            std::string a1 = trim_copy(entry_arg1->get_text());
            if (!a1.empty() && a1[0] == '$')
                a1.erase(0, 1);
            b->arg1 = a1;
        }
        if (entry_arg2)
            b->arg2 = trim_copy(entry_arg2->get_text());
        if (b->arg1.empty())
            b->arg1 = "var";
    }
    relayout();
    queue_draw();
    notify_changed();
}
