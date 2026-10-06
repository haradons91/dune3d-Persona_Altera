#include "group_replace_face.hpp"
#include "nlohmann/json.hpp"
#include "util/json_util.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupReplaceFace::GroupReplaceFace(const UUID &uu) : GroupFaceOperation(uu)
{
}

GroupReplaceFace::GroupReplaceFace(const UUID &uu, const json &j)
    : GroupFaceOperation(uu, j), m_reference_wrkpl(j.value("reference_wrkpl", UUID()))
{
}

json GroupReplaceFace::serialize() const
{
    auto j = GroupFaceOperation::serialize();
    j["reference_wrkpl"] = m_reference_wrkpl;
    return j;
}

std::unique_ptr<Group> GroupReplaceFace::clone() const
{
    return std::make_unique<GroupReplaceFace>(*this);
}

void GroupReplaceFace::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

std::set<UUID> GroupReplaceFace::get_referenced_entities(const Document &doc) const
{
    auto r = GroupFaceOperation::get_referenced_entities(doc);
    if (m_reference_wrkpl)
        r.insert(m_reference_wrkpl);
    return r;
}

std::set<UUID> GroupReplaceFace::get_required_entities(const Document &doc) const
{
    auto r = GroupFaceOperation::get_required_entities(doc);
    if (m_reference_wrkpl)
        r.insert(m_reference_wrkpl);
    return r;
}

} // namespace dune3d
