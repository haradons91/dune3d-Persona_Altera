#include "import_stl.hpp"
#include <RWStl.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly.hxx>
#include <Message_ProgressRange.hxx>
#include <TCollection_AsciiString.hxx>
#include <gp_XYZ.hxx>
#include <map>
#include <limits>
#include <algorithm>

namespace dune3d::STLImporter {

Result import(const std::filesystem::path &filename)
{
    Result result;

    // StlAPI_Reader (in this OpenCascade version) builds a real BREP shape
    // with one small planar face per input triangle and no triangulation
    // attached to any of them -- not the single triangulated face STEP
    // imports work with. RWStl::ReadFile() reads the raw mesh directly,
    // which is what a reference-only import actually needs here.
    TCollection_AsciiString fname(filename.string().c_str());
    Handle(Poly_Triangulation) triangulation = RWStl::ReadFile(fname.ToCString(), Message_ProgressRange());
    if (triangulation.IsNull())
        return result;

    Poly::ComputeNormals(triangulation);

    result.faces.emplace_back();
    auto &face_out = result.faces.back();
    face_out.color = {0.5, 0.5, 0.5};

    face_out.vertices.reserve(triangulation->NbNodes());
    gp_XYZ bbox_min(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                    std::numeric_limits<double>::max());
    gp_XYZ bbox_max(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
                    std::numeric_limits<double>::lowest());
    for (int i = 1; i <= triangulation->NbNodes(); i++) {
        gp_XYZ v(triangulation->Node(i).Coord());
        bbox_min.SetX(std::min(bbox_min.X(), v.X()));
        bbox_min.SetY(std::min(bbox_min.Y(), v.Y()));
        bbox_min.SetZ(std::min(bbox_min.Z(), v.Z()));
        bbox_max.SetX(std::max(bbox_max.X(), v.X()));
        bbox_max.SetY(std::max(bbox_max.Y(), v.Y()));
        bbox_max.SetZ(std::max(bbox_max.Z(), v.Z()));
    }
    // Many real-world STL exports (e.g. a single part pulled out of a larger
    // assembly) carry whatever absolute coordinates they had in the source
    // assembly, which can be far from this mesh's own geometry -- centering
    // it here means EntitySTL::m_origin (which the import tool drives
    // directly off the cursor position) actually places the part where the
    // user points, instead of somewhere possibly meters away.
    const gp_XYZ bbox_center = (bbox_min + bbox_max) * 0.5;

    std::map<Vertex, std::vector<size_t>> pts_map;
    for (int i = 1; i <= triangulation->NbNodes(); i++) {
        gp_XYZ v(triangulation->Node(i).Coord());
        v -= bbox_center;
        const Vertex vertex(v.X(), v.Y(), v.Z());
        pts_map[vertex].push_back(i - 1);
        face_out.vertices.push_back(vertex);
    }

    face_out.normals.reserve(triangulation->NbNodes());
    for (int i = 1; i <= triangulation->NbNodes(); i++) {
        const auto n = triangulation->Normal(i);
        face_out.normals.emplace_back(n.X(), n.Y(), n.Z());
    }

    // average normals at coincident vertices
    for (const auto &[k, v] : pts_map) {
        if (v.size() > 1) {
            Vertex n_acc(0, 0, 0);
            for (const auto idx : v) {
                n_acc += face_out.normals.at(idx);
            }
            n_acc /= v.size();
            for (const auto idx : v) {
                face_out.normals.at(idx) = n_acc;
            }
        }
    }

    face_out.triangle_indices.reserve(triangulation->NbTriangles());
    for (int i = 1; i <= triangulation->NbTriangles(); i++) {
        int a, b, c;
        triangulation->Triangle(i).Get(a, b, c);
        face_out.triangle_indices.emplace_back(a - 1, b - 1, c - 1);
    }

    return result;
}

} // namespace dune3d::STLImporter
