#include "tool_sketch_trim_extend.hpp"
#include "document/document.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "editor/editor_interface.hpp"
#include "core/tool_id.hpp"
#include "tool_common_impl.hpp"

#include <array>
#include <cmath>
#include <optional>

namespace dune3d {

// Returns the intersection of the two infinite lines through (p1,d1) and
// (p2,d2), plus how far along EACH line (as a 0..1 fraction of that
// line's own p1->p2 segment) the intersection falls -- the caller decides
// what tolerance around [0,1] counts as "a real crossing" vs. where the
// lines would only meet if extended.
struct LineIntersection {
    glm::dvec2 point;
    double t1, t2;
};

static std::optional<LineIntersection> intersect(glm::dvec2 p1, glm::dvec2 d1, glm::dvec2 p2, glm::dvec2 d2)
{
    const double denom = d1.x * d2.y - d1.y * d2.x;
    if (std::abs(denom) < 1e-9)
        return std::nullopt;
    const double t1 = ((p2.x - p1.x) * d2.y - (p2.y - p1.y) * d2.x) / denom;
    const double t2 = ((p2.x - p1.x) * d1.y - (p2.y - p1.y) * d1.x) / denom;
    return LineIntersection{p1 + d1 * t1, t1, t2};
}

ToolBase::CanBegin ToolSketchTrimExtend::can_begin()
{
    if (get_workplane_uuid() == UUID())
        return false;
    if (m_tool_id == ToolID::SKETCH_TRIM)
        return true;

    // SKETCH_EXTEND: old one-shot behavior, unchanged for now.
    EntityLine2D *lines[2] = {nullptr, nullptr};
    for (const auto &sr : m_selection) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto *line = dynamic_cast<EntityLine2D *>(&get_entity(sr.item));
        if (!line || line->m_wrkpl != get_workplane_uuid())
            continue;
        if (!lines[0])
            lines[0] = line;
        else if (line != lines[0]) {
            lines[1] = line;
            break;
        }
    }
    if (!lines[0] || !lines[1])
        return false;
    return intersect(lines[0]->m_p1, lines[0]->m_p2 - lines[0]->m_p1, lines[1]->m_p1,
                      lines[1]->m_p2 - lines[1]->m_p1)
            .has_value();
}

ToolResponse ToolSketchTrimExtend::begin_extend_one_shot()
{
    EntityLine2D *line1 = nullptr;
    EntityLine2D *line2 = nullptr;
    for (const auto &sr : m_selection) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto *line = dynamic_cast<EntityLine2D *>(&get_entity(sr.item));
        if (!line || line->m_wrkpl != get_workplane_uuid())
            continue;
        if (!line1)
            line1 = line;
        else if (line != line1) {
            line2 = line;
            break;
        }
    }
    if (!line1 || !line2)
        return ToolResponse::end();

    const auto inter = intersect(line1->m_p1, line1->m_p2 - line1->m_p1, line2->m_p1, line2->m_p2 - line2->m_p1);
    if (!inter)
        return ToolResponse::end();

    auto move_nearer_endpoint = [&](EntityLine2D &line) -> unsigned int {
        const auto d_p1 = glm::length(line.m_p1 - inter->point);
        const auto d_p2 = glm::length(line.m_p2 - inter->point);
        if (d_p1 <= d_p2) {
            line.m_p1 = inter->point;
            return 1;
        }
        else {
            line.m_p2 = inter->point;
            return 2;
        }
    };
    const auto point1 = move_nearer_endpoint(*line1);
    const auto point2 = move_nearer_endpoint(*line2);

    auto &coincident = add_constraint<ConstraintPointsCoincident>();
    coincident.m_wrkpl = line1->m_wrkpl;
    coincident.m_entity1 = {line1->m_uuid, point1};
    coincident.m_entity2 = {line2->m_uuid, point2};

    set_current_group_solve_pending();
    return ToolResponse::commit();
}

ToolResponse ToolSketchTrimExtend::begin(const ToolArgs &args)
{
    if (m_tool_id != ToolID::SKETCH_TRIM)
        return begin_extend_one_shot();

    m_intf.enable_hover_selection();
    return ToolResponse();
}

