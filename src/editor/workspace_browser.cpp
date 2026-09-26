#include "workspace_browser.hpp"
#include "core/core.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/group_extrude.hpp"
#include "document/group/group_sketch.hpp"
#include "document/group/group_reference.hpp"
#include "document/group/igroup_source_group.hpp"
#include "document/group/group_occurrence.hpp"
#include "document/component.hpp"
#include "document/entity/entity_step.hpp"
#include "workspace/document_view.hpp"
#include "util/fs_util.hpp"
#include "util/debug.hpp"
#include "util/json_util.hpp"
#include "nlohmann/json.hpp"

namespace dune3d {
using json = nlohmann::json;

class WorkspaceBrowser::GroupItem : public Glib::Object {
public:
    static Glib::RefPtr<GroupItem> create()
    {
        return Glib::make_refptr_for_instance<GroupItem>(new GroupItem);
    }


    Glib::Property<Glib::ustring> m_name;
    Glib::Property<bool> m_active;
    Glib::Property<bool> m_check_active;
    Glib::Property<bool> m_check_sensitive;
    Glib::Property<int> m_dof;
    Glib::Property<GroupStatusMessage::Status> m_status;
    Glib::Property<Glib::ustring> m_status_message;
    Glib::Property<bool> m_source_group;
    UUID m_uuid;
    UUID m_doc;
    bool m_is_body_label = false;
    // Set for a row inside the "Sketches" folder -- together with
    // m_is_body_label, identifies which GroupItem rows are drag sources for
    // moving into a component (see WorkspaceRow's Gtk::DragSource).
    bool m_is_sketch = false;
    // Which Document (relative to the root) m_uuid actually lives in --
    // empty for the root document itself. Non-empty only for rows nested
    // under an Occurrence's row; see WorkspaceBrowser::populate_body_store().
    std::vector<UUID> m_occurrence_path;

    // No idea why the ObjectBase::get_type won't work for us but
    // reintroducing the method and using the name used by gtkmm seems
    // to work.
    static GType get_type()
    {
        // Let's cache once the type does exist.
        if (!gtype)
            gtype = g_type_from_name("gtkmm__CustomObject_GroupItem");
        return gtype;
    }

private:
    GroupItem()
        : Glib::ObjectBase("GroupItem"), m_name(*this, "name"), m_active(*this, "active", false),
          m_check_active(*this, "check_active", false), m_check_sensitive(*this, "check_sensitive", true),
          m_dof(*this, "dof"), m_status(*this, "status", GroupStatusMessage::Status::NONE),
          m_status_message(*this, "status_message"), m_source_group(*this, "source_group")
    {
    }

    static GType gtype;
};

GType WorkspaceBrowser::GroupItem::gtype;


class WorkspaceBrowser::BodyItem : public Glib::Object {
public:
    static Glib::RefPtr<BodyItem> create()
    {
        return Glib::make_refptr_for_instance<BodyItem>(new BodyItem);
    }


    Glib::Property<Glib::ustring> m_name;
    UUID m_uuid;
    UUID m_doc;
    Glib::Property<bool> m_check_active;
    Glib::Property<bool> m_expanded;
    Glib::Property<bool> m_check_sensitive;
    Glib::Property<bool> m_solid_model_active;
    Glib::Property<bool> m_has_color;
    Glib::Property<Gdk::RGBA> m_color;

    Glib::RefPtr<Gio::ListStore<GroupItem>> m_group_store;
    bool m_is_document_folder = false;
    bool m_is_origin_folder = false;
    bool m_is_sketch_folder = false;

    // Set for a row that represents a GroupOccurrence. Its children are a
    // recursive population of the placed Component's own document (Bodies/
    // Sketches folders, same shape as a top-level document) rather than the
    // usual single "BodyN" GroupItem -- returned from create_model() instead
    // of m_group_store when set. Declared as the base ListModel interface
    // (not Gio::ListStore<BodyItem>) purely to avoid BodyItem needing to be a
    // complete type within its own member-list; it's always actually a
    // ListStore<BodyItem> underneath, built in populate_body_store().
    bool m_is_occurrence = false;
    UUID m_occurrence_entity;
    Glib::RefPtr<Gio::ListModel> m_occurrence_children;
    // Context path for this row itself -- see GroupItem::m_occurrence_path.
    std::vector<UUID> m_occurrence_path;

    // No idea why the ObjectBase::get_type won't work for us but
    // reintroducing the method and using the name used by gtkmm seems
    // to work.
    static GType get_type()
    {
        // Let's cache once the type does exist.
        if (!gtype)
            gtype = g_type_from_name("gtkmm__CustomObject_BodyItem");
        return gtype;
    }

private:
    BodyItem()
        : Glib::ObjectBase("BodyItem"), m_name(*this, "name"), m_check_active(*this, "check_active", true),
          m_expanded(*this, "expanded", true), m_check_sensitive(*this, "check_sensitive", true),
          m_solid_model_active(*this, "m_solid_model_active", true), m_has_color(*this, "has_color", false),
          m_color(*this, "color")
    {
        m_group_store = Gio::ListStore<GroupItem>::create();
    }

    static GType gtype;
};

GType WorkspaceBrowser::BodyItem::gtype;

class WorkspaceBrowser::DocumentItem : public Glib::Object {
public:
    static Glib::RefPtr<DocumentItem> create()
    {
        return Glib::make_refptr_for_instance<DocumentItem>(new DocumentItem);
    }


    Glib::Property<Glib::ustring> m_name;
    Glib::Property<Glib::ustring> m_tooltip;
    Glib::Property<bool> m_active;
    Glib::Property<bool> m_check_active;
    Glib::Property<bool> m_check_sensitive;
    Glib::Property<bool> m_close_sensitive;
    UUID m_uuid;
    Glib::RefPtr<Gio::ListStore<BodyItem>> m_body_store;

    // No idea why the ObjectBase::get_type won't work for us but
    // reintroducing the method and using the name used by gtkmm seems
    // to work.
    static GType get_type()
    {
        // Let's cache once the type does exist.
        if (!gtype)
            gtype = g_type_from_name("gtkmm__CustomObject_DocumentItem");
        return gtype;
    }

private:
    DocumentItem()
        : Glib::ObjectBase("DocumentItem"), m_name(*this, "name"), m_tooltip(*this, "tooltip"),
          m_active(*this, "active"), m_check_active(*this, "check_active", false),
          m_check_sensitive(*this, "check_sensitive", true), m_close_sensitive(*this, "close_sensitive", true)
    {
        m_body_store = Gio::ListStore<BodyItem>::create();
    }

