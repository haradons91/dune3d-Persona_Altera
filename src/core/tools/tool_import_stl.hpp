#pragma once
#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"

namespace dune3d {

class EntitySTL;
class EntityThreeMF;

// Imports a mesh (STL or 3MF, picked by the file dialog's extension) as a
// positioned, movable reference body -- see the STL/3MF import plans.
// Exactly one of m_stl/m_threemf is set for the lifetime of a single tool
// invocation, matching whichever file extension was picked; the two are
// otherwise driven identically (same fields, same placement UX).
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
    EntitySTL *m_stl = nullptr;
    EntityThreeMF *m_threemf = nullptr;
    bool m_lock_rotation = false;

    bool has_mesh() const
    {
        return m_stl || m_threemf;
    }
    void update_tip();
};
} // namespace dune3d
