#include "tool_sketch_break.hpp"
#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_point2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "editor/editor_interface.hpp"
#include "util/arc_util.hpp"
#include "tool_common_impl.hpp"

#include <algorithm>
#include <cmath>

namespace dune3d {

ToolBase::CanBegin ToolSketchBreak::can_begin()
{
    return get_workplane_uuid() != UUID();
}

ToolResponse ToolSketchBreak::begin(const ToolArgs &args)
{
    m_intf.enable_hover_selection();
    return ToolResponse();
}

glm::dvec2 ToolSketchBreak::compute_break_point(Entity &entity)
{
    const auto workplane = get_workplane();
    const auto cursor = workplane->project(get_cursor_pos_for_workplane(*workplane));
    if (auto line = dynamic_cast<EntityLine2D *>(&entity)) {
        const auto d = line->m_p2 - line->m_p1;
        const auto len2 = glm::dot(d, d);
        auto t = len2 > 1e-12 ? glm::dot(cursor - line->m_p1, d) / len2 : 0.0;
        t = std::clamp(t, 0.02, 0.98);
        return line->m_p1 + d * t;
    }
    else if (auto arc = dynamic_cast<EntityArc2D *>(&entity)) {
        const auto radius = glm::length(arc->m_from - arc->m_center);
        const auto a0 = c2pi(angle(arc->m_from - arc->m_center));
        const auto a1 = c2pi(angle(arc->m_to - arc->m_center));
        const auto dphi = c2pi(a1 - a0);
        const auto ac = c2pi(angle(cursor - arc->m_center));
        const auto da = c2pi(ac - a0);
        // Fraction along the arc's own sweep (0 at m_from, 1 at m_to) --
        // outside that sweep (the cursor is over the "missing" part of
        // the circle), clamp to whichever end of the sweep is nearer.
        double t;
        if (da <= dphi)
            t = dphi > 1e-9 ? da / dphi : 0.0;
        else
            t = (da - dphi) <= (2 * M_PI - da) ? 1.0 : 0.0;
        t = std::clamp(t, 0.02, 0.98);
        return arc->m_center + euler(radius, a0 + t * dphi);
    }
    return cursor;
}

bool ToolSketchBreak::update_preview()
{
    const auto hover = m_intf.get_hover_selection();

    // Unconditional every frame (not nested inside the "something is
    // hovered" branch below): this is what drives the on-canvas
    // crosshair (sets Editor's snap-indicator position as a side
    // effect). Calling it only when an entity was hovered meant the
    // crosshair froze in place whenever the cursor drifted off a curve,
    // instead of continuously tracking the mouse -- same bug Trim/Extend
    // hit and fixed this same way.
    const auto workplane = get_workplane();
    if (!workplane)
        return false;
    get_cursor_pos_for_workplane(*workplane);

    const UUID wrkpl = get_workplane_uuid();
    Entity *entity = nullptr;
    if (hover && hover->type == SelectableRef::Type::ENTITY) {
        auto &candidate = get_entity(hover->item);
        if (auto line = dynamic_cast<EntityLine2D *>(&candidate)) {
            if (line->m_wrkpl == wrkpl)
                entity = &candidate;
        }
        else if (auto arc = dynamic_cast<EntityArc2D *>(&candidate)) {
            if (arc->m_wrkpl == wrkpl)
                entity = &candidate;
        }
    }

    if (!entity) {
        m_hovered_entity = nullptr;
        if (m_preview)
            m_preview->m_visible = false;
        return false;
    }

    m_hovered_entity = entity;
    m_break_point = compute_break_point(*entity);

    if (!m_preview) {
        m_preview = &add_entity<EntityPoint2D>();
        m_preview->m_wrkpl = wrkpl;
        m_preview->m_construction = true;
        m_preview->m_selection_invisible = true;
    }
    m_preview->m_visible = true;
    m_preview->m_p = m_break_point;
    return true;
}

ToolResponse ToolSketchBreak::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        update_preview();
        set_first_update_group_current();
        return ToolResponse();
    }

    if (args.type != ToolEventType::ACTION)
        return ToolResponse();

    switch (args.action) {
    case InToolActionID::LMB: {
        if (!m_hovered_entity)
            return ToolResponse();

        const auto break_point = m_break_point;
        const auto wrkpl = get_workplane_uuid();

        if (m_preview)
            get_doc().m_entities.erase(m_preview->m_uuid);

        if (auto line = dynamic_cast<EntityLine2D *>(m_hovered_entity)) {
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
        else if (auto arc = dynamic_cast<EntityArc2D *>(m_hovered_entity)) {
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
        if (m_preview)
            get_doc().m_entities.erase(m_preview->m_uuid);
        return ToolResponse::revert();

    default:
        return ToolResponse();
    }
}

} // namespace dune3d
