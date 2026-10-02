#include "solid_model.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "util/color.hpp"
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

namespace dune3d {

class SolidModelOcc : public SolidModel {
public:
    TopoDS_Shape m_shape;
    TopoDS_Shape m_shape_acc;
    Color m_color;
    // Parallel to the base class's triangulated m_faces -- m_face_shapes.at(i)
    // is the actual TopoDS_Face that produced m_faces.at(i), populated by the
    // same Triangulator pass in the same order. Lets a face index coming from
    // a SelectableRef::Type::SOLID_MODEL_FACE click (which identifies a face
    // purely by its position in the renderer's per-body loop over m_faces,
    // see Renderer's per-current-document body rendering) be turned back into
    // a real OCCT face for a group that needs to act on it (e.g. Remove's
    // BRepAlgoAPI_Defeaturing::AddFaceToRemove).
    std::vector<TopoDS_Face> m_face_shapes;


    void export_stl(const std::filesystem::path &path) const override;
    void add_to_step_exporter(STEPExporter &exporter, const char *name) const override;

    bool update_acc_finish(const Document &doc, const Group &group);
    void finish(const Document &doc, const Group &group);

    static TopoDS_Shape calc(IGroupSolidModel::Operation op, TopoDS_Shape argument, TopoDS_Shape tool);
    // Exposes the .cpp-local Triangulator to other solid_model_*.cpp files
    // (e.g. solid_model_extrude.cpp's cut preview, which needs to
    // triangulate a shape that isn't m_shape_acc).
    static void triangulate_shape(const TopoDS_Shape &shape, const Color &color, face::Faces &faces);

private:
    void update_acc(IGroupSolidModel::Operation op, const TopoDS_Shape &last);
    void update_acc(IGroupSolidModel::Operation op, const SolidModelOcc *last);

    void triangulate();
    void find_edges();
};

} // namespace dune3d
