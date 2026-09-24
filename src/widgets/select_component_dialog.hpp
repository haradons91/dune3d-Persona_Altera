#pragma once
#include <gtkmm.h>
#include "util/uuid.hpp"
#include "util/changeable.hpp"
#include <vector>

namespace dune3d {

class Document;

// Simple "pick one of the document's Components by name" dialog -- the
// closest existing precedent is SelectGroupDialog (src/widgets/), but that
// one is built around a ListView + a .ui resource factory for Groups
// specifically. This uses a plain Gtk::DropDown/Gtk::StringList instead, so
// no new .ui/gresource entry is needed for a first version of this dialog.
class SelectComponentDialog : public Gtk::Window, public Changeable {
public:
    explicit SelectComponentDialog(const Document &doc);

    // {} (invalid UUID) if the document has no components at all.
    UUID get_selected_component() const;

private:
    std::vector<UUID> m_component_uuids;
    Gtk::DropDown *m_dropdown = nullptr;
};

} // namespace dune3d
