#include "mesh_weld.hpp"
#include "mesh_edge_collapse.hpp"

namespace dune3d {

std::vector<MeshTriangle> weld_mesh(const std::vector<MeshTriangle> &triangles, double tolerance)
{
    if (tolerance <= 0)
        return triangles;
    // No face-count target -- keep collapsing manifold-safe edges shorter
    // than `tolerance` until none remain. Previously used naive grid
    // bucketing (snap each vertex to a cell, drop any triangle that
    // collapses to zero area) instead of this shared edge-collapse core;
    // that tore real meshes into as many as 13 disconnected shells at
    // larger tolerances, same "drops a triangle without patching the hole"
    // flaw decimation already hit -- see mesh_edge_collapse.cpp.
    return edge_collapse(triangles, 0, tolerance);
}

} // namespace dune3d
