#pragma once
#include "util/uuid.hpp"
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace dune3d {

class Document;
class Component;

// Where a SelectableRef::occurrence_path actually points: which Document
// owns `item`, which Component that Document belongs to (nullptr for an
// empty path, i.e. the root itself), and the accumulated world placement of
// that Document's content (matching Renderer's own double-precision
// accumulation in visit(const EntityOccurrence&), so gizmos/dimensions
// placed at the returned origin/rotation land in the right spot).
struct ResolvedLocation {
    Document &doc;
    Component *component;
    glm::dvec3 accumulated_origin;
    glm::dquat accumulated_rotation;
};

// Throws (out_of_range/bad_cast, matching Document::get_entity/get_group's
// own behavior) if path references an occurrence or component that no
// longer exists -- callers resolving a possibly-stale SelectableRef (e.g.
// from a previous frame) should be prepared for that, same as they already
// must be for a plain entity UUID.
ResolvedLocation resolve_occurrence_path(Document &root, const std::vector<UUID> &path);

} // namespace dune3d
