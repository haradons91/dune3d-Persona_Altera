#include "group_split_body_result.hpp"
#include "nlohmann/json.hpp"
#include "util/json_util.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupSplitBodyResult::GroupSplitBodyResult(const UUID &uu) : Group(uu)
{
}

GroupSplitBodyResult::GroupSplitBodyResult(const UUID &uu, const json &j)
    : Group(uu, j), m_source_group(j.at("source_group").get<UUID>())
{
}

json GroupSplitBodyResult::serialize() const
{
    auto j = Group::serialize();
    j["source_group"] = m_source_group;
    return j;
}

std::unique_ptr<Group> GroupSplitBodyResult::clone() const
{
    return std::make_unique<GroupSplitBodyResult>(*this);
}

std::list<GroupStatusMessage> GroupSplitBodyResult::get_messages() const
{
    auto msg = Group::get_messages();
    msg.insert(msg.end(), m_split_body_messages.begin(), m_split_body_messages.end());
    return msg;
}

const SolidModel *GroupSplitBodyResult::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupSplitBodyResult::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
