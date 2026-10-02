#pragma once
#include "group_editor.hpp"

namespace dune3d {

class GroupFaceOperation;

// Face-selection counterpart of GroupEditorLocalOperation (Fillet/Chamfer's
// shared editor base) -- just the "Select faces..." button, no radius (each
// concrete subclass adds its own parameters, if any, via construct_extra()).
class GroupEditorFaceOperation : public GroupEditor {
public:
    using GroupEditor::GroupEditor;

protected:
    void construct();
    virtual void construct_extra();

    GroupFaceOperation &get_group();
};

} // namespace dune3d
