#pragma once
#include <vector>
#include "mesh_triangle.hpp"

namespace dune3d {

// Merges vertices within `tolerance` of each other (grid-bucketed, keeps the
// first vertex seen per bucket) before sewing -- used by GroupConvertMesh's
// "Weld + Sew" algorithm to close tiny floating-point gaps between triangles
// that should share an edge but don't exactly, without losing any detail the
// way decimation does. A triangle that becomes degenerate after welding (two
// or more corners landing in the same bucket) is dropped.
std::vector<MeshTriangle> weld_mesh(const std::vector<MeshTriangle> &triangles, double tolerance);

} // namespace dune3d
