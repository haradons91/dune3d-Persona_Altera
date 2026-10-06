#include "group_split_body.hpp"
#include "nlohmann/json.hpp"
#include "util/glm_util.hpp"
#include "util/json_util.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupSplitBody::GroupSplitBody(const UUID &uu) : Group(uu)
{
}

GroupSplitBody::GroupSplitBody(const UUID &uu, const json &j)
    : Group(uu, j), m_plane_point(j.value("plane_point", glm::dvec3(0, 0, 0))),
      m_plane_normal(j.value("plane_normal", glm::dvec3(0, 0, 1))),
      m_result_group(j.at("result_group").get<UUID>())
{
}

json GroupSplitBody::serialize() const
{
    auto j = Group::serialize();
    j["plane_point"] = m_plane_point;
    j["plane_normal"] = m_plane_normal;
    j["result_group"] = m_result_group;
    return j;
}

std::unique_ptr<Group> GroupSplitBody::clone() const
{
    return std::make_unique<GroupSplitBody>(*this);
}

std::list<GroupStatusMessage> GroupSplitBody::get_messages() const
{
    auto msg = Group::get_messages();
    msg.insert(msg.end(), m_split_body_messages.begin(), m_split_body_messages.end());
    return msg;
}

const SolidModel *GroupSplitBody::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupSplitBody::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

std::set<UUID> GroupSplitBody::get_required_groups(const Document &doc) const
{
    return {m_result_group};
}

} // namespace dune3d
