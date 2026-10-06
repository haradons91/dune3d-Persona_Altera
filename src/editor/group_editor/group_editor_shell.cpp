#include "group_editor_shell.hpp"
#include "document/group/group_shell.hpp"
#include "util/gtk_util.hpp"
#include "core/core.hpp"

namespace dune3d {

GroupEditorShell::GroupEditorShell(Core &core, const UUID &group_uu) : GroupEditorFaceOperation(core, group_uu)
{
    construct();
}

void GroupEditorShell::construct_extra()
{
    auto &group = get_group_shell();

    m_thickness_sp = Gtk::make_managed<Gtk::SpinButton>();
    m_thickness_sp->set_hexpand(true);
    m_thickness_sp->set_range(1e-3, 1e3);
    m_thickness_sp->set_increments(.1, .1);
    m_thickness_sp->set_digits(4);
    m_thickness_sp->set_value(group.m_thickness);
    connect_spinbutton(*m_thickness_sp, sigc::mem_fun(*this, &GroupEditorShell::update_thickness));
    grid_attach_label_and_widget(*this, "Thickness", *m_thickness_sp, m_top);
}

void GroupEditorShell::do_reload()
{
    GroupEditorFaceOperation::do_reload();
    m_thickness_sp->set_value(get_group_shell().m_thickness);
}

GroupShell &GroupEditorShell::get_group_shell()
{
    return m_core.get_current_document().get_group<GroupShell>(m_group_uu);
}

bool GroupEditorShell::update_thickness()
{
    if (is_reloading())
        return false;
    if (get_group_shell().m_thickness == m_thickness_sp->get_value())
        return false;
    get_group_shell().m_thickness = m_thickness_sp->get_value();
    m_core.get_current_document().set_group_update_solid_model_pending(get_group_shell().m_uuid);
    return true;
}

} // namespace dune3d