    static GType gtype;
};

static Gdk::RGBA rgba_from_color(const Color &c)
{
    Gdk::RGBA r;
    r.set_rgba(c.r, c.g, c.b);
    return r;
}


GType WorkspaceBrowser::DocumentItem::gtype;

void WorkspaceBrowser::set_document(const UUID &document_uuid)
{
    m_document_uuid = document_uuid;
}

void WorkspaceBrowser::set_body_checked(const UUID &document_uuid, const UUID &body_uuid, bool checked)
{
    for (size_t i_doc = 0; i_doc < m_document_store->get_n_items(); i_doc++) {
        auto &document = *m_document_store->get_item(i_doc);
        if (document.m_uuid != document_uuid)
            continue;
        for (size_t i_body = 0; i_body < document.m_body_store->get_n_items(); i_body++) {
            auto &body = *document.m_body_store->get_item(i_body);
            if (!body.m_is_document_folder && !body.m_is_sketch_folder && body.m_uuid == body_uuid)
                body.m_check_active = checked;
        }
    }
}

void WorkspaceBrowser::set_sketches_checked(const UUID &document_uuid, bool checked)
{
    for (size_t i_doc = 0; i_doc < m_document_store->get_n_items(); i_doc++) {
        auto &document = *m_document_store->get_item(i_doc);
        if (document.m_uuid != document_uuid)
            continue;
        for (size_t i_body = 0; i_body < document.m_body_store->get_n_items(); i_body++) {
            auto &body = *document.m_body_store->get_item(i_body);
            if (body.m_is_sketch_folder)
                body.m_check_active = checked;
        }
    }
}

void WorkspaceBrowser::populate_body_store(const Document &root, const Document &doc, const UUID &doc_uuid,
                                           const std::vector<UUID> &occurrence_path,
                                           const Glib::RefPtr<Gio::ListStore<BodyItem>> &body_store)
{
    Glib::RefPtr<BodyItem> sketches;
    Glib::RefPtr<BodyItem> body_item;
    unsigned int body_number = 1;
    unsigned int cut_number = 1;
    unsigned int join_number = 1;
    const auto extrusion_is_connected = [&doc](const Group &group) {
        const auto *extrude = dynamic_cast<const GroupExtrude *>(&group);
        if (!extrude)
            return false;
        if (extrude->m_operation == IGroupSolidModel::Operation::DIFFERENCE)
            return true;
        if (!doc.get_groups().contains(extrude->m_source_group))
            return false;
        const auto *sketch = dynamic_cast<const GroupSketch *>(&doc.get_group(extrude->m_source_group));
        return sketch && sketch->m_attached_to_face;
    };
    for (auto gr : doc.get_groups_sorted()) {
        // Reference is an internal document group.  Its workplanes and
        // origin remain available, but it is not shown as a tree item.
        if (gr->get_type() == Group::Type::REFERENCE)
            continue;
        if (gr->get_type() == Group::Type::SKETCH) {
            if (!sketches) {
                sketches = BodyItem::create();
                sketches->m_doc = doc_uuid;
                sketches->m_name = "Sketches";
                sketches->m_is_sketch_folder = true;
                sketches->m_check_active = true;
                sketches->m_occurrence_path = occurrence_path;
                body_store->append(sketches);
            }
            auto gi = GroupItem::create();
            gi->m_name = gr->m_name;
            gi->m_uuid = gr->m_uuid;
            gi->m_doc = doc_uuid;
            gi->m_occurrence_path = occurrence_path;
            gi->m_is_sketch = true;
            sketches->m_group_store->append(gi);
            continue;
        }
        if (gr->get_type() == Group::Type::OCCURRENCE) {
            // An Occurrence's row expands into the placed Component's own
            // Bodies/Sketches, exactly like a document's top-level tree --
            // not a synthetic "BodyN" feature entry, since the occurrence
            // itself isn't a feature, it's a placed instance.
            const auto &occ_group = dynamic_cast<const GroupOccurrence &>(*gr);
            body_item = BodyItem::create();
            // gr->m_name is the group's generic feature name (shown in the
            // Timeline); the tree/Rename dialog use m_body->m_name instead,
            // same field Color already reads just below -- keeping both
            // reads on the same field is what makes per-instance rename
            // actually show up here (see on_workspace_browser_rename_body()).
            body_item->m_name = gr->m_body->m_name;
            body_item->m_has_color = gr->m_body->m_color.has_value();
            if (gr->m_body->m_color.has_value())
                body_item->m_color = rgba_from_color(gr->m_body->m_color.value());
            body_item->m_uuid = gr->m_uuid;
            body_item->m_doc = doc_uuid;
            body_item->m_occurrence_path = occurrence_path;
            body_item->m_is_occurrence = true;
            body_item->m_occurrence_entity = occ_group.get_entity_uuid();
            body_store->append(body_item);

            if (auto *comp = root.get_component_ptr(occ_group.m_component)) {
                auto child_path = occurrence_path;
                child_path.push_back(body_item->m_occurrence_entity);
                auto child_store = Gio::ListStore<BodyItem>::create();
                populate_body_store(root, comp->m_document, doc_uuid, child_path, child_store);
                if (child_store->get_n_items() > 0)
                    body_item->m_occurrence_children = child_store;
            }
            body_number++;
            continue;
        }
        if (gr->get_type() == Group::Type::STEP) {
            body_item = BodyItem::create();
            body_item->m_name = gr->m_name;
            for (const auto &[entity_uuid, entity] : doc.m_entities) {
                (void)entity_uuid;
                if (entity->m_group != gr->m_uuid)
                    continue;
                if (const auto *step = dynamic_cast<const EntitySTEP *>(entity.get()); step
                    && !step->m_path.filename().empty()) {
                    body_item->m_name = step->m_path.filename().string();
                    break;
                }
            }
            body_item->m_has_color = gr->m_body->m_color.has_value();
            if (gr->m_body->m_color.has_value())
                body_item->m_color = rgba_from_color(gr->m_body->m_color.value());
            body_item->m_uuid = gr->m_uuid;
            body_item->m_doc = doc_uuid;
            body_item->m_occurrence_path = occurrence_path;
            body_store->append(body_item);

            auto gi = GroupItem::create();
            gi->m_name = "Body1";
            gi->m_is_body_label = true;
            gi->m_uuid = gr->m_uuid;
            gi->m_doc = doc_uuid;
            gi->m_occurrence_path = occurrence_path;
            body_item->m_group_store->append(gi);
            body_number++;
            continue;
        }
        const bool connected_extrude = extrusion_is_connected(*gr);
        if (gr->m_body.has_value() && !connected_extrude) {
            body_item = BodyItem::create();
            body_item->m_name = "Bodies";
            body_item->m_has_color = gr->m_body->m_color.has_value();
            if (gr->m_body->m_color.has_value())
                body_item->m_color = rgba_from_color(gr->m_body->m_color.value());
            body_item->m_uuid = gr->m_uuid;
            body_item->m_doc = doc_uuid;
            body_item->m_occurrence_path = occurrence_path;
            body_store->append(body_item);
        }

        // Connected extrusions remain part of the existing body.  They
        // stay available in the timeline, but do not create another
        // feature/body row in the tree.
        if (connected_extrude)
            continue;

        auto gi = GroupItem::create();
        const bool is_body_group = gr->m_body.has_value() && !connected_extrude
                                   && gr->get_type() != Group::Type::REFERENCE;
        const bool is_first_extrusion_body = gr->get_type() == Group::Type::EXTRUDE && body_number == 1;
        if (is_body_group || is_first_extrusion_body) {
            gi->m_name = "Body" + std::to_string(body_number++);
            gi->m_is_body_label = true;
        }
        else if (connected_extrude) {
            const auto &extrude = dynamic_cast<const GroupExtrude &>(*gr);
            if (extrude.m_operation == IGroupSolidModel::Operation::DIFFERENCE)
                gi->m_name = "Cut" + std::to_string(cut_number++);
            else
                gi->m_name = "Join" + std::to_string(join_number++);
        }
        else
            gi->m_name = gr->m_name;
        gi->m_uuid = gr->m_uuid;
        gi->m_doc = doc_uuid;
        gi->m_occurrence_path = occurrence_path;
        body_item->m_group_store->append(gi);
    }
}

void WorkspaceBrowser::update_documents(const std::map<UUID, DocumentView> &doc_views)
{
    DUNE3D_TRACE(DebugCategory::TREE);
    block_signals();
    auto store = Gio::ListStore<DocumentItem>::create();
    for (auto doci : m_core.get_documents()) {
        if (m_document_uuid && doci->get_uuid() != *m_document_uuid)
            continue;
        auto mi = DocumentItem::create();
        mi->m_uuid = doci->get_uuid();
        mi->m_close_sensitive = doci->can_close();
        // Fusion-style document-level folders. They are represented by the
        // same expandable row type as bodies, but are not actual bodies.
        for (const auto *name : {"Document Settings", "Named Views", "Origin"}) {
            auto folder = BodyItem::create();
            folder->m_doc = mi->m_uuid;
            folder->m_name = name;
            folder->m_is_document_folder = true;
            if (std::string_view(name) == "Origin") {
                folder->m_is_origin_folder = true;
                folder->m_check_active = doci->get_document().get_reference_group().m_show_origin;
                folder->m_check_sensitive = true;
            }
            mi->m_body_store->append(folder);
        }
        const auto &doc = doci->get_document();
        populate_body_store(doc, doc, mi->m_uuid, {}, mi->m_body_store);
        store->append(mi);
    }
    m_document_store = store;
    m_model = Gtk::TreeListModel::create(m_document_store, sigc::mem_fun(*this, &WorkspaceBrowser::create_model),
                                         /* passthrough */ false, /* autoexpand */ true);
    m_selection_model->set_model(m_model);
    unblock_signals();
    update_current_group(doc_views);
    // m_selection_model->set_selected(sel);
}

void WorkspaceBrowser::block_signals()
{
    m_blocked_count++;

    m_signal_group_checked.block();
    m_signal_document_checked.block();
    m_signal_body_checked.block();
    m_signal_origin_checked.block();
    m_signal_sketches_checked.block();
    m_signal_group_selected.block();
    m_signal_body_solid_model_checked.block();
    m_signal_body_expanded.block();
}

void WorkspaceBrowser::unblock_signals()
{
    if (m_blocked_count > 0)
        m_blocked_count--;

    if (m_blocked_count != 0)
        return;

    m_signal_group_checked.unblock();
    m_signal_document_checked.unblock();
    m_signal_body_checked.unblock();
    m_signal_origin_checked.unblock();
    m_signal_sketches_checked.unblock();
    m_signal_group_selected.unblock();
    m_signal_body_solid_model_checked.unblock();
    m_signal_body_expanded.unblock();
}

static std::string icon_name_from_status(GroupStatusMessage::Status st)
{
    using S = GroupStatusMessage::Status;
    switch (st) {
    case S::NONE:
        return "";
    case S::INFO:
        return "dialog-information-symbolic";

    case S::WARN:
        return "dialog-warning-symbolic";

    case S::ERR:
        return "dialog-error-symbolic";
    }

    return "face-worried-symbolic";
}

void WorkspaceBrowser::update_name(DocumentItem &it_doc, IDocumentInfo &doci)
{
    if (doci.get_path().empty())
        it_doc.m_tooltip = "Not saved yet";
    else
        it_doc.m_tooltip = path_to_string(doci.get_path());

    it_doc.m_name = doci.get_name();
    if (doci.get_needs_save())
        it_doc.m_name = it_doc.m_name + " *";
}

void WorkspaceBrowser::update_current_group(const std::map<UUID, DocumentView> &doc_views)
{
    DUNE3D_TRACE(DebugCategory::TREE);
    block_signals();
    for (size_t i_doc = 0; i_doc < m_document_store->get_n_items(); i_doc++) {
        auto &it_doc = *m_document_store->get_item(i_doc);
        auto &doci = m_core.get_idocument_info(it_doc.m_uuid);
        auto &doc_view = doc_views.at(doci.get_uuid());
        const auto is_current_doc = doci.get_uuid() == m_core.get_current_idocument_info().get_uuid();
        it_doc.m_check_sensitive = !is_current_doc;
        it_doc.m_check_active = is_current_doc || doc_view.document_is_visible();
        it_doc.m_active = is_current_doc;
        update_name(it_doc, doci);
        const auto &doc = doci.get_document();
        const auto &current_group = doc.get_group(doci.get_current_group());
        std::set<UUID> source_groups;
        if (auto group_src = dynamic_cast<const IGroupSourceGroup *>(&current_group))
            source_groups = group_src->get_source_groups(doc);
        auto body = current_group.find_body(doc);
        UUID body_uu = body.group.m_uuid;
        for (size_t i_body = 0; i_body < it_doc.m_body_store->get_n_items(); i_body++) {
            auto &it_body = *it_doc.m_body_store->get_item(i_body);
            if (it_body.m_is_document_folder) {
                if (it_body.m_is_origin_folder)
                    it_body.m_check_active = doc.get_reference_group().m_show_origin;
                continue;
            }
            if (it_body.m_is_sketch_folder) {
                it_body.m_expanded = it_body.m_group_store->get_n_items() > 0;
                it_body.m_check_sensitive = true;
                for (size_t i_group = 0; i_group < it_body.m_group_store->get_n_items(); i_group++) {
                    auto &it_group = *it_body.m_group_store->get_item(i_group);
                    const bool is_current = doci.get_current_group() == it_group.m_uuid;
                    it_body.m_expanded = it_body.m_expanded || is_current;
                    it_group.m_active = is_current && is_current_doc;
                    auto &gr = doc.get_group(it_group.m_uuid);
                    it_group.m_dof = gr.m_dof;
                    if (!it_group.m_is_body_label)
                        it_group.m_name = gr.m_name;
                    it_group.m_source_group = source_groups.contains(it_group.m_uuid);
                    it_group.m_check_sensitive = it_body.m_check_active.get_value();
                    it_group.m_check_active = doc_view.group_is_visible(it_group.m_uuid);
                    auto msgs = gr.get_messages();
                    it_group.m_status = GroupStatusMessage::summarize(msgs);
                    Glib::ustring txt;
                    for (auto &msg : msgs) {
                        if (txt.size())
                            txt += "\n";
                        txt += msg.message;
                    }
                    it_group.m_status_message = txt;
                }
                continue;
            }
            const bool is_current_body = body_uu == it_body.m_uuid && is_current_doc;
            // The active body can also be hidden.  Keep its checkbox usable so
            // the tree behaves consistently for the current and inactive body.
            it_body.m_check_sensitive = true;
            it_body.m_solid_model_active = doc_view.body_solid_model_is_visible(it_body.m_uuid);
            it_body.m_expanded = doc_view.body_is_expanded(it_body.m_uuid) | is_current_body;


            for (size_t i_group = 0; i_group < it_body.m_group_store->get_n_items(); i_group++) {
                auto &it_group = *it_body.m_group_store->get_item(i_group);
                bool is_current = doci.get_current_group() == it_group.m_uuid;
                it_group.m_active = is_current && is_current_doc;
                auto &gr = doc.get_group(it_group.m_uuid);
                it_group.m_dof = gr.m_dof;
                if (!it_group.m_is_body_label)
                    it_group.m_name = gr.m_name;
                it_group.m_source_group = source_groups.contains(it_group.m_uuid);
                // The child remains actionable when its own visibility is
                // off.  Only the Bodies parent should disable its children.
                it_group.m_check_sensitive = it_body.m_check_active.get_value();
                // Body1 is the visible label for the whole logical body.  It
                // must reflect the body visibility rather than only the
                // visibility of the first feature, otherwise later joined or
                // cut features remain rendered when Body1 is unchecked.
                it_group.m_check_active = it_group.m_is_body_label
                                                  ? doc_view.body_is_visible(it_body.m_uuid)
                                                  : doc_view.group_is_visible(it_group.m_uuid);
                {
                    auto msgs = gr.get_messages();
                    it_group.m_status = GroupStatusMessage::summarize(msgs);
                    Glib::ustring txt;
                    for (auto &msg : msgs) {
                        if (txt.size())
                            txt += "\n";
                        txt += msg.message;
                    }
                    it_group.m_status_message = txt;
                }
            }
        }
        select_group(doci.get_uuid(), doci.get_current_group());
    }
    if (m_core.has_documents()) {
        auto &current_group = m_core.get_current_document().get_group(m_core.get_current_group());
        auto msgs = current_group.get_messages();
        auto st = GroupStatusMessage::summarize(msgs);
        if (st != GroupStatusMessage::Status::NONE) {
            m_info_bar->set_revealed(true);
            m_info_bar_icon->set_from_icon_name(icon_name_from_status(st));
            Glib::ustring txt;
            for (auto &msg : msgs) {
                if (txt.size())
                    txt += "\n";
                txt += msg.message;
            }
            m_info_bar_label->set_markup(txt);
        }
        else {
            m_info_bar->set_revealed(false);
        }
    }
    else {
        m_info_bar->set_revealed(false);
    }

    unblock_signals();
}

void WorkspaceBrowser::update_needs_save()
{
    for (size_t i_doc = 0; i_doc < m_document_store->get_n_items(); i_doc++) {
        auto &it_doc = *m_document_store->get_item(i_doc);
        auto &doci = m_core.get_idocument_info(it_doc.m_uuid);
        update_name(it_doc, doci);
    }
}

class SolidModelToggleButton : public Gtk::ToggleButton {
public:
    SolidModelToggleButton()
    {
        add_css_class("solid-model-toggle-button");
        set_has_frame(false);
        signal_toggled().connect(sigc::mem_fun(*this, &SolidModelToggleButton::update_icon));
        m_area = Gtk::make_managed<Gtk::DrawingArea>();
        m_area->set_content_height(16);
        m_area->set_content_width(16);
        m_area->set_valign(Gtk::Align::CENTER);
        m_area->set_draw_func(sigc::mem_fun(*this, &SolidModelToggleButton::render_icon));
        set_child(*m_area);
        update_icon();
    }

