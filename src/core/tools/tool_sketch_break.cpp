#include "tool_sketch_break.hpp"
#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"

#include <algorithm>
#include <sstream>

namespace dune3d {

static Entity *breakable_selected_entity(Document &doc, const std::set<SelectableRef> &sel, const UUID &wrkpl)
{
    Entity *found = nullptr;
    for (const auto &sr : sel) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto *entity = &doc.get_entity(sr.item);
        bool ok = false;
        if (auto line = dynamic_cast<EntityLine2D *>(entity))
            ok = line->m_wrkpl == wrkpl;
        else if (auto arc = dynamic_cast<EntityArc2D *>(entity))
            ok = arc->m_wrkpl == wrkpl;
        if (!ok)
            continue;
        if (found && found != entity)
            return nullptr; // more than one distinct candidate -- ambiguous
        found = entity;
    }
    return found;
}

ToolBase::CanBegin ToolSketchBreak::can_begin()
{
    if (get_workplane_uuid() == UUID())
        return false;
    return breakable_selected_entity(get_doc(), m_selection, get_workplane_uuid()) != nullptr;
}

ToolResponse ToolSketchBreak::begin(const ToolArgs &args)
{
    m_entity = breakable_selected_entity(get_doc(), m_selection, get_workplane_uuid());
    if (!m_entity)
        return ToolResponse::end();
    return ToolResponse();
}

glm::dvec2 ToolSketchBreak::compute_break_point()
{
    const auto workplane = get_workplane();
    const auto cursor = workplane->project(get_cursor_pos_for_workplane(*workplane));
    if (auto line = dynamic_cast<EntityLine2D *>(m_entity)) {
        const auto d = line->m_p2 - line->m_p1;
        const auto len2 = glm::dot(d, d);
        auto t = len2 > 1e-12 ? glm::dot(cursor - line->m_p1, d) / len2 : 0.0;
        t = std::clamp(t, 0.02, 0.98);
        return line->m_p1 + d * t;
    }
    else if (auto arc = dynamic_cast<EntityArc2D *>(m_entity)) {
        const auto radius = glm::length(arc->m_from - arc->m_center);
        const auto dir_len = glm::length(cursor - arc->m_center);
        if (dir_len < 1e-9)
            return arc->m_from;
        return arc->m_center + (cursor - arc->m_center) / dir_len * radius;
    }
    return cursor;
}

ToolResponse ToolSketchBreak::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        m_break_point = compute_break_point();
        std::ostringstream os;
        os << "break at " << m_break_point.x << ", " << m_break_point.y;
        m_intf.tool_bar_set_tool_tip(os.str());
        return ToolResponse();
    }

    if (args.type != ToolEventType::ACTION)
        return ToolResponse();

    switch (args.action) {
    case InToolActionID::LMB: {
        const auto break_point = compute_break_point();
        const auto wrkpl = get_workplane_uuid();

        if (auto line = dynamic_cast<EntityLine2D *>(m_entity)) {
            const auto far_point = line->m_p2;
            line->m_p2 = break_point;

            auto &new_line = add_entity<EntityLine2D>();
            new_line.m_wrkpl = wrkpl;
            new_line.m_p1 = break_point;
            new_line.m_p2 = far_point;

            for (auto &[uu, constraint] : get_doc().m_constraints)
                constraint->replace_point({line->m_uuid, 2}, {new_line.m_uuid, 2});

            auto &coincident = add_constraint<ConstraintPointsCoincident>();
            coincident.m_wrkpl = wrkpl;
            coincident.m_entity1 = {line->m_uuid, 2};
            coincident.m_entity2 = {new_line.m_uuid, 1};
        }
        else if (auto arc = dynamic_cast<EntityArc2D *>(m_entity)) {
            const auto far_point = arc->m_to;
            arc->m_to = break_point;

            auto &new_arc = add_entity<EntityArc2D>();
            new_arc.m_wrkpl = wrkpl;
            new_arc.m_center = arc->m_center;
            new_arc.m_from = break_point;
            new_arc.m_to = far_point;

            for (auto &[uu, constraint] : get_doc().m_constraints)
                constraint->replace_point({arc->m_uuid, 2}, {new_arc.m_uuid, 2});

            auto &coincident = add_constraint<ConstraintPointsCoincident>();
            coincident.m_wrkpl = wrkpl;
            coincident.m_entity1 = {arc->m_uuid, 2};
            coincident.m_entity2 = {new_arc.m_uuid, 1};
        }
        else {
            return ToolResponse::revert();
        }

        set_current_group_solve_pending();
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
