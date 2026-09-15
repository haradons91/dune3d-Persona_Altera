#include "tool_draw_ellipse.hpp"

#include "document/entity/entity_bezier2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"
#include "core/tool_id.hpp"

#include <glm/glm.hpp>
#include <array>
#include <cmath>

namespace dune3d {

ToolResponse ToolDrawEllipse::begin(const ToolArgs &args)
{
    m_wrkpl = get_workplane();
    m_intf.enable_hover_selection();
    return ToolResponse();
}

ToolBase::CanBegin ToolDrawEllipse::can_begin()
{
    return get_workplane_uuid() != UUID();
}

glm::dvec2 ToolDrawEllipse::get_cursor_pos_in_plane() const
{
    return m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
}

void ToolDrawEllipse::update_geometry(const glm::dvec2 &p)
{
    const auto major = m_major_point - m_center;
    const auto major_length = glm::length(major);
    if (major_length < 1e-12)
        return;
    const auto u = major / major_length;
    const auto v = glm::dvec2(-u.y, u.x);
    const auto minor_radius = std::abs(glm::dot(p - m_center, v));
    constexpr double kappa = 0.5522847498307936;
    for (unsigned int i = 0; i < 4; i++) {
        const auto a0 = i * M_PI / 2.;
        const auto a1 = (i + 1) * M_PI / 2.;
        const auto c0 = std::cos(a0);
        const auto s0 = std::sin(a0);
        const auto c1 = std::cos(a1);
        const auto s1 = std::sin(a1);
        const auto p0 = m_center + u * (major_length * c0) + v * (minor_radius * s0);
        const auto p1 = m_center + u * (major_length * c1) + v * (minor_radius * s1);
        const auto t0 = u * (-major_length * s0) + v * (minor_radius * c0);
        const auto t1 = u * (-major_length * s1) + v * (minor_radius * c1);
        m_segments.at(i)->m_p1 = p0;
        m_segments.at(i)->m_p2 = p1;
        m_segments.at(i)->m_c1 = p0 + kappa * t0;
        m_segments.at(i)->m_c2 = p1 - kappa * t1;
    }
}

ToolResponse ToolDrawEllipse::update(const ToolArgs &args)
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
            m_center = get_cursor_pos_in_plane();
            m_points_placed = 1;
        }
        else if (m_points_placed == 1) {
            m_major_point = get_cursor_pos_in_plane();
            for (auto &segment : m_segments) {
                segment = &add_entity<EntityBezier2D>();
                segment->m_wrkpl = m_wrkpl->m_uuid;
                segment->m_selection_invisible = true;
            }
            update_geometry(get_cursor_pos_in_plane());
            m_points_placed = 2;
        }
        else {
            for (auto segment : m_segments)
                segment->m_selection_invisible = false;
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
