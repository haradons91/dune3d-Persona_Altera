#pragma once

#include "tool_common.hpp"
#include <glm/glm.hpp>
#include <map>

namespace dune3d {

class Entity;

// Backs SKETCH_MOVE (translate the selection in place) and SKETCH_COPY
// (clone the selection first, then translate the clones, leaving the
// originals untouched -- tool_paste.cpp's clone + UUID-translation-map
// pattern, reused here since 2D sketch entities have none of the
// document/body complexity that left the solid-modeling Move/Copy's own
// "Copy" checkbox unimplemented). The offset is entered as two sequential
// datum dialogs (X then Y), since there's no existing two-field dialog.
class ToolSketchMoveCopy : public ToolCommon {
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
    enum class Stage { X, Y };
    Stage m_stage = Stage::X;
    double m_dx = 0;

    std::vector<Entity *> m_entities;
    std::map<Entity *, std::map<unsigned int, glm::dvec2>> m_originals;

    void apply_translation(double dx, double dy);
};

} // namespace dune3d
