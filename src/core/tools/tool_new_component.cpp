#include "tool_new_component.hpp"
#include "document/document.hpp"
#include "document/component.hpp"
#include "document/group/group_occurrence.hpp"
#include "document/group/group_reference.hpp"
#include "tool_common_impl.hpp"
#include <algorithm>

namespace dune3d {

ToolBase::CanBegin ToolNewComponent::can_begin()
{
    return true;
}

ToolResponse ToolNewComponent::begin(const ToolArgs &args)
{
    // Components only ever live in the root's m_components (see Component's
    // own comment on why), and this action is always root-relative -- it's
    // triggered from the top-level document row in the workspace browser,
    // regardless of what's currently being edited/descended.
    auto &root = m_core.get_root_document();

    std::string name;
    for (unsigned int i = 1;; i++) {
        name = "Component" + std::to_string(i);
        const auto taken = std::ranges::any_of(
                root.get_components(), [&name](const auto &it) { return it.second->m_name == name; });
        if (!taken)
            break;
    }

    auto &comp = root.add_component(UUID::random(), root.get_reference_group().m_uuid);
    comp.m_name = name;

    auto &occ_group = root.insert_group<GroupOccurrence>(UUID::random(), root.get_groups_sorted().back()->m_uuid);
    occ_group.m_component = comp.m_uuid;
    occ_group.m_body.emplace();
    occ_group.m_body->m_name = name;
    occ_group.m_name = name;

    root.set_group_generate_pending(occ_group.m_uuid);

    return ToolResponse::commit_and_set_current_group(occ_group.m_uuid);
}

ToolResponse ToolNewComponent::update(const ToolArgs &args)
{
    return ToolResponse();
}

} // namespace dune3d
