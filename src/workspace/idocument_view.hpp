#pragma once
#include "util/uuid.hpp"

namespace dune3d {

class EntityView;

class IDocumentView {
public:
    virtual bool document_is_visible() const = 0;
    virtual bool body_is_visible(const UUID &uu) const = 0;
    virtual bool body_solid_model_is_visible(const UUID &uu) const = 0;
    virtual bool group_is_visible(const UUID &uu) const = 0;
    // Whether the "Sketches" folder is checked for a given Document -- keyed
    // by the owning Component's UUID, or a nil UUID for the root document's
    // own folder (see WorkspaceBrowser::populate_body_store()). Unlike
    // group/body visibility, a sketch folder has no group/body UUID of its
    // own to key off of, since it spans many independent sketch groups.
    virtual bool sketch_folder_is_visible(const UUID &uu) const = 0;
    // Same idea as sketch_folder_is_visible(), for the "Meshes" folder
    // spanning imported STL/3MF bodies.
    virtual bool mesh_folder_is_visible(const UUID &uu) const = 0;
    virtual const EntityView *get_entity_view(const UUID &uu) const = 0;
};

} // namespace dune3d
