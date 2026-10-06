#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/group/group_replace_face.hpp"

#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Plane.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <glm/glm.hpp>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupReplaceFace &group)
{
    group.m_face_operation_messages.clear();
    if (group.m_faces.size() != 1) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                       "select exactly one face to replace");
        return nullptr;
    }
    if (!group.m_reference_wrkpl) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "no reference plane picked");
        return nullptr;
    }
    if (!doc.m_entities.contains(group.m_reference_wrkpl)) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "reference plane not found");
        return nullptr;
    }
    const auto *wrkpl = dynamic_cast<const EntityWorkplane *>(doc.m_entities.at(group.m_reference_wrkpl).get());
    if (!wrkpl) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                       "reference entity isn't a workplane");
        return nullptr;
    }

    auto mod = std::make_shared<SolidModelOcc>();

    const auto last_solid_model_group = SolidModel::get_last_solid_model_group(doc, group);
    if (!last_solid_model_group) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model group");
        return nullptr;
    }
    group.set_operation(last_solid_model_group->get_operation());
    const auto last_solid_model = dynamic_cast<const SolidModelOcc *>(last_solid_model_group->get_solid_model());
    if (!last_solid_model) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model");
        return nullptr;
    }

    const auto face_idx = *group.m_faces.begin();
    if (face_idx >= last_solid_model->m_face_shapes.size()) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "face index out of range");
        return nullptr;
    }
    const auto &face = last_solid_model->m_face_shapes.at(face_idx);

    try {
        auto surf = BRep_Tool::Surface(face);
        auto pln_face = Handle(Geom_Plane)::DownCast(surf);
        if (pln_face.IsNull()) {
            group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                           "selected face isn't planar");
            return nullptr;
        }
        gp_Ax3 face_ax3 = pln_face->Position();
        if (face.Orientation() == TopAbs_REVERSED)
            face_ax3.ZReverse();
        const gp_Pnt face_origin = face_ax3.Location();
        const gp_Dir face_outward_normal = face_ax3.Direction();

        const auto ref_origin_glm = wrkpl->m_origin;
        const auto ref_normal_glm = glm::normalize(wrkpl->get_normal_vector());
        const gp_Pnt ref_origin(ref_origin_glm.x, ref_origin_glm.y, ref_origin_glm.z);
        const gp_Dir ref_normal(ref_normal_glm.x, ref_normal_glm.y, ref_normal_glm.z);

        // Common() against a half-space can only trim material away, never
        // add it past the existing boundary -- so the reference plane must
        // lie on the inward side of the selected face's own plane. Measured
        // along the FACE's own outward normal (not the reference plane's,
        // which may be tilted relative to it): positive means the
        // reference plane is farther out, which would require adding
        // material.
        const double delta = gp_Vec(face_origin, ref_origin).Dot(gp_Vec(face_outward_normal));
        if (delta > 1e-6) {
            group.m_face_operation_messages.emplace_back(
                    GroupStatusMessage::Status::ERR,
                    "the reference plane lies outward of the selected face; adding material isn't supported yet");
            return nullptr;
        }

        gp_Pln ref_pln(ref_origin, ref_normal);
        TopoDS_Face seed_face = BRepBuilderAPI_MakeFace(ref_pln, -1e5, 1e5, -1e5, 1e5).Face();

        // The "kept" side of the reference plane is wherever the bulk of
        // the existing solid already is -- its own volume centroid, not a
        // small nudge off the selected face (which can land on the wrong
        // side of a reference plane that isn't close to the face).
        GProp_GProps solid_props;
        BRepGProp::VolumeProperties(last_solid_model->m_shape_acc, solid_props);
        const gp_Pnt kept_pt = solid_props.CentreOfMass();

        BRepPrimAPI_MakeHalfSpace half(seed_face, kept_pt);
        BRepAlgoAPI_Common common(last_solid_model->m_shape_acc, half.Solid());
        mod->m_shape_acc = common.Shape();
    }
    catch (const Standard_Failure &e) {
        std::ostringstream os;
        e.Print(os);
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "exception: " + os.str());
    }
    catch (const std::exception &e) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                      std::string{"exception: "} + e.what());
    }
    catch (...) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "unknown exception");
    }
    if (mod->m_shape_acc.IsNull()) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "didn't generate a shape");
        return nullptr;
    }

    mod->finish(doc, group);

    return mod;
}

} // namespace dune3d
