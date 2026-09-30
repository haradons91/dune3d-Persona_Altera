#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "mesh_decimate.hpp"
#include "mesh_weld.hpp"
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
#include <ShapeFix_Solid.hxx>
#include <ShapeFix_Shell.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <format>
#include <algorithm>
#include <atomic>

namespace dune3d {

namespace {

using Algorithm = GroupConvertMesh::Algorithm;

constexpr unsigned int s_direct_triangle_cap = 10000;

// OpenCascade's own algorithms below have no cancellation API and have been
// confirmed (on a real mesh) to hang indefinitely regardless of tolerance --
// see bounded_execute.hpp. This bounds the wait so a pathological mesh can
// only ever cost the user this much time, not the whole app -- now mostly a
// last-resort safety net rather than the primary way to stop a long wait,
// since Cancel actually interrupts the sewing/fixing steps in ~tens of
// milliseconds (see CancelToken below). On OCCT 8.0.1 the full pipeline
// (weld, sew, fix, merge) on the largest real mesh tested end-to-end takes
// ~23s, so 60s leaves comfortable headroom without making a user wait
// minutes on a mesh that's genuinely too pathological to convert.
constexpr auto s_geometry_timeout = std::chrono::seconds(60);

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

// Adapts a WorkContext to OpenCascade's own Message_ProgressIndicator
// mechanism -- confirmed (by direct testing against a real mesh) that
// BRepBuilderAPI_Sewing::Perform() and ShapeFix_Solid::Perform() both check
// UserBreak() frequently enough for real, responsive interruption (~45ms
// from request to return, mid-computation), and that GetPosition() is a
// real (if unevenly-paced -- see WorkContext::progress) 0..1 signal they
// update via Show(). `context` must outlive this object; the caller in
// bounded_execute.hpp keeps it alive via a shared_ptr for exactly this
// reason.
class CancelToken : public Message_ProgressIndicator {
public:
    explicit CancelToken(WorkContext &context) : m_context(context)
    {
    }

protected:
    Standard_Boolean UserBreak() override
    {
        return m_context.cancel_requested.load() ? Standard_True : Standard_False;
    }
    void Show(const Message_ProgressScope &, const Standard_Boolean) override
    {
        m_context.progress.store(GetPosition());
    }

private:
    WorkContext &m_context;
};

struct SewResult {
    enum class Status { OK, MULTI_SHELL, MAKE_SOLID_FAILED, CANCELLED } status = Status::MULTI_SHELL;
    int n_shells = 0;
    TopoDS_Solid solid;
};

// Pure function: takes/returns plain values only (no Group/Document access)
// so it's safe to run on a detached worker thread via run_with_timeout --
// see that header for why this needs to be bulletproof against the caller
// moving on before the worker finishes. `context.cancel_requested` is
// checked after each interruptible OpenCascade call; once it's seen, the
// (now unsafe to query further -- confirmed calling SewedShape() after a
// break segfaults) objects are abandoned immediately rather than touched
// again.
SewResult sew_into_solid_impl(const std::vector<MeshTriangle> &triangles, WorkContext &context)
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
    Handle(CancelToken) cancel_token = new CancelToken(context);
    context.phase_label.store("Sewing mesh…");
    sewing.Perform(cancel_token->Start());
    if (context.cancel_requested.load()) {
        result.status = SewResult::Status::CANCELLED;
        return result;
    }
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
    const TopoDS_Solid solid = mk_solid.Solid();

    // ShapeFix_Solid's FixOrientationMode (a whole-shell face-winding
    // consistency check) can dominate its total runtime -- confirmed
    // empirically at 30-40x the total time on some real meshes -- for
    // seemingly no benefit when the mesh's winding was already consistent.
    // But it's not always a no-op: on another real mesh, skipping it left
    // the shape not even closed into a proper solid. So: try without it
    // first, and only use that result if it's a fully valid solid; if not,
    // fall back to the slower, safer default -- see the plan for the
    // before/after numbers across several real meshes that led to this.
    context.progress.store(0.0);
    context.phase_label.store("Closing solid…");
    ShapeFix_Solid fix(solid);
    fix.FixShellTool()->FixOrientationMode() = 0;
    fix.Perform(cancel_token->Start());
    if (context.cancel_requested.load()) {
        result.status = SewResult::Status::CANCELLED;
        return result;
    }
    const TopoDS_Shape fast_result = fix.Solid();
    if (fast_result.ShapeType() == TopAbs_SOLID && BRepCheck_Analyzer(fast_result).IsValid()) {
        result.solid = TopoDS::Solid(fast_result);
        result.status = SewResult::Status::OK;
        return result;
    }

