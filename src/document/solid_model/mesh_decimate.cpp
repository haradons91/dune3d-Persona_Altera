#include "mesh_decimate.hpp"
#include "mesh_edge_collapse.hpp"

namespace dune3d {

std::vector<MeshTriangle> decimate_mesh(const std::vector<MeshTriangle> &triangles, unsigned int target_face_count)
{
    return edge_collapse(triangles, target_face_count);
}

} // namespace dune3d
