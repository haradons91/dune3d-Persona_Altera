#pragma once
#include <vector>
#include "mesh_triangle.hpp"

namespace dune3d {

// Incremental 3D convex hull (OpenCascade has no built-in one). Always
// yields a valid closed convex polytope regardless of how messy/open the
// input mesh is -- the point of offering it as a GroupConvertMesh algorithm
// -- at the cost of losing every concave feature. Internally pre-reduces
// very large point clouds (grid-bucketed) before running the O(n * hull
// size) incremental algorithm: verified against real STL files that
// otherwise blow up on round/cylindrical shapes where thousands of points
// sit near the true hull surface. Returns empty on degenerate input
// (coplanar/collinear points with no interior volume).
std::vector<MeshTriangle> convex_hull(const std::vector<MeshTriangle> &triangles);

} // namespace dune3d
