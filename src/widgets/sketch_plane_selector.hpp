#pragma once

#include <gtkmm.h>

namespace dune3d {

class SketchPlaneSelector : public Gtk::DrawingArea {
public:
    enum class Plane { XY, YZ, ZX };

    SketchPlaneSelector();

    sigc::signal<void(Plane)> signal_plane_selected()
    {
        return m_signal_plane_selected;
    }

private:
    int m_hovered_plane = -1;
    double m_last_x = 0;
    double m_last_y = 0;
    sigc::signal<void(Plane)> m_signal_plane_selected;

    int get_plane_at(double x, double y) const;
    void render(const Cairo::RefPtr<Cairo::Context> &cr, int width, int height);
};

} // namespace dune3d
