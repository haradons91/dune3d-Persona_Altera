#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_split_body.hpp"

#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <glm/glm.hpp>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupSplitBody &group)
{
    group.m_split_body_messages.clear();
    if (glm::length(group.m_plane_normal) < 1e-9) {
        group.m_split_body_messages.emplace_back(GroupStatusMessage::Status::ERR, "plane normal is zero-length");
        return nullptr;
    }

    auto mod = std::make_shared<SolidModelOcc>();

    const auto last_solid_model_group = SolidModel::get_last_solid_model_group(doc, group);
    if (!last_solid_model_group) {
        group.m_split_body_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model group");
        return nullptr;
    }
    group.set_operation(last_solid_model_group->get_operation());
    const auto last_solid_model = dynamic_cast<const SolidModelOcc *>(last_solid_model_group->get_solid_model());
    if (!last_solid_model) {
        group.m_split_body_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model");
        return nullptr;
    }

    try {
        const auto &p = group.m_plane_point;
        const auto n = glm::normalize(group.m_plane_normal);
        gp_Pln pln(gp_Pnt(p.x, p.y, p.z), gp_Dir(n.x, n.y, n.z));
        TopoDS_Face seed_face = BRepBuilderAPI_MakeFace(pln, -1e5, 1e5, -1e5, 1e5).Face();
        // A point just off the plane along +normal marks that side as the
        // one BRepPrimAPI_MakeHalfSpace keeps "inside" -- GroupSplitBody
        // keeps the +normal side, GroupSplitBodyResult (same plane, Cut
        // instead of Common against the identical half-space) keeps the
        // other.
        gp_Pnt kept_side_pt(p.x + n.x * 1e-3, p.y + n.y * 1e-3, p.z + n.z * 1e-3);
        BRepPrimAPI_MakeHalfSpace half(seed_face, kept_side_pt);

        BRepAlgoAPI_Common common(last_solid_model->m_shape_acc, half.Solid());
        mod->m_shape_acc = common.Shape();
    }
    catch (const Standard_Failure &e) {
        std::ostringstream os;
        e.Print(os);
        group.m_split_body_messages.emplace_back(GroupStatusMessage::Status::ERR, "exception: " + os.str());
    }
    catch (const std::exception &e) {
        group.m_split_body_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                  std::string{"exception: "} + e.what());
    }
    catch (...) {
        group.m_split_body_messages.emplace_back(GroupStatusMessage::Status::ERR, "unknown exception");
    }
    if (mod->m_shape_acc.IsNull()) {
        group.m_split_body_messages.emplace_back(GroupStatusMessage::Status::ERR, "didn't generate a shape");
        return nullptr;
    }

    mod->finish(doc, group);

    return mod;
}

} // namespace dune3d
