#include "group_editor_align.hpp"

namespace dune3d {

GroupEditorAlign::GroupEditorAlign(Core &core, const UUID &group_uu) : GroupEditorFaceOperation(core, group_uu)
{
    construct();
}

} // namespace dune3d
