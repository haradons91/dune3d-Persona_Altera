#pragma once
#include <vector>
#include <limits>
#include "mesh_triangle.hpp"

namespace dune3d {

// Manifold-safe edge-collapse (shortest-edge-first, midpoint collapse, link
// condition checked before every collapse -- see mesh_edge_collapse.cpp)
// shared by decimate_mesh() and weld_mesh(), which differ only in when they
// stop: decimate_mesh stops at a face-count target with no length limit;
// weld_mesh has no face-count target and stops once the shortest remaining
// edge reaches `max_edge_length`. Passing both un-defaulted lets either
// caller drive the other criterion too, though neither currently needs to.
std::vector<MeshTriangle> edge_collapse(const std::vector<MeshTriangle> &triangles, unsigned int target_face_count,
                                        double max_edge_length = std::numeric_limits<double>::infinity());

} // namespace dune3d
