#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_scale.hpp"

#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pnt.hxx>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupScale &group)
{
    group.m_scale_messages.clear();

    if (group.m_factor <= 0) {
        group.m_scale_messages.emplace_back(GroupStatusMessage::Status::ERR, "factor must be positive");
        return nullptr;
    }

    auto mod = std::make_shared<SolidModelOcc>();

    const auto last_solid_model_group = SolidModel::get_last_solid_model_group(doc, group);
    if (!last_solid_model_group) {
        group.m_scale_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model group");
        return nullptr;
    }
    group.set_operation(last_solid_model_group->get_operation());
    const auto last_solid_model = dynamic_cast<const SolidModelOcc *>(last_solid_model_group->get_solid_model());
    if (!last_solid_model) {
        group.m_scale_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model");
        return nullptr;
    }

    try {
        gp_Trsf trsf;
        trsf.SetScale(gp_Pnt(group.m_center.x, group.m_center.y, group.m_center.z), group.m_factor);
        BRepBuilderAPI_Transform bt(last_solid_model->m_shape_acc, trsf, /*Copy=*/true);
        mod->m_shape_acc = bt.Shape();
    }
    catch (const Standard_Failure &e) {
        std::ostringstream os;
        e.Print(os);
        group.m_scale_messages.emplace_back(GroupStatusMessage::Status::ERR, "exception: " + os.str());
    }
    catch (const std::exception &e) {
        group.m_scale_messages.emplace_back(GroupStatusMessage::Status::ERR, std::string{"exception: "} + e.what());
    }
    catch (...) {
        group.m_scale_messages.emplace_back(GroupStatusMessage::Status::ERR, "unknown exception");
    }
    if (mod->m_shape_acc.IsNull()) {
        group.m_scale_messages.emplace_back(GroupStatusMessage::Status::ERR, "didn't generate a shape");
        return nullptr;
    }

    mod->finish(doc, group);

    return mod;
}

} // namespace dune3d
