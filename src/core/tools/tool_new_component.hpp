#pragma once
#include "tool_common.hpp"

namespace dune3d {

// "New Component": creates a brand new, empty Component (just its own
// Reference group, no bodies/sketches yet) and places one Occurrence of it
// at the root, ready to be descended into and built up -- matching Fusion
// 360's "New Component" (as opposed to "New Component from Body", which
// extracts existing content; see ToolCreateComponent).
class ToolNewComponent : public ToolCommon {
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
