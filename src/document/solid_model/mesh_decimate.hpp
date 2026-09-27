#pragma once
#include <vector>
#include "mesh_triangle.hpp"

namespace dune3d {

// Manifold-safe edge-collapse decimation (see mesh_edge_collapse.hpp) used
// by GroupConvertMesh's "Decimate + Sew" algorithm -- see
// solid_model_convert_mesh.cpp for why decimating before sewing helps with
// large meshes. Best-effort: may stop above target_face_count if the mesh
// runs out of manifold-safe collapses first.
std::vector<MeshTriangle> decimate_mesh(const std::vector<MeshTriangle> &triangles, unsigned int target_face_count);

} // namespace dune3d
