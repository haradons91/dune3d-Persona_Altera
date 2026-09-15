#include "tool_project_sketch_geometry.hpp"

#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_bezier2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/ientity_in_workplane_set.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"

namespace dune3d {

ToolBase::CanBegin ToolProjectSketchGeometry::can_begin()
{
    return get_workplane_uuid() != UUID() && !m_selection.empty();
}

ToolResponse ToolProjectSketchGeometry::begin(const ToolArgs &args)
{
    const auto workplane = get_workplane_uuid();
    auto &doc = get_doc();
    unsigned int copied = 0;

    for (const auto &selection : m_selection) {
        if (selection.type != SelectableRef::Type::ENTITY)
            continue;
        const auto &source = doc.get_entity(selection.item);
        if (!source.of_type(Entity::Type::LINE_2D, Entity::Type::ARC_2D, Entity::Type::BEZIER_2D,
                            Entity::Type::CIRCLE_2D))
            continue;
        auto copy = source.clone();
        copy->m_uuid = UUID::random();
        copy->m_group = m_core.get_current_group();
        copy->m_kind = ItemKind::USER;
        if (auto in_workplane = dynamic_cast<IEntityInWorkplaneSet *>(copy.get()))
            in_workplane->set_workplane(workplane);
        else
            continue;
        doc.m_entities.emplace(copy->m_uuid, std::move(copy));
        copied++;
    }

    if (copied == 0)
        return ToolResponse::end();
    return ToolResponse::commit();
}

ToolResponse ToolProjectSketchGeometry::update(const ToolArgs &args)
{
    return ToolResponse();
}

} // namespace dune3d
