#include "group_scale.hpp"
#include "nlohmann/json.hpp"
#include "util/glm_util.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupScale::GroupScale(const UUID &uu) : Group(uu)
{
}

GroupScale::GroupScale(const UUID &uu, const json &j)
    : Group(uu, j), m_factor(j.value("factor", 1.0)), m_center(j.value("center", glm::dvec3(0, 0, 0)))
{
}

json GroupScale::serialize() const
{
    auto j = Group::serialize();
    j["factor"] = m_factor;
    j["center"] = m_center;
    return j;
}

std::unique_ptr<Group> GroupScale::clone() const
{
    return std::make_unique<GroupScale>(*this);
}

std::list<GroupStatusMessage> GroupScale::get_messages() const
{
    auto msg = Group::get_messages();
    msg.insert(msg.end(), m_scale_messages.begin(), m_scale_messages.end());
    return msg;
}

const SolidModel *GroupScale::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupScale::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
