#include "rectangle_dimensions_window.hpp"
#include "editor/editor_interface.hpp"
#include <format>
#include <stdexcept>

namespace dune3d {
RectangleDimensionsWindow::RectangleDimensionsWindow(EditorInterface &intf, double width, double height)
    : Gtk::Fixed(), m_interface(intf)
{
    m_dimension_guides = Gtk::make_managed<Gtk::DrawingArea>();
    m_dimension_guides->set_content_width(330);
    m_dimension_guides->set_content_height(160);
    m_dimension_guides->set_can_target(false);
    m_dimension_guides->set_draw_func([this](const Cairo::RefPtr<Cairo::Context> &cr, int, int) {
        cr->set_source_rgba(0.35, 0.35, 0.35, 0.9);
        cr->set_line_width(1.0);

        if (m_circle_mode || m_extrude_mode) {
            if (m_extrude_mode)
                return;
            cr->move_to(m_x_axis_min, m_x_axis_y);
            cr->line_to(m_x_axis_max, m_x_axis_y);
            cr->move_to(m_x_axis_min, m_x_axis_y - 8);
            cr->line_to(m_x_axis_min, m_x_axis_y + 8);
            cr->move_to(m_x_axis_max, m_x_axis_y - 8);
            cr->line_to(m_x_axis_max, m_x_axis_y + 8);
            cr->stroke();
            return;
        }

        // Horizontal dimension line and T-caps.
        cr->move_to(m_x_axis_min, m_x_axis_y);
        cr->line_to(m_x_axis_max, m_x_axis_y);
        cr->move_to(m_x_axis_min, m_x_axis_y - 8);
        cr->line_to(m_x_axis_min, m_x_axis_y + 8);
        cr->move_to(m_x_axis_max, m_x_axis_y - 8);
        cr->line_to(m_x_axis_max, m_x_axis_y + 8);

        // Vertical dimension line and T-caps.
        cr->move_to(m_y_axis_x, m_y_axis_min);
        cr->line_to(m_y_axis_x, m_y_axis_max);
        cr->move_to(m_y_axis_x - 8, m_y_axis_min);
        cr->line_to(m_y_axis_x + 8, m_y_axis_min);
        cr->move_to(m_y_axis_x - 8, m_y_axis_max);
        cr->line_to(m_y_axis_x + 8, m_y_axis_max);
        cr->stroke();
    });
    put(*m_dimension_guides, 0, 0);

    auto add_dimension = [&](Gtk::Entry *&entry, double value) {
        entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_focusable(true);
        entry->set_focus_on_click(true);
        entry->set_width_chars(6);
        entry->set_max_length(10);
        entry->set_text(std::format("{:.3f}", value));
        entry->add_css_class("rectangle-dimension-entry");
    };
    add_dimension(m_width, width);
    add_dimension(m_height, height);
    // The entries are centered on the horizontal and vertical dimension
    // lines.  The guide drawing is inserted first so it remains behind them.
    put(*m_height, 25, 50);
    put(*m_width, 145, 75);
    m_width->signal_changed().connect([this] {
        update_entry_width(*m_width);
        rectangle_debug_log(std::format("[rectangle-input] width changed text='{}'", m_width->get_text().raw()));
        if ((m_circle_mode || m_extrude_mode) && !m_updating) {
            // Do not update the tool from inside GTK's text user action.
            // Re-entering the tool here causes GTK's "Cannot end irreversible
            // action while in user action" warning and interrupts typing.
            Glib::signal_timeout().connect_once([this] {
                if (!m_circle_mode && !m_extrude_mode)
                    return;
                try {
                    size_t parsed = 0;
                    const auto text = m_width->get_text();
                    const auto diameter = std::stod(text, &parsed);
                    if (parsed == text.size() && diameter >= 0 && diameter <= 1e6) {
                        if (m_extrude_mode) {
                            m_interface.update_extrude_dimension(diameter);
                        }
                        else {
                            auto data = std::make_unique<ToolDataCircleDimensionsWindow>();
                            data->event = ToolDataWindow::Event::UPDATE;
                            data->diameter = diameter;
                            m_interface.tool_update_data(std::move(data));
                        }
                    }
                }
                catch (const std::exception &) {
                }
            }, 50);
        }
    });
    m_height->signal_changed().connect([this] {
        update_entry_width(*m_height);
        rectangle_debug_log(std::format("[rectangle-input] height changed text='{}'", m_height->get_text().raw()));
    });
    auto add_replace_controller = [this](Gtk::Entry &entry) {
        auto *entry_ptr = &entry;
        auto controller = Gtk::EventControllerKey::create();
        controller->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        controller->signal_key_pressed().connect(
                [this, entry_ptr](guint keyval, guint, Gdk::ModifierType) {
                    if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
                        auto *next_entry = entry_ptr == m_width ? m_height : m_width;
                        if (m_circle_mode || m_extrude_mode || !next_entry->get_visible()) {
                            // Only one field is visible (circle/fillet tools
                            // always have one, and a 3-point rectangle's
                            // first edge only defines one dimension yet);
                            // never move focus to the hidden entry.
                            Glib::signal_idle().connect_once([this, entry_ptr] { focus_and_select(*entry_ptr); });
                            return true;
                        }
                        rectangle_debug_log(std::format("[rectangle-input] Tab entry={} text='{}'",
                                                        entry_ptr == m_width ? "width" : "height",
                                                        entry_ptr->get_text().raw()));
                        emit_dimensions(entry_ptr == m_width, entry_ptr == m_height);
                        m_tab_on_width = next_entry == m_width;
                        Glib::signal_idle().connect_once([this, next_entry] {
                            focus_and_select(*next_entry);
                        });
                        // Consume Tab here. The editor-level controller must
                        // not also process it or move focus a second time.
                        return true;
                    }
                    const bool numeric_input = (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9)
                                                || (keyval >= GDK_KEY_KP_0 && keyval <= GDK_KEY_KP_9)
                                                || keyval == GDK_KEY_minus || keyval == GDK_KEY_period
                                                || keyval == GDK_KEY_KP_Decimal;
                    if (!numeric_input)
                        return false;
                    auto &replace_pending = entry_ptr == m_width ? m_replace_width_on_input
                                                                  : m_replace_height_on_input;
                    if (replace_pending) {
                        // The field is already fully selected by
                        // focus_and_select(). Let GTK replace that selection
                        // with the first digit; calling set_text() here would
                        // end GTK's active text action and break the next
                        // keypress.
                        replace_pending = false;
                    }
                    return false;
                },
                false);
        entry.add_controller(controller);
    };
    add_replace_controller(*m_width);
    add_replace_controller(*m_height);
    m_width->signal_activate().connect([this] {
        if (m_circle_mode) {
            commit_circle_dimension();
            return;
        }
        if (m_extrude_mode) {
            m_interface.update_extrude_dimension(get_width());
            // Finish after the GTK text action has completed, matching the
            // Finish Extrude button without ending the action mid-edit.
            Glib::signal_idle().connect_once([this] { m_interface.accept_extrude_dimension(); });
            return;
        }
        emit_dimensions(true, false);
        m_interface.accept_rectangle_dimensions();
    });
    m_height->signal_activate().connect([this] {
        emit_dimensions(false, true);
        m_interface.accept_rectangle_dimensions();
    });
    auto allow_canvas_scroll = [](Gtk::Entry &entry) {
        auto scroll_controller = Gtk::EventControllerScroll::create();
        scroll_controller->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
        scroll_controller->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        scroll_controller->signal_scroll().connect([](double, double) { return false; }, false);
        entry.add_controller(scroll_controller);
    };
    allow_canvas_scroll(*m_width);
    allow_canvas_scroll(*m_height);
    m_width->grab_focus();
}

