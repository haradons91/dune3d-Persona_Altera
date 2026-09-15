#include "tool_draw_conic.hpp"

#include "document/entity/entity_bezier2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"

namespace dune3d {

ToolResponse ToolDrawConic::begin(const ToolArgs &args)
{
    m_wrkpl = get_workplane();
    m_intf.enable_hover_selection();
    return ToolResponse();
}

ToolBase::CanBegin ToolDrawConic::can_begin()
{
    return get_workplane_uuid() != UUID();
}

glm::dvec2 ToolDrawConic::get_cursor_pos_in_plane() const
{
    return m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
}

void ToolDrawConic::update_geometry(const glm::dvec2 &p)
{
    const auto bow = p - (m_first_point + m_second_point) / 2.;
    m_curve->m_p1 = m_first_point;
    m_curve->m_p2 = m_second_point;
    m_curve->m_c1 = m_first_point + (m_second_point - m_first_point) / 3. + bow;
    m_curve->m_c2 = m_second_point - (m_second_point - m_first_point) / 3. + bow;
}

ToolResponse ToolDrawConic::update(const ToolArgs &args)
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
            m_curve = &add_entity<EntityBezier2D>();
            m_curve->m_wrkpl = m_wrkpl->m_uuid;
            m_curve->m_selection_invisible = true;
            update_geometry(get_cursor_pos_in_plane());
            m_points_placed = 2;
        }
        else {
            m_curve->m_selection_invisible = false;
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
