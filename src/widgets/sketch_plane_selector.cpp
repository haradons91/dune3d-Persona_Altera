#include "sketch_plane_selector.hpp"

#include <array>

namespace dune3d {

namespace {
struct Point {
    double x;
    double y;
};

using Polygon = std::array<Point, 4>;

const std::array<Polygon, 3> &polygons()
{
    static const std::array<Polygon, 3> p = {{
            {{{76, 58}, {99, 45}, {118, 56}, {94, 70}}}, // XY
            {{{76, 42}, {76, 14}, {100, 28}, {100, 52}}}, // YZ
            {{{42, 56}, {42, 30}, {68, 14}, {68, 42}}}, // ZX
    }};
    return p;
}

bool point_in_polygon(const Point &p, const Polygon &polygon)
{
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto &a = polygon[i];
        const auto &b = polygon[j];
        if (((a.y > p.y) != (b.y > p.y))
            && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}
} // namespace

SketchPlaneSelector::SketchPlaneSelector()
{
    set_content_width(140);
    set_content_height(100);
    set_halign(Gtk::Align::CENTER);
    set_valign(Gtk::Align::CENTER);
    add_css_class("sketch-plane-selector");
    set_draw_func(sigc::mem_fun(*this, &SketchPlaneSelector::render));

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) {
        m_last_x = x;
        m_last_y = y;
        const auto hovered = get_plane_at(x, y);
        if (hovered != m_hovered_plane) {
            m_hovered_plane = hovered;
            queue_draw();
        }
    });
    motion->signal_leave().connect([this] {
        m_hovered_plane = -1;
        queue_draw();
    });
    add_controller(motion);

    auto click = Gtk::GestureClick::create();
    click->set_button(1);
    click->signal_released().connect([this](int, double x, double y) {
        const auto plane = get_plane_at(x, y);
        if (plane >= 0)
            m_signal_plane_selected.emit(static_cast<Plane>(plane));
    });
    add_controller(click);
}

int SketchPlaneSelector::get_plane_at(double x, double y) const
{
    const Point p{x, y};
    const auto &pgns = polygons();
    for (int i = 2; i >= 0; i--) {
        if (point_in_polygon(p, pgns[i]))
            return i;
    }
    return -1;
}

void SketchPlaneSelector::render(const Cairo::RefPtr<Cairo::Context> &cr, int, int)
{
    const std::array<std::array<double, 3>, 3> colors = {{{1.0, 0.78, 0.05}, {1.0, 0.78, 0.05}, {1.0, 0.78, 0.05}}};
    const auto &pgns = polygons();

    cr->set_line_width(1.2);
    for (int i = 0; i < 3; i++) {
        const auto &p = pgns[i];
        const double factor = i == m_hovered_plane ? 1.25 : 1.0;
        cr->move_to(p[0].x, p[0].y);
        for (size_t j = 1; j < p.size(); j++)
            cr->line_to(p[j].x, p[j].y);
        cr->close_path();
        cr->set_source_rgba(std::min(1.0, colors[i][0] * factor), std::min(1.0, colors[i][1] * factor),
                            std::min(1.0, colors[i][2] * factor), 0.45);
        cr->fill_preserve();
        cr->set_source_rgb(0.08, 0.08, 0.08);
        cr->stroke();
    }
}

} // namespace dune3d
