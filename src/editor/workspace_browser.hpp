#pragma once
#include <gtkmm.h>
#include <set>
#include "util/uuid.hpp"
#include "document/group/group.hpp"
#include "document/document.hpp"

namespace dune3d {

class Core;
class DocumentView;
class IDocumentInfo;
enum class WorkspaceBrowserAddGroupMode { WITH_BODY, WITHOUT_BODY };

class WorkspaceBrowser : public Gtk::Box {
public:
    WorkspaceBrowser(Core &core, std::optional<UUID> document_uuid = {});

    void set_document(const UUID &document_uuid);
    void set_body_checked(const UUID &document_uuid, const UUID &body_uuid, bool checked);

    void update_documents(const std::map<UUID, DocumentView> &doc_views);
    void update_current_group(const std::map<UUID, DocumentView> &doc_views);
    void update_needs_save();


    using type_signal_group_selected = sigc::signal<void(UUID, UUID)>;
    type_signal_group_selected signal_group_selected()
    {
        return m_signal_group_selected;
    }

    type_signal_group_selected signal_group_activated()
    {
        return m_signal_group_activated;
    }

    type_signal_group_selected signal_rename_body()
    {
        return m_signal_rename_body;
    }

    type_signal_group_selected signal_set_body_color()
    {
        return m_signal_set_body_color;
    }

    type_signal_group_selected signal_reset_body_color()
    {
        return m_signal_reset_body_color;
    }

    type_signal_group_selected signal_export_body_stl()
    {
        return m_signal_export_body_stl;
    }

    type_signal_group_selected signal_export_body_step()
    {
        return m_signal_export_body_step;
    }

    // Right-click on the top-level document row -- always root-relative,
    // there's no occurrence_path to speak of at that level.
    using type_signal_new_component = sigc::signal<void(UUID)>;
    type_signal_new_component signal_new_component()
    {
        return m_signal_new_component;
    }

    // Right-click on a plain (non-occurrence) body row: extract it into a
    // new Component, same as ToolCreateComponent.
    type_signal_group_selected signal_new_component_from_body()
    {
        return m_signal_new_component_from_body;
    }

    // Right-click on an Occurrence's own row: place another Occurrence of
    // that same Component (a live-linked instance, not a copy).
    type_signal_group_selected signal_new_instance()
    {
        return m_signal_new_instance;
    }

    // Dragging a Sketch/BodyN/Occurrence row (root-level or nested, to any
    // depth) onto either a placed component's row or the top-level document
    // row: move it (and everything it depends on, in both directions) into
    // that component, or back out to the root. (doc, the dragged row's own
    // occurrence_path, dragged seed group, target occurrence group -- nil
    // target UUID means "move to the root").
    using type_signal_move_group_into_component = sigc::signal<void(UUID, std::vector<UUID>, UUID, UUID)>;
    type_signal_move_group_into_component signal_move_group_into_component()
    {
        return m_signal_move_group_into_component;
    }

    // (doc, occurrence_path of the row itself -- empty for root-level, same
    // convention as everywhere else this session -- group/body uuid, checked).
    using type_signal_group_checked = sigc::signal<void(UUID, std::vector<UUID>, UUID, bool)>;
    using type_signal_document_checked = sigc::signal<void(UUID, bool)>;
    using type_signal_origin_checked = sigc::signal<void(UUID, bool)>;
    using type_signal_sketches_checked = sigc::signal<void(UUID, std::vector<UUID>, bool)>;
    using type_signal_meshes_checked = sigc::signal<void(UUID, std::vector<UUID>, bool)>;
    type_signal_group_checked signal_group_checked()
    {
        return m_signal_group_checked;
    }

    type_signal_group_checked signal_body_checked()
    {
        return m_signal_body_checked;
    }

    type_signal_group_checked signal_body_solid_model_checked()
    {
        return m_signal_body_solid_model_checked;
    }

    type_signal_document_checked signal_document_checked()
    {
        return m_signal_document_checked;
    }

