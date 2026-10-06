#include "group_draft.hpp"
#include "nlohmann/json.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupDraft::GroupDraft(const UUID &uu) : GroupFaceOperation(uu)
{
}

GroupDraft::GroupDraft(const UUID &uu, const json &j) : GroupFaceOperation(uu, j), m_angle(j.value("angle", 5.0))
{
}

json GroupDraft::serialize() const
{
    auto j = GroupFaceOperation::serialize();
    j["angle"] = m_angle;
    return j;
}

std::unique_ptr<Group> GroupDraft::clone() const
{
    return std::make_unique<GroupDraft>(*this);
}

void GroupDraft::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
