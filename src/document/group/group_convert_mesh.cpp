#include "group_convert_mesh.hpp"
#include "nlohmann/json.hpp"
#include "util/json_util.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

NLOHMANN_JSON_SERIALIZE_ENUM(GroupConvertMesh::Algorithm,
                             {
                                     {GroupConvertMesh::Algorithm::DIRECT, "direct"},
                                     {GroupConvertMesh::Algorithm::WELD_SEW, "weld_sew"},
                                     {GroupConvertMesh::Algorithm::DECIMATE_SEW, "decimate_sew"},
                             })

GroupConvertMesh::GroupConvertMesh(const UUID &uu) : Group(uu)
{
}

GroupConvertMesh::GroupConvertMesh(const UUID &uu, const json &j)
    : Group(uu, j), m_source_group(j.at("source_group").get<UUID>()),
      m_algorithm(j.value("algorithm", Algorithm::DIRECT)),
      m_decimate_target_faces(j.value("decimate_target_faces", 1500)),
      m_weld_tolerance(j.value("weld_tolerance", 0.01))
{
}

json GroupConvertMesh::serialize() const
{
    auto j = Group::serialize();
    j["source_group"] = m_source_group;
    j["algorithm"] = m_algorithm;
    j["decimate_target_faces"] = m_decimate_target_faces;
    j["weld_tolerance"] = m_weld_tolerance;
    return j;
}

std::unique_ptr<Group> GroupConvertMesh::clone() const
{
    return std::make_unique<GroupConvertMesh>(*this);
}

std::set<UUID> GroupConvertMesh::get_referenced_groups(const Document &doc) const
{
    auto r = Group::get_referenced_groups(doc);
    r.insert(m_source_group);
    return r;
}

std::set<UUID> GroupConvertMesh::get_required_groups(const Document &doc) const
{
    return {m_source_group};
}

const SolidModel *GroupConvertMesh::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupConvertMesh::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d
