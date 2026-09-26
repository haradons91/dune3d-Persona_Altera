#pragma once
#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"

namespace dune3d {

class EntitySTL;

// Imports an STL mesh as a positioned, movable reference body (EntitySTL) --
// see the STL import plan. 3MF is not supported yet: this OpenCascade
// install has no 3MF reader, so the file dialog only offers .stl for now.
class ToolImportSTL : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    CanBegin can_begin() override;
    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;

    std::set<InToolActionID> get_actions() const override
    {
        using I = InToolActionID;
        return {I::LMB, I::CANCEL, I::RMB, I::TOGGLE_LOCK_ROTATION_CONSTRAINT, I::ROTATE_X, I::ROTATE_Y,
                I::ROTATE_Z};
    }

private:
    EntitySTL *m_mesh = nullptr;
    bool m_lock_rotation = false;

    void update_tip();
};
} // namespace dune3d