    context.progress.store(0.0);
    context.phase_label.store("Closing solid (thorough)…");
    ShapeFix_Solid fix2(solid);
    fix2.Perform(cancel_token->Start());
    if (context.cancel_requested.load()) {
        result.status = SewResult::Status::CANCELLED;
        return result;
    }
    const TopoDS_Shape slow_result = fix2.Solid();
    if (slow_result.ShapeType() != TopAbs_SOLID) {
        result.status = SewResult::Status::MAKE_SOLID_FAILED;
        return result;
    }
    result.solid = TopoDS::Solid(slow_result);
    result.status = SewResult::Status::OK;
    return result;
}

struct MergeResult {
    bool ok = false;
    TopoDS_Solid solid;
};

// Always run after sewing, for every algorithm -- merging coplanar triangles
// back into real flat faces is a strict quality improvement (a cube reads as
// 6 faces instead of 12) with no accuracy cost, so it isn't a separate
// user-facing choice. ShapeUpgrade_UnifySameDomain::Build() has no
// progress/cancellation hook at all, so `context.progress`/`cancel_requested`
// are never touched here -- this path can only ever fall back to
// bounded_execute's orphan-on-timeout behavior, never stop cleanly or report
// real progress like the sewing path can. Still sets a phase label so the
// dialog explains the (indeterminate) wait rather than just going quiet.
MergeResult merge_coplanar_faces_impl(TopoDS_Solid solid, WorkContext &context)
{
    context.phase_label.store("Merging coplanar faces…");
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

} // namespace

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupConvertMesh &group)
{
    auto triangles = collect_triangles(doc, group);
    if (triangles.empty()) {
        if (group.m_solve_messages.empty())
            group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR, "Source mesh has no geometry");
        return nullptr;
    }

    if (group.m_algorithm == Algorithm::WELD_SEW) {
        triangles = weld_mesh(triangles, group.m_weld_tolerance);
    }
    else if (group.m_algorithm == Algorithm::DECIMATE_SEW) {
        triangles = decimate_mesh(triangles, group.m_decimate_target_faces);
    }
    else if (triangles.size() > s_direct_triangle_cap) {
        group.m_solve_messages.emplace_back(
                GroupStatusMessage::Status::ERR,
                std::format("Mesh has {} triangles; Direct supports up to {} -- use Weld + Sew or Decimate + Sew "
                            "instead",
                            triangles.size(), s_direct_triangle_cap));
        return nullptr;
    }

    bool sew_cancelled = false;
    auto sew_result = run_with_timeout(
            [triangles](WorkContext &context) { return sew_into_solid_impl(triangles, context); }, s_geometry_timeout,
            &sew_cancelled);
    if (!sew_result) {
        // Fell back to orphaning the worker -- either it was cancelled but
        // didn't stop within the grace period, or the 30s timeout hit with
        // no cancel request at all.
        if (sew_cancelled)
            group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR, "Conversion was cancelled");
        else
            group.m_solve_messages.emplace_back(
                    GroupStatusMessage::Status::ERR,
                    std::format("Conversion is taking too long and was aborted after {}s -- this mesh may be too "
                                "complex or malformed for this algorithm; try a different algorithm or a lower "
                                "decimation target",
                                s_geometry_timeout.count()));
        return nullptr;
    }
    if (sew_result->status == SewResult::Status::CANCELLED) {
        // Cooperative cancellation via UserBreak() -- stopped cleanly well
        // within the grace period, no orphaned thread left behind.
        group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR, "Conversion was cancelled");
        return nullptr;
    }
    if (sew_result->status == SewResult::Status::MULTI_SHELL) {
        group.m_solve_messages.emplace_back(
                GroupStatusMessage::Status::ERR,
                std::format("Mesh did not sew into a single closed surface (got {} separate piece(s)) -- this mesh "
                            "isn't clean/watertight enough to convert; try a lower decimation target or a "
                            "different algorithm",
                            std::max(sew_result->n_shells, 1)));
        return nullptr;
    }
    if (sew_result->status == SewResult::Status::MAKE_SOLID_FAILED) {
        group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR, "Could not close mesh into a solid");
        return nullptr;
    }
    TopoDS_Solid solid = sew_result->solid;

    bool merge_cancelled = false;
    auto merge_result = run_with_timeout(
            [solid](WorkContext &context) { return merge_coplanar_faces_impl(solid, context); }, s_geometry_timeout,
            &merge_cancelled);
    if (!merge_result) {
        if (merge_cancelled)
            group.m_solve_messages.emplace_back(GroupStatusMessage::Status::ERR, "Conversion was cancelled");
        else
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
