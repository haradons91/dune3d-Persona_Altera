#pragma once
#include "tool_common.hpp"

namespace dune3d {

// "Convert Mesh to Body": one-shot, non-interactive -- everything happens in
// begin(). Reads the current group (set beforehand by the workspace-browser
// right-click handler, see Editor::on_workspace_browser_convert_mesh_to_body)
// as the source GroupSTL/GroupThreeMF, and creates a new GroupConvertMesh
// right after it, same shape as ToolCreateComponent.
class ToolConvertMeshToBody : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    bool is_specific() override
    {
        return false;
    }

    CanBegin can_begin() override;
};

} // namespace dune3d
