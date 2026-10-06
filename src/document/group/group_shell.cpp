#include "group_shell.hpp"
#include "nlohmann/json.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupShell::GroupShell(const UUID &uu) : GroupFaceOperation(uu)
{
}

GroupShell::GroupShell(const UUID &uu, const json &j)
    : GroupFaceOperation(uu, j), m_thickness(j.value("thickness", 1.0))
{
}

json GroupShell::serialize() const
{
    auto j = GroupFaceOperation::serialize();
    j["thickness"] = m_thickness;
    return j;
}

std::unique_ptr<Group> GroupShell::clone() const
{
    return std::make_unique<GroupShell>(*this);
}

void GroupShell::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
