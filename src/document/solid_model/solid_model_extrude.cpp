#include "solid_model.hpp"
#include "solid_model_util.hpp"
#include "solid_model_occ.hpp"
#include "document/group/group_extrude.hpp"
#include "util/paths.hpp"
#include "util/debug.hpp"

#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <fstream>
#include <format>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupExtrude &group)
{
    group.m_sweep_messages.clear();
    auto mod = std::make_shared<SolidModelOcc>();
    debug_log(DebugCategory::MODEL,
              std::format("extrude group={} source={} operation={} dvec=({},{},{})",
                          static_cast<std::string>(group.m_uuid), static_cast<std::string>(group.m_source_group),
                          static_cast<int>(group.m_operation), group.m_dvec.x, group.m_dvec.y, group.m_dvec.z));


    glm::dvec3 offset = {0, 0, 0};
    glm::dvec3 dvec = group.m_dvec;

    switch (group.m_mode) {
    case GroupExtrude::Mode::SINGLE:
        break;
    case GroupExtrude::Mode::OFFSET_SYMMETRIC:
        offset = -group.m_dvec;
        dvec = group.m_dvec * 2.;
        break;
    case GroupExtrude::Mode::OFFSET:
        offset = group.m_dvec * group.m_offset_mul;
        dvec = group.m_dvec * (1 - group.m_offset_mul);
        break;
    }

    try {
        TopoDS_Compound extrusion_faces;
        BRep_Builder extrusion_builder;
        extrusion_builder.MakeCompound(extrusion_faces);
        TopoDS_Compound extrusion_solids;
        extrusion_builder.MakeCompound(extrusion_solids);
        unsigned int face_count = 0;
        bool has_hole = false;

        if (!group.m_source_profiles.empty()) {
            const auto sketch_paths = paths::Paths::from_document(doc, group.m_wrkpl, group.m_source_group);
            for (const auto profile : group.m_source_profiles) {
                const auto cell = std::ranges::find_if(sketch_paths.cells, [profile](const auto &candidate) {
                    return candidate.boundary == profile;
                });
                auto boundary_builder = FaceBuilder::from_document(doc, group.m_wrkpl, group.m_source_group, offset,
                                                                    std::set<unsigned int>{profile});
                face_count += boundary_builder.get_n_faces();
                if (boundary_builder.get_n_faces() == 0)
                    continue;
                TopoDS_Shape cell_solid = BRepPrimAPI_MakePrism(boundary_builder.get_faces(),
                                                                gp_Vec(dvec.x, dvec.y, dvec.z));
                if (cell != sketch_paths.cells.end()) {
                    for (const auto hole : cell->holes) {
                        auto hole_builder = FaceBuilder::from_document(doc, group.m_wrkpl, group.m_source_group,
                                                                        offset, std::set<unsigned int>{hole});
                        if (hole_builder.get_n_faces() == 0)
                            continue;
                        const TopoDS_Shape hole_solid = BRepPrimAPI_MakePrism(hole_builder.get_faces(),
                                                                              gp_Vec(dvec.x, dvec.y, dvec.z));
                        cell_solid = BRepAlgoAPI_Cut(cell_solid, hole_solid).Shape();
                        has_hole = true;
                    }
                }
                extrusion_builder.Add(extrusion_solids, cell_solid);
            }
        }
        else {
            std::optional<std::set<unsigned int>> source_paths;
            if (!group.m_source_paths.empty())
                source_paths = group.m_source_paths;
            else if (group.m_source_path)
                source_paths = std::set<unsigned int>{*group.m_source_path};
            auto face_builder = FaceBuilder::from_document(doc, group.m_wrkpl, group.m_source_group, offset,
                                                            source_paths);
            face_count = face_builder.get_n_faces();
            has_hole = face_builder.has_hole();
            extrusion_builder.Add(extrusion_faces, face_builder.get_faces());
        }

        if (face_count == 0) {
            std::ofstream log("/tmp/dune3d-profile-debug.log", std::ios::app);
            log << std::format("extrude facebuilder faces=0 source_profiles={} source_paths={}\n",
                               group.m_source_profiles.size(), group.m_source_paths.size());
            group.m_sweep_messages.emplace_back(GroupStatusMessage::Status::ERR, "no faces");
            return nullptr;
        }
        {
            std::ofstream log("/tmp/dune3d-profile-debug.log", std::ios::app);
            log << std::format("extrude facebuilder faces={} holes={} source_profiles={} source_paths={}\n", face_count,
                               has_hole, group.m_source_profiles.size(), group.m_source_paths.size());
        }

        if (glm::length(dvec) < 1e-6) {
            group.m_sweep_messages.emplace_back(GroupStatusMessage::Status::ERR, "zero length extrusion vector");
            return nullptr;
        }

        if (!group.m_source_profiles.empty())
            mod->m_shape = extrusion_solids;
        else
            mod->m_shape = BRepPrimAPI_MakePrism(extrusion_faces, gp_Vec(dvec.x, dvec.y, dvec.z));
    }
    catch (const Standard_Failure &e) {
        std::ostringstream os;
        e.Print(os);
        group.m_sweep_messages.emplace_back(GroupStatusMessage::Status::ERR, "exception: " + os.str());
        std::ofstream log("/tmp/dune3d-profile-debug.log", std::ios::app);
        log << "extrude OCC exception: " << os.str() << '\n';
    }
    catch (const std::exception &e) {
        group.m_sweep_messages.emplace_back(GroupStatusMessage::Status::ERR, std::string{"exception: "} + e.what());
        std::ofstream log("/tmp/dune3d-profile-debug.log", std::ios::app);
        log << "extrude std exception: " << e.what() << '\n';
    }
    catch (...) {
        group.m_sweep_messages.emplace_back(GroupStatusMessage::Status::ERR, "unknown exception");
    }
    if (mod->m_shape.IsNull()) {
        group.m_sweep_messages.emplace_back(GroupStatusMessage::Status::ERR, "didn't generate a shape");
        return nullptr;
    }

    if (!mod->update_acc_finish(doc, group)) {
        group.m_sweep_messages.emplace_back(GroupStatusMessage::Status::ERR, "didn't generate a shape");
        return nullptr;
    }

    return mod;
}
} // namespace dune3d
