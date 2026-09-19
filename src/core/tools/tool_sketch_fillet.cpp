#include "tool_sketch_fillet.hpp"
#include "document/document.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_point2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_arc_line_tangent.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/rectangle_dimensions_window.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "tool_common_impl.hpp"
#include "util/glm_util.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace dune3d {

ToolBase::CanBegin ToolSketchFillet::can_begin()
{
    return get_workplane_uuid() != UUID();
}

ToolResponse ToolSketchFillet::begin(const ToolArgs &args)
{
    m_intf.enable_hover_selection();
    for (const auto &selection : args.selection) {
        if (selection.type != SelectableRef::Type::ENTITY)
            continue;
        auto *line = dynamic_cast<EntityLine2D *>(&get_doc().get_entity(selection.item));
        if (!line || line->m_wrkpl != get_workplane_uuid())
            continue;
        if (!m_line1)
            m_line1 = line;
        else if (line != m_line1) {
            m_line2 = line;
            break;
        }
    }
    if (m_line1 && m_line2 && !setup_corner()) {
        m_line1 = nullptr;
        m_line2 = nullptr;
    }
    return ToolResponse();
}

bool ToolSketchFillet::select_line(EntityLine2D *&line)
{
    const auto hover = m_intf.get_hover_selection();
    if (!hover || hover->type != SelectableRef::Type::ENTITY)
        return false;

    auto *candidate = dynamic_cast<EntityLine2D *>(&get_doc().get_entity(hover->item));
    if (!candidate || candidate->m_wrkpl != get_workplane_uuid() || candidate == line)
        return false;
    line = candidate;
    return true;
}

bool ToolSketchFillet::setup_corner()
{
    const std::array<glm::dvec2, 2> line1_points = {m_line1->m_p1, m_line1->m_p2};
    const std::array<glm::dvec2, 2> line2_points = {m_line2->m_p1, m_line2->m_p2};
    double best_distance = 1e-6;
    int corner1 = -1;
    int corner2 = -1;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            const auto distance = glm::length(line1_points[i] - line2_points[j]);
            if (distance <= best_distance) {
                best_distance = distance;
                corner1 = i;
                corner2 = j;
            }
        }
    }
    if (corner1 < 0)
        return false;

    m_corner = (line1_points[corner1] + line2_points[corner2]) / 2.;
    m_line1_corner = corner1;
    m_line2_corner = corner2;
    const auto line1_end = line1_points[1 - corner1];
    const auto line2_end = line2_points[1 - corner2];
    const auto line1_vector = line1_end - m_corner;
    const auto line2_vector = line2_end - m_corner;
    const auto line1_length = glm::length(line1_vector);
    const auto line2_length = glm::length(line2_vector);
    if (line1_length < 1e-6 || line2_length < 1e-6)
        return false;

    m_line1_direction = line1_vector / line1_length;
    m_line2_direction = line2_vector / line2_length;
    const auto angle = std::acos(std::clamp(glm::dot(m_line1_direction, m_line2_direction), -1., 1.));
    const auto tangent_factor = std::tan(angle / 2.);
    if (tangent_factor < 1e-6 || std::abs(angle - M_PI) < 1e-6)
        return false;

    m_max_radius = std::min(line1_length, line2_length) * tangent_factor * 0.95;
    if (m_max_radius < 1e-6)
        return false;

    m_preview_arc = &add_entity<EntityArc2D>();
    m_preview_arc->m_selection_invisible = true;
    m_preview_arc->m_wrkpl = m_line1->m_wrkpl;
    m_ready = true;
    update_fillet();
    m_intf.show_circle_dimension(m_radius);
    return true;
}

double ToolSketchFillet::get_radius()
{
    const auto workplane = get_workplane();
    const auto cursor = workplane->project(get_cursor_pos_for_workplane(*workplane));
    return std::clamp(glm::length(cursor - m_corner), 0.05, m_max_radius);
}

