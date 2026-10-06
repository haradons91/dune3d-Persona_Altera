#include "group_align.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

void GroupAlign::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

std::unique_ptr<Group> GroupAlign::clone() const
{
    return std::make_unique<GroupAlign>(*this);
}

} // namespace dune3d
