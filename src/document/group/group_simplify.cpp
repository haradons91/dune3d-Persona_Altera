#include "group_simplify.hpp"
#include "nlohmann/json.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupSimplify::GroupSimplify(const UUID &uu) : Group(uu)
{
}

GroupSimplify::GroupSimplify(const UUID &uu, const json &j) : Group(uu, j)
{
}

json GroupSimplify::serialize() const
{
    return Group::serialize();
}

std::unique_ptr<Group> GroupSimplify::clone() const
{
    return std::make_unique<GroupSimplify>(*this);
}

std::list<GroupStatusMessage> GroupSimplify::get_messages() const
{
    auto msg = Group::get_messages();
    msg.insert(msg.end(), m_simplify_messages.begin(), m_simplify_messages.end());
    return msg;
}

const SolidModel *GroupSimplify::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupSimplify::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
