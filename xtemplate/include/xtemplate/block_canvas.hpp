#pragma once

#include <gtkmm.h>
#include <memory>
#include <string>
#include <vector>

enum class BlockKind { Text, IfOn, IfOff, IfEq, IfNeq };

struct Block {
        BlockKind kind = BlockKind::Text;
        std::string text; // TEXT body (may contain $vars)
        std::string arg1; // condition variable name
        std::string arg2; // EQ/NEQ comparison value

        std::vector<std::unique_ptr<Block>> children;

        // Cached layout (canvas coordinates)
        double x = 0, y = 0, w = 0, h = 0;
        double header_h = 0;
        double body_x = 0, body_y = 0, body_w = 0, body_h = 0;

        bool is_cond() const { return kind != BlockKind::Text; }
};

enum class SnapKind { None, Before, After, IntoBody };

struct SnapTarget {
        SnapKind kind = SnapKind::None;
        Block *anchor = nullptr; // sibling insert relative to this, or IF body owner
        double line_y = 0;
        double line_x = 0;
        double line_w = 0;
};

// Scratch-like statement canvas: draw, drag, snap, serialize to xtemplate body.
class BlockCanvas : public Gtk::DrawingArea {
    public:
        BlockCanvas();

        void add_block(BlockKind kind);
        void clear();
        bool empty() const;
        std::string serialize() const;

        sigc::signal<void> &signal_changed() { return m_signal_changed; }

    protected:
        bool on_draw(const Cairo::RefPtr<Cairo::Context> &cr) override;
        bool on_button_press_event(GdkEventButton *event) override;
        bool on_button_release_event(GdkEventButton *event) override;
        bool on_motion_notify_event(GdkEventMotion *event) override;
        bool on_key_press_event(GdkEventKey *event) override;

    private:
        std::vector<std::unique_ptr<Block>> m_roots;
        Block *m_selected = nullptr;
        Block *m_dragging = nullptr;
        Block *m_press_target = nullptr; // left-press candidate; drag starts after threshold
        std::unique_ptr<Block> m_drag_owned;
        double m_drag_dx = 0, m_drag_dy = 0;
        double m_pointer_x = 0, m_pointer_y = 0;
        double m_press_x = 0, m_press_y = 0;
        SnapTarget m_snap;
        sigc::signal<void> m_signal_changed;

        void relayout();
        double layout_list(std::vector<std::unique_ptr<Block>> &list, double x,
                           double y, double max_w);
        double layout_block(Block &b, double x, double y, double max_w);

        Block *hit_test(double x, double y,
                        std::vector<std::unique_ptr<Block>> &list) const;
        Block *hit_test_block(Block &b, double x, double y) const;

        bool detach(Block *target, std::unique_ptr<Block> &out);
        bool detach_from_list(std::vector<std::unique_ptr<Block>> &list,
                              Block *target, std::unique_ptr<Block> &out);
        void apply_snap(std::unique_ptr<Block> block);
        SnapTarget find_snap(double x, double y, Block *exclude) const;
        void find_snap_in_list(const std::vector<std::unique_ptr<Block>> &list,
                              double x, double y, Block *exclude,
                              SnapTarget &best, double &best_dist) const;
        void find_snap_in_block(const Block &b, double x, double y,
                               Block *exclude, SnapTarget &best,
                               double &best_dist) const;

        void draw_list(const Cairo::RefPtr<Cairo::Context> &cr,
                       const std::vector<std::unique_ptr<Block>> &list) const;
        void draw_block(const Cairo::RefPtr<Cairo::Context> &cr,
                        const Block &b) const;
        void draw_snap(const Cairo::RefPtr<Cairo::Context> &cr) const;

        void edit_block(Block *b);
        void notify_changed();
        static const char *kind_label(BlockKind kind);
        static void color_for(BlockKind kind, double &r, double &g, double &b);
        static void serialize_list(const std::vector<std::unique_ptr<Block>> &list,
                                   std::string &out, int indent);
};
