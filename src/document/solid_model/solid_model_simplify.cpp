#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_simplify.hpp"

#include <ShapeUpgrade_UnifySameDomain.hxx>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupSimplify &group)
{
    group.m_simplify_messages.clear();

    auto mod = std::make_shared<SolidModelOcc>();

    const auto last_solid_model_group = SolidModel::get_last_solid_model_group(doc, group);
    if (!last_solid_model_group) {
        group.m_simplify_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model group");
        return nullptr;
    }
    group.set_operation(last_solid_model_group->get_operation());
    const auto last_solid_model = dynamic_cast<const SolidModelOcc *>(last_solid_model_group->get_solid_model());
    if (!last_solid_model) {
        group.m_simplify_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model");
        return nullptr;
    }

    try {
        ShapeUpgrade_UnifySameDomain unify(last_solid_model->m_shape_acc, /*UnifyEdges=*/true, /*UnifyFaces=*/true);
        unify.Build();
        mod->m_shape_acc = unify.Shape();
    }
    catch (const Standard_Failure &e) {
        std::ostringstream os;
        e.Print(os);
        group.m_simplify_messages.emplace_back(GroupStatusMessage::Status::ERR, "exception: " + os.str());
    }
    catch (const std::exception &e) {
        group.m_simplify_messages.emplace_back(GroupStatusMessage::Status::ERR, std::string{"exception: "} + e.what());
    }
    catch (...) {
        group.m_simplify_messages.emplace_back(GroupStatusMessage::Status::ERR, "unknown exception");
    }
    if (mod->m_shape_acc.IsNull()) {
        group.m_simplify_messages.emplace_back(GroupStatusMessage::Status::ERR, "didn't generate a shape");
        return nullptr;
    }

    mod->finish(doc, group);

    return mod;
}

} // namespace dune3d
