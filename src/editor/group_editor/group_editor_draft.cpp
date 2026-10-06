#include "group_editor_draft.hpp"
#include "document/group/group_draft.hpp"
#include "util/gtk_util.hpp"
#include "core/core.hpp"

namespace dune3d {

GroupEditorDraft::GroupEditorDraft(Core &core, const UUID &group_uu) : GroupEditorFaceOperation(core, group_uu)
{
    construct();
}

void GroupEditorDraft::construct_extra()
{
    auto &group = get_group_draft();

    m_angle_sp = Gtk::make_managed<Gtk::SpinButton>();
    m_angle_sp->set_hexpand(true);
    m_angle_sp->set_range(-89, 89);
    m_angle_sp->set_increments(1, 1);
    m_angle_sp->set_digits(2);
    m_angle_sp->set_value(group.m_angle);
    connect_spinbutton(*m_angle_sp, sigc::mem_fun(*this, &GroupEditorDraft::update_angle));
    grid_attach_label_and_widget(*this, "Angle", *m_angle_sp, m_top);
}

void GroupEditorDraft::do_reload()
{
    GroupEditorFaceOperation::do_reload();
    m_angle_sp->set_value(get_group_draft().m_angle);
}

GroupDraft &GroupEditorDraft::get_group_draft()
{
    return m_core.get_current_document().get_group<GroupDraft>(m_group_uu);
}

bool GroupEditorDraft::update_angle()
{
    if (is_reloading())
        return false;
    if (get_group_draft().m_angle == m_angle_sp->get_value())
        return false;
    get_group_draft().m_angle = m_angle_sp->get_value();
    m_core.get_current_document().set_group_update_solid_model_pending(get_group_draft().m_uuid);
    return true;
}

} // namespace dune3d
