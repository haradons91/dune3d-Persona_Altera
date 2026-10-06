#include "group_split_face.hpp"
#include "nlohmann/json.hpp"
#include "util/glm_util.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupSplitFace::GroupSplitFace(const UUID &uu) : GroupFaceOperation(uu)
{
}

GroupSplitFace::GroupSplitFace(const UUID &uu, const json &j)
    : GroupFaceOperation(uu, j), m_plane_point(j.value("plane_point", glm::dvec3(0, 0, 0))),
      m_plane_normal(j.value("plane_normal", glm::dvec3(0, 0, 1)))
{
}

json GroupSplitFace::serialize() const
{
    auto j = GroupFaceOperation::serialize();
    j["plane_point"] = m_plane_point;
    j["plane_normal"] = m_plane_normal;
    return j;
}

std::unique_ptr<Group> GroupSplitFace::clone() const
{
    return std::make_unique<GroupSplitFace>(*this);
}

void GroupSplitFace::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