    void set_body_color(const std::optional<Gdk::RGBA> &color)
    {
        m_body_color = color;
        m_area->queue_draw();
    }

private:
    void update_icon()
    {
        m_area->queue_draw();
    }

    void render_icon(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
    {
        static const std::vector<glm::vec2> points = {
                {.5, 4.}, {.5, 14.}, {10.5, 15.5}, {15.5, 11.}, {15.5, 1.5}, {6.5, .5},
        };
        static const glm::vec2 center = {10.5, 5.5};
        cr->set_line_width(1);
        cr->set_line_cap(Cairo::Context::LineCap::ROUND);
        cr->set_line_join(Cairo::Context::LineJoin::ROUND);
        const auto line_color = get_color();

        for (const auto &pt : points) {
            cr->line_to(pt.x, pt.y);
        }
        cr->close_path();

        if (get_active()) {
            auto solid_color = line_color;
            solid_color.set_alpha(.3);
            if (m_body_color.has_value()) {
                solid_color = *m_body_color;
            }
            Gdk::Cairo::set_source_rgba(cr, solid_color);
            cr->fill_preserve();
        }

        Gdk::Cairo::set_source_rgba(cr, line_color);

        cr->stroke();

        for (size_t i = 0; i < points.size(); i += 2) {
            cr->move_to(center.x, center.y);
            const auto &pt = points.at(i);
            cr->line_to(pt.x, pt.y);
            cr->stroke();
        }
    }

    Gtk::DrawingArea *m_area = nullptr;
    std::optional<Gdk::RGBA> m_body_color;
};

class WorkspaceBrowser::WorkspaceRow : public Gtk::TreeExpander {
public:
    WorkspaceRow(WorkspaceBrowser &browser) : m_browser(browser)
    {
        m_checkbutton = Gtk::make_managed<Gtk::CheckButton>();
        m_checkbutton->set_active(true);

        m_solid_toggle = Gtk::make_managed<SolidModelToggleButton>();
        m_solid_toggle->signal_toggled().connect([this] {
            m_browser.signal_body_solid_model_checked().emit(m_body->m_doc, m_body->m_uuid,
                                                             m_solid_toggle->get_active());
        });

        m_label = Gtk::make_managed<Gtk::Label>();
        m_label->set_halign(Gtk::Align::START);
        m_label->set_has_tooltip();

        // Marks a row as a placed Component instance (an Occurrence),
        // matching Fusion 360's browser convention of a distinct icon for
        // components vs. plain bodies -- otherwise they'd be visually
        // identical despite behaving very differently (shared, live-linked
        // definition vs. a one-off feature).
        m_component_icon = Gtk::make_managed<Gtk::Image>();
        m_component_icon->set_from_icon_name("package-x-generic-symbolic");
        m_component_icon->set_visible(false);

        m_source_group_image = Gtk::make_managed<Gtk::Image>();
        m_source_group_image->set_hexpand(true);
        m_source_group_image->set_from_icon_name("action-link-symbolic");
        m_source_group_image->set_halign(Gtk::Align::START);
        m_source_group_image->set_tooltip_text("Source of current group");

        m_status_button = Gtk::make_managed<Gtk::MenuButton>();
        m_status_button->set_icon_name("dialog-information-symbolic");
        m_status_button->set_has_frame(false);

        m_status_label = Gtk::make_managed<Gtk::Label>();
        m_status_label->set_hexpand(true);
        m_status_label->set_halign(Gtk::Align::START);
        m_status_label->set_use_markup(true);
        m_status_label->signal_activate_link().connect(
                [this](const std::string &link) {
                    m_browser.m_signal_activate_link.emit(link);
                    return true;
                },
                false);


        m_close_button = Gtk::make_managed<Gtk::Button>();
        m_close_button->set_icon_name("window-close-symbolic");
        m_close_button->set_has_frame(false);
        m_close_button->signal_clicked().connect([this] { m_browser.m_signal_close_document.emit(m_doc->m_uuid); });

        {
            auto popover = Gtk::make_managed<Gtk::Popover>();
            popover->set_child(*m_status_label);
            m_status_button->set_popover(*popover);
        }


        {
            auto attr = Pango::Attribute::create_attr_weight(Pango::Weight::BOLD);
            m_attrs_bold.insert(attr);
        }

        m_checkbutton->signal_toggled().connect([this] {
            if (debug_enabled(DebugCategory::UI))
                debug_log(DebugCategory::UI, "tree checkbox toggled");
            if (m_body && m_body->m_is_origin_folder)
                m_browser.signal_origin_checked().emit(m_body->m_doc, m_checkbutton->get_active());
            else if (m_body && m_body->m_is_sketch_folder)
                m_browser.signal_sketches_checked().emit(m_body->m_doc, m_checkbutton->get_active());
            else if (m_body && !m_body->m_is_document_folder)
                m_browser.signal_body_checked().emit(m_body->m_doc, m_body->m_uuid, m_checkbutton->get_active());
            if (m_group) {
                debug_log(DebugCategory::UI,
                          "tree checkbox group=" + static_cast<std::string>(m_group->m_uuid)
                                  + " name=" + m_group->m_name.get_value()
                                  + " body_label=" + std::to_string(m_group->m_is_body_label)
                                  + " active=" + std::to_string(m_checkbutton->get_active()));
                m_browser.signal_group_checked().emit(m_group->m_doc, m_group->m_uuid,
                                                      m_checkbutton->get_active());
            }
            if (m_doc)
                m_browser.signal_document_checked().emit(m_doc->m_uuid, m_checkbutton->get_active());
        });

        m_dof_label = Gtk::make_managed<Gtk::Label>("0");
        m_dof_label->add_css_class("dim-label");
        m_dof_label->set_width_chars(2);

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
        auto box2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
        box2->set_hexpand(true);
        box->append(*m_checkbutton);
        box->append(*m_solid_toggle);
        box->append(*m_component_icon);
        box2->append(*m_label);
        box2->append(*m_source_group_image);
        box->append(*box2);

        box->append(*m_status_button);
        box->append(*m_close_button);
        box->append(*m_dof_label);

        set_child(*box);

        auto controller = Gtk::GestureClick::create();
        controller->set_button(3);
        controller->signal_pressed().connect([this](int n_press, double x, double y) {
            Glib::RefPtr<Gio::Menu> menu;
            if (m_doc) {
                // The top-level document row -- always root-relative, so
                // "New Component" here is unambiguous regardless of what's
                // currently being edited/descended.
                m_browser.m_body_menu_document = m_doc->m_uuid;
                menu = m_browser.m_document_menu;
            }
            else if (m_body && !m_body->m_is_document_folder && !m_body->m_is_sketch_folder
                     && m_body->m_occurrence_path.empty()) {
                m_browser.m_body_menu_document = m_body->m_doc;
                m_browser.m_body_menu_body = m_body->m_uuid;
                m_browser.m_reset_body_color_action->set_enabled(m_body->m_has_color);
                menu = m_body->m_is_occurrence ? m_browser.m_body_menu_occurrence : m_browser.m_body_menu_plain;
            }
            else if (m_group && m_group->m_is_body_label && m_group->m_occurrence_path.empty()) {
                m_browser.m_body_menu_document = m_group->m_doc;
                m_browser.m_body_menu_body = m_group->m_uuid;
                menu = m_browser.m_body_label_menu;
            }
            else {
                return;
            }
            const graphene_point_t pt_in{(float)x, (float)y};
            graphene_point_t pt_out;
            if (!gtk_widget_compute_point(GTK_WIDGET(gobj()), GTK_WIDGET(m_browser.gobj()), &pt_in, &pt_out))
                return;
            Gdk::Rectangle rect;
            rect.set_x(pt_out.x);
            rect.set_y(pt_out.y);
            m_browser.m_body_popover->set_menu_model(menu);
            m_browser.m_body_popover->set_pointing_to(rect);
            m_browser.m_body_popover->popup();
        });
        add_controller(controller);

        auto activate_controller = Gtk::GestureClick::create();
        activate_controller->set_button(1);
        activate_controller->signal_pressed().connect([this](int n_press, double, double) {
            if (n_press != 2)
                return;
            if (m_group && !m_group->m_occurrence_path.empty()) {
                // Nested group row -- descend into the Document it lives in
                // and land directly on it, rather than the generic "last
                // group" default set_active_occurrence_path() picks.
                m_browser.m_signal_occurrence_activated.emit(m_group->m_occurrence_path, m_group->m_uuid);
            }
            else if (m_body && m_body->m_is_occurrence) {
                auto path = m_body->m_occurrence_path;
                path.push_back(m_body->m_occurrence_entity);
                m_browser.m_signal_occurrence_activated.emit(path, UUID());
            }
            else if (m_group) {
                m_browser.m_signal_group_activated.emit(m_group->m_doc, m_group->m_uuid);
            }
        });
        add_controller(activate_controller);

        // Drag a Sketch/BodyN/Occurrence row -- root-level or nested, to any
        // depth -- onto a placed component's row or the top-level document
        // row, to move it into that component or back out to the root --
        // see Editor::on_workspace_browser_move_group_into_component().
        auto drag_source = Gtk::DragSource::create();
        drag_source->set_actions(Gdk::DragAction::MOVE);
        drag_source->signal_prepare().connect(
                [this](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
                    UUID seed;
                    std::vector<UUID> path;
                    if (m_group && (m_group->m_is_body_label || m_group->m_is_sketch)) {
                        seed = m_group->m_uuid;
                        path = m_group->m_occurrence_path;
                    }
                    // An Occurrence's own row -- the same condition that
                    // already shows "New Instance" for a root-level one --
                    // can also be dragged, to nest that component inside
                    // another one (or, if already nested, move it further).
                    else if (m_body && m_body->m_is_occurrence) {
                        seed = m_body->m_uuid;
                        path = m_body->m_occurrence_path;
                    }
                    else
                        return {};
                    json j;
                    j["path"] = path;
                    j["seed"] = seed;
                    Glib::Value<Glib::ustring> value;
                    value.init(Glib::Value<Glib::ustring>::value_type());
                    value.set(j.dump());
                    return Gdk::ContentProvider::create(value);
                },
                false);
        add_controller(drag_source);

        auto drop_target = Gtk::DropTarget::create(Glib::Value<Glib::ustring>::value_type(), Gdk::DragAction::MOVE);
        drop_target->signal_accept().connect(
                [this](const Glib::RefPtr<Gdk::Drop> &) {
                    return m_doc || (m_body && m_body->m_is_occurrence && m_body->m_occurrence_path.empty());
                },
                false);
        drop_target->signal_drop().connect(
                [this](const Glib::ValueBase &value, double, double) {
                    // The document row means "move to the root" (nil target
                    // UUID); an Occurrence's row means "nest/move into this
                    // component" -- either way, only a *root-level* target is
                    // accepted (matching signal_accept() above), since
                    // nested rows stay non-interactive as drop targets too.
                    UUID target;
                    if (m_doc) {
                        // target stays nil
                    }
                    else if (m_body && m_body->m_is_occurrence && m_body->m_occurrence_path.empty())
                        target = m_body->m_uuid;
                    else
                        return false;
                    Glib::Value<Glib::ustring> str_value;
                    str_value.init(value.gobj());
                    std::vector<UUID> source_path;
                    UUID seed_group;
                    try {
                        const auto j = json::parse(static_cast<std::string>(str_value.get()));
                        source_path = j.at("path").get<std::vector<UUID>>();
                        seed_group = j.at("seed").get<UUID>();
                    }
                    catch (const std::exception &) {
                        return false;
                    }
                    // Defer the actual move (which rebuilds the tree's whole
                    // model, via Editor's rebuild -> update_documents()) to
                    // the next idle iteration rather than doing it here, from
                    // inside GTK's own drop-handling call stack -- doing it
                    // synchronously left GTK's TreeExpander/TreeListRow
                    // machinery for THIS row re-entered mid-update, which
                    // showed up as the app hanging in a notify::expanded
                    // feedback loop.
                    auto &browser = m_browser;
                    const auto doc = m_doc ? m_doc->m_uuid : m_body->m_doc;
                    Glib::signal_idle().connect_once([&browser, doc, source_path, seed_group, target] {
                        browser.m_signal_move_group_into_component.emit(doc, source_path, seed_group, target);
                    });
                    return true;
                },
                false);
        add_controller(drop_target);
    }

