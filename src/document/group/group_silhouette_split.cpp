#include "group_silhouette_split.hpp"
#include "nlohmann/json.hpp"
#include "util/glm_util.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupSilhouetteSplit::GroupSilhouetteSplit(const UUID &uu) : GroupFaceOperation(uu)
{
}

GroupSilhouetteSplit::GroupSilhouetteSplit(const UUID &uu, const json &j)
    : GroupFaceOperation(uu, j), m_direction(j.value("direction", glm::dvec3(0, 0, -1)))
{
}

json GroupSilhouetteSplit::serialize() const
{
    auto j = GroupFaceOperation::serialize();
    j["direction"] = m_direction;
    return j;
}

std::unique_ptr<Group> GroupSilhouetteSplit::clone() const
{
    return std::make_unique<GroupSilhouetteSplit>(*this);
}

void GroupSilhouetteSplit::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
