#include "enter_datum_window.hpp"
#include "widgets/spin_button_dim.hpp"
#include "widgets/spin_button_angle.hpp"
#include "widgets/spin_button_ratio.hpp"
#include "editor/editor_interface.hpp"
#include "util/gtk_util.hpp"
#include "util/debug.hpp"
#include <format>

namespace dune3d {
namespace {
void sketch_dimension_debug_log(const std::string &message)
{
    debug_log(DebugCategory::UI, message);
}
} // namespace


EnterDatumWindow::EnterDatumWindow(Gtk::Window &parent, EditorInterface &intf, const std::string &label, DatumUnit unit,
                                   double def)
    : ToolWindow(parent, intf)
{
    set_title("Enter datum");

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    box->set_margin(6);
    auto la = Gtk::manage(new Gtk::Label(label));
    la->set_halign(Gtk::Align::START);
    box->append(*la);

    switch (unit) {
    case DatumUnit::MM:
        m_sp = Gtk::make_managed<SpinButtonDim>();
        m_sp->set_range(-1e3, 1e3);
        break;

    case DatumUnit::DEGREE:
        m_sp = Gtk::make_managed<SpinButtonAngle>();
        break;

    case DatumUnit::INTEGER:
        m_sp = Gtk::make_managed<Gtk::SpinButton>();
        m_sp->set_range(-1e3, 1e3);
        break;

    case DatumUnit::RATIO: {
        auto sp = Gtk::make_managed<SpinButtonRatio>();
        m_sp = sp;
    } break;
    }
    m_sp->set_margin_start(8);
    m_sp->set_value(def);
    // Enter should commit the datum immediately, including when the value
    // was just edited. This makes keyboard confirmation match the OK button.
    spinbutton_connect_activate_immediate(*m_sp, [this] { emit_event(ToolDataWindow::Event::OK); });
    m_sp->signal_value_changed().connect([this] {
        sketch_dimension_debug_log(std::format("datum spin changed value={}", get_value()));
        auto data = std::make_unique<ToolDataEnterDatumWindow>();
        data->event = ToolDataWindow::Event::UPDATE;
        data->value = get_value();
        m_interface.tool_update_data(std::move(data));
    });
    box->append(*m_sp);
    set_child(*box);
    // Put keyboard focus in the value field so typing a replacement value
    // immediately updates the active dimensional constraint.
    Glib::signal_idle().connect_once([this] {
        m_sp->grab_focus();
        m_sp->select_region(0, -1);
    });
}

void EnterDatumWindow::set_range(double lo, double hi)
{
    m_sp->set_range(lo, hi);
}

void EnterDatumWindow::set_step_size(double sz)
{
    m_sp->set_increments(sz, sz);
}

double EnterDatumWindow::get_value()
{
    return m_sp->get_value();
}

} // namespace dune3d
