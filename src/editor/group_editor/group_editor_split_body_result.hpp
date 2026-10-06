#pragma once
#include "group_editor.hpp"

namespace dune3d {

class GroupEditorSplitBodyResult : public GroupEditor {
public:
    GroupEditorSplitBodyResult(Core &core, const UUID &group_uu);
};

} // namespace dune3d