void ToolSketchFillet::update_fillet()
{
    if (!m_ready)
        return;

    const auto radius = m_radius_locked ? m_radius : get_radius();
    m_radius = radius;
    const auto dot = std::clamp(glm::dot(m_line1_direction, m_line2_direction), -1., 1.);
    const auto angle = std::acos(dot);
    const auto tangent_distance = radius / std::tan(angle / 2.);
    const auto bisector = glm::normalize(m_line1_direction + m_line2_direction);
    const auto center_distance = radius / std::sin(angle / 2.);
    const auto p1 = m_corner + m_line1_direction * tangent_distance;
    const auto p2 = m_corner + m_line2_direction * tangent_distance;
    m_preview_arc->m_center = m_corner + bisector * center_distance;
    // EntityArc2D follows the counter-clockwise minor arc from m_from to
    // m_to. Keep the line-1 tangent point associated with the correct arc
    // endpoint while choosing the short, inside fillet arc.
    const auto r1 = p1 - m_preview_arc->m_center;
    const auto r2 = p2 - m_preview_arc->m_center;
    m_line1_arc_point1 = r1.x * r2.y - r1.y * r2.x >= 0;
    if (m_line1_arc_point1) {
        m_preview_arc->m_from = p1;
        m_preview_arc->m_to = p2;
    }
    else {
        m_preview_arc->m_from = p2;
        m_preview_arc->m_to = p1;
    }

    const auto workplane = get_workplane();
    const auto center = workplane->transform(m_preview_arc->m_center);
    const auto edge = workplane->transform({m_preview_arc->m_center.x + radius, m_preview_arc->m_center.y});
    m_intf.update_circle_dimension(radius);
    m_intf.position_circle_dimension(center, center, edge);
}

