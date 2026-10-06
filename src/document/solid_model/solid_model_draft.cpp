#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_draft.hpp"

#include <BRepOffsetAPI_DraftAngle.hxx>
#include <gp_Pln.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax3.hxx>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupDraft &group)
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
        BRepOffsetAPI_DraftAngle da(last_solid_model->m_shape_acc);
        const gp_Pln neutral_plane(gp::XOY());
        const double angle_rad = glm::radians(group.m_angle);
        for (const auto face_idx : group.m_faces) {
            if (face_idx >= last_solid_model->m_face_shapes.size())
                continue;
            da.Add(last_solid_model->m_face_shapes.at(face_idx), gp::DZ(), angle_rad, neutral_plane);
            if (!da.AddDone()) {
                group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                               "couldn't taper face " + std::to_string(face_idx));
                return nullptr;
            }
        }
        da.Build();
        if (!da.IsDone()) {
            group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "not done");
            return nullptr;
        }

        mod->m_shape_acc = da.Shape();
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
