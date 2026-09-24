#pragma once
#include "util/uuid.hpp"
#include "nlohmann/json_fwd.hpp"
#include "document.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace dune3d {
using json = nlohmann::json;

// A Component is a reusable, named definition: its own Document (entities,
// constraints, groups -- the same machinery a top-level open document uses).
// It is placed into a scene by an EntityOccurrence, which references it by
// UUID and supplies a placement transform. Many EntityOccurrences can
// reference the same Component, so it is stored once, in the *root*
// Document's m_components map -- never physically nested inside another
// Document -- which is what makes recursive occurrences (a Component whose
// own content places occurrences of other Components) cheap: an occurrence
// is a small record with a UUID, not a copy of the definition.
class Component {
public:
    explicit Component(const UUID &uu);
    // reference_group_uuid: see Document::Document(const UUID&) -- lets a
    // component created by extracting groups out of an existing document
    // share that document's Reference group UUID, so moved sketches' wrkpl
    // references keep resolving without remapping.
    explicit Component(const UUID &uu, const UUID &reference_group_uuid);
    explicit Component(const UUID &uu, const json &j, const std::filesystem::path &containing_dir);
    Component(const Component &other) = default; // Document's own copy ctor deep-clones m_document

    std::unique_ptr<Component> clone() const;

    UUID m_uuid;
    std::string m_name = "Component";
    Document m_document;

    json serialize() const;
};

} // namespace dune3d
