#pragma once
#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"

namespace dune3d {

class EntitySTEP;

// STUB: the file picker accepts .stl/.3mf, but the entity this tool creates
// hands the path to EntitySTEP::update_imported(), which only reads
// STEP/IGES via STEPImportManager. There is no mesh (STL/3MF) parser in the
// tree yet, so picking an actual mesh file will fail to import. Wire up a
// real mesh reader (e.g. OpenCASCADE's RWStl/RWGltf) before advertising this
// as working mesh import.
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
    EntitySTEP *m_mesh = nullptr;
    bool m_lock_rotation = false;

    void update_tip();
};
} // namespace dune3d
