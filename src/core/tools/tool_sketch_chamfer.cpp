#include "tool_sketch_chamfer.hpp"
#include "document/document.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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
    if (!candidate || candidate->m_wrkpl != get_workplane_uuid() || candidate == line)
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
    m_preview_line->m_selection_invisible = true;
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

        if (m_line1_corner == 0)
            m_line1->m_p1 = m_preview_line->m_p1;
        else
            m_line1->m_p2 = m_preview_line->m_p1;
        if (m_line2_corner == 0)
            m_line2->m_p1 = m_preview_line->m_p2;
        else
            m_line2->m_p2 = m_preview_line->m_p2;
        m_preview_line->m_selection_invisible = false;

        const EntityAndPoint line1_corner{m_line1->m_uuid, static_cast<unsigned int>(m_line1_corner + 1)};
        const EntityAndPoint line2_corner{m_line2->m_uuid, static_cast<unsigned int>(m_line2_corner + 1)};
        for (auto it = get_doc().m_constraints.begin(); it != get_doc().m_constraints.end();) {
            auto coincident = dynamic_cast<ConstraintPointsCoincident *>(it->second.get());
            if (coincident && ((coincident->m_entity1 == line1_corner && coincident->m_entity2 == line2_corner)
                               || (coincident->m_entity1 == line2_corner && coincident->m_entity2 == line1_corner))) {
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
