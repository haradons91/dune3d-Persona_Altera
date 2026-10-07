#pragma once

#include "tool_common.hpp"

namespace dune3d {

// Backs both SKETCH_TRIM and SKETCH_EXTEND identically: given two selected
// EntityLine2D on the active workplane, move each line's nearer endpoint to
// the intersection of their underlying infinite lines. Whether that
// shortens (trim) or lengthens (extend) either line is just a consequence
// of where the lines currently end relative to that intersection, not a
// different algorithm -- so one tool backs both menu entries. A one-shot
// action (no interactive loop), like ToolFlipArc.
class ToolSketchTrimExtend : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    bool is_specific() override
    {
        return false;
    }
    CanBegin can_begin() override;
};

} // namespace dune3d
