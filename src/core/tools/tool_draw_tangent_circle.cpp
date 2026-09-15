#include "tool_draw_tangent_circle.hpp"
#include "in_tool_action/in_tool_action.hpp"

#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"

#include <glm/glm.hpp>
#include <cmath>

namespace dune3d {

static std::optional<glm::dvec2> line_intersection(const EntityLine2D &a, const EntityLine2D &b)
{
    const auto da = a.m_p2 - a.m_p1;
    const auto db = b.m_p2 - b.m_p1;
    const auto cross = da.x * db.y - da.y * db.x;
    if (std::abs(cross) < 1e-12)
        return {};
    const auto delta = b.m_p1 - a.m_p1;
    const auto t = (delta.x * db.y - delta.y * db.x) / cross;
    return a.m_p1 + da * t;
}

ToolBase::CanBegin ToolDrawTangentCircle::can_begin()
{
    return get_workplane_uuid() != UUID() && m_selection.size() == 2;
}

ToolResponse ToolDrawTangentCircle::begin(const ToolArgs &args)
{
    m_wrkpl = get_workplane();
    std::vector<const EntityLine2D *> lines;
    for (const auto &selection : m_selection) {
        if (selection.type != SelectableRef::Type::ENTITY)
            continue;
        if (auto line = dynamic_cast<const EntityLine2D *>(&get_doc().get_entity(selection.item)))
            lines.push_back(line);
    }
    if (lines.size() != 2 || lines.at(0)->m_wrkpl != m_wrkpl->m_uuid || lines.at(1)->m_wrkpl != m_wrkpl->m_uuid)
        return ToolResponse::end();
    auto intersection = line_intersection(*lines.at(0), *lines.at(1));
    if (!intersection)
        return ToolResponse::end();
    m_line_a = lines.at(0);
    m_line_b = lines.at(1);
    m_intersection = *intersection;
    const auto da = glm::normalize(m_line_a->m_p2 - m_line_a->m_p1);
    const auto db = glm::normalize(m_line_b->m_p2 - m_line_b->m_p1);
    const auto na = glm::dvec2(-da.y, da.x);
    const auto nb = glm::dvec2(-db.y, db.x);
    m_bisector_a = glm::normalize(na + nb);
    m_bisector_b = glm::normalize(na - nb);
    m_circle = &add_entity<EntityCircle2D>();
    m_circle->m_wrkpl = m_wrkpl->m_uuid;
    m_circle->m_selection_invisible = true;
    m_intf.enable_hover_selection();
    return ToolResponse();
}

void ToolDrawTangentCircle::update_circle(const glm::dvec2 &p)
{
    const auto va = p - m_intersection;
    const auto axis = std::abs(glm::dot(va, m_bisector_a)) > std::abs(glm::dot(va, m_bisector_b)) ? m_bisector_a
                                                                                                    : m_bisector_b;
    const auto center = m_intersection + axis * glm::dot(va, axis);
    const auto direction = glm::normalize(m_line_a->m_p2 - m_line_a->m_p1);
    const auto normal = glm::dvec2(-direction.y, direction.x);
    m_circle->m_center = center;
    m_circle->m_radius = std::abs(glm::dot(center - m_line_a->m_p1, normal));
}

ToolResponse ToolDrawTangentCircle::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        update_circle(m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl)));
        return ToolResponse();
    }
    if (args.type == ToolEventType::ACTION) {
        if (args.action == InToolActionID::LMB) {
            update_circle(m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl)));
            m_circle->m_selection_invisible = false;
            return ToolResponse::commit();
        }
        if (args.action == InToolActionID::RMB || args.action == InToolActionID::CANCEL)
            return ToolResponse::revert();
    }
    return ToolResponse();
}

} // namespace dune3d
