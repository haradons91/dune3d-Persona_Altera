#include "tool_sketch_offset.hpp"
#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_parallel.hpp"
#include "document/constraint/constraint_diameter_radius.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/rectangle_dimensions_window.hpp"
#include "tool_common_impl.hpp"

#include <algorithm>
#include <cmath>

namespace dune3d {

static bool entity_offsettable(Entity *entity, const UUID &wrkpl)
{
    if (auto line = dynamic_cast<EntityLine2D *>(entity))
        return line->m_wrkpl == wrkpl;
    if (auto arc = dynamic_cast<EntityArc2D *>(entity))
        return arc->m_wrkpl == wrkpl;
    if (auto circle = dynamic_cast<EntityCircle2D *>(entity))
        return circle->m_wrkpl == wrkpl;
    return false;
}

ToolBase::CanBegin ToolSketchOffset::can_begin()
{
    if (get_workplane_uuid() == UUID())
        return false;
    for (const auto &sr : m_selection) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        if (entity_offsettable(&get_entity(sr.item), get_workplane_uuid()))
            return true;
    }
    return false;
}

ToolResponse ToolSketchOffset::begin(const ToolArgs &args)
{
    const auto wrkpl = get_workplane_uuid();
    auto &doc = get_doc();

    std::set<UUID> seen;
    for (const auto &sr : m_selection) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto *entity = &get_entity(sr.item);
        if (!entity_offsettable(entity, wrkpl) || seen.contains(entity->m_uuid))
            continue;
        seen.insert(entity->m_uuid);

        auto new_entity = entity->clone();
        new_entity->m_uuid = UUID::random();
        new_entity->m_group = m_core.get_current_group();
        new_entity->m_kind = ItemKind::USER;
        new_entity->m_selection_invisible = true;
        auto *preview = new_entity.get();
        doc.m_entities.emplace(new_entity->m_uuid, std::move(new_entity));
        if (!m_reference_source)
            m_reference_source = entity;
        m_source_to_preview[entity] = preview;
    }
    if (m_source_to_preview.empty())
        return ToolResponse::end();

    // Unlike the old modal-dialog design (which froze the canvas and
    // explicitly refreshed it only on each dialog OK), this tracks the
    // cursor live every MOVE like ToolSketchFillet -- it needs the
    // canvas's normal per-frame redraw, not suppressed.
    update_offset_ui(true);
    m_intf.canvas_update_from_tool();

    return ToolResponse();
}

double ToolSketchOffset::compute_live_distance(glm::dvec2 cursor, glm::dvec2 &base, glm::dvec2 &tip)
{
    if (auto line = dynamic_cast<EntityLine2D *>(m_reference_source)) {
        const auto d = glm::normalize(line->m_p2 - line->m_p1);
        const glm::dvec2 n(-d.y, d.x);
        base = (line->m_p1 + line->m_p2) / 2.;
        const auto distance = glm::dot(cursor - line->m_p1, n);
        tip = base + n * distance;
        return distance;
    }
    if (auto arc = dynamic_cast<EntityArc2D *>(m_reference_source)) {
        const auto radius = glm::length(arc->m_from - arc->m_center);
        base = arc->m_center;
        const auto to_cursor = cursor - arc->m_center;
        const auto cursor_dist = glm::length(to_cursor);
        const auto dir = cursor_dist > 1e-9 ? to_cursor / cursor_dist : glm::dvec2(1, 0);
        const auto distance = cursor_dist - radius;
        tip = base + dir * (radius + distance);
        return distance;
    }
    if (auto circle = dynamic_cast<EntityCircle2D *>(m_reference_source)) {
        base = circle->m_center;
        const auto to_cursor = cursor - circle->m_center;
        const auto cursor_dist = glm::length(to_cursor);
        const auto dir = cursor_dist > 1e-9 ? to_cursor / cursor_dist : glm::dvec2(1, 0);
        const auto distance = cursor_dist - circle->m_radius;
        tip = base + dir * (circle->m_radius + distance);
        return distance;
    }
    base = cursor;
    tip = cursor;
    return 0;
}

