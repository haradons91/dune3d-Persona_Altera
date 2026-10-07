#pragma once

#include "tool_common.hpp"
#include <glm/glm.hpp>
#include <map>

namespace dune3d {

class Entity;

// Scales every selected 2D entity about the active workplane's origin
// (0,0) by a factor entered via the generic datum dialog
// (show_enter_datum_window, DatumUnit::RATIO) -- same fixed-origin
// convention GroupScale used for the solid-modeling Modify dropdown,
// rather than a picked center point.
class ToolSketchScale : public ToolCommon {
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
    std::vector<Entity *> m_entities;
    // Original point values per entity, keyed by point index, cached in
    // begin() so CLOSE can restore them exactly (scaling is always applied
    // fresh from these originals, not accumulated across UPDATE events).
    std::map<Entity *, std::map<unsigned int, glm::dvec2>> m_originals;

    void apply_scale(double factor);
};

} // namespace dune3d
