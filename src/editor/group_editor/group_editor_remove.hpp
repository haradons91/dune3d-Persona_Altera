#pragma once
#include "group_editor_face_operation.hpp"

namespace dune3d {

class GroupEditorRemove : public GroupEditorFaceOperation {
public:
    GroupEditorRemove(Core &core, const UUID &group_uu);
};

} // namespace dune3d
