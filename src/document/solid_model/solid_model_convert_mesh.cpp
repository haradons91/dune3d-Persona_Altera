#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "mesh_decimate.hpp"
#include "mesh_weld.hpp"
#include "mesh_convex_hull.hpp"
#include "bounded_execute.hpp"
#include "document/group/group_convert_mesh.hpp"
#include "document/document.hpp"
#include "document/entity/entity_stl.hpp"
#include "document/entity/entity_threemf.hpp"
#include "import_stl/imported_stl.hpp"
#include "import_3mf/imported_3mf.hpp"

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <ShapeFix_Solid.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <format>
#include <algorithm>

namespace dune3d {

namespace {

using Algorithm = GroupConvertMesh::Algorithm;

constexpr unsigned int s_direct_triangle_cap = 10000;

// OpenCascade's own algorithms below have no cancellation API and have been
// confirmed (on a real mesh) to hang indefinitely regardless of tolerance --
// see bounded_execute.hpp. This bounds the wait so a pathological mesh can
// only ever cost the user this much time, not the whole app.
constexpr auto s_geometry_timeout = std::chrono::seconds(30);

template <typename TEntity>
std::vector<MeshTriangle> triangles_from_entity(const TEntity &en)
{
    std::vector<MeshTriangle> tris;
    if (!en.m_imported)
        return tris;
    for (const auto &face : en.m_imported->result.faces) {
        for (const auto &[ia, ib, ic] : face.triangle_indices) {
            const auto &va = face.vertices.at(ia);
            const auto &vb = face.vertices.at(ib);
            const auto &vc = face.vertices.at(ic);
            const auto ta = en.transform({va.x, va.y, va.z});
            const auto tb = en.transform({vb.x, vb.y, vb.z});
            const auto tc = en.transform({vc.x, vc.y, vc.z});
            tris.push_back({gp_Pnt(ta.x, ta.y, ta.z), gp_Pnt(tb.x, tb.y, tb.z), gp_Pnt(tc.x, tc.y, tc.z)});
        }
    }
    return tris;
}

// Collects triangles from the mesh entity owned by group.m_source_group,
// already transformed into world space (the imported mesh data is stored
// untransformed -- see the plan for how this was confirmed).
std::vector<MeshTriangle> collect_triangles(const Document &doc, GroupConvertMesh &group)
{
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group != group.m_source_group)
            continue;
        if (const auto *stl = dynamic_cast<const EntitySTL *>(en.get()))
            return triangles_from_entity(*stl);
        if (const auto *mf = dynamic_cast<const EntityThreeMF *>(en.get()))
            return triangles_from_entity(*mf);
    }
    group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR, "Source mesh not found");
    return {};
}

struct SewResult {
    enum class Status { OK, MULTI_SHELL, MAKE_SOLID_FAILED } status = Status::MULTI_SHELL;
    int n_shells = 0;
    TopoDS_Solid solid;
};

// Pure function: takes/returns plain values only (no Group/Document access)
// so it's safe to run on a detached worker thread via run_with_timeout --
// see that header for why this needs to be bulletproof against the caller
// moving on before the worker finishes.
SewResult sew_into_solid_impl(const std::vector<MeshTriangle> &triangles)
{
    SewResult result;

    BRepBuilderAPI_Sewing sewing(1e-4);
    for (const auto &t : triangles) {
        BRepBuilderAPI_MakePolygon poly;
        poly.Add(t.a);
        poly.Add(t.b);
        poly.Add(t.c);
        poly.Close();
        if (!poly.IsDone())
            continue;
        BRepBuilderAPI_MakeFace mk_face(poly.Wire());
        if (mk_face.IsDone()) {
            TopoDS_Face f = mk_face.Face();
            sewing.Add(f);
        }
    }
    sewing.Perform();
    const TopoDS_Shape sewed = sewing.SewedShape();

    int n_shells = 0;
    for (TopExp_Explorer ex(sewed, TopAbs_SHELL); ex.More(); ex.Next())
        n_shells++;

    if (sewed.ShapeType() != TopAbs_SHELL || n_shells != 1) {
        result.status = SewResult::Status::MULTI_SHELL;
        result.n_shells = n_shells;
        return result;
    }

    BRepBuilderAPI_MakeSolid mk_solid(TopoDS::Shell(sewed));
    if (!mk_solid.IsDone()) {
        result.status = SewResult::Status::MAKE_SOLID_FAILED;
        return result;
    }
    TopoDS_Solid solid = mk_solid.Solid();
    ShapeFix_Solid fix(solid);
    fix.Perform();
    result.solid = TopoDS::Solid(fix.Solid());
    result.status = SewResult::Status::OK;
    return result;
}

struct MergeResult {
    bool ok = false;
    TopoDS_Solid solid;
};

MergeResult merge_coplanar_faces_impl(TopoDS_Solid solid)
{
    MergeResult result;
    ShapeUpgrade_UnifySameDomain unify(solid, true, true, false);
    unify.Build();
    const TopoDS_Shape unified = unify.Shape();
    if (unified.ShapeType() != TopAbs_SOLID)
        return result;
    result.solid = TopoDS::Solid(unified);
    result.ok = true;
    return result;
}