    type_signal_origin_checked signal_origin_checked()
    {
        return m_signal_origin_checked;
    }

    type_signal_sketches_checked signal_sketches_checked()
    {
        return m_signal_sketches_checked;
    }

    type_signal_meshes_checked signal_meshes_checked()
    {
        return m_signal_meshes_checked;
    }

    using type_signal_delete_current_group = sigc::signal<void()>;

    using AddGroupMode = WorkspaceBrowserAddGroupMode;
    using type_signal_add_group = sigc::signal<void(Group::Type, AddGroupMode)>;

    type_signal_delete_current_group signal_delete_current_group()
    {
        return m_signal_delete_current_group;
    }

    type_signal_add_group signal_add_group()
    {
        return m_signal_add_group;
    }

    using type_signal_move_group = sigc::signal<void(Document::MoveGroup)>;
    type_signal_move_group signal_move_group()
    {
        return m_signal_move_group;
    }

    using type_signal_close_document = sigc::signal<void(UUID)>;
    type_signal_close_document signal_close_document()
    {
        return m_signal_close_document;
    }

    using type_signal_activate_link = sigc::signal<void(std::string)>;
    type_signal_activate_link signale_active_link()
    {
        return m_signal_activate_link;
    }

    using type_signal_item_expanded = sigc::signal<bool(UUID, bool)>;
    type_signal_item_expanded signal_body_expanded()
    {
        return m_signal_body_expanded;
    }

    // Fired on double-click of an Occurrence's own row, or of a group row
    // nested inside one -- the only interaction the recursive (component-
    // aware) part of the tree supports for now, matching how Milestone 4/5
    // brought read-only selection before editing. The path is root-relative
    // (EntityOccurrence UUIDs), ready for Core::set_active_occurrence_path();
    // group is set (and non-root) when a specific group inside that document
    // was double-clicked, so the caller can also select it as the current
    // group after descending.
    using type_signal_occurrence_activated = sigc::signal<void(std::vector<UUID>, UUID)>;
    type_signal_occurrence_activated signal_occurrence_activated()
    {
        return m_signal_occurrence_activated;
    }


    void group_prev_next(int dir);
    void select_group(const UUID &uu);

    void show_toast(const std::string &msg);

private:
    Gtk::ListView *m_view = nullptr;


    class DocumentItem;
    class BodyItem;
    class GroupItem;
    class WorkspaceRow;
    friend class WorkspaceRow;
    Glib::RefPtr<Gio::ListStore<DocumentItem>> m_document_store;

    Glib::RefPtr<Gio::ListModel> create_model(const Glib::RefPtr<Glib::ObjectBase> &item = {});
    Glib::RefPtr<Gtk::TreeListModel> m_model;
    Glib::RefPtr<Gtk::SingleSelection> m_selection_model;
    Gtk::Revealer *m_toast_revealer = nullptr;
    Gtk::Label *m_toast_label = nullptr;
    Gtk::InfoBar *m_info_bar = nullptr;
    Gtk::Image *m_info_bar_icon = nullptr;
    Gtk::Label *m_info_bar_label = nullptr;

    Core &m_core;
    std::optional<UUID> m_document_uuid;

    type_signal_group_selected m_signal_group_selected;
    type_signal_group_selected m_signal_group_activated;
    type_signal_group_checked m_signal_group_checked;
    type_signal_group_checked m_signal_body_checked;
    type_signal_group_checked m_signal_body_solid_model_checked;
    type_signal_document_checked m_signal_document_checked;
    type_signal_origin_checked m_signal_origin_checked;
    type_signal_sketches_checked m_signal_sketches_checked;
    type_signal_meshes_checked m_signal_meshes_checked;

    type_signal_delete_current_group m_signal_delete_current_group;
    type_signal_add_group m_signal_add_group;
    type_signal_move_group m_signal_move_group;
    type_signal_close_document m_signal_close_document;
    type_signal_activate_link m_signal_activate_link;

