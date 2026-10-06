#include "group_press_pull.hpp"
#include "nlohmann/json.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupPressPull::GroupPressPull(const UUID &uu) : GroupFaceOperation(uu)
{
}

GroupPressPull::GroupPressPull(const UUID &uu, const json &j)
    : GroupFaceOperation(uu, j), m_offset(j.value("offset", 1.0))
{
}

json GroupPressPull::serialize() const
{
    auto j = GroupFaceOperation::serialize();
    j["offset"] = m_offset;
    return j;
}

std::unique_ptr<Group> GroupPressPull::clone() const
{
    return std::make_unique<GroupPressPull>(*this);
}

void GroupPressPull::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
