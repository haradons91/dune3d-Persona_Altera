#pragma once
#include "util/uuid.hpp"
#include "document/entity/entity_and_point.hpp"
#include <filesystem>
#include <vector>

namespace dune3d {
class Document;
class ICore {
public:
    virtual bool has_documents() const = 0;
    // get_current_document()/get_current_group() resolve through
    // Core::set_active_occurrence_path() while descended into an
    // occurrence, so most Tools (which mutate "the current document")
    // don't need to know occurrences exist at all. A Tool that instead
    // needs the true root regardless of descend state -- e.g. one listing
    // or looking up Components, which only the root's own m_components map
    // ever populates -- uses get_root_document()/get_active_occurrence_path()
    // instead.
    virtual Document &get_current_document() = 0;
    virtual Document &get_root_document() = 0;
    virtual const std::vector<UUID> &get_active_occurrence_path() const = 0;
    virtual const Document &get_current_last_document() const = 0;
    virtual UUID get_current_group() const = 0;
    virtual UUID get_current_workplane() const = 0;
    virtual std::filesystem::path get_current_document_directory() const = 0;

    using DraggedList = std::vector<EntityAndPoint>;

    virtual void solve_current(const DraggedList &dragged = {}) = 0;
};
} // namespace dune3d