    type_signal_group_selected m_signal_rename_body;
    type_signal_group_selected m_signal_set_body_color;
    type_signal_group_selected m_signal_reset_body_color;
    type_signal_group_selected m_signal_export_body_stl;
    type_signal_group_selected m_signal_export_body_step;
    type_signal_new_component m_signal_new_component;
    type_signal_group_selected m_signal_new_component_from_body;
    type_signal_group_selected m_signal_new_instance;
    type_signal_move_group_into_component m_signal_move_group_into_component;

    type_signal_item_expanded m_signal_body_expanded;
    type_signal_occurrence_activated m_signal_occurrence_activated;

    void emit_add_group(GroupType type, AddGroupMode add_group_mode = AddGroupMode::WITHOUT_BODY);
    bool emit_body_expanded(const UUID &body_uu, bool expanded);

    // Recursion point for the component-aware tree: populates body_store from
    // doc's own groups exactly like the non-recursive code this replaced did
    // for the root document, except a GroupOccurrence additionally resolves
    // its Component against `root` (components only ever live in the root's
    // own registry, never in a nested Document -- see Component's own
    // comment) and recurses into its m_document, one level deeper in
    // occurrence_path each time.
    static void populate_body_store(const Document &root, const Document &doc, const UUID &doc_uuid,
                                     const std::vector<UUID> &occurrence_path, const UUID &component_uuid,
                                     const Glib::RefPtr<Gio::ListStore<BodyItem>> &body_store);

    // Refreshes checkbox state (m_check_active) for a nested BodyItem store
    // (an Occurrence's m_occurrence_children) and everything under it,
    // recursing into further-nested Occurrences. update_current_group()
    // only walks the root-level m_body_store/m_group_store directly; this
    // is its counterpart for content inside a component, called once per
    // occurrence row found there. No occurrence_path resolution is needed --
    // DocumentView::group_is_visible()/body_is_visible() are a flat map
    // keyed by the group's own globally-unique UUID, the same lookup as any
    // root-level group.
    static void update_nested_checkbox_state(const Glib::RefPtr<Gio::ListModel> &store, const DocumentView &doc_view,
                                             bool parent_enabled = true);

    // Refreshes one generic body row's own fields (checkbox, solid-model
    // toggle, expansion, and its own feature/group children's DOF/name/
    // status/checkbox) -- factored out of update_current_group()'s own
    // per-document loop so the same logic can also refresh each mesh row
    // nested inside a "Meshes" folder (see populate_body_store()), which
    // isn't a direct child of the document's own m_body_store any more.
    // parent_enabled is the folder/ancestor gate (always true at the root
    // document level, where there's no such folder above a generic body).
    static void refresh_body_row(BodyItem &it_body, const Document &doc, const DocumentView &doc_view,
                                 const std::set<UUID> &source_groups, bool is_current_doc,
                                 const UUID &current_group_uu, const UUID &body_uu, bool parent_enabled);

    void block_signals();
    void unblock_signals();
    unsigned int m_blocked_count = 0;

    void select_group(const UUID &doc_uu, const UUID &uu);


    static void update_name(DocumentItem &it, IDocumentInfo &doci);

    sigc::connection m_toast_connection;

    Gtk::PopoverMenu *m_body_popover = nullptr;
    // Same popover widget, three different menu models swapped in right
    // before popup() depending on what was right-clicked -- their available
    // actions genuinely differ (a plain body can become a component; an
    // Occurrence can be instanced again; the document row can only start a
    // brand new component), so one shared model can't cleanly serve all three.
    Glib::RefPtr<Gio::Menu> m_document_menu = nullptr;
    Glib::RefPtr<Gio::Menu> m_body_menu_plain = nullptr;
    Glib::RefPtr<Gio::Menu> m_body_menu_occurrence = nullptr;
    Glib::RefPtr<Gio::Menu> m_body_label_menu = nullptr;
    UUID m_body_menu_document;
    UUID m_body_menu_body;
    Glib::RefPtr<Gio::SimpleAction> m_reset_body_color_action;
};
} // namespace dune3d
