#include "group_editor_replace_face.hpp"
#include "document/group/group_replace_face.hpp"
#include "document/entity/entity.hpp"
#include "util/gtk_util.hpp"
#include "core/core.hpp"
#include "core/tool_id.hpp"

namespace dune3d {

GroupEditorReplaceFace::GroupEditorReplaceFace(Core &core, const UUID &group_uu)
    : GroupEditorFaceOperation(core, group_uu)
{
    construct();
}

void GroupEditorReplaceFace::construct_extra()
{
    auto button = Gtk::make_managed<Gtk::Button>("Pick reference plane…");
    button->signal_clicked().connect([this] { m_signal_trigger_action.emit(ToolID::SET_REPLACE_FACE_PLANE); });
    attach(*button, 0, m_top++, 2, 1);

    m_reference_label = Gtk::make_managed<Gtk::Label>();
    m_reference_label->set_xalign(0);
    attach(*m_reference_label, 0, m_top++, 2, 1);
    update_label();
}

void GroupEditorReplaceFace::do_reload()
{
    GroupEditorFaceOperation::do_reload();
    update_label();
}

void GroupEditorReplaceFace::update_label()
{
    auto &group = get_group_replace_face();
    auto &doc = m_core.get_current_document();
    if (group.m_reference_wrkpl && doc.m_entities.contains(group.m_reference_wrkpl))
        m_reference_label->set_text("Reference plane: " + doc.get_entity(group.m_reference_wrkpl).m_name);
    else
        m_reference_label->set_text("Reference plane: none selected");
}

GroupReplaceFace &GroupEditorReplaceFace::get_group_replace_face()
{
    return m_core.get_current_document().get_group<GroupReplaceFace>(m_group_uu);
}

} // namespace dune3d
