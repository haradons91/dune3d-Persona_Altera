#pragma once
#include <gtkmm.h>
#include "core/tool_data_window.hpp"
#include "util/debug.hpp"
#include <string>

namespace dune3d {
inline void rectangle_debug_log(const std::string &message)
{
    debug_log(DebugCategory::UI, message);
}
class EditorInterface;
class ToolDataRectangleDimensionsWindow : public ToolDataWindow {
public:
    double width = 0;
    double height = 0;
    bool lock_width = false;
    bool lock_height = false;
};

class ToolDataCircleDimensionsWindow : public ToolDataWindow {
public:
    double diameter = 0;
};

class RectangleDimensionsWindow : public Gtk::Fixed {
public:
    RectangleDimensionsWindow(EditorInterface &intf, double width, double height);
    void set_dimensions(double width, double height, bool width_visible = true, bool height_visible = true);
    void focus_width();
    void focus_next_dimension();
    void commit_dimensions();
    void commit_circle_dimension();
    void commit_and_focus_next_dimension();
    void position_dimensions(bool negative_x, bool negative_y, double x_min, double x_max, double y_min, double y_max,
                             double guide_width, double guide_height);
    void set_circle_dimension(double diameter);
    void reset_circle_dimension_editing();
    void position_circle_dimension(double x_min, double x_max, double y, double guide_width, double guide_height);
    void set_extrude_dimension(double height, bool force = false);
    void reset_extrude_dimension_editing();
    void commit_extrude_dimension();
    void position_extrude_dimension(double base_x, double base_y, double tip_x, double tip_y, double guide_width,
                                    double guide_height);
    double get_width() const;
    double get_height() const;
private:
    Gtk::DrawingArea *m_dimension_guides = nullptr;
    double m_x_axis_min = 35;
    double m_x_axis_max = 295;
    double m_x_axis_y = 90;
    double m_y_axis_min = 15;
    double m_y_axis_max = 145;
    double m_y_axis_x = 65;
    Gtk::Entry *m_width = nullptr;
    Gtk::Entry *m_height = nullptr;
    bool m_updating = false;
    bool m_tab_on_width = true;
    bool m_replace_width_on_input = false;
    bool m_replace_height_on_input = false;
    bool m_circle_mode = false;
    bool m_circle_user_editing = false;
    bool m_extrude_mode = false;
    bool m_extrude_user_editing = false;
    EditorInterface &m_interface;
    void emit_dimensions(bool lock_width = true, bool lock_height = true);
    void emit_circle_dimension();
    static void update_entry_width(Gtk::Entry &entry);
    void focus_and_select(Gtk::Entry &entry);
};
} // namespace dune3d
