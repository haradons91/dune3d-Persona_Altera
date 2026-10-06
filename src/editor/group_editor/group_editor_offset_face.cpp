#include "group_editor_offset_face.hpp"
#include "document/group/group_offset_face.hpp"
#include "util/gtk_util.hpp"
#include "core/core.hpp"

namespace dune3d {

GroupEditorOffsetFace::GroupEditorOffsetFace(Core &core, const UUID &group_uu)
    : GroupEditorFaceOperation(core, group_uu)
{
    construct();
}

void GroupEditorOffsetFace::construct_extra()
{
    auto &group = get_group_offset_face();

    m_offset_sp = Gtk::make_managed<Gtk::SpinButton>();
    m_offset_sp->set_hexpand(true);
    m_offset_sp->set_range(-1e3, 1e3);
    m_offset_sp->set_increments(.1, .1);
    m_offset_sp->set_digits(4);
    m_offset_sp->set_value(group.m_offset);
    connect_spinbutton(*m_offset_sp, sigc::mem_fun(*this, &GroupEditorOffsetFace::update_offset));
    grid_attach_label_and_widget(*this, "Offset", *m_offset_sp, m_top);
}

void GroupEditorOffsetFace::do_reload()
{
    GroupEditorFaceOperation::do_reload();
    m_offset_sp->set_value(get_group_offset_face().m_offset);
}

GroupOffsetFace &GroupEditorOffsetFace::get_group_offset_face()
{
    return m_core.get_current_document().get_group<GroupOffsetFace>(m_group_uu);
}

bool GroupEditorOffsetFace::update_offset()
{
    if (is_reloading())
        return false;
    if (get_group_offset_face().m_offset == m_offset_sp->get_value())
        return false;
    get_group_offset_face().m_offset = m_offset_sp->get_value();
    m_core.get_current_document().set_group_update_solid_model_pending(get_group_offset_face().m_uuid);
    return true;
}

} // namespace dune3d
