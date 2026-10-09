#include "tool_sketch_blend_curve.hpp"
#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_bezier2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_bezier_line_tangent.hpp"
#include "document/constraint/constraint_arc_arc_tangent.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/rectangle_dimensions_window.hpp"
#include "tool_common_impl.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace dune3d {

static bool curve_on_workplane(Entity *e, const UUID &wrkpl)
{
    if (auto line = dynamic_cast<EntityLine2D *>(e))
        return line->m_wrkpl == wrkpl;
    if (auto arc = dynamic_cast<EntityArc2D *>(e))
        return arc->m_wrkpl == wrkpl;
    if (auto bezier = dynamic_cast<EntityBezier2D *>(e))
        return bezier->m_wrkpl == wrkpl;
    return false;
}

static std::array<Entity *, 2> selected_curves(const std::set<SelectableRef> &sel, const UUID &wrkpl, Document &doc)
{
    std::array<Entity *, 2> curves{nullptr, nullptr};
    for (const auto &sr : sel) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto *e = &doc.get_entity(sr.item);
        if (!curve_on_workplane(e, wrkpl))
            continue;
        if (!curves[0])
            curves[0] = e;
        else if (e != curves[0]) {
            curves[1] = e;
            break;
        }
    }
    return curves;
}

// m_p1/m_p2 (EntityLine2D/EntityBezier2D) or m_from/m_to (EntityArc2D) --
// the three curve types this tool connects. A "conic" drawn by
// ToolDrawConic is an EntityBezier2D under the hood, same as this
// tool's own preview curve.
static std::array<glm::dvec2, 2> curve_endpoints(Entity *e)
{
    if (auto line = dynamic_cast<EntityLine2D *>(e))
        return {line->m_p1, line->m_p2};
    if (auto arc = dynamic_cast<EntityArc2D *>(e))
        return {arc->m_from, arc->m_to};
    if (auto bezier = dynamic_cast<EntityBezier2D *>(e))
        return {bezier->m_p1, bezier->m_p2};
    return {glm::dvec2{}, glm::dvec2{}};
}

// EntityAndPoint's point index for curve_endpoints()[local_index]: 1 for
// index 0 (m_p1/m_from), 2 for index 1 (m_p2/m_to) -- same convention
// both EntityLine2D and EntityArc2D already use.
static unsigned int curve_point_index(int local_index)
{
    return local_index == 0 ? 1u : 2u;
}

// The tangent direction AT curve_endpoints()[local_index], pointing the
// way a Bezier control point should extend to continue smoothly past
// that endpoint (i.e. the same direction the curve is already heading
// there, extended outward).
static glm::dvec2 curve_tangent_dir(Entity *e, int local_index)
{
    if (auto line = dynamic_cast<EntityLine2D *>(e)) {
        const auto pts = curve_endpoints(e);
        const auto d = pts[local_index] - pts[1 - local_index];
        const auto len = glm::length(d);
        return len > 1e-9 ? d / len : glm::dvec2(1, 0);
    }
    if (auto arc = dynamic_cast<EntityArc2D *>(e)) {
        const auto pts = curve_endpoints(e);
        const auto radius_vec = pts[local_index] - arc->m_center;
        const auto len = glm::length(radius_vec);
        if (len < 1e-9)
            return {1, 0};
        const auto r = radius_vec / len;
        // EntityArc2D sweeps CCW from m_from (index 0) to m_to (index
        // 1). At m_to, continuing CCW past the end: rotate(r, +90deg).
        // At m_from, continuing CW past the start (the direction
        // "before" the arc began): rotate(r, -90deg).
        const auto rot = local_index == 1 ? M_PI / 2 : -M_PI / 2;
        return glm::dvec2(r.x * std::cos(rot) - r.y * std::sin(rot), r.x * std::sin(rot) + r.y * std::cos(rot));
    }
    if (auto bezier = dynamic_cast<EntityBezier2D *>(e)) {
        // Outward direction (away from the curve's own body, continuing
        // past the endpoint): at p1 that's away from c1, at p2 it's away
        // from c2 -- the control point always pulls INTO the curve, so
        // the outward tangent is the point-minus-control direction at
        // either end (unlike a line, which has no control points to
        // derive this from directly).
        const auto p = local_index == 0 ? bezier->m_p1 : bezier->m_p2;
        const auto c = local_index == 0 ? bezier->m_c1 : bezier->m_c2;
        const auto d = p - c;
        const auto len = glm::length(d);
        return len > 1e-9 ? d / len : glm::dvec2(1, 0);
    }
    return {1, 0};
}

ToolBase::CanBegin ToolSketchBlendCurve::can_begin()
{
    if (get_workplane_uuid() == UUID())
        return false;
    auto curves = selected_curves(m_selection, get_workplane_uuid(), get_doc());
    return curves[0] && curves[1];
}

ToolResponse ToolSketchBlendCurve::begin(const ToolArgs &args)
{
    auto curves = selected_curves(m_selection, get_workplane_uuid(), get_doc());
    m_curve1 = curves[0];
    m_curve2 = curves[1];
    if (!m_curve1 || !m_curve2)
        return ToolResponse::end();

    const auto curve1_points = curve_endpoints(m_curve1);
    const auto curve2_points = curve_endpoints(m_curve2);
    double best = 1e18;
    int bi = 0, bj = 0;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            const auto d = glm::length(curve1_points[i] - curve2_points[j]);
            if (d < best) {
                best = d;
                bi = i;
                bj = j;
            }
        }
    }
    m_bi = bi;
    m_bj = bj;
    m_p1 = curve1_points[bi];
    m_p2 = curve2_points[bj];
    m_dir1 = curve_tangent_dir(m_curve1, bi);
    m_dir2 = curve_tangent_dir(m_curve2, bj);
    m_chord = std::max(best, 1e-3);

    m_preview = &add_entity<EntityBezier2D>();
    m_preview->m_wrkpl = get_workplane_uuid();
    m_preview->m_selection_invisible = true;

    m_bulge = 0.5;
    update_preview();
    m_intf.show_circle_dimension(m_bulge);
    m_intf.canvas_update_from_tool();

    return ToolResponse();
}

