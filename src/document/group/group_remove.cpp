#include "group_remove.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

std::unique_ptr<Group> GroupRemove::clone() const
{
    return std::make_unique<GroupRemove>(*this);
}

void GroupRemove::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
