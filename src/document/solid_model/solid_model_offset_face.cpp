#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_offset_face.hpp"

#include <BRepOffset_MakeOffset.hxx>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupOffsetFace &group)
{
    group.m_face_operation_messages.clear();
    if (group.m_faces.size() == 0) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "no faces");
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

    try {
        BRepOffset_MakeOffset mo;
        mo.Initialize(last_solid_model->m_shape_acc, 0.0, 1e-3, BRepOffset_Skin, false, false,
                       GeomAbs_Intersection, false, false);
        for (const auto face_idx : group.m_faces) {
            if (face_idx >= last_solid_model->m_face_shapes.size())
                continue;
            mo.SetOffsetOnFace(last_solid_model->m_face_shapes.at(face_idx), group.m_offset);
        }
        mo.MakeOffsetShape();
        if (!mo.IsDone()) {
            group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "not done");
            return nullptr;
        }

        mod->m_shape_acc = mo.Shape();
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