    void bind(DocumentItem &it)
    {
        m_doc = &it;
        if (auto row_box = dynamic_cast<Gtk::Box *>(get_child()))
            row_box->set_margin_start(0);
        m_browser.block_signals();
        m_checkbutton->set_visible(true);
        m_solid_toggle->set_visible(false);
        m_dof_label->set_visible(false);
        m_status_button->set_visible(false);
        m_close_button->set_visible(true);
        m_source_group_image->set_visible(false);
        m_component_icon->set_visible(false);
        m_label->set_attributes(m_attrs_normal);
        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_check_active.get_proxy(), m_checkbutton->property_active(), Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_check_sensitive.get_proxy(),
                                                                m_checkbutton->property_sensitive(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_name.get_proxy(), m_label->property_label(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_close_sensitive.get_proxy(),
                                                                m_close_button->property_sensitive(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_tooltip.get_proxy(), m_label->property_tooltip_text(), Glib::Binding::Flags::SYNC_CREATE));
        update_label_attrs(it);


        m_connections.push_back(
                it.m_active.get_proxy().signal_changed().connect([this, &it] { update_label_attrs(it); }));

        set_hide_expander(true);

        m_browser.unblock_signals();
    }
    void bind(BodyItem &it)
    {
        m_browser.block_signals();
        m_body = &it;
        // All direct children of a document, including the Fusion-style
        // folders, share the same indentation as a document's Body row.
        if (auto row_box = dynamic_cast<Gtk::Box *>(get_child()))
            row_box->set_margin_start(16);
        // Content nested inside a placed Occurrence's Component (non-empty
        // occurrence_path) is read-only for now -- visibility toggling and
        // renaming aren't tracked per-occurrence-path yet (see
        // WorkspaceBrowser::populate_body_store()). Only navigation
        // (double-click descend, wired in the click controller below) works
        // on it, same "read before write" rollout as selection/picking was.
        const bool nested = !it.m_occurrence_path.empty();
        if (it.m_is_document_folder || it.m_is_sketch_folder) {
            m_checkbutton->set_visible((it.m_is_origin_folder || it.m_is_sketch_folder) && !nested);
            m_solid_toggle->set_visible(false);
            m_dof_label->set_visible(false);
            m_status_button->set_visible(false);
            m_close_button->set_visible(false);
            m_source_group_image->set_visible(false);
            m_component_icon->set_visible(false);
            m_label->set_attributes(m_attrs_bold);
            if ((it.m_is_origin_folder || it.m_is_sketch_folder) && !nested) {
                m_bindings.push_back(Glib::Binding::bind_property_value(
                        it.m_check_active.get_proxy(), m_checkbutton->property_active(),
                        Glib::Binding::Flags::SYNC_CREATE));
                m_bindings.push_back(Glib::Binding::bind_property_value(
                        it.m_check_sensitive.get_proxy(), m_checkbutton->property_sensitive(),
                        Glib::Binding::Flags::SYNC_CREATE));
            }
            m_bindings.push_back(Glib::Binding::bind_property_value(
                    it.m_name.get_proxy(), m_label->property_label(), Glib::Binding::Flags::SYNC_CREATE));
            get_list_row()->set_expanded(true);
            m_browser.unblock_signals();
            return;
        }
        if (nested) {
            m_checkbutton->set_visible(false);
            m_solid_toggle->set_visible(false);
            m_dof_label->set_visible(false);
            m_status_button->set_visible(false);
            m_close_button->set_visible(false);
            m_source_group_image->set_visible(false);
            m_component_icon->set_visible(it.m_is_occurrence);
            m_label->set_attributes(m_attrs_normal);
            m_bindings.push_back(Glib::Binding::bind_property_value(
                    it.m_name.get_proxy(), m_label->property_label(), Glib::Binding::Flags::SYNC_CREATE));
            get_list_row()->set_expanded(true);
            m_browser.unblock_signals();
            return;
        }
        m_checkbutton->set_active(true);
        m_checkbutton->set_sensitive(true);
        m_solid_toggle->set_visible(true);
        m_dof_label->set_visible(false);
        m_status_button->set_visible(false);
        m_close_button->set_visible(false);
        m_source_group_image->set_visible(false);
        m_component_icon->set_visible(it.m_is_occurrence);
        m_label->set_attributes(m_attrs_normal);
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_name.get_proxy(), m_label->property_label(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_check_active.get_proxy(), m_checkbutton->property_active(), Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_check_sensitive.get_proxy(),
                                                                m_checkbutton->property_sensitive(),
                                                                Glib::Binding::Flags::SYNC_CREATE));

        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_solid_model_active.get_proxy(),
                                                                m_solid_toggle->property_active(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        get_list_row()->set_expanded(it.m_expanded);
        m_connections.push_back(it.m_expanded.get_proxy().signal_changed().connect([this, &it] {
            // Guard against a feedback loop with the property_expanded()
            // handler below: GTK's own notify::expanded doesn't reliably
            // no-op on an unchanged value (seen hanging the app when a full
            // tree rebuild -- Editor::canvas_update()'s
            // update_documents() -- lands while a row's expanded state is
            // still settling), so the two handlers must each check they're
            // not just echoing a value that's already in sync before
            // touching the other side.
            if (get_list_row()->get_expanded() != it.m_expanded.get_value())
                get_list_row()->set_expanded(it.m_expanded);
        }));
        const bool has_color = it.m_has_color.get_value();
        if (has_color)
            m_solid_toggle->set_body_color(it.m_color.get_value());
        else
            m_solid_toggle->set_body_color({});

        m_connections.push_back(
                it.m_color.get_proxy().signal_changed().connect([this, &it] { update_solid_color(it); }));
        m_connections.push_back(
                it.m_has_color.get_proxy().signal_changed().connect([this, &it] { update_solid_color(it); }));

        const auto body_uu = it.m_uuid;
        m_connections.push_back(get_list_row()->property_expanded().signal_changed().connect([this, body_uu, &it] {
            const auto expanded = get_list_row()->get_expanded();
            const bool new_value = m_browser.emit_body_expanded(body_uu, expanded) ? expanded : true;
            // See the matching guard on m_expanded's own signal above.
            if (it.m_expanded.get_value() != new_value)
                it.m_expanded = new_value;
        }));

        m_browser.unblock_signals();
    }
    void bind(GroupItem &it)
    {
        m_browser.block_signals();
        // Group rows are children of Body/Sketches rows and need a visible
        // inset instead of sitting flush with their parent.
        if (auto row_box = dynamic_cast<Gtk::Box *>(get_child()))
            row_box->set_margin_start(16);
        // See the matching comment in bind(BodyItem&) -- nested content is
        // read-only display for now: its checkbox (visibility, untracked per
        // occurrence path) and dof/status (not refreshed incrementally,
        // only ever populated once at tree-rebuild time) stay hidden rather
        // than risk showing stale or misleading values.
        const bool nested = !it.m_occurrence_path.empty();
        m_checkbutton->set_visible(!nested);
        m_checkbutton->set_sensitive(!nested);
        m_solid_toggle->set_visible(false);
        m_component_icon->set_visible(false);
        m_dof_label->set_visible(!nested);
        m_status_button->set_visible(!nested);
        m_close_button->set_visible(false);
        m_group = &it;
        if (!nested) {
            m_bindings.push_back(Glib::Binding::bind_property_value(it.m_check_active.get_proxy(),
                                                                    m_checkbutton->property_active(),
                                                                    Glib::Binding::Flags::SYNC_CREATE));
            m_bindings.push_back(Glib::Binding::bind_property_value(it.m_check_sensitive.get_proxy(),
                                                                    m_checkbutton->property_sensitive(),
                                                                    Glib::Binding::Flags::SYNC_CREATE));
        }
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_name.get_proxy(), m_label->property_label(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_status_message.get_proxy(), m_status_label->property_label(), Glib::Binding::Flags::SYNC_CREATE));
        update_label_attrs(it);
        m_connections.push_back(
                it.m_active.get_proxy().signal_changed().connect([this, &it] { update_label_attrs(it); }));

        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_check_active.get_proxy(), m_checkbutton->property_active(), Glib::Binding::Flags::SYNC_CREATE));

        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_check_sensitive.get_proxy(),
                                                                m_checkbutton->property_sensitive(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_source_group.get_proxy(),
                                                                m_source_group_image->property_visible(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_dof_label->set_text(std::to_string(it.m_dof.get_value()));
        m_connections.push_back(it.m_dof.get_proxy().signal_changed().connect(
                [this, &it] { m_dof_label->set_text(std::to_string(it.m_dof.get_value())); }));

        set_status(it.m_status.get_value());
        m_connections.push_back(
                it.m_status.get_proxy().signal_changed().connect([this, &it] { set_status(it.m_status.get_value()); }));

        m_browser.unblock_signals();
    }

    void unbind()
    {
        for (auto &conn : m_connections)
            conn.disconnect();
        m_connections.clear();
        for (auto bind : m_bindings)
            bind->unbind();
        m_bindings.clear();
        m_group = nullptr;
        m_body = nullptr;
        m_doc = nullptr;
        set_hide_expander(false);
    }

private:
    WorkspaceBrowser &m_browser;

    const DocumentItem *m_doc = nullptr;
    const GroupItem *m_group = nullptr;
    const BodyItem *m_body = nullptr;

    Gtk::CheckButton *m_checkbutton = nullptr;
    SolidModelToggleButton *m_solid_toggle = nullptr;
    Gtk::Label *m_label = nullptr;
    Gtk::Image *m_source_group_image = nullptr;
    Gtk::Image *m_component_icon = nullptr;
    Gtk::Label *m_dof_label = nullptr;
    Gtk::MenuButton *m_status_button = nullptr;
    Gtk::Label *m_status_label = nullptr;
    Gtk::Button *m_close_button = nullptr;

    std::vector<Glib::RefPtr<Glib::Binding>> m_bindings;
    std::vector<sigc::connection> m_connections;

    Pango::AttrList m_attrs_normal;
    Pango::AttrList m_attrs_bold;

    void update_label_attrs(GroupItem &it)
    {
        update_label_attrs(it.m_active);
    }
    void update_label_attrs(DocumentItem &it)
    {
        update_label_attrs(it.m_active);
    }
    void update_label_attrs(const Glib::Property<bool> &prop)
    {
        if (prop.get_value())
            m_label->set_attributes(m_attrs_bold);
        else
            m_label->set_attributes(m_attrs_normal);
    }

    void set_status(GroupStatusMessage::Status st)
    {
        using S = GroupStatusMessage::Status;
        m_status_button->set_visible(st != S::NONE);
        m_status_button->set_icon_name(icon_name_from_status(st));
    }

    void update_solid_color(const BodyItem &it)
    {
        const bool has_color = it.m_has_color.get_value();
        if (has_color)
            m_solid_toggle->set_body_color(it.m_color.get_value());
        else
            m_solid_toggle->set_body_color({});
    }
};

WorkspaceBrowser::WorkspaceBrowser(Core &core, std::optional<UUID> document_uuid)
    : Gtk::Box(Gtk::Orientation::VERTICAL), m_core(core), m_document_uuid(document_uuid)
{
    m_document_store = Gio::ListStore<DocumentItem>::create();


    // Set list model and selection model.
    // passthrough must be false when Gtk::TreeExpander is used in the view.
    m_model = Gtk::TreeListModel::create(m_document_store, sigc::mem_fun(*this, &WorkspaceBrowser::create_model),
                                         /* passthrough */ false, /* autoexpand */ true);
    m_selection_model = Gtk::SingleSelection::create(m_model);

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([this](const Glib::RefPtr<Gtk::ListItem> &list_item) {
        // Each ListItem contains a TreeExpander, which contains a Label.
        // The Label shows the StringObject's string. That's done in on_bind_name().
        auto expander = Gtk::make_managed<WorkspaceRow>(*this);
        list_item->set_child(*expander);
    });
    factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem> &list_item) {
        // When TreeListModel::property_passthrough() is false, ListItem::get_item()
        // is a TreeListRow. TreeExpander needs the TreeListRow.
        // The StringObject item is returned by TreeListRow::get_item().
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(list_item->get_item());
        if (!row)
            return;
        auto expander = dynamic_cast<WorkspaceRow *>(list_item->get_child());
        if (!expander)
            return;
        expander->set_list_row(row);
        if (auto col = std::dynamic_pointer_cast<DocumentItem>(row->get_item())) {
            expander->bind(*col);
            // expander->set_label(col->m_name.get_value());
        }
        else if (auto col = std::dynamic_pointer_cast<BodyItem>(row->get_item())) {
            expander->bind(*col);
            // expander->set_label("Body " + col->m_name.get_value());
        }
        else if (auto col = std::dynamic_pointer_cast<GroupItem>(row->get_item())) {
            expander->bind(*col);
            // expander->set_label("Group " + col->m_name.get_value());
        }
    });
    factory->signal_unbind().connect([](const Glib::RefPtr<Gtk::ListItem> &list_item) {
        // When TreeListModel::property_passthrough() is false, ListItem::get_item()
        // is a TreeListRow. TreeExpander needs the TreeListRow.
        // The StringObject item is returned by TreeListRow::get_item().
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(list_item->get_item());
        if (!row)
            return;
        auto expander = dynamic_cast<WorkspaceRow *>(list_item->get_child());
        if (!expander)
            return;
        expander->unbind();
    });