bool ToolSketchTrimExtend::update_preview()
{
    // get_hover_selection() first, same order as before -- hover picking
    // seems to depend on being read before anything else touches canvas/
    // cursor state this frame.
    const auto hover = m_intf.get_hover_selection();

    // Unconditional from here on (not nested inside the "a line is
    // hovered" branch below): this is what drives the on-canvas crosshair
    // (it sets Editor's snap-indicator position as a side effect). Calling
    // it only when a line was hovered meant the crosshair froze in place
    // every frame the cursor wasn't exactly over a line, instead of
    // continuously tracking the mouse.
    const auto workplane = get_workplane();
    if (!workplane)
        return false;
    const auto cursor = workplane->project(get_cursor_pos_for_workplane(*workplane));

    EntityLine2D *line = nullptr;
    if (hover && hover->type == SelectableRef::Type::ENTITY)
        line = dynamic_cast<EntityLine2D *>(&get_entity(hover->item));
    if (!line || line->m_wrkpl != get_workplane_uuid()) {
        m_hovered_line = nullptr;
        if (m_preview)
            m_preview->m_visible = false;
        return false;
    }

    const auto d = line->m_p2 - line->m_p1;
    const auto len2 = glm::dot(d, d);
    if (len2 < 1e-12) {
        if (m_preview)
            m_preview->m_visible = false;
        return false;
    }
    const auto cursor_t = glm::dot(cursor - line->m_p1, d) / len2;

    EntityLine2D *best_other = nullptr;
    double best_t = 0;
    double best_dt = 1e18;
    for (auto &[uu, entity] : get_doc().m_entities) {
        auto *other = dynamic_cast<EntityLine2D *>(entity.get());
        if (!other || other == line || other->m_wrkpl != line->m_wrkpl)
            continue;
        const auto inter = intersect(line->m_p1, d, other->m_p1, other->m_p2 - other->m_p1);
        if (!inter)
            continue;
        // A real crossing: within both segments. For the OTHER line, close
        // to its own endpoint is fine (e.g. trimming back to meet an
        // L-shaped corner is normal) -- but for the HOVERED line, a
        // crossing close to ITS OWN existing endpoint isn't a trim target
        // at all, it's just that line already meeting a neighbor there
        // (e.g. every corner of a rectangle). Treating it as one collapses
        // the line onto its own other endpoint and piles a redundant
        // ConstraintPointOnLine on top of whatever coincidence constraint
        // already ties that corner together, which sent the solver into a
        // degenerate state that collapsed other connected sides too.
        constexpr double own_endpoint_margin = 0.02;
        if (inter->t1 < own_endpoint_margin || inter->t1 > 1 - own_endpoint_margin)
            continue;
        if (inter->t2 < -1e-4 || inter->t2 > 1 + 1e-4)
            continue;
        const auto dt = std::abs(inter->t1 - cursor_t);
        if (dt < best_dt) {
            best_dt = dt;
            best_t = inter->t1;
            best_other = other;
        }
    }

    if (!best_other) {
        m_hovered_line = nullptr;
        if (m_preview)
            m_preview->m_visible = false;
        return false;
    }

    m_hovered_line = line;
    m_other_line = best_other;
    m_trim_point = line->m_p1 + d * best_t;
    m_trim_point_index = cursor_t > best_t ? 2u : 1u;
    const auto removed_end = m_trim_point_index == 2u ? line->m_p2 : line->m_p1;

    if (!m_preview) {
        m_preview = &add_entity<EntityLine2D>();
        m_preview->m_wrkpl = line->m_wrkpl;
        m_preview->m_construction = true;
        m_preview->m_selection_invisible = true;
    }
    m_preview->m_visible = true;
    m_preview->m_p1 = m_trim_point;
    m_preview->m_p2 = removed_end;
    return true;
}

ToolResponse ToolSketchTrimExtend::update(const ToolArgs &args)
{
    if (m_tool_id != ToolID::SKETCH_TRIM)
        return ToolResponse();

    if (args.type == ToolEventType::MOVE) {
        update_preview();
        set_first_update_group_current();
        return ToolResponse();
    }

    if (args.type != ToolEventType::ACTION)
        return ToolResponse();

    switch (args.action) {
    case InToolActionID::LMB: {
        if (!m_hovered_line || !m_other_line)
            return ToolResponse();

        if (m_trim_point_index == 1u)
            m_hovered_line->m_p1 = m_trim_point;
        else
            m_hovered_line->m_p2 = m_trim_point;

        if (m_preview)
            get_doc().m_entities.erase(m_preview->m_uuid);

        // The trim point generally lands partway along the OTHER line's
        // interior, not at one of its own endpoints -- ConstraintPointOnLine
        // is the right constraint for "this point stays on that line",
        // unlike ConstraintPointsCoincident (which only relates two
        // specific, fixed points).
        auto &on_line = add_constraint<ConstraintPointOnLine>();
        on_line.m_wrkpl = m_hovered_line->m_wrkpl;
        on_line.m_point = {m_hovered_line->m_uuid, m_trim_point_index};
        on_line.m_line = m_other_line->m_uuid;
        set_current_group_solve_pending();
        return ToolResponse::commit();
    }

    case InToolActionID::RMB:
    case InToolActionID::CANCEL:
        if (m_preview)
            get_doc().m_entities.erase(m_preview->m_uuid);
        return ToolResponse::revert();

    default:
        return ToolResponse();
    }
}

} // namespace dune3d
