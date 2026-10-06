#include "tool_set_replace_face_plane.hpp"
#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/group/group_replace_face.hpp"
#include "tool_common_impl.hpp"
#include "util/selection_util.hpp"

namespace dune3d {

ToolBase::CanBegin ToolSetReplaceFacePlane::can_begin()
{
    auto *group = dynamic_cast<GroupReplaceFace *>(&get_doc().get_group(m_core.get_current_group()));
    if (!group)
        return false;
    auto wrkpl = point_from_selection(get_doc(), m_selection, Entity::Type::WORKPLANE);
    return wrkpl.has_value();
}

ToolResponse ToolSetReplaceFacePlane::begin(const ToolArgs &args)
{
    auto &group = get_doc().get_group<GroupReplaceFace>(m_core.get_current_group());
    auto wrkpl = point_from_selection(get_doc(), m_selection, Entity::Type::WORKPLANE);
    group.m_reference_wrkpl = wrkpl->entity;
    get_doc().set_group_generate_pending(m_core.get_current_group());
    return ToolResponse::commit();
}

ToolResponse ToolSetReplaceFacePlane::update(const ToolArgs &args)
{
    return ToolResponse();
}

} // namespace dune3d
