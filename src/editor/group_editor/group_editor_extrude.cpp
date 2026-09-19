#include "group_editor_extrude.hpp"
#include "document/group/group_extrude.hpp"
#include "util/gtk_util.hpp"
#include "util/paths.hpp"
#include "core/core.hpp"
#include <format>

namespace dune3d {

GroupEditorExtrude::GroupEditorExtrude(Core &core, const UUID &group_uu) : GroupEditorSweep(core, group_uu)
{
    m_normal_switch = Gtk::make_managed<Gtk::Switch>();
    m_normal_switch->set_valign(Gtk::Align::CENTER);
    m_normal_switch->set_halign(Gtk::Align::START);
    auto &group = get_group();
    m_normal_switch->set_active(group.m_direction == GroupExtrude::Direction::NORMAL);
    grid_attach_label_and_widget(*this, "Along normal", *m_normal_switch, m_top);
    m_normal_switch->property_active().signal_changed().connect([this] {
        if (is_reloading())
            return;
        auto &group = get_group();
        if (m_normal_switch->get_active())
            group.m_direction = GroupExtrude::Direction::NORMAL;
        else
            group.m_direction = GroupExtrude::Direction::ARBITRARY;
        m_core.get_current_document().set_group_solve_pending(group.m_uuid);
        m_signal_changed.emit(CommitMode::IMMEDIATE);
    });
    add_operation_combo();
    {
        auto items = Gtk::StringList::create();
        items->append("All profiles");
        try {
            const auto paths = paths::Paths::from_document(m_core.get_current_document(), group.m_wrkpl,
                                                           group.m_source_group);
            for (size_t i = 0; i < paths.paths.size(); i++)
                items->append(std::format("Profile {}", i + 1));
        }
        catch (...) {
            // The source sketch can be temporarily incomplete while an
            // extrusion is being created.  Keep the default choice usable.
        }

        m_profile_combo = Gtk::make_managed<Gtk::DropDown>(items);
        const auto selected_profile = group.m_source_paths.size() == 1
                                              ? *group.m_source_paths.begin() + 1
                                              : (group.m_source_path ? *group.m_source_path + 1 : 0);
        m_profile_combo->set_selected(selected_profile < items->get_n_items() ? selected_profile : 0);
        m_profile_combo->property_selected().signal_changed().connect([this] {
            if (is_reloading())
                return;
            auto &group = get_group();
            const auto selected = m_profile_combo->get_selected();
            group.m_source_paths.clear();
            if (selected == 0)
                group.m_source_path.reset();
            else
                group.m_source_path = selected - 1;
            m_core.get_current_document().set_group_generate_pending(group.m_uuid);
            m_signal_changed.emit(CommitMode::IMMEDIATE);
        });
        grid_attach_label_and_widget(*this, "Profile", *m_profile_combo, m_top);
    }
    {
        auto items = Gtk::StringList::create();
        items->append("Single");
        items->append("Offset");
        items->append("Offset symmetric");

        m_mode_combo = Gtk::make_managed<Gtk::DropDown>(items);
        m_mode_combo->set_selected(static_cast<guint>(group.m_mode));
        m_mode_combo->property_selected().signal_changed().connect([this] {
            if (is_reloading())
                return;
            auto &group = get_group();
            group.m_mode = static_cast<GroupExtrude::Mode>(m_mode_combo->get_selected());
            m_core.get_current_document().set_group_generate_pending(group.m_uuid);
            m_signal_changed.emit(CommitMode::IMMEDIATE);
        });
        grid_attach_label_and_widget(*this, "Mode", *m_mode_combo, m_top);
    }
}

void GroupEditorExtrude::do_reload()
{
    GroupEditorSweep::do_reload();
    auto &group = get_group();
    m_normal_switch->set_active(group.m_direction == GroupExtrude::Direction::NORMAL);
    reload_profiles();
    m_mode_combo->set_selected(static_cast<guint>(group.m_mode));
}

void GroupEditorExtrude::reload_profiles()
{
    auto &group = get_group();
    auto items = Gtk::StringList::create();
    items->append("All profiles");
    try {
        const auto paths = paths::Paths::from_document(m_core.get_current_document(), group.m_wrkpl,
                                                       group.m_source_group);
        for (size_t i = 0; i < paths.paths.size(); i++)
            items->append(std::format("Profile {}", i + 1));
    }
    catch (...) {
    }
    m_profile_combo->set_model(items);
    const auto selected_profile = group.m_source_paths.size() == 1
                                          ? *group.m_source_paths.begin() + 1
                                          : (group.m_source_path ? *group.m_source_path + 1 : 0);
    m_profile_combo->set_selected(selected_profile < items->get_n_items() ? selected_profile : 0);
}

GroupExtrude &GroupEditorExtrude::get_group()
{
    return m_core.get_current_document().get_group<GroupExtrude>(m_group_uu);
}


} // namespace dune3d
