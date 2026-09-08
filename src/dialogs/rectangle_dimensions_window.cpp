#include "rectangle_dimensions_window.hpp"
#include "editor/editor_interface.hpp"
#include <format>
#include <stdexcept>

namespace dune3d {
RectangleDimensionsWindow::RectangleDimensionsWindow(EditorInterface &intf, double width, double height)
    : Gtk::Fixed(), m_interface(intf)
{
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
    // The overlay positions this compact pair diagonally around the anchor.
    put(*m_height, 0, 0);
    put(*m_width, 165, 70);
    m_width->signal_changed().connect([this] {
        update_entry_width(*m_width);
        rectangle_debug_log(std::format("[rectangle-input] width changed text='{}'", m_width->get_text().raw()));
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
                        rectangle_debug_log(std::format("[rectangle-input] Tab entry={} text='{}'",
                                                        entry_ptr == m_width ? "width" : "height",
                                                        entry_ptr->get_text().raw()));
                        emit_dimensions(entry_ptr == m_width, entry_ptr == m_height);
                        auto *next_entry = entry_ptr == m_width ? m_height : m_width;
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
                        entry_ptr->set_text("");
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

void RectangleDimensionsWindow::set_dimensions(double width, double height)
{
    m_updating = true;
    m_width->set_text(std::format("{:.3f}", width));
    m_height->set_text(std::format("{:.3f}", height));
    update_entry_width(*m_width);
    update_entry_width(*m_height);
    m_updating = false;
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
    focus_and_select(*m_width);
}
void RectangleDimensionsWindow::commit_dimensions()
{
    emit_dimensions();
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
void RectangleDimensionsWindow::position_dimensions(bool negative_x, bool negative_y)
{
    // m_height is the X dimension and m_width is the Y dimension.
    move(*m_height, negative_x ? 0 : 165, negative_y ? 0 : 70);
    move(*m_width, negative_x ? 165 : 0, negative_y ? 70 : 0);
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
} // namespace dune3d