    m_view = Gtk::make_managed<Gtk::ListView>(m_selection_model, factory);
    m_selection_model->signal_selection_changed().connect([this](guint, guint) {
        auto sel = m_selection_model->get_selected_item();
        auto tr = std::dynamic_pointer_cast<Gtk::TreeListRow>(sel);
        if (!tr)
            return;
        if (auto gr = std::dynamic_pointer_cast<WorkspaceBrowser::GroupItem>(tr->get_item())) {
            // Nested rows (inside a placed Occurrence's Component) aren't
            // root-document groups -- selecting them would make
            // Editor::on_workspace_browser_group_selected() resolve gr->m_uuid
            // against the wrong document. Double-click-to-descend (see
            // signal_occurrence_activated()) is the only wired-up interaction
            // for them so far.
            if (gr->m_occurrence_path.empty())
                m_signal_group_selected.emit(gr->m_doc, gr->m_uuid);
        }
    });
    m_view->add_css_class("navigation-sidebar");
    {
        auto sc = Gtk::make_managed<Gtk::ScrolledWindow>();
        sc->set_child(*m_view);
        sc->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        sc->set_vexpand(true);

        auto overlay = Gtk::make_managed<Gtk::Overlay>();
        overlay->set_vexpand(true);
        overlay->set_child(*sc);

        m_toast_label = Gtk::make_managed<Gtk::Label>();
        m_toast_label->set_wrap(true);
        m_toast_revealer = Gtk::make_managed<Gtk::Revealer>();
        {
            auto ctrl = Gtk::EventControllerMotion::create();
            ctrl->signal_enter().connect([this](double, double) { m_toast_connection.disconnect(); });
            ctrl->signal_leave().connect([this] {
                m_toast_connection.disconnect();
                m_toast_connection = Glib::signal_timeout().connect(
                        [this] {
                            m_toast_revealer->set_reveal_child(false);
                            return false;
                        },
                        3000);
            });
            m_toast_revealer->add_controller(ctrl);
        }
        m_toast_revealer->set_child(*m_toast_label);
        m_toast_revealer->set_visible(false);
        m_toast_revealer->set_halign(Gtk::Align::CENTER);
        m_toast_revealer->set_valign(Gtk::Align::END);
        m_toast_revealer->set_transition_type(Gtk::RevealerTransitionType::CROSSFADE);
        m_toast_revealer->add_css_class("osd");
        m_toast_revealer->add_css_class("workspace-browser-toast");
        m_toast_revealer->set_margin(20);
        m_toast_revealer->property_child_revealed().signal_changed().connect(
                [this] { m_toast_revealer->set_visible(m_toast_revealer->get_child_revealed()); });
        overlay->add_overlay(*m_toast_revealer);

        append(*overlay);
    }