void RectangleDimensionsWindow::set_dimensions(double width, double height, bool width_visible, bool height_visible)
{
    m_circle_mode = false;
    m_circle_user_editing = false;
    m_extrude_mode = false;
    m_extrude_user_editing = false;
    m_width->set_visible(width_visible);
    m_height->set_visible(height_visible);
    // Never leave focus on an entry that just became hidden (e.g. a 3-point
    // rectangle's first edge is being dragged, so only one dimension is
    // defined yet): move it to whichever entry is still visible.
    if (!width_visible && m_width->has_focus())
        focus_and_select(*m_height);
    else if (!height_visible && m_height->has_focus())
        focus_and_select(*m_width);
    m_updating = true;
    m_width->set_text(std::format("{:.3f}", width));
    m_height->set_text(std::format("{:.3f}", height));
    update_entry_width(*m_width);
    update_entry_width(*m_height);
    m_updating = false;
}

void RectangleDimensionsWindow::set_circle_dimension(double diameter)
{
    m_circle_mode = true;
    m_extrude_mode = false;
    m_height->set_visible(false);
    // While the user is typing a multi-digit value, the circle/fillet tool
    // continuously refreshes its preview. Do not replace the entry text on
    // every preview update or the next digit will be lost.
    if (m_circle_user_editing)
        return;
    m_updating = true;
    m_width->set_text(std::format("{:.3f}", diameter));
    update_entry_width(*m_width);
    m_updating = false;
}

