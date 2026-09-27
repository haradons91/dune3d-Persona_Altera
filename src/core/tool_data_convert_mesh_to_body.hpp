#pragma once
#include "tool_data.hpp"
#include "document/group/group_convert_mesh.hpp"

namespace dune3d {

class ToolDataConvertMeshToBody : public ToolData {
public:
    explicit ToolDataConvertMeshToBody(GroupConvertMesh::Algorithm algorithm) : algorithm(algorithm)
    {
    }

    GroupConvertMesh::Algorithm algorithm;
};
} // namespace dune3d
