#include "group_sketch.hpp"
#include "nlohmann/json.hpp"
#include "document/solid_model/solid_model.hpp"
#include "igroup_solid_model_json.hpp"

namespace dune3d {
GroupSketch::GroupSketch(const UUID &uu) : Group(uu)
{
}

GroupSketch::GroupSketch(const UUID &uu, const json &j)
    : Group(uu, j), m_attached_to_face(j.value("attached_to_face", false)),
      m_operation(j.value("operation", Operation::UNION))
{
}

json GroupSketch::serialize() const
{
    auto j = Group::serialize();
    j["operation"] = m_operation;
    if (m_attached_to_face)
        j["attached_to_face"] = true;
    return j;
}

std::unique_ptr<Group> GroupSketch::clone() const
{
    return std::make_unique<GroupSketch>(*this);
}

const SolidModel *GroupSketch::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupSketch::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
