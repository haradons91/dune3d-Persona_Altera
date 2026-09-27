#include "conversion_progress_dialog.hpp"
#include "util/gtk_util.hpp"
#include <string>

namespace dune3d {

ConversionProgressDialog::ConversionProgressDialog()
{
    set_modal(true);
    set_title("Converting Mesh");
    set_default_size(320, -1);
    set_resizable(false);

    auto headerbar = Gtk::make_managed<Gtk::HeaderBar>();
    headerbar->set_show_title_buttons(false);
    set_titlebar(*headerbar);

    auto cancel_button = Gtk::make_managed<Gtk::Button>("Cancel");
    headerbar->pack_start(*cancel_button);
    cancel_button->signal_clicked().connect([this] { close(); });

    install_esc_to_close(*this);
    // The only way to dismiss this window at all is Cancel or Escape (no
    // title bar close button, no OK) -- both funnel through here, so this
    // is the one place that needs to set the flag.
    signal_close_request().connect(
            [this] {
                m_cancel_requested = true;
                return false; // let the close proceed
            },
            false);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_margin(20);
    box->append(*Gtk::make_managed<Gtk::Label>("Converting mesh to body…"));
    m_progress_bar = Gtk::make_managed<Gtk::ProgressBar>();
    m_progress_bar->set_pulse_step(0.1);
    m_progress_bar->set_show_text(true);
    box->append(*m_progress_bar);
    set_child(*box);
}

void ConversionProgressDialog::pulse()
{
    m_progress_bar->set_text("");
    m_progress_bar->pulse();
}

void ConversionProgressDialog::set_progress(double progress)
{
    m_progress_bar->set_fraction(progress);
    m_progress_bar->set_text(std::to_string((int)(progress * 100 + 0.5)) + "%");
}

} // namespace dune3d
