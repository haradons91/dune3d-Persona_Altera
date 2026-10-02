#include "group_editor_face_operation.hpp"
#include "document/group/group_face_operation.hpp"
#include "util/gtk_util.hpp"
#include "core/tool_id.hpp"
#include "core/core.hpp"

namespace dune3d {

void GroupEditorFaceOperation::construct()
{
    construct_extra();

    auto button = Gtk::make_managed<Gtk::Button>("Select faces…");
    button->signal_clicked().connect([this] { m_signal_trigger_action.emit(ToolID::SELECT_FACES); });
    attach(*button, 0, m_top++, 2, 1);
}

void GroupEditorFaceOperation::construct_extra()
{
}

GroupFaceOperation &GroupEditorFaceOperation::get_group()
{
    return m_core.get_current_document().get_group<GroupFaceOperation>(m_group_uu);
}

} // namespace dune3d
