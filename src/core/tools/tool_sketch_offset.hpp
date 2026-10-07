#pragma once

#include "tool_common.hpp"
#include <map>

namespace dune3d {

class Entity;

// Creates a new parallel entity for each selected EntityLine2D/
// EntityArc2D/EntityCircle2D, offset by a distance entered via the datum
// dialog -- the originals are left untouched (Fusion-style sketch
// Offset), with ConstraintParallel (lines) / ConstraintEqualRadius
// (arcs/circles) tying each new entity back to its source.
class ToolSketchOffset : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    bool is_specific() override
    {
        return true;
    }
    CanBegin can_begin() override;

private:
    std::map<Entity *, Entity *> m_source_to_preview;
    double m_last_distance = 0;

    void apply_offset(double distance);
};

} // namespace dune3d
