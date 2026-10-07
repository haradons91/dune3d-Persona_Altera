#include "tool_sketch_trim_extend.hpp"
#include "document/document.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "editor/editor_interface.hpp"
#include "core/tool_id.hpp"
#include "tool_common_impl.hpp"

#include <cmath>
#include <optional>
#include <sstream>

namespace dune3d {

// Returns the intersection of the two infinite lines through (p1,d1) and
// (p2,d2), plus how far along EACH line (as a 0..1 fraction of that
// line's own p1->p2 segment, extending past either end as negative or
// >1) the intersection falls.
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
    return get_workplane_uuid() != UUID();
}

ToolResponse ToolSketchTrimExtend::begin(const ToolArgs &args)
{
    m_intf.enable_hover_selection();
    return ToolResponse();
}

bool ToolSketchTrimExtend::find_trim_target(EntityLine2D &line, glm::dvec2 d, double cursor_t,
                                            EntityLine2D *&best_other, double &best_t)
{
    double best_dt = 1e18;
    for (auto &[uu, entity] : get_doc().m_entities) {
        auto *other = dynamic_cast<EntityLine2D *>(entity.get());
        if (!other || other == &line || other == m_preview || other == m_preview2 || other->m_wrkpl != line.m_wrkpl)
            continue;
        const auto inter = intersect(line.m_p1, d, other->m_p1, other->m_p2 - other->m_p1);
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
        // already ties that corner together, which sends the solver into
        // a degenerate state that can collapse other connected sides too.
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
    return best_other != nullptr;
}

bool ToolSketchTrimExtend::find_extend_target(EntityLine2D &line, glm::dvec2 d, double cursor_t,
                                              EntityLine2D *&best_other, double &best_t,
                                              unsigned int &best_other_point_index)
{
    // Extend past whichever end the cursor is nominally closer to.
    const bool extend_p2_end = cursor_t > 0.5;
    double best_overshoot = 1e18;
    constexpr double beyond_margin = 1e-3;
    const double len2 = glm::dot(d, d);
    for (auto &[uu, entity] : get_doc().m_entities) {
        auto *other = dynamic_cast<EntityLine2D *>(entity.get());
        if (!other || other == &line || other == m_preview || other == m_preview2 || other->m_wrkpl != line.m_wrkpl)
            continue;
        const auto other_d = other->m_p2 - other->m_p1;
        const auto cross = d.x * other_d.y - d.y * other_d.x;
        if (std::abs(cross) < 1e-9) {
            // Parallel direction. Still a valid extend target when the two
            // lines are actually COLLINEAR (sitting end-to-end on the same
            // axis, just offset along it) rather than merely parallel at
            // some perpendicular offset -- intersect() can't find that
            // case at all since the cross-product formula it's built on is
            // undefined for any parallel pair. Check collinearity directly
            // via the other line's perpendicular distance from this line's
            // own infinite extension.
            const auto d_len = std::sqrt(len2);
            if (d_len < 1e-9)
                continue;
            const auto perp = glm::dvec2(-d.y, d.x) / d_len;
            if (std::abs(glm::dot(other->m_p1 - line.m_p1, perp)) > 1e-4)
                continue; // genuinely parallel, not collinear -- no target

            // Collinear: the candidate target is whichever of the other
            // line's own (fixed, unmoving) endpoints is nearer along this
            // line's own direction, on the correct side.
            for (auto [other_point, other_idx] :
                 {std::pair{other->m_p1, 1u}, std::pair{other->m_p2, 2u}}) {
                const auto t_candidate = glm::dot(other_point - line.m_p1, d) / len2;
                double overshoot;
                if (extend_p2_end) {
                    if (t_candidate < 1 + beyond_margin)
                        continue;
                    overshoot = t_candidate - 1;
                }
                else {
                    if (t_candidate > -beyond_margin)
                        continue;
                    overshoot = -t_candidate;
                }
                if (overshoot < best_overshoot) {
                    best_overshoot = overshoot;
                    best_t = t_candidate;
                    best_other = other;
                    best_other_point_index = other_idx;
                }
            }
            continue;
        }
        const auto inter = intersect(line.m_p1, d, other->m_p1, other_d);
        if (!inter)
            continue;

        double overshoot;
        if (extend_p2_end) {
            if (inter->t1 < 1 + beyond_margin)
                continue;
            overshoot = inter->t1 - 1;
        }
        else {
            if (inter->t1 > -beyond_margin)
                continue;
            overshoot = -inter->t1;
        }

        unsigned int other_point_index;
        if (inter->t2 >= -1e-4 && inter->t2 <= 1 + 1e-4) {
            // "T" against a line that's already long enough -- only the
            // hovered line needs to move.
            other_point_index = 0;
        }
        else if (inter->t2 < -beyond_margin) {
            // Open corner: the other line falls short of the same point
            // too, on its own p1 side -- extend it as well.
            other_point_index = 1;
        }
        else if (inter->t2 > 1 + beyond_margin) {
            other_point_index = 2;
        }
        else {
            // Right at the other line's own endpoint already -- not a
            // meaningful extend target (nothing to add).
            continue;
        }

        // Extend out to the NEAREST line it would hit first, not past a
        // closer one to a farther one.
        if (overshoot < best_overshoot) {
            best_overshoot = overshoot;
            best_t = inter->t1;
            best_other = other;
            best_other_point_index = other_point_index;
        }
    }
    return best_other != nullptr;
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
    // Never treat our own preview line(s) as "the hovered line" -- if
    // get_hover_selection() ever returns one (construction +
    // selection_invisible doesn't reliably keep it out of hover-pick,
    // apparently, even though it does keep it out of normal selection),
    // using it as m_hovered_line would add a real constraint referencing
    // an entity we then immediately erase a few lines later on commit,
    // leaving a dangling "entity UUID not found" constraint behind.
    if (line == m_preview || line == m_preview2)
        line = nullptr;
    if (!line || line->m_wrkpl != get_workplane_uuid()) {
        m_hovered_line = nullptr;
        if (m_preview)
            m_preview->m_visible = false;
        if (m_preview2)
            m_preview2->m_visible = false;
        {
            std::ostringstream os;
            os << "DBG: no line hovered (hover=" << (hover.has_value() ? "entity" : "none") << ", is_preview="
               << (hover && (hover->item == (m_preview ? m_preview->m_uuid : UUID())
                             || hover->item == (m_preview2 ? m_preview2->m_uuid : UUID())))
               << ")";
            m_intf.tool_bar_set_tool_tip(os.str());
        }
        return false;
    }

    const auto d = line->m_p2 - line->m_p1;
    const auto len2 = glm::dot(d, d);
    if (len2 < 1e-12) {
        if (m_preview)
            m_preview->m_visible = false;
        if (m_preview2)
            m_preview2->m_visible = false;
        return false;
    }
    const auto cursor_t = glm::dot(cursor - line->m_p1, d) / len2;

    EntityLine2D *best_other = nullptr;
    double best_t = 0;
    unsigned int other_point_index = 0;
    const bool found = m_tool_id == ToolID::SKETCH_TRIM
                                ? find_trim_target(*line, d, cursor_t, best_other, best_t)
                                : find_extend_target(*line, d, cursor_t, best_other, best_t, other_point_index);

    if (!found) {
        m_hovered_line = nullptr;
        if (m_preview)
            m_preview->m_visible = false;
        if (m_preview2)
            m_preview2->m_visible = false;
        {
            std::ostringstream os;
            os << "DBG: line hovered, cursor_t=" << cursor_t << ", no target found";
            m_intf.tool_bar_set_tool_tip(os.str());
        }
        return false;
    }
    {
        std::ostringstream os;
        os << "DBG: cursor_t=" << cursor_t << " best_t=" << best_t << " other_point_index=" << other_point_index
           << " hovered=" << static_cast<std::string>(line->m_uuid).substr(0, 6)
           << " other=" << static_cast<std::string>(best_other->m_uuid).substr(0, 6);
        m_intf.tool_bar_set_tool_tip(os.str());
    }

    m_hovered_line = line;
    m_other_line = best_other;
    m_other_point_index = other_point_index;
    m_trim_point = line->m_p1 + d * best_t;
    // Trim: which end is on the cursor's side of the crossing (the part to
    // remove). Extend: which end is being stretched out -- both come down
    // to "is best_t past the line's midpoint".
    m_trim_point_index = (m_tool_id == ToolID::SKETCH_TRIM ? cursor_t > best_t : best_t > 0.5) ? 2u : 1u;
    // The endpoint's CURRENT position (before it moves) -- for Trim this is
    // the far end of the segment being cut away; for Extend it's the near
    // end the line currently stops short at. Either way the preview line
    // between it and the new target shows exactly what's changing.
    const auto current_point = m_trim_point_index == 2u ? line->m_p2 : line->m_p1;

    if (!m_preview) {
        m_preview = &add_entity<EntityLine2D>();
        m_preview->m_wrkpl = line->m_wrkpl;
        m_preview->m_construction = true;
        m_preview->m_selection_invisible = true;
    }
    m_preview->m_visible = true;
    m_preview->m_p1 = m_trim_point;
    m_preview->m_p2 = current_point;

    // Open-corner case: the other line falls short of the same point too --
    // preview its own extension as a second temporary line. (For the
    // collinear-extend case this point is already exactly AT the other
    // line's own endpoint -- nothing to preview there, it doesn't move.)
    const bool other_also_moves = m_other_point_index != 0
                                   && glm::distance(m_trim_point, m_other_point_index == 2u ? m_other_line->m_p2
                                                                                             : m_other_line->m_p1)
                                              > 1e-6;
    if (other_also_moves) {
        const auto other_current_point =
                m_other_point_index == 2u ? m_other_line->m_p2 : m_other_line->m_p1;
        if (!m_preview2) {
            m_preview2 = &add_entity<EntityLine2D>();
            m_preview2->m_wrkpl = line->m_wrkpl;
            m_preview2->m_construction = true;
            m_preview2->m_selection_invisible = true;
        }
        m_preview2->m_visible = true;
        m_preview2->m_p1 = m_trim_point;
        m_preview2->m_p2 = other_current_point;
    }
    else if (m_preview2) {
        m_preview2->m_visible = false;
    }
    return true;
}

ToolResponse ToolSketchTrimExtend::update(const ToolArgs &args)
{
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

        // If the endpoint(s) we're about to move were already coincident
        // with something else (e.g. the shared corner of a connected
        // rectangle/polyline edge), drop that old relationship first --
        // otherwise the solver keeps honoring it too, dragging that OTHER
        // entity's endpoint along with the trim/extend instead of leaving
        // it in place. Trimming/extending one edge deliberately detaches
        // that corner; it shouldn't resize the rest of the connected
        // shape.
        auto drop_existing_coincidence = [&](const EntityAndPoint &moving_point) {
            for (auto it = get_doc().m_constraints.begin(); it != get_doc().m_constraints.end();) {
                auto *coincident = dynamic_cast<ConstraintPointsCoincident *>(it->second.get());
                if (coincident && (coincident->m_entity1 == moving_point || coincident->m_entity2 == moving_point))
                    it = get_doc().m_constraints.erase(it);
                else
                    ++it;
            }
        };
        drop_existing_coincidence({m_hovered_line->m_uuid, m_trim_point_index});
        if (m_other_point_index != 0)
            drop_existing_coincidence({m_other_line->m_uuid, m_other_point_index});

        if (m_trim_point_index == 1u)
            m_hovered_line->m_p1 = m_trim_point;
        else
            m_hovered_line->m_p2 = m_trim_point;

        if (m_preview)
            get_doc().m_entities.erase(m_preview->m_uuid);

        if (m_other_point_index != 0) {
            // Open corner: move the other line's own short endpoint to the
            // same point and join the two with a plain coincidence -- both
            // are now real endpoints meeting at a shared vertex.
            if (m_other_point_index == 1u)
                m_other_line->m_p1 = m_trim_point;
            else
                m_other_line->m_p2 = m_trim_point;
            if (m_preview2)
                get_doc().m_entities.erase(m_preview2->m_uuid);

            auto &coincident = add_constraint<ConstraintPointsCoincident>();
            coincident.m_wrkpl = m_hovered_line->m_wrkpl;
            coincident.m_entity1 = {m_hovered_line->m_uuid, m_trim_point_index};
            coincident.m_entity2 = {m_other_line->m_uuid, m_other_point_index};
        }
        else {
            // The target point generally lands partway along the OTHER
            // line's interior, not at one of its own endpoints --
            // ConstraintPointOnLine is the right constraint for "this
            // point stays on that line", unlike ConstraintPointsCoincident
            // (which only relates two specific, fixed points).
            auto &on_line = add_constraint<ConstraintPointOnLine>();
            on_line.m_wrkpl = m_hovered_line->m_wrkpl;
            on_line.m_point = {m_hovered_line->m_uuid, m_trim_point_index};
            on_line.m_line = m_other_line->m_uuid;
        }
        set_current_group_solve_pending();
        return ToolResponse::commit();
    }

    case InToolActionID::RMB:
    case InToolActionID::CANCEL:
        if (m_preview)
            get_doc().m_entities.erase(m_preview->m_uuid);
        if (m_preview2)
            get_doc().m_entities.erase(m_preview2->m_uuid);
        return ToolResponse::revert();

    default:
        return ToolResponse();
    }
}

} // namespace dune3d
