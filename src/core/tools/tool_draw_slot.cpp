#include "tool_draw_slot.hpp"

#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"
#include "core/tool_id.hpp"

#include <glm/glm.hpp>

namespace dune3d {

ToolResponse ToolDrawSlot::begin(const ToolArgs &args)
{
    m_wrkpl = get_workplane();
    if (m_tool_id == ToolID::DRAW_SLOT_OVERALL)
        m_mode = Mode::OVERALL;
    else if (m_tool_id == ToolID::DRAW_SLOT_CENTER_POINT)
        m_mode = Mode::CENTER_POINT;
    else if (m_tool_id == ToolID::DRAW_SLOT_3_POINT_ARC)
        m_mode = Mode::THREE_POINT_ARC;
    else if (m_tool_id == ToolID::DRAW_SLOT_CENTER_POINT_ARC)
        m_mode = Mode::CENTER_POINT_ARC;
    m_intf.enable_hover_selection();
    return ToolResponse();
}

ToolBase::CanBegin ToolDrawSlot::can_begin()
{
    return get_workplane_uuid() != UUID();
}

glm::dvec2 ToolDrawSlot::get_cursor_pos_in_plane() const
{
    return m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
}

void ToolDrawSlot::update_geometry(const glm::dvec2 &p)
{
    const auto original_first = m_first_point;
    const auto original_second = m_second_point;
    if (m_mode == Mode::THREE_POINT_ARC || m_mode == Mode::CENTER_POINT_ARC) {
        const auto a = original_first;
        const auto b = original_second;
        const auto c = m_third_point;
        glm::dvec2 center;
        double centerline_radius;
        if (m_mode == Mode::CENTER_POINT_ARC) {
            center = a;
            centerline_radius = glm::length(b - center);
        }
        else {
            const auto d = 2. * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
            if (std::abs(d) < 1e-12)
                return;
            center = glm::dvec2{
                    (glm::dot(a, a) * (b.y - c.y) + glm::dot(b, b) * (c.y - a.y) + glm::dot(c, c) * (a.y - b.y)) /
                            d,
                    (glm::dot(a, a) * (c.x - b.x) + glm::dot(b, b) * (a.x - c.x) + glm::dot(c, c) * (b.x - a.x)) /
                            d};
            centerline_radius = glm::length(a - center);
        }
        if (centerline_radius < 1e-12)
            return;
        const auto width = std::abs(glm::length(p - center) - centerline_radius);
        const auto outer_radius = centerline_radius + width;
        const auto inner_radius = std::max(centerline_radius - width, 1e-9);
        const auto ua = (a - center) / centerline_radius;
        const auto ub = (b - center) / centerline_radius;
        const auto outer_a = center + ua * outer_radius;
        const auto outer_b = center + ub * outer_radius;
        const auto inner_a = center + ua * inner_radius;
        const auto inner_b = center + ub * inner_radius;
        m_line_a->m_p1 = outer_a;
        m_line_a->m_p2 = inner_a;
        m_line_b->m_p1 = inner_b;
        m_line_b->m_p2 = outer_b;
        m_arc_a->m_center = center;
        m_arc_a->m_from = outer_a;
        m_arc_a->m_to = outer_b;
        m_arc_b->m_center = center;
        m_arc_b->m_from = inner_b;
        m_arc_b->m_to = inner_a;
        return;
    }
    const auto edge = original_second - original_first;
    const auto length = glm::length(edge);
    if (length < 1e-12)
        return;

    const auto direction = edge / length;
    const auto normal = glm::dvec2(-direction.y, direction.x);
    const auto radius = std::abs(glm::dot(p - (original_first + original_second) / 2., normal));
    auto first_center = original_first;
    auto second_center = original_second;
    if (m_mode == Mode::OVERALL) {
        first_center += direction * radius;
        second_center -= direction * radius;
    }
    else if (m_mode == Mode::CENTER_POINT) {
        const auto center_to_arc = original_second - original_first;
        first_center = original_first - center_to_arc;
        second_center = original_first + center_to_arc;
    }
    const auto a_outer = first_center + normal * radius;
    const auto b_outer = second_center + normal * radius;
    const auto a_inner = first_center - normal * radius;
    const auto b_inner = second_center - normal * radius;

    m_line_a->m_p1 = a_outer;
    m_line_a->m_p2 = b_outer;
    m_line_b->m_p1 = b_inner;
    m_line_b->m_p2 = a_inner;

    m_arc_a->m_center = first_center;
    m_arc_a->m_from = a_inner;
    m_arc_a->m_to = a_outer;
    m_arc_b->m_center = second_center;
    m_arc_b->m_from = b_outer;
    m_arc_b->m_to = b_inner;
}

ToolResponse ToolDrawSlot::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        if (m_points_placed == 2)
            update_geometry(get_cursor_pos_in_plane());
        return ToolResponse();
    }
    if (args.type != ToolEventType::ACTION)
        return ToolResponse();

    switch (args.action) {
    case InToolActionID::LMB:
        if (m_points_placed == 0) {
            m_first_point = get_cursor_pos_in_plane();
            m_points_placed = 1;
        }
        else if (m_points_placed == 1) {
            m_second_point = get_cursor_pos_in_plane();
            m_points_placed = 2;
            if (m_mode == Mode::THREE_POINT_ARC || m_mode == Mode::CENTER_POINT_ARC)
                return ToolResponse();
            m_line_a = &add_entity<EntityLine2D>();
            m_line_b = &add_entity<EntityLine2D>();
            m_arc_a = &add_entity<EntityArc2D>();
            m_arc_b = &add_entity<EntityArc2D>();
            for (auto *entity : {static_cast<Entity *>(m_line_a), static_cast<Entity *>(m_line_b),
                                 static_cast<Entity *>(m_arc_a), static_cast<Entity *>(m_arc_b)}) {
                entity->m_selection_invisible = true;
            }
            m_line_a->m_wrkpl = m_wrkpl->m_uuid;
            m_line_b->m_wrkpl = m_wrkpl->m_uuid;
            m_arc_a->m_wrkpl = m_wrkpl->m_uuid;
            m_arc_b->m_wrkpl = m_wrkpl->m_uuid;
            update_geometry(get_cursor_pos_in_plane());
            m_points_placed = 2;
        }
        else if (m_points_placed == 2 &&
                 (m_mode == Mode::THREE_POINT_ARC || m_mode == Mode::CENTER_POINT_ARC)) {
            m_third_point = get_cursor_pos_in_plane();
            m_line_a = &add_entity<EntityLine2D>();
            m_line_b = &add_entity<EntityLine2D>();
            m_arc_a = &add_entity<EntityArc2D>();
            m_arc_b = &add_entity<EntityArc2D>();
            for (auto *entity : {static_cast<Entity *>(m_line_a), static_cast<Entity *>(m_line_b),
                                 static_cast<Entity *>(m_arc_a), static_cast<Entity *>(m_arc_b)})
                entity->m_selection_invisible = true;
            m_line_a->m_wrkpl = m_wrkpl->m_uuid;
            m_line_b->m_wrkpl = m_wrkpl->m_uuid;
            m_arc_a->m_wrkpl = m_wrkpl->m_uuid;
            m_arc_b->m_wrkpl = m_wrkpl->m_uuid;
            update_geometry(get_cursor_pos_in_plane());
            m_points_placed = 3;
        }
        else {
            m_line_a->m_selection_invisible = false;
            m_line_b->m_selection_invisible = false;
            m_arc_a->m_selection_invisible = false;
            m_arc_b->m_selection_invisible = false;
            return ToolResponse::commit();
        }
        break;
    case InToolActionID::RMB:
    case InToolActionID::CANCEL:
        return ToolResponse::revert();
    default:
        break;
    }
    return ToolResponse();
}

} // namespace dune3d
