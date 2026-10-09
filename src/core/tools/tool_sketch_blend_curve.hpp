#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class Entity;
class EntityBezier2D;

// Bridges a gap between two selected curve endpoints -- EntityLine2D,
// EntityArc2D, or EntityBezier2D (a "conic" drawn by ToolDrawConic is an
// EntityBezier2D under the hood), any two in any combination -- with a
// tangent-continuous cubic Bezier. Structurally like ToolSketchFillet's
// two-line corner detection, but connecting the nearest pair of
// endpoints (which may be far apart, unlike Fillet's shared corner)
// rather than rounding a shared corner, and needing no trim step since
// it only adds a new entity. "Bulge" (how far the curve bows out, as a
// fraction of the chord length between the two connection points) is
// live-dragged from the cursor like Fillet's radius, with the same
// inline on-canvas textbox for typing an exact value.
//
// A line endpoint gets ConstraintBezierLineTangent; an arc or conic
// endpoint gets ConstraintArcArcTangent (the generic curve-curve tangent
// constraint this codebase already uses for any ARC_2D/BEZIER_2D pair,
// despite the name) -- both persistently enforce tangency, so the blend
// stays tangent if the connected curve is later edited.
//
// Hover-driven like ToolSketchFillet: pre-selecting two curves before
// invoking still works, but isn't required -- click to pick each curve
// one at a time if nothing (or only one) was pre-selected.
class ToolSketchBlendCurve : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    std::set<InToolActionID> get_actions() const override
    {
        using I = InToolActionID;
        return {I::LMB, I::RMB, I::CANCEL};
    }
    bool is_specific() override
    {
        return true;
    }
    CanBegin can_begin() override;

private:
    Entity *m_curve1 = nullptr;
    Entity *m_curve2 = nullptr;
    EntityBezier2D *m_preview = nullptr;
    glm::dvec2 m_p1{}, m_p2{};
    glm::dvec2 m_dir1{}, m_dir2{};
    int m_bi = 0, m_bj = 0;
    double m_chord = 0;
    double m_bulge = 0.5;
    bool m_bulge_locked = false;

    bool select_curve(Entity *&curve);
    bool setup_connection();
    void apply_bulge(double bulge);
    double compute_live_bulge(glm::dvec2 cursor) const;
    void update_preview();
};

} // namespace dune3d
