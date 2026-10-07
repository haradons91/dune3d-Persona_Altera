#include "tool_sketch_offset.hpp"
#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/constraint/constraint_parallel.hpp"
#include "document/constraint/constraint_diameter_radius.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "dialogs/enter_datum_window.hpp"
#include "tool_common_impl.hpp"

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
        m_source_to_preview[entity] = preview;
    }
    if (m_source_to_preview.empty())
        return ToolResponse::end();

    m_intf.get_dialogs().show_enter_datum_window("Enter offset distance", DatumUnit::MM, 1.0);
    m_intf.set_no_canvas_update(true);
    m_intf.canvas_update_from_tool();

    return ToolResponse();
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

ToolResponse ToolSketchOffset::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::UPDATE) {
                if (auto d = dynamic_cast<const ToolDataEnterDatumWindow *>(args.data.get())) {
                    apply_offset(d->value);
                    set_current_group_solve_pending();
                    m_core.solve_current();
                    set_first_update_group_current();
                    m_intf.canvas_update_from_tool();
                }
            }
            else if (data->event == ToolDataWindow::Event::OK) {
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
            else if (data->event == ToolDataWindow::Event::CLOSE) {
                return ToolResponse::revert();
            }
        }
    }
    return ToolResponse();
}

} // namespace dune3d