void ToolSketchBlendCurve::apply_bulge(double bulge)
{
    m_preview->m_p1 = m_p1;
    m_preview->m_p2 = m_p2;
    m_preview->m_c1 = m_p1 + m_dir1 * m_chord * bulge;
    m_preview->m_c2 = m_p2 + m_dir2 * m_chord * bulge;
}

double ToolSketchBlendCurve::compute_live_bulge(glm::dvec2 cursor) const
{
    // The two connection points may be far apart on unrelated curves, so
    // (unlike Fillet's shared-corner radius) there's no single natural
    // "distance from X" reference -- use the cursor's distance from the
    // chord's own midpoint, scaled by half the chord length, so dragging
    // one half-chord-length away from the straight connecting line gives
    // a reasonably pronounced bulge of 1.0.
    const auto mid = (m_p1 + m_p2) / 2.;
    const auto half_chord = m_chord / 2.;
    const auto bulge = half_chord > 1e-9 ? glm::length(cursor - mid) / half_chord : 0.5;
    return std::clamp(bulge, 0.05, 3.0);
}

void ToolSketchBlendCurve::update_preview()
{
    const auto workplane = get_workplane();
    const auto cursor = workplane->project(get_cursor_pos_for_workplane(*workplane));

    const auto bulge = m_bulge_locked ? m_bulge : compute_live_bulge(cursor);
    apply_bulge(bulge);

    if (!m_bulge_locked) {
        m_bulge = bulge;
        m_intf.update_circle_dimension(m_bulge);
    }
    const auto mid = (m_p1 + m_p2) / 2.;
    m_intf.position_circle_dimension(workplane->transform(mid), workplane->transform(mid),
                                     workplane->transform(m_preview->m_c1));
}

ToolResponse ToolSketchBlendCurve::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        update_preview();
        set_first_update_group_current();
        return ToolResponse();
    }

    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataCircleDimensionsWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::UPDATE) {
                m_bulge_locked = true;
                m_bulge = std::clamp(data->diameter, 0.05, 3.0);
                apply_bulge(m_bulge);
                set_current_group_solve_pending();
                m_core.solve_current();
                set_first_update_group_current();
                m_intf.canvas_update_from_tool();
            }
        }
        return ToolResponse();
    }

    if (args.type != ToolEventType::ACTION)
        return ToolResponse();

    switch (args.action) {
    case InToolActionID::LMB: {
        m_intf.hide_circle_dimension();
        m_preview->m_selection_invisible = false;

        const auto wrkpl = get_workplane_uuid();

        auto &coincident1 = add_constraint<ConstraintPointsCoincident>();
        coincident1.m_wrkpl = wrkpl;
        coincident1.m_entity1 = {m_preview->m_uuid, 1};
        coincident1.m_entity2 = {m_curve1->m_uuid, curve_point_index(m_bi)};

        auto &coincident2 = add_constraint<ConstraintPointsCoincident>();
        coincident2.m_wrkpl = wrkpl;
        coincident2.m_entity1 = {m_preview->m_uuid, 2};
        coincident2.m_entity2 = {m_curve2->m_uuid, curve_point_index(m_bj)};

        // EntityLine2D gets the line-specific ConstraintBezierLineTangent.
        // Arc/conic connections use ConstraintArcArcTangent instead --
        // despite the name, it's the generic curve-curve tangent
        // constraint this codebase already uses for any ARC_2D/BEZIER_2D
        // pair (see ToolConstrainCurveCurveTangent), not arc-specific.
        if (dynamic_cast<EntityLine2D *>(m_curve1)) {
            auto &tangent1 = add_constraint<ConstraintBezierLineTangent>();
            tangent1.m_bezier = {m_preview->m_uuid, 1};
            tangent1.m_line = m_curve1->m_uuid;
        }
        else {
            auto &tangent1 = add_constraint<ConstraintArcArcTangent>();
            tangent1.m_arc1 = {m_preview->m_uuid, 1};
            tangent1.m_arc2 = {m_curve1->m_uuid, curve_point_index(m_bi)};
        }
        if (dynamic_cast<EntityLine2D *>(m_curve2)) {
            auto &tangent2 = add_constraint<ConstraintBezierLineTangent>();
            tangent2.m_bezier = {m_preview->m_uuid, 2};
            tangent2.m_line = m_curve2->m_uuid;
        }
        else {
            auto &tangent2 = add_constraint<ConstraintArcArcTangent>();
            tangent2.m_arc1 = {m_preview->m_uuid, 2};
            tangent2.m_arc2 = {m_curve2->m_uuid, curve_point_index(m_bj)};
        }

        set_current_group_solve_pending();
        return ToolResponse::commit();
    }

    case InToolActionID::RMB:
    case InToolActionID::CANCEL:
        m_intf.hide_circle_dimension();
        if (m_preview)
            get_doc().m_entities.erase(m_preview->m_uuid);
        return ToolResponse::revert();

    default:
        return ToolResponse();
    }
}

} // namespace dune3d
