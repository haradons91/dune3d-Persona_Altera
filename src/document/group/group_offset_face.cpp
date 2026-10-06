#include "group_offset_face.hpp"
#include "nlohmann/json.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupOffsetFace::GroupOffsetFace(const UUID &uu) : GroupFaceOperation(uu)
{
}

GroupOffsetFace::GroupOffsetFace(const UUID &uu, const json &j)
    : GroupFaceOperation(uu, j), m_offset(j.value("offset", 1.0))
{
}

json GroupOffsetFace::serialize() const
{
    auto j = GroupFaceOperation::serialize();
    j["offset"] = m_offset;
    return j;
}

std::unique_ptr<Group> GroupOffsetFace::clone() const
{
    return std::make_unique<GroupOffsetFace>(*this);
}

void GroupOffsetFace::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
