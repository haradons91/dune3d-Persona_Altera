#include "group_occurrence.hpp"
#include "nlohmann/json.hpp"
#include "util/util.hpp"
#include "util/json_util.hpp"
#include "document/document.hpp"
#include "document/entity/entity_occurrence.hpp"
#include <glm/gtc/quaternion.hpp>

namespace dune3d {
GroupOccurrence::GroupOccurrence(const UUID &uu) : Group(uu)
{
}

GroupOccurrence::GroupOccurrence(const UUID &uu, const json &j)
    : Group(uu, j), m_component(j.at("component").get<UUID>())
{
}

json GroupOccurrence::serialize() const
{
    auto j = Group::serialize();
    j["component"] = m_component;
    return j;
}

UUID GroupOccurrence::get_entity_uuid() const
{
    return hash_uuids("6a6e8f0e-6f19-4b16-90ec-3f6b0d8f7e0a", {m_uuid});
}

void GroupOccurrence::generate(Document &doc)
{
    bool added = false;
    auto &occ = doc.get_or_add_entity<EntityOccurrence>(get_entity_uuid(), &added);
    occ.m_kind = ItemKind::GENRERATED;
    occ.m_group = m_uuid;
    occ.m_component = m_component;
    if (added) {
        occ.m_origin = {0, 0, 0};
        occ.m_normal = glm::quat_identity<double, glm::defaultp>();
    }
}

std::unique_ptr<Group> GroupOccurrence::clone() const
{
    return std::make_unique<GroupOccurrence>(*this);
}

} // namespace dune3d
