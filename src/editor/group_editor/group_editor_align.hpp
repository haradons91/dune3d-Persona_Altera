#pragma once
#include "group_editor_face_operation.hpp"

namespace dune3d {

class GroupEditorAlign : public GroupEditorFaceOperation {
public:
    GroupEditorAlign(Core &core, const UUID &group_uu);
};

} // namespace dune3d
