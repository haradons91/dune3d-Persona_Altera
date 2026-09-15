#include "tool_draw_three_tangent_circle.hpp"
#include "in_tool_action/in_tool_action.hpp"

#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"

#include <glm/glm.hpp>
#include <array>
#include <cmath>

namespace dune3d {

static std::optional<glm::dvec2> intersect_lines(const EntityLine2D &a, const EntityLine2D &b)
{
    const auto da = a.m_p2 - a.m_p1;
    const auto db = b.m_p2 - b.m_p1;
    const auto cross = da.x * db.y - da.y * db.x;
    if (std::abs(cross) < 1e-12)
        return {};
    const auto delta = b.m_p1 - a.m_p1;
    return a.m_p1 + da * ((delta.x * db.y - delta.y * db.x) / cross);
}

ToolBase::CanBegin ToolDrawThreeTangentCircle::can_begin()
{
    return get_workplane_uuid() != UUID() && m_selection.size() == 3;
}

ToolResponse ToolDrawThreeTangentCircle::begin(const ToolArgs &args)
{
    std::array<const EntityLine2D *, 3> lines = {nullptr, nullptr, nullptr};
    unsigned int index = 0;
    for (const auto &selection : m_selection) {
        if (selection.type != SelectableRef::Type::ENTITY)
            continue;
        auto line = dynamic_cast<const EntityLine2D *>(&get_doc().get_entity(selection.item));
        if (!line || index == lines.size())
            return ToolResponse::end();
        lines.at(index++) = line;
    }
    if (index != 3)
        return ToolResponse::end();
    const auto p12 = intersect_lines(*lines.at(0), *lines.at(1));
    const auto p23 = intersect_lines(*lines.at(1), *lines.at(2));
    const auto p31 = intersect_lines(*lines.at(2), *lines.at(0));
    if (!p12 || !p23 || !p31)
        return ToolResponse::end();
    const auto side_a = glm::length(*p23 - *p31);
    const auto side_b = glm::length(*p31 - *p12);
    const auto side_c = glm::length(*p12 - *p23);
    const auto perimeter = side_a + side_b + side_c;
    if (perimeter < 1e-12)
        return ToolResponse::end();
    const auto center = (side_a * *p12 + side_b * *p23 + side_c * *p31) / perimeter;
    const auto line_direction = glm::normalize(lines.at(0)->m_p2 - lines.at(0)->m_p1);
    const auto normal = glm::dvec2(-line_direction.y, line_direction.x);
    m_circle = &add_entity<EntityCircle2D>();
    m_circle->m_wrkpl = lines.at(0)->m_wrkpl;
    m_circle->m_center = center;
    m_circle->m_radius = std::abs(glm::dot(center - lines.at(0)->m_p1, normal));
    m_circle->m_selection_invisible = true;
    m_intf.enable_hover_selection();
    return ToolResponse();
}

ToolResponse ToolDrawThreeTangentCircle::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::ACTION) {
        if (args.action == InToolActionID::LMB) {
            m_circle->m_selection_invisible = false;
            return ToolResponse::commit();
        }
        if (args.action == InToolActionID::RMB || args.action == InToolActionID::CANCEL)
            return ToolResponse::revert();
    }
    return ToolResponse();
}

} // namespace dune3d