    m_info_bar = Gtk::make_managed<Gtk::InfoBar>();
    m_info_bar->set_revealed(false);
    m_info_bar->set_message_type(Gtk::MessageType::ERROR);
    m_info_bar_icon = Gtk::make_managed<Gtk::Image>();
    m_info_bar_label = Gtk::make_managed<Gtk::Label>("foo");
    m_info_bar_label->signal_activate_link().connect(
            [this](const std::string &link) {
                m_signal_activate_link.emit(link);
                return true;
            },
            false);

    m_info_bar_label->set_wrap(true);
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        box->append(*m_info_bar_icon);
        box->append(*m_info_bar_label);
        m_info_bar->add_child(*box);
    }


    append(*m_info_bar);

    auto actions = Gio::SimpleActionGroup::create();
    m_reset_body_color_action = actions->add_action(
            "reset_color", [this] { signal_reset_body_color().emit(m_body_menu_document, m_body_menu_body); });
    actions->add_action("rename", [this] { signal_rename_body().emit(m_body_menu_document, m_body_menu_body); });
    actions->add_action("set_color", [this] { signal_set_body_color().emit(m_body_menu_document, m_body_menu_body); });
    actions->add_action(
            "export_stl", [this] { signal_export_body_stl().emit(m_body_menu_document, m_body_menu_body); });
    actions->add_action(
            "export_step", [this] { signal_export_body_step().emit(m_body_menu_document, m_body_menu_body); });
    actions->add_action("new_component", [this] { signal_new_component().emit(m_body_menu_document); });
    actions->add_action("new_component_from_body",
                        [this] { signal_new_component_from_body().emit(m_body_menu_document, m_body_menu_body); });
    actions->add_action("new_instance",
                        [this] { signal_new_instance().emit(m_body_menu_document, m_body_menu_body); });
    insert_action_group("body", actions);

    m_document_menu = Gio::Menu::create();
    m_document_menu->append("New Component", "body.new_component");

    m_body_menu_plain = Gio::Menu::create();
    m_body_menu_plain->append("Export STL", "body.export_stl");
    m_body_menu_plain->append("Export STEP", "body.export_step");
    m_body_menu_plain->append_section("", Gio::Menu::create());
    m_body_menu_plain->append("Set color", "body.set_color");
    m_body_menu_plain->append("Reset color", "body.reset_color");
    m_body_menu_plain->append("Rename", "body.rename");

    // "New Component from Body" lives on the BodyN feature row itself (a
    // GroupItem, is_body_label -- see populate_body_store()), not the
    // parent "Bodies" folder row above: a "Bodies" folder can hold several
    // distinct features (Body1, Cut1, Join1, ...) that happen to share one
    // physical body, and it's the specific feature you'd point at to say
    // "make a component out of this."
    m_body_label_menu = Gio::Menu::create();
    m_body_label_menu->append("New Component from Body", "body.new_component_from_body");

    m_body_menu_occurrence = Gio::Menu::create();
    m_body_menu_occurrence->append("Export STL", "body.export_stl");
    m_body_menu_occurrence->append("Export STEP", "body.export_step");
    m_body_menu_occurrence->append_section("", Gio::Menu::create());
    m_body_menu_occurrence->append("Set color", "body.set_color");
    m_body_menu_occurrence->append("Reset color", "body.reset_color");
    m_body_menu_occurrence->append("Rename", "body.rename");
    m_body_menu_occurrence->append_section("", Gio::Menu::create());
    m_body_menu_occurrence->append("New Instance", "body.new_instance");


    m_body_popover = Gtk::make_managed<Gtk::PopoverMenu>();
    m_body_popover->set_menu_model(m_body_menu_plain);

    m_body_popover->set_parent(*this);
}