ToolResponse ToolSketchFillet::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        update_fillet();
        set_first_update_group_current();
        return ToolResponse();
    }

    if (args.type != ToolEventType::ACTION)
    {
        if (args.type == ToolEventType::DATA) {
            if (auto data = dynamic_cast<const ToolDataCircleDimensionsWindow *>(args.data.get())) {
                if (data->event == ToolDataWindow::Event::UPDATE && m_ready) {
                    m_radius = std::clamp(std::abs(data->diameter), 0.05, m_max_radius);
                    m_radius_locked = true;
                    update_fillet();
                    set_first_update_group_current();
                    m_intf.canvas_update_from_tool();
                }
            }
        }
        return ToolResponse();
    }

    switch (args.action) {
    case InToolActionID::LMB: {
        if (!m_line1) {
            select_line(m_line1);
            return ToolResponse();
        }
        if (!m_line2) {
            if (!select_line(m_line2))
                return ToolResponse();
            if (!setup_corner()) {
                m_line1 = nullptr;
                m_line2 = nullptr;
            }
            return ToolResponse();
        }
        if (!m_ready)
            return ToolResponse();

        const auto line1_tangent = m_line1_arc_point1 ? m_preview_arc->m_from : m_preview_arc->m_to;
        const auto line2_tangent = m_line1_arc_point1 ? m_preview_arc->m_to : m_preview_arc->m_from;
        if (m_line1_corner == 0)
            m_line1->m_p1 = line1_tangent;
        else
            m_line1->m_p2 = line1_tangent;
        if (m_line2_corner == 0)
            m_line2->m_p1 = line2_tangent;
        else
            m_line2->m_p2 = line2_tangent;

        // Keep the original sharp corner available as a solver reference
        // when the fillet is made at the workplane origin. It is deliberately
        // hidden; the workplane origin remains the only visible marker.
        if (glm::length(m_corner) < 1e-6) {
            EntityPoint2D *origin_reference = nullptr;
            for (const auto &[uuid, entity] : get_doc().m_entities) {
                const auto *point = dynamic_cast<const EntityPoint2D *>(entity.get());
                if (point && point->m_wrkpl == m_line1->m_wrkpl && !point->m_visible
                    && glm::length(point->m_p) < 1e-6) {
                    origin_reference = const_cast<EntityPoint2D *>(point);
                    break;
                }
            }
            if (!origin_reference) {
                origin_reference = &add_entity<EntityPoint2D>();
                origin_reference->m_wrkpl = m_line1->m_wrkpl;
                origin_reference->m_p = m_corner;
                origin_reference->m_construction = true;
                origin_reference->m_visible = false;
                origin_reference->m_selection_invisible = true;
                auto &origin_constraint = add_constraint<ConstraintPointsCoincident>();
                origin_constraint.m_wrkpl = m_line1->m_wrkpl;
                origin_constraint.m_entity1 = {origin_reference->m_uuid, 0};
                origin_constraint.m_entity2 = {m_line1->m_wrkpl, 1};

                auto &line1_origin = add_constraint<ConstraintPointOnLine>();
                line1_origin.m_wrkpl = m_line1->m_wrkpl;
                line1_origin.m_point = {origin_reference->m_uuid, 0};
                line1_origin.m_line = m_line1->m_uuid;
                auto &line2_origin = add_constraint<ConstraintPointOnLine>();
                line2_origin.m_wrkpl = m_line2->m_wrkpl;
                line2_origin.m_point = {origin_reference->m_uuid, 0};
                line2_origin.m_line = m_line2->m_uuid;
            }
        }
        m_preview_arc->m_selection_invisible = false;
        m_intf.hide_circle_dimension();

        const EntityAndPoint line1_corner{m_line1->m_uuid, static_cast<unsigned int>(m_line1_corner + 1)};
        const EntityAndPoint line2_corner{m_line2->m_uuid, static_cast<unsigned int>(m_line2_corner + 1)};
        for (auto it = get_doc().m_constraints.begin(); it != get_doc().m_constraints.end();) {
            auto coincident = dynamic_cast<ConstraintPointsCoincident *>(it->second.get());
            const auto is_corner_pair = coincident
                                        && ((coincident->m_entity1 == line1_corner
                                             && coincident->m_entity2 == line2_corner)
                                            || (coincident->m_entity1 == line2_corner
                                                && coincident->m_entity2 == line1_corner));
            const auto is_origin_corner = coincident
                                          && ((coincident->m_entity1 == line1_corner
                                               && coincident->m_entity2 == EntityAndPoint{m_line1->m_wrkpl, 1})
                                              || (coincident->m_entity2 == line1_corner
                                                  && coincident->m_entity1 == EntityAndPoint{m_line1->m_wrkpl, 1})
                                              || (coincident->m_entity1 == line2_corner
                                                  && coincident->m_entity2 == EntityAndPoint{m_line2->m_wrkpl, 1})
                                              || (coincident->m_entity2 == line2_corner
                                                  && coincident->m_entity1 == EntityAndPoint{m_line2->m_wrkpl, 1}));
            if (is_corner_pair || is_origin_corner) {
                it = get_doc().m_constraints.erase(it);
                set_current_group_solve_pending();
            }
            else {
                ++it;
            }
        }

        auto &coincident1 = add_constraint<ConstraintPointsCoincident>();
        coincident1.m_wrkpl = m_line1->m_wrkpl;
        coincident1.m_entity1 = line1_corner;
        coincident1.m_entity2 = {m_preview_arc->m_uuid, m_line1_arc_point1 ? 1u : 2u};

        auto &coincident2 = add_constraint<ConstraintPointsCoincident>();
        coincident2.m_wrkpl = m_line2->m_wrkpl;
        coincident2.m_entity1 = line2_corner;
        coincident2.m_entity2 = {m_preview_arc->m_uuid, m_line1_arc_point1 ? 2u : 1u};

        auto &tangent1 = add_constraint<ConstraintArcLineTangent>();
        tangent1.m_arc = {m_preview_arc->m_uuid, m_line1_arc_point1 ? 1u : 2u};
        tangent1.m_line = m_line1->m_uuid;

        auto &tangent2 = add_constraint<ConstraintArcLineTangent>();
        tangent2.m_arc = {m_preview_arc->m_uuid, m_line1_arc_point1 ? 2u : 1u};
        tangent2.m_line = m_line2->m_uuid;
        return ToolResponse::commit();
    }

    case InToolActionID::RMB:
    case InToolActionID::CANCEL:
        m_intf.hide_circle_dimension();
        return ToolResponse::revert();
    default:
        return ToolResponse();
    }
}

} // namespace dune3d
