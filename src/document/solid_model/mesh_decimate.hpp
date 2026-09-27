#pragma once
#include <vector>
#include "mesh_triangle.hpp"

namespace dune3d {

// Edge-collapse decimation (shortest-edge-first, midpoint collapse) used by
// GroupConvertMesh's "Decimate + Sew" algorithm -- see
// solid_model_convert_mesh.cpp for why decimating before sewing helps with
// large meshes. Vertices are recovered from the input triangle soup by
// exact-coordinate dedup (triangles need not share indices going in).
// Best-effort: may stop above target_face_count if the mesh runs out of
// collapsible edges first.
std::vector<MeshTriangle> decimate_mesh(const std::vector<MeshTriangle> &triangles, unsigned int target_face_count);

} // namespace dune3d
