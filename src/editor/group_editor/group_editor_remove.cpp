#include "group_editor_remove.hpp"

namespace dune3d {

GroupEditorRemove::GroupEditorRemove(Core &core, const UUID &group_uu) : GroupEditorFaceOperation(core, group_uu)
{
    construct();
}

} // namespace dune3d