void RectangleDimensionsWindow::set_extrude_dimension(double height, bool force)
{
    m_extrude_mode = true;
    m_circle_mode = false;
    m_height->set_visible(false);
    if (m_extrude_user_editing && !force)
        return;
    m_updating = true;
    m_width->set_text(std::format("{:.3f}", height));
    update_entry_width(*m_width);
    m_updating = false;
}

void RectangleDimensionsWindow::reset_extrude_dimension_editing()
{
    m_extrude_user_editing = false;
}

void RectangleDimensionsWindow::commit_extrude_dimension()
{
    if (m_extrude_mode)
        m_interface.update_extrude_dimension(get_width());
}

void RectangleDimensionsWindow::reset_circle_dimension_editing()
{
    m_circle_user_editing = false;
}

void RectangleDimensionsWindow::position_circle_dimension(double x_min, double x_max, double y, double guide_width,
                                                          double guide_height)
{
    m_circle_mode = true;
    m_height->set_visible(false);
    m_dimension_guides->set_content_width(std::max(330, static_cast<int>(std::ceil(guide_width))));
    m_dimension_guides->set_content_height(std::max(100, static_cast<int>(std::ceil(guide_height))));
    m_x_axis_min = std::min(x_min, x_max);
    m_x_axis_max = std::max(x_min, x_max);
    m_x_axis_y = y;
    const auto entry_width = std::max(1, m_width->get_width());
    const auto entry_height = std::max(1, m_width->get_height());
    constexpr double circle_dimension_text_buffer = 20.;
    move(*m_width, (m_x_axis_min + m_x_axis_max - entry_width) / 2.,
         m_x_axis_y - entry_height - circle_dimension_text_buffer);
    m_dimension_guides->queue_draw();
}

