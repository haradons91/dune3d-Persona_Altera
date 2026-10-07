#include "tool_sketch_scale.hpp"
#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_point2d.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "dialogs/enter_datum_window.hpp"
#include "tool_common_impl.hpp"

namespace dune3d {

static bool entity_scalable(Entity *entity, const UUID &wrkpl)
{
    if (auto line = dynamic_cast<EntityLine2D *>(entity))
        return line->m_wrkpl == wrkpl;
    if (auto arc = dynamic_cast<EntityArc2D *>(entity))
        return arc->m_wrkpl == wrkpl;
    if (auto circle = dynamic_cast<EntityCircle2D *>(entity))
        return circle->m_wrkpl == wrkpl;
    if (auto point = dynamic_cast<EntityPoint2D *>(entity))
        return point->m_wrkpl == wrkpl;
    return false;
}

ToolBase::CanBegin ToolSketchScale::can_begin()
{
    if (get_workplane_uuid() == UUID())
        return false;
    for (const auto &sr : m_selection) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        if (entity_scalable(&get_entity(sr.item), get_workplane_uuid()))
            return true;
    }
    return false;
}

ToolResponse ToolSketchScale::begin(const ToolArgs &args)
{
    const auto wrkpl = get_workplane_uuid();
    std::set<UUID> seen;
    for (const auto &sr : m_selection) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto *entity = &get_entity(sr.item);
        if (!entity_scalable(entity, wrkpl) || seen.contains(entity->m_uuid))
            continue;
        seen.insert(entity->m_uuid);
        m_entities.push_back(entity);

        auto &orig = m_originals[entity];
        if (auto line = dynamic_cast<EntityLine2D *>(entity)) {
            orig[1] = line->m_p1;
            orig[2] = line->m_p2;
        }
        else if (auto arc = dynamic_cast<EntityArc2D *>(entity)) {
            orig[0] = arc->m_center;
            orig[1] = arc->m_from;
            orig[2] = arc->m_to;
        }
        else if (auto circle = dynamic_cast<EntityCircle2D *>(entity)) {
            orig[0] = circle->m_center;
        }
        else if (auto point = dynamic_cast<EntityPoint2D *>(entity)) {
            orig[0] = point->m_p;
        }
    }
    if (m_entities.empty())
        return ToolResponse::end();

    m_intf.get_dialogs().show_enter_datum_window("Enter scale factor", DatumUnit::RATIO, 1.0);
    m_intf.set_no_canvas_update(true);
    m_intf.canvas_update_from_tool();

    return ToolResponse();
}

void ToolSketchScale::apply_scale(double factor)
{
    for (auto *entity : m_entities) {
        const auto &orig = m_originals.at(entity);
        if (auto line = dynamic_cast<EntityLine2D *>(entity)) {
            line->m_p1 = orig.at(1) * factor;
            line->m_p2 = orig.at(2) * factor;
        }
        else if (auto arc = dynamic_cast<EntityArc2D *>(entity)) {
            arc->m_center = orig.at(0) * factor;
            arc->m_from = orig.at(1) * factor;
            arc->m_to = orig.at(2) * factor;
        }
        else if (auto circle = dynamic_cast<EntityCircle2D *>(entity)) {
            circle->m_center = orig.at(0) * factor;
            circle->m_radius *= factor;
        }
        else if (auto point = dynamic_cast<EntityPoint2D *>(entity)) {
            point->m_p = orig.at(0) * factor;
        }
    }
}

ToolResponse ToolSketchScale::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::UPDATE) {
                if (auto d = dynamic_cast<const ToolDataEnterDatumWindow *>(args.data.get())) {
                    apply_scale(d->value);
                    set_current_group_solve_pending();
                    m_core.solve_current();
                    set_first_update_group_current();
                    m_intf.canvas_update_from_tool();
                }
            }
            else if (data->event == ToolDataWindow::Event::OK) {
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
