#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_shell.hpp"

#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <TopTools_ListOfShape.hxx>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupShell &group)
{
    group.m_face_operation_messages.clear();
    if (group.m_faces.size() == 0) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "no faces");
        return nullptr;
    }
    if (group.m_thickness <= 0) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "thickness must be positive");
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
        TopTools_ListOfShape faces_to_remove;
        for (const auto face_idx : group.m_faces) {
            if (face_idx >= last_solid_model->m_face_shapes.size())
                continue;
            faces_to_remove.Append(last_solid_model->m_face_shapes.at(face_idx));
        }

        BRepOffsetAPI_MakeThickSolid shell;
        shell.MakeThickSolidByJoin(last_solid_model->m_shape_acc, faces_to_remove, -group.m_thickness, 1e-3);
        shell.Build();
        if (!shell.IsDone()) {
            group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "not done");
            return nullptr;
        }

        mod->m_shape_acc = shell.Shape();
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
