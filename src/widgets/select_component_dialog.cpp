#include "select_component_dialog.hpp"
#include "document/document.hpp"
#include "document/component.hpp"
#include "util/gtk_util.hpp"

namespace dune3d {

SelectComponentDialog::SelectComponentDialog(const Document &doc) : Gtk::Window()
{
    set_modal(true);
    set_title("Select Component");
    set_default_size(300, -1);

    auto headerbar = Gtk::make_managed<Gtk::HeaderBar>();
    headerbar->set_show_title_buttons(false);
    set_titlebar(*headerbar);
    install_esc_to_close(*this);

    auto sg = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);

    auto cancel_button = Gtk::make_managed<Gtk::Button>("Cancel");
    headerbar->pack_start(*cancel_button);
    cancel_button->signal_clicked().connect([this] { close(); });
    sg->add_widget(*cancel_button);

    auto ok_button = Gtk::make_managed<Gtk::Button>("OK");
    ok_button->add_css_class("suggested-action");
    headerbar->pack_end(*ok_button);
    sg->add_widget(*ok_button);
    ok_button->signal_clicked().connect([this] {
        m_signal_changed.emit();
        close();
    });

    auto names = Gtk::StringList::create({});
    for (const auto &[uu, component] : doc.get_components()) {
        m_component_uuids.push_back(uu);
        names->append(component->m_name);
    }

    m_dropdown = Gtk::make_managed<Gtk::DropDown>(names);
    m_dropdown->set_margin(10);
    if (m_component_uuids.size())
        m_dropdown->set_selected(0);
    set_child(*m_dropdown);
}

UUID SelectComponentDialog::get_selected_component() const
{
    auto idx = m_dropdown->get_selected();
    if (idx == GTK_INVALID_LIST_POSITION || idx >= m_component_uuids.size())
        return {};
    return m_component_uuids.at(idx);
}

} // namespace dune3d