void RectangleDimensionsWindow::position_extrude_dimension(double base_x, double base_y, double tip_x, double tip_y,
                                                           double guide_width, double guide_height)
{
    m_extrude_mode = true;
    m_circle_mode = false;
    m_height->set_visible(false);
    m_dimension_guides->set_content_width(std::max(160, static_cast<int>(std::ceil(guide_width))));
    m_dimension_guides->set_content_height(std::max(80, static_cast<int>(std::ceil(guide_height))));
    const auto entry_height = std::max(1, m_width->get_height());
    move(*m_width, tip_x + 15., tip_y - entry_height / 2.);
    m_dimension_guides->queue_draw();
}
double RectangleDimensionsWindow::get_width() const
{
    try {
        size_t parsed = 0;
        const auto value = std::stod(m_width->get_text(), &parsed);
        return parsed == m_width->get_text().size() ? value : 0;
    }
    catch (const std::exception &) {
        return 0;
    }
}
double RectangleDimensionsWindow::get_height() const
{
    try {
        size_t parsed = 0;
        const auto value = std::stod(m_height->get_text(), &parsed);
        return parsed == m_height->get_text().size() ? value : 0;
    }
    catch (const std::exception &) {
        return 0;
    }
}
void RectangleDimensionsWindow::focus_width()
{
    m_tab_on_width = true;
    if (m_circle_mode)
        m_circle_user_editing = true;
    if (m_extrude_mode)
        m_extrude_user_editing = true;
    focus_and_select(*m_width);
}
void RectangleDimensionsWindow::commit_dimensions()
{
    emit_dimensions();
}
void RectangleDimensionsWindow::commit_circle_dimension()
{
    emit_circle_dimension();
    m_interface.accept_circle_dimension();
}
void RectangleDimensionsWindow::commit_and_focus_next_dimension()
{
    emit_dimensions(m_tab_on_width, !m_tab_on_width);
    m_tab_on_width = !m_tab_on_width;
    auto *entry = m_tab_on_width ? m_width : m_height;
    // Committing dimensions updates the tool and can refresh the viewport.
    // Focus after that work so the refresh cannot take focus away from the
    // field selected by Tab.
    Glib::signal_idle().connect_once([this, entry] { focus_and_select(*entry); });
}
void RectangleDimensionsWindow::focus_next_dimension()
{
    m_tab_on_width = !m_tab_on_width;
    focus_and_select(*(m_tab_on_width ? m_width : m_height));
}
void RectangleDimensionsWindow::position_dimensions(bool negative_x, bool negative_y, double x_min, double x_max,
                                                    double y_min, double y_max, double guide_width, double guide_height)
{
    m_dimension_guides->set_content_width(std::max(330, static_cast<int>(std::ceil(guide_width))));
    m_dimension_guides->set_content_height(std::max(160, static_cast<int>(std::ceil(guide_height))));
    m_x_axis_min = x_min;
    m_x_axis_max = x_max;
    m_y_axis_min = y_min;
    m_y_axis_max = y_max;
    constexpr double dimension_buffer = 50.;
    m_y_axis_x = negative_x ? x_min - dimension_buffer : x_max + dimension_buffer;
    m_x_axis_y = negative_y ? m_y_axis_min - dimension_buffer : m_y_axis_max + dimension_buffer;
    if (m_x_axis_min > m_x_axis_max)
        std::swap(m_x_axis_min, m_x_axis_max);
    // m_height is the X dimension and m_width is the Y dimension. Keep each
    // entry centered on its guide while moving it to the matching quadrant.
    const auto y_line_center = (m_y_axis_min + m_y_axis_max) / 2.;
    const auto y_entry_width = std::max(1, m_height->get_width());
    const auto y_entry_height = std::max(1, m_height->get_height());
    move(*m_height, m_y_axis_x - y_entry_width / 2., y_line_center - y_entry_height / 2.);
    const auto x_line_center = (m_x_axis_min + m_x_axis_max) / 2.;
    const auto x_entry_width = std::max(1, m_width->get_width());
    const auto x_entry_height = std::max(1, m_width->get_height());
    move(*m_width, x_line_center - x_entry_width / 2., m_x_axis_y - x_entry_height / 2.);
    m_dimension_guides->queue_draw();
}
void RectangleDimensionsWindow::update_entry_width(Gtk::Entry &entry)
{
    entry.set_width_chars(std::max(6, static_cast<int>(entry.get_text_length())));
}
void RectangleDimensionsWindow::focus_and_select(Gtk::Entry &entry)
{
    const auto focus_grabbed = entry.grab_focus();
    entry.set_position(0);
    entry.select_region(0, entry.get_text_length());
    if (&entry == m_width)
        m_replace_width_on_input = true;
    else
        m_replace_height_on_input = true;
    rectangle_debug_log(std::format("[rectangle-input] focus/select text='{}' length={} grabbed={} has_focus={}",
                                    entry.get_text().raw(), entry.get_text_length(), focus_grabbed,
                                    entry.has_focus()));
}
void RectangleDimensionsWindow::emit_dimensions(bool lock_width, bool lock_height)
{
    if (m_updating)
        return;
    // A plain Gtk::Entry allows temporary intermediate text while typing.
    // Only send complete numeric values to the rectangle tool.
    const auto width_text = m_width->get_text();
    const auto height_text = m_height->get_text();
    try {
        size_t width_parsed = 0;
        size_t height_parsed = 0;
        const auto width = std::stod(width_text, &width_parsed);
        const auto height = std::stod(height_text, &height_parsed);
        if (width_parsed != width_text.size() || height_parsed != height_text.size() || width < -1e3
            || width > 1e3 || height < -1e3 || height > 1e3)
            return;
    }
    catch (const std::exception &) {
        return;
    }
    auto data = std::make_unique<ToolDataRectangleDimensionsWindow>();
    data->event = ToolDataWindow::Event::UPDATE;
    data->width = get_width();
    data->height = get_height();
    data->lock_width = lock_width;
    data->lock_height = lock_height;
    rectangle_debug_log(std::format("[rectangle-dim] values width={} height={}", data->width, data->height));
    m_interface.tool_update_data(std::move(data));
}

void RectangleDimensionsWindow::emit_circle_dimension()
{
    if (m_updating)
        return;
    try {
        size_t parsed = 0;
        const auto text = m_width->get_text();
        const auto diameter = std::stod(text, &parsed);
        if (parsed != text.size() || diameter < 0 || diameter > 1e6)
            return;
        auto data = std::make_unique<ToolDataCircleDimensionsWindow>();
        data->event = ToolDataWindow::Event::UPDATE;
        data->diameter = diameter;
        m_interface.tool_update_data(std::move(data));
    }
    catch (const std::exception &) {
    }
}
} // namespace dune3d
