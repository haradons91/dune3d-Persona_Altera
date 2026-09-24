#pragma once
#include "tool_common.hpp"

namespace dune3d {

// Places a new Occurrence of an existing Component, picked from a simple
// dialog. Placement starts at identity (origin, no rotation); moving it to
// a useful position is a follow-up tool (see the plan's Milestone 6).
class ToolInsertOccurrence : public ToolCommon {
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
