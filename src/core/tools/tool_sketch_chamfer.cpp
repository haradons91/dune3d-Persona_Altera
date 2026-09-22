#include "tool_sketch_chamfer.hpp"
#include "document/document.hpp"
#include "document/constraint/constraint_point_distance.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_point2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include "util/debug.hpp"

namespace dune3d {

ToolBase::CanBegin ToolSketchChamfer::can_begin()
{
    return get_workplane_uuid() != UUID();
}

ToolResponse ToolSketchChamfer::begin(const ToolArgs &args)
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

bool ToolSketchChamfer::select_line(EntityLine2D *&line)
{
    const auto hover = m_intf.get_hover_selection();
    if (!hover || hover->type != SelectableRef::Type::ENTITY)
        return false;
    auto *candidate = dynamic_cast<EntityLine2D *>(&get_doc().get_entity(hover->item));
    if (!candidate || candidate->m_wrkpl != get_workplane_uuid() || candidate == line || candidate == m_line1
        || candidate == m_line2 || candidate == m_preview_line)
        return false;
    line = candidate;
    return true;
}

bool ToolSketchChamfer::setup_corner()
{
    const std::array<glm::dvec2, 2> line1_points = {m_line1->m_p1, m_line1->m_p2};
    const std::array<glm::dvec2, 2> line2_points = {m_line2->m_p1, m_line2->m_p2};
    double best_distance = 1e-4;
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
    const auto line1_vector = line1_points[1 - corner1] - m_corner;
    const auto line2_vector = line2_points[1 - corner2] - m_corner;
    const auto line1_length = glm::length(line1_vector);
    const auto line2_length = glm::length(line2_vector);
    if (line1_length < 1e-6 || line2_length < 1e-6)
        return false;

    m_line1_direction = line1_vector / line1_length;
    m_line2_direction = line2_vector / line2_length;
    m_max_distance = std::min(line1_length, line2_length) * 0.95;
    m_preview_line = &add_entity<EntityLine2D>();
    // Render the preview with the same line style as committed sketch
    // geometry.  Tool selection explicitly ignores this preview line below.
    m_preview_line->m_selection_invisible = false;
    m_preview_line->m_wrkpl = m_line1->m_wrkpl;
    update_chamfer();
    return true;
}

double ToolSketchChamfer::get_distance()
{
    const auto workplane = get_workplane();
    const auto cursor = workplane->project(get_cursor_pos_for_workplane(*workplane));
    return std::clamp(glm::length(cursor - m_corner), 0.05, m_max_distance);
}

void ToolSketchChamfer::update_chamfer()
{
    if (!m_preview_line)
        return;
    const auto distance = get_distance();
    m_preview_line->m_p1 = m_corner + m_line1_direction * distance;
    m_preview_line->m_p2 = m_corner + m_line2_direction * distance;
}

ToolResponse ToolSketchChamfer::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        update_chamfer();
        set_first_update_group_current();
        return ToolResponse();
    }
    if (args.type != ToolEventType::ACTION)
        return ToolResponse();

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
        if (!m_preview_line)
            return ToolResponse();

        debug_log(DebugCategory::MODEL,
                  "commit line1=" + static_cast<std::string>(m_line1->m_uuid)
                          + " line2=" + static_cast<std::string>(m_line2->m_uuid)
                          + " preview=" + static_cast<std::string>(m_preview_line->m_uuid)
                          + " p1_before=" + std::to_string(m_line1->m_p1.x) + ","
                          + std::to_string(m_line1->m_p1.y) + " p2_before=" + std::to_string(m_line1->m_p2.x) + ","
                          + std::to_string(m_line1->m_p2.y) + " preview_p1=" + std::to_string(m_preview_line->m_p1.x)
                          + "," + std::to_string(m_preview_line->m_p1.y)
                          + " preview_p2=" + std::to_string(m_preview_line->m_p2.x) + ","
                          + std::to_string(m_preview_line->m_p2.y));

        if (m_line1_corner == 0)
            m_line1->m_p1 = m_preview_line->m_p1;
        else
            m_line1->m_p2 = m_preview_line->m_p1;
        if (m_line2_corner == 0)
            m_line2->m_p1 = m_preview_line->m_p2;
        else
            m_line2->m_p2 = m_preview_line->m_p2;
        m_preview_line->m_selection_invisible = false;

        // Keep the original sharp corner available as a solver reference
        // when the chamfer is made at the workplane origin. It is
        // deliberately hidden; the workplane origin remains the only
        // visible marker.
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

        const EntityAndPoint line1_corner{m_line1->m_uuid, static_cast<unsigned int>(m_line1_corner + 1)};
        const EntityAndPoint line2_corner{m_line2->m_uuid, static_cast<unsigned int>(m_line2_corner + 1)};

        // Rectangle dimensions are commonly represented by the two endpoints
        // of one edge. Once that edge is trimmed, keeping the old corner in
        // the dimension would force the solver to restore the sharp corner.
        // Move the affected dimension endpoint to the adjacent edge corner;
        // the dimension still measures the full rectangle width/height while
        // the chamfer is free to occupy the corner.
        for (auto &[uuid, constraint] : get_doc().m_constraints) {
            auto *distance = dynamic_cast<ConstraintPointDistanceBase *>(constraint.get());
            if (!distance)
                continue;
            auto replace_corner = [&](EntityAndPoint &point) {
                if (point == line1_corner)
                    point = line2_corner;
                else if (point == line2_corner)
                    point = line1_corner;
            };
            replace_corner(distance->m_entity1);
            replace_corner(distance->m_entity2);
        }

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
        coincident1.m_entity2 = {m_preview_line->m_uuid, 1};
        auto &coincident2 = add_constraint<ConstraintPointsCoincident>();
        coincident2.m_wrkpl = m_line2->m_wrkpl;
        coincident2.m_entity1 = line2_corner;
        coincident2.m_entity2 = {m_preview_line->m_uuid, 2};
        // The chamfer changes existing line endpoints, creates a new line,
        // and adds constraints. Mark the group for generation so the commit
        // rebuild preserves both the trimmed edges and the new chamfer line.
        set_current_group_generate_pending();
        debug_log(DebugCategory::MODEL,
                  "commit endpoints line1=" + std::to_string(m_line1->m_p1.x) + "," + std::to_string(m_line1->m_p1.y)
                          + " / " + std::to_string(m_line1->m_p2.x) + "," + std::to_string(m_line1->m_p2.y)
                          + " line2=" + std::to_string(m_line2->m_p1.x) + "," + std::to_string(m_line2->m_p1.y)
                          + " / " + std::to_string(m_line2->m_p2.x) + "," + std::to_string(m_line2->m_p2.y));
        return ToolResponse::commit();
    }
    case InToolActionID::RMB:
    case InToolActionID::CANCEL:
        return ToolResponse::revert();
    default:
        return ToolResponse();
    }
}

} // namespace dune3d
