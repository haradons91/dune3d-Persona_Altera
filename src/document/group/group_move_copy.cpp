#include "group_move_copy.hpp"
#include "nlohmann/json.hpp"
#include "util/glm_util.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupMoveCopy::GroupMoveCopy(const UUID &uu) : Group(uu)
{
}

GroupMoveCopy::GroupMoveCopy(const UUID &uu, const json &j)
    : Group(uu, j), m_translation(j.value("translation", glm::dvec3(0, 0, 0))),
      m_rotation_axis(j.value("rotation_axis", glm::dvec3(0, 0, 1))),
      m_rotation_angle(j.value("rotation_angle", 0.0)), m_copy(j.value("copy", false))
{
}

json GroupMoveCopy::serialize() const
{
    auto j = Group::serialize();
    j["translation"] = m_translation;
    j["rotation_axis"] = m_rotation_axis;
    j["rotation_angle"] = m_rotation_angle;
    j["copy"] = m_copy;
    return j;
}

std::unique_ptr<Group> GroupMoveCopy::clone() const
{
    return std::make_unique<GroupMoveCopy>(*this);
}

std::list<GroupStatusMessage> GroupMoveCopy::get_messages() const
{
    auto msg = Group::get_messages();
    msg.insert(msg.end(), m_move_copy_messages.begin(), m_move_copy_messages.end());
    return msg;
}

const SolidModel *GroupMoveCopy::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupMoveCopy::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
