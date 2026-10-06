#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_align.hpp"

#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Plane.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pln.hxx>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupAlign &group)
{
    group.m_face_operation_messages.clear();
    if (group.m_faces.size() != 1) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                       "select exactly one planar face to align");
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
        auto pln = Handle(Geom_Plane)::DownCast(surf);
        if (pln.IsNull()) {
            group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                           "selected face isn't planar");
            return nullptr;
        }
        gp_Ax3 face_ax3 = pln->Position();
        if (face.Orientation() == TopAbs_REVERSED)
            face_ax3.ZReverse();

        gp_Trsf trsf;
        trsf.SetDisplacement(face_ax3, gp::XOY());
        BRepBuilderAPI_Transform bt(last_solid_model->m_shape_acc, trsf, /*Copy=*/true);
        mod->m_shape_acc = bt.Shape();
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
