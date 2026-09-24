#include "occurrence_path.hpp"
#include "document.hpp"
#include "component.hpp"
#include "entity/entity_occurrence.hpp"
#include <glm/gtx/quaternion.hpp>

namespace dune3d {

ResolvedLocation resolve_occurrence_path(Document &root, const std::vector<UUID> &path)
{
    Document *cur = &root;
    Component *component = nullptr;
    glm::dvec3 origin = {0, 0, 0};
    glm::dquat rot = glm::quat_identity<double, glm::defaultp>();

    for (const auto &uu : path) {
        const auto &occ = cur->get_entity<EntityOccurrence>(uu);
        auto &comp = root.get_component<Component>(occ.m_component);
        origin = origin + glm::rotate(rot, occ.m_origin);
        rot = rot * occ.m_normal;
        cur = &comp.m_document;
        component = &comp;
    }

    return {*cur, component, origin, rot};
}

} // namespace dune3d
