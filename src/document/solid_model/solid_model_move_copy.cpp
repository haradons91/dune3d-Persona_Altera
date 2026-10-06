#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_move_copy.hpp"

#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupMoveCopy &group)
{
    group.m_move_copy_messages.clear();

    if (group.m_copy) {
        group.m_move_copy_messages.emplace_back(
                GroupStatusMessage::Status::ERR,
                "copying into a new body isn't implemented yet -- uncheck \"Copy\" to move in place");
        return nullptr;
    }

    if (glm::length(group.m_rotation_axis) < 1e-9 && group.m_rotation_angle != 0) {
        group.m_move_copy_messages.emplace_back(GroupStatusMessage::Status::ERR, "rotation axis is zero-length");
        return nullptr;
    }

    auto mod = std::make_shared<SolidModelOcc>();

    const auto last_solid_model_group = SolidModel::get_last_solid_model_group(doc, group);
    if (!last_solid_model_group) {
        group.m_move_copy_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model group");
        return nullptr;
    }
    group.set_operation(last_solid_model_group->get_operation());
    const auto last_solid_model = dynamic_cast<const SolidModelOcc *>(last_solid_model_group->get_solid_model());
    if (!last_solid_model) {
        group.m_move_copy_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model");
        return nullptr;
    }

    try {
        TopoDS_Shape shape = last_solid_model->m_shape_acc;

        if (group.m_rotation_angle != 0) {
            const auto &axis = group.m_rotation_axis;
            gp_Trsf rot;
            rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(axis.x, axis.y, axis.z)),
                             glm::radians(group.m_rotation_angle));
            shape = BRepBuilderAPI_Transform(shape, rot, /*Copy=*/true).Shape();
        }

        const auto &t = group.m_translation;
        if (t.x != 0 || t.y != 0 || t.z != 0) {
            gp_Trsf tr;
            tr.SetTranslation(gp_Vec(t.x, t.y, t.z));
            shape = BRepBuilderAPI_Transform(shape, tr, /*Copy=*/true).Shape();
        }

        mod->m_shape_acc = shape;
    }
    catch (const Standard_Failure &e) {
        std::ostringstream os;
        e.Print(os);
        group.m_move_copy_messages.emplace_back(GroupStatusMessage::Status::ERR, "exception: " + os.str());
    }
    catch (const std::exception &e) {
        group.m_move_copy_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                 std::string{"exception: "} + e.what());
    }
    catch (...) {
        group.m_move_copy_messages.emplace_back(GroupStatusMessage::Status::ERR, "unknown exception");
    }
    if (mod->m_shape_acc.IsNull()) {
        group.m_move_copy_messages.emplace_back(GroupStatusMessage::Status::ERR, "didn't generate a shape");
        return nullptr;
    }

    mod->finish(doc, group);

    return mod;
}

} // namespace dune3d
