#pragma once
#include "tool_common.hpp"

namespace dune3d {

// "New Component from Selection": Body isn't canvas-selectable (see
// SelectableRef::Type), so this operates on whichever body owns the
// currently active group instead, matching how other body-scoped
// operations in this app key off the current group. Moves that body's
// groups (via Document::extract_groups) into a freshly created Component,
// and leaves a GroupOccurrence with an identity placement in their place so
// nothing visually moves.
class ToolCreateComponent : public ToolCommon {
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
