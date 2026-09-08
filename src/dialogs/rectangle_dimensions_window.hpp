#pragma once
#include <gtkmm.h>
#include "core/tool_data_window.hpp"
#include <fstream>
#include <string>

namespace dune3d {
inline void rectangle_debug_log(const std::string &message)
{
    static std::ofstream log("/tmp/dune3d-rectangle-debug.log", std::ios::app);
    log << message << '\n';
    log.flush();
}
class EditorInterface;
class ToolDataRectangleDimensionsWindow : public ToolDataWindow {
public:
    double width = 0;
    double height = 0;
    bool lock_width = false;
    bool lock_height = false;
};

class RectangleDimensionsWindow : public Gtk::Fixed {
public:
    RectangleDimensionsWindow(EditorInterface &intf, double width, double height);
    void set_dimensions(double width, double height);
    void focus_width();
    void focus_next_dimension();
    void commit_dimensions();
    void commit_and_focus_next_dimension();
    void position_dimensions(bool negative_x, bool negative_y);
    double get_width() const;
    double get_height() const;
private:
    Gtk::Entry *m_width = nullptr;
    Gtk::Entry *m_height = nullptr;
    bool m_updating = false;
    bool m_tab_on_width = true;
    bool m_replace_width_on_input = false;
    bool m_replace_height_on_input = false;
    EditorInterface &m_interface;
    void emit_dimensions(bool lock_width = true, bool lock_height = true);
    static void update_entry_width(Gtk::Entry &entry);
    void focus_and_select(Gtk::Entry &entry);
};
} // namespace dune3d
