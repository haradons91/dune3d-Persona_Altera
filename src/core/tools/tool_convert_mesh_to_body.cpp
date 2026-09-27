#include "tool_convert_mesh_to_body.hpp"
#include "document/document.hpp"
#include "document/group/group_stl.hpp"
#include "document/group/group_threemf.hpp"
#include "document/group/group_convert_mesh.hpp"
#include "core/tool_data_convert_mesh_to_body.hpp"
#include "tool_common_impl.hpp"

namespace dune3d {

ToolBase::CanBegin ToolConvertMeshToBody::can_begin()
{
    auto &group = get_group();
    return dynamic_cast<GroupSTL *>(&group) || dynamic_cast<GroupThreeMF *>(&group);
}

ToolResponse ToolConvertMeshToBody::begin(const ToolArgs &args)
{
    auto &doc = get_doc();
    const auto source_group_uu = m_core.get_current_group();

    auto &group = doc.insert_group<GroupConvertMesh>(UUID::random(), source_group_uu);
    group.m_source_group = source_group_uu;
    group.m_body.emplace();
    group.m_name = doc.find_next_group_name(Group::Type::CONVERT_MESH);

    if (auto data = dynamic_cast<ToolDataConvertMeshToBody *>(args.data.get()))
        group.m_algorithm = data->algorithm;

    doc.set_group_generate_pending(group.m_uuid);
    return ToolResponse::commit_and_set_current_group(group.m_uuid);
}

ToolResponse ToolConvertMeshToBody::update(const ToolArgs &args)
{
    return ToolResponse();
}

} // namespace dune3d
