#include "group_face_operation.hpp"
#include "nlohmann/json.hpp"
#include "util/util.hpp"

namespace dune3d {

GroupFaceOperation::GroupFaceOperation(const UUID &uu) : Group(uu)
{
}

GroupFaceOperation::GroupFaceOperation(const UUID &uu, const json &j)
    : Group(uu, j), m_faces(j.at("faces").get<std::set<unsigned int>>())
{
}

json GroupFaceOperation::serialize() const
{
    auto j = Group::serialize();
    j["faces"] = m_faces;
    return j;
}

std::list<GroupStatusMessage> GroupFaceOperation::get_messages() const
{
    auto msg = Group::get_messages();
    msg.insert(msg.end(), m_face_operation_messages.begin(), m_face_operation_messages.end());
    return msg;
}

const SolidModel *GroupFaceOperation::get_solid_model() const
{
    return m_solid_model.get();
}

} // namespace dune3d
