#pragma once
#include <vector>
#include "mesh_triangle.hpp"

namespace dune3d {

// Merges vertices within `tolerance` of each other before sewing -- used by
// GroupConvertMesh's "Weld + Sew" algorithm to close tiny floating-point
// gaps between triangles that should share an edge but don't exactly.
// Implemented as manifold-safe edge collapse (see mesh_edge_collapse.hpp)
// bounded by edge length instead of face count, so unlike plain vertex
// snapping it won't tear the mesh apart at larger tolerances.
std::vector<MeshTriangle> weld_mesh(const std::vector<MeshTriangle> &triangles, double tolerance);

} // namespace dune3d