bool build_bounding_box(const std::vector<MeshTriangle> &triangles, TopoDS_Solid &solid)
{
    gp_Pnt lo = triangles.front().a;
    gp_Pnt hi = lo;
    auto expand = [&](const gp_Pnt &p) {
        lo.SetX(std::min(lo.X(), p.X()));
        lo.SetY(std::min(lo.Y(), p.Y()));
        lo.SetZ(std::min(lo.Z(), p.Z()));
        hi.SetX(std::max(hi.X(), p.X()));
        hi.SetY(std::max(hi.Y(), p.Y()));
        hi.SetZ(std::max(hi.Z(), p.Z()));
    };
    for (const auto &t : triangles) {
        expand(t.a);
        expand(t.b);
        expand(t.c);
    }
    if (hi.X() <= lo.X() || hi.Y() <= lo.Y() || hi.Z() <= lo.Z())
        return false;
    solid = BRepPrimAPI_MakeBox(lo, hi).Solid();
    return true;
}

} // namespace

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupConvertMesh &group)
{
    auto triangles = collect_triangles(doc, group);
    if (triangles.empty()) {
        if (group.m_solve_messages.empty())
            group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR, "Source mesh has no geometry");
        return nullptr;
    }

    TopoDS_Solid solid;

    if (group.m_algorithm == Algorithm::BOUNDING_BOX) {
        if (!build_bounding_box(triangles, solid)) {
            group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                "Mesh is degenerate -- bounding box has no volume");
            return nullptr;
        }
    }
    else {
        if (group.m_algorithm == Algorithm::CONVEX_HULL) {
            triangles = convex_hull(triangles);
            if (triangles.empty()) {
                group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                    "Mesh is degenerate -- convex hull has no volume");
                return nullptr;
            }
        }
        else if (group.m_algorithm == Algorithm::WELD_SEW) {
            triangles = weld_mesh(triangles, group.m_weld_tolerance);
        }

        const bool should_decimate =
                group.m_algorithm == Algorithm::DECIMATE_SEW || group.m_algorithm == Algorithm::DECIMATE_MERGE_FACES;
        if (should_decimate) {
            triangles = decimate_mesh(triangles, group.m_decimate_target_faces);
        }
        else if (group.m_algorithm != Algorithm::CONVEX_HULL && triangles.size() > s_direct_triangle_cap) {
            group.m_solve_messages.emplace_back(
                    GroupStatusMessage::Status::ERR,
                    std::format("Mesh has {} triangles; this algorithm supports up to {} -- use Decimate + Sew "
                                "instead",
                                triangles.size(), s_direct_triangle_cap));
            return nullptr;
        }

        auto sew_result = run_with_timeout([triangles] { return sew_into_solid_impl(triangles); },
                                           s_geometry_timeout);
        if (!sew_result) {
            group.m_solve_messages.emplace_back(
                    GroupStatusMessage::Status::ERR,
                    std::format("Conversion is taking too long and was aborted after {}s -- this mesh may be too "
                                "complex or malformed for this algorithm; try a different algorithm or a lower "
                                "decimation target",
                                s_geometry_timeout.count()));
            return nullptr;
        }
        if (sew_result->status == SewResult::Status::MULTI_SHELL) {
            group.m_solve_messages.emplace_back(
                    GroupStatusMessage::Status::ERR,
                    std::format(
                            "Mesh did not sew into a single closed surface (got {} separate piece(s)) -- this mesh "
                            "isn't clean/watertight enough to convert; try a lower decimation target or a "
                            "different algorithm",
                            std::max(sew_result->n_shells, 1)));
            return nullptr;
        }
        if (sew_result->status == SewResult::Status::MAKE_SOLID_FAILED) {
            group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR, "Could not close mesh into a solid");
            return nullptr;
        }
        solid = sew_result->solid;

        const bool should_merge =
                group.m_algorithm == Algorithm::MERGE_FACES || group.m_algorithm == Algorithm::DECIMATE_MERGE_FACES;
        if (should_merge) {
            auto merge_result = run_with_timeout([solid] { return merge_coplanar_faces_impl(solid); },
                                                 s_geometry_timeout);
            if (!merge_result) {
                group.m_solve_messages.emplace_back(
                        GroupStatusMessage::Status::ERR,
                        std::format("Merging coplanar faces is taking too long and was aborted after {}s -- try a "
                                    "different algorithm",
                                    s_geometry_timeout.count()));
                return nullptr;
            }
            if (!merge_result->ok) {
                group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                    "Merging coplanar faces did not produce a solid");
                return nullptr;
            }
            solid = merge_result->solid;
        }
    }

    BRepCheck_Analyzer analyzer(solid);
    if (!analyzer.IsValid()) {
        group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR, "Resulting solid failed validation");
        return nullptr;
    }

    auto mod = std::make_shared<SolidModelOcc>();
    mod->m_shape = solid;

    if (!mod->update_acc_finish(doc, group))
        return nullptr;

    return mod;
}

} // namespace dune3d
