#include "group_editor_split_body_result.hpp"
#include "document/group/group_split_body_result.hpp"
#include "core/core.hpp"

namespace dune3d {

GroupEditorSplitBodyResult::GroupEditorSplitBodyResult(Core &core, const UUID &group_uu) : GroupEditor(core, group_uu)
{
    auto &group = m_core.get_current_document().get_group<GroupSplitBodyResult>(m_group_uu);
    auto &doc = m_core.get_current_document();
    std::string source_name = "(deleted group)";
    if (doc.get_groups().contains(group.m_source_group))
        source_name = doc.get_group(group.m_source_group).m_name;

    auto label = Gtk::make_managed<Gtk::Label>("Complementary piece of \"" + source_name
                                                + "\" -- edit the plane there.");
    label->set_wrap(true);
    label->set_xalign(0);
    attach(*label, 0, m_top++, 2, 1);
}

} // namespace dune3d
