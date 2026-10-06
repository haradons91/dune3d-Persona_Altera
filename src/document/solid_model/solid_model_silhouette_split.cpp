#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_silhouette_split.hpp"

#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRAlgo_Projector.hxx>
#include <BRepProj_Projection.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <glm/glm.hpp>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupSilhouetteSplit &group)
{
    group.m_face_operation_messages.clear();
    if (group.m_faces.size() == 0) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "no faces");
        return nullptr;
    }
    if (glm::length(group.m_direction) < 1e-9) {
        group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "direction is zero-length");
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
        const auto n = glm::normalize(group.m_direction);
        const gp_Dir direction(n.x, n.y, n.z);

        Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
        algo->Add(last_solid_model->m_shape_acc);
        const gp_Ax2 view_cs(gp_Pnt(0, 0, 0), direction);
        algo->Projector(HLRAlgo_Projector(view_cs));
        algo->Update();
        algo->Hide();

        HLRBRep_HLRToShape hlr_to_shape(algo);
        const TopoDS_Shape outline_visible = hlr_to_shape.OutLineVCompound3d();
        const TopoDS_Shape outline_hidden = hlr_to_shape.CompoundOfEdges(HLRBRep_OutLine, false, true);

        BRepFeat_SplitShape splitter(last_solid_model->m_shape_acc);
        bool any_added = false;
        for (const auto face_idx : group.m_faces) {
            if (face_idx >= last_solid_model->m_face_shapes.size())
                continue;
            const auto &face = last_solid_model->m_face_shapes.at(face_idx);

            for (const auto &outline : {outline_visible, outline_hidden}) {
                for (TopExp_Explorer ex(outline, TopAbs_EDGE); ex.More(); ex.Next()) {
                    BRepProj_Projection proj(TopoDS::Edge(ex.Current()), face, direction);
                    if (!proj.IsDone())
                        continue;
                    for (proj.Init(); proj.More(); proj.Next()) {
                        const TopoDS_Wire w = proj.Current();
                        for (TopExp_Explorer ex2(w, TopAbs_EDGE); ex2.More(); ex2.Next()) {
                            splitter.Add(TopoDS::Edge(ex2.Current()), face);
                            any_added = true;
                        }
                    }
                }
            }
        }
        if (!any_added) {
            group.m_face_operation_messages.emplace_back(
                    GroupStatusMessage::Status::ERR, "the silhouette doesn't cross any selected face from this direction");
            return nullptr;
        }

        splitter.Build();
        if (!splitter.IsDone()) {
            group.m_face_operation_messages.emplace_back(GroupStatusMessage::Status::ERR, "not done");
            return nullptr;
        }

        mod->m_shape_acc = splitter.Shape();
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