void WorkspaceBrowser::emit_add_group(GroupType type, AddGroupMode add_group_mode)
{
    m_signal_add_group.emit(type, add_group_mode);
}

bool WorkspaceBrowser::emit_body_expanded(const UUID &body_uu, bool expanded)
{
    if (m_blocked_count)
        return true;
    return m_signal_body_expanded.emit(body_uu, expanded);
}

Glib::RefPtr<Gio::ListModel> WorkspaceBrowser::create_model(const Glib::RefPtr<Glib::ObjectBase> &item)
{
    // The items in a StringList are StringObjects.
    if (auto col = std::dynamic_pointer_cast<DocumentItem>(item))
        return col->m_body_store;
    if (auto col = std::dynamic_pointer_cast<BodyItem>(item)) {
        if (col->m_is_occurrence)
            return col->m_occurrence_children;
        return col->m_group_store;
    }
    return nullptr;
    /*Glib::RefPtr<Gio::ListModel> result;
    if (!col)
        // Top names
        result = Gtk::StringList::create({"Billy Bob", "Joey Jojo", "Rob McRoberts"});
    else if (col->get_string() == "Billy Bob")
        result = Gtk::StringList::create({"Billy Bob Junior", "Sue Bob"});
    else if (col->get_string() == "Rob McRoberts")
        result = Gtk::StringList::create({"Xavier McRoberts"});
*/
    // If result is empty, it's a leaf in the tree, i.e. an item without children.
    // Returning an empty RefPtr (not a RefPtr with an empty StringList)
    // signals that the item is not expandable.
    // return result;
}