void ToolSketchOffset::apply_offset(double distance)
{
    m_last_distance = distance;
    for (auto &[source, preview] : m_source_to_preview) {
        if (auto line = dynamic_cast<EntityLine2D *>(source)) {
            auto *preview_line = dynamic_cast<EntityLine2D *>(preview);
            const auto d = glm::normalize(line->m_p2 - line->m_p1);
            const glm::dvec2 n(-d.y, d.x);
            preview_line->m_p1 = line->m_p1 + n * distance;
            preview_line->m_p2 = line->m_p2 + n * distance;
        }
        else if (auto arc = dynamic_cast<EntityArc2D *>(source)) {
            auto *preview_arc = dynamic_cast<EntityArc2D *>(preview);
            const auto radius = glm::length(arc->m_from - arc->m_center);
            const auto new_radius = std::max(radius + distance, 1e-3);
            preview_arc->m_center = arc->m_center;
            preview_arc->m_from = arc->m_center + glm::normalize(arc->m_from - arc->m_center) * new_radius;
            preview_arc->m_to = arc->m_center + glm::normalize(arc->m_to - arc->m_center) * new_radius;
        }
        else if (auto circle = dynamic_cast<EntityCircle2D *>(source)) {
            auto *preview_circle = dynamic_cast<EntityCircle2D *>(preview);
            preview_circle->m_center = circle->m_center;
            preview_circle->m_radius = std::max(circle->m_radius + distance, 1e-3);
        }
    }
}

void ToolSketchOffset::update_offset_ui(bool first)
{
    const auto workplane = get_workplane();
    const auto cursor = workplane->project(get_cursor_pos_for_workplane(*workplane));

    glm::dvec2 base{}, tip{};
    const auto live_distance = compute_live_distance(cursor, base, tip);
    const auto distance = m_distance_locked ? m_last_distance : live_distance;
    apply_offset(distance);

    // Only overwrite the displayed text while the user isn't actively
    // typing a value -- same reasoning as ToolSketchFillet's
    // m_radius_locked vs. live mouse-tracked radius. show_offset_dimension
    // (focuses/opens the box) only runs once; later frames just refresh
    // the text via update_offset_dimension.
    if (!m_distance_locked) {
        if (first)
            m_intf.show_offset_dimension(distance);
        else
            m_intf.update_offset_dimension(distance);
    }
    m_intf.position_offset_dimension(workplane->transform(base), workplane->transform(tip));
}

ToolResponse ToolSketchOffset::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        update_offset_ui();
        set_first_update_group_current();
        return ToolResponse();
    }

    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataOffsetDimensionsWindow *>(args.data.get())) {
            m_distance_locked = true;
            apply_offset(data->distance);
            set_current_group_solve_pending();
            m_core.solve_current();
            set_first_update_group_current();
            m_intf.canvas_update_from_tool();
        }
        return ToolResponse();
    }

    if (args.type != ToolEventType::ACTION)
        return ToolResponse();

    switch (args.action) {
    case InToolActionID::LMB: {
        m_intf.hide_offset_dimension();
        for (auto &[source, preview] : m_source_to_preview) {
            preview->m_selection_invisible = false;
            if (dynamic_cast<EntityLine2D *>(source)) {
                auto &constr = add_constraint<ConstraintParallel>();
                constr.m_wrkpl = get_workplane_uuid();
                constr.m_entity1 = source->m_uuid;
                constr.m_entity2 = preview->m_uuid;
            }
            else {
                // No "offset by N from this other entity's radius"
                // constraint exists -- pin the preview's own radius
                // to the value it was just given instead (same
                // ConstraintRadius pattern ToolSketchFillet uses to
                // pin its preview arc's radius after a live
                // preview).
                double source_radius = 0;
                if (auto arc = dynamic_cast<EntityArc2D *>(source))
                    source_radius = glm::length(arc->m_from - arc->m_center);
                else if (auto circle = dynamic_cast<EntityCircle2D *>(source))
                    source_radius = circle->m_radius;
                auto &constr = add_constraint<ConstraintRadius>();
                constr.m_entity = preview->m_uuid;
                constr.m_distance = std::max(source_radius + m_last_distance, 1e-3);
            }
        }
        return ToolResponse::commit();
    }

    case InToolActionID::RMB:
    case InToolActionID::CANCEL:
        m_intf.hide_offset_dimension();
        return ToolResponse::revert();

    default:
        return ToolResponse();
    }
}

} // namespace dune3d
