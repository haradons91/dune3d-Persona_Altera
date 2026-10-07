#include "tool_sketch_move_copy.hpp"
#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_point2d.hpp"
#include "document/constraint/constraint.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "dialogs/enter_datum_window.hpp"
#include "core/tool_id.hpp"
#include "tool_common_impl.hpp"

namespace dune3d {

static bool entity_movable(Entity *entity, const UUID &wrkpl)
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

ToolBase::CanBegin ToolSketchMoveCopy::can_begin()
{
    if (get_workplane_uuid() == UUID())
        return false;
    for (const auto &sr : m_selection) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        if (entity_movable(&get_entity(sr.item), get_workplane_uuid()))
            return true;
    }
    return false;
}

ToolResponse ToolSketchMoveCopy::begin(const ToolArgs &args)
{
    const auto wrkpl = get_workplane_uuid();
    auto &doc = get_doc();

    std::set<UUID> selected_uuids;
    for (const auto &sr : m_selection) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto *entity = &get_entity(sr.item);
        if (!entity_movable(entity, wrkpl))
            continue;
        selected_uuids.insert(entity->m_uuid);
    }
    if (selected_uuids.empty())
        return ToolResponse::end();

    if (m_tool_id == ToolID::SKETCH_COPY) {
        std::map<UUID, UUID> entity_xlat;
        for (const auto &uu : selected_uuids) {
            auto &en = doc.get_entity(uu);
            auto new_entity = en.clone();
            new_entity->m_uuid = UUID::random();
            new_entity->m_group = m_core.get_current_group();
            new_entity->m_kind = ItemKind::USER;
            entity_xlat.emplace(uu, new_entity->m_uuid);
            m_entities.push_back(new_entity.get());
            doc.m_entities.emplace(new_entity->m_uuid, std::move(new_entity));
        }
        for (const auto &[uu, co] : doc.m_constraints) {
            auto referenced = co->get_referenced_entities_and_points();
            bool all_internal = !referenced.empty();
            for (const auto &enp : referenced) {
                if (!entity_xlat.contains(enp.entity)) {
                    all_internal = false;
                    break;
                }
            }
            if (!all_internal)
                continue;
            auto new_co = co->clone();
            new_co->m_uuid = UUID::random();
            new_co->m_group = m_core.get_current_group();
            for (const auto &enp : referenced)
                new_co->replace_point(enp, {entity_xlat.at(enp.entity), enp.point});
            doc.m_constraints.emplace(new_co->m_uuid, std::move(new_co));
        }
    }
    else {
        for (const auto &uu : selected_uuids)
            m_entities.push_back(&doc.get_entity(uu));
    }

    for (auto *entity : m_entities) {
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

    m_intf.get_dialogs().show_enter_datum_window("Enter X offset", DatumUnit::MM, 0.0);
    m_intf.set_no_canvas_update(true);
    m_intf.canvas_update_from_tool();

    return ToolResponse();
}

void ToolSketchMoveCopy::apply_translation(double dx, double dy)
{
    const glm::dvec2 delta{dx, dy};
    for (auto *entity : m_entities) {
        const auto &orig = m_originals.at(entity);
        if (auto line = dynamic_cast<EntityLine2D *>(entity)) {
            line->m_p1 = orig.at(1) + delta;
            line->m_p2 = orig.at(2) + delta;
        }
        else if (auto arc = dynamic_cast<EntityArc2D *>(entity)) {
            arc->m_center = orig.at(0) + delta;
            arc->m_from = orig.at(1) + delta;
            arc->m_to = orig.at(2) + delta;
        }
        else if (auto circle = dynamic_cast<EntityCircle2D *>(entity)) {
            circle->m_center = orig.at(0) + delta;
        }
        else if (auto point = dynamic_cast<EntityPoint2D *>(entity)) {
            point->m_p = orig.at(0) + delta;
        }
    }
}

ToolResponse ToolSketchMoveCopy::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::UPDATE) {
                if (auto d = dynamic_cast<const ToolDataEnterDatumWindow *>(args.data.get())) {
                    if (m_stage == Stage::X)
                        apply_translation(d->value, 0);
                    else
                        apply_translation(m_dx, d->value);
                    set_current_group_solve_pending();
                    m_core.solve_current();
                    set_first_update_group_current();
                    m_intf.canvas_update_from_tool();
                }
            }
            else if (data->event == ToolDataWindow::Event::OK) {
                if (auto d = dynamic_cast<const ToolDataEnterDatumWindow *>(args.data.get())) {
                    if (m_stage == Stage::X) {
                        m_dx = d->value;
                        apply_translation(m_dx, 0);
                        m_stage = Stage::Y;
                        m_intf.get_dialogs().show_enter_datum_window("Enter Y offset", DatumUnit::MM, 0.0);
                        return ToolResponse();
                    }
                    else {
                        apply_translation(m_dx, d->value);
                        return ToolResponse::commit();
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