void WorkspaceBrowser::group_prev_next(int dir)
{
    auto next_group = m_core.get_current_document().get_group_rel(m_core.get_current_group(), dir);
    if (!next_group)
        return;

    select_group(next_group);
}

void WorkspaceBrowser::select_group(const UUID &uu)
{
    select_group(m_core.get_current_idocument_info().get_uuid(), uu);
}

void WorkspaceBrowser::select_group(const UUID &doc_uu, const UUID &uu)
{
    {
        const auto n = m_selection_model->get_n_items();

        auto &doc = m_core.get_idocument_info(doc_uu).get_document();
        const auto body_group = doc.get_group(uu).find_body(doc).group.m_uuid;
        const bool is_sketch = doc.get_group(uu).get_type() == Group::Type::SKETCH;
        {
            for (size_t i = 0; i < n; i++) {
                auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(m_selection_model->get_object(i));
                if (!row)
                    continue;
                auto it = std::dynamic_pointer_cast<BodyItem>(row->get_item());
                if (!it)
                    continue;
                if (it->m_doc == doc_uu &&
                    ((is_sketch && it->m_is_sketch_folder) || (!is_sketch && it->m_uuid == body_group)))
                    row->set_expanded(true);
            }
        }
    }
    const auto n = m_selection_model->get_n_items();

    for (size_t i = 0; i < n; i++) {
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(m_selection_model->get_object(i));
        if (!row)
            continue;
        auto it = std::dynamic_pointer_cast<GroupItem>(row->get_item());
        if (!it)
            continue;

        if (it->m_doc == m_core.get_current_idocument_info().get_uuid() && it->m_uuid == uu) {
            m_selection_model->select_item(i, true);
            return;
        }
    }
}

void WorkspaceBrowser::show_toast(const std::string &msg)
{
    m_toast_connection.disconnect();
    m_toast_label->set_label(msg);
    m_toast_revealer->set_reveal_child(true);
    m_toast_revealer->set_visible(true);
    m_toast_connection = Glib::signal_timeout().connect(
            [this] {
                m_toast_revealer->set_reveal_child(false);
                return false;
            },
            3000);
}

} // namespace dune3d
