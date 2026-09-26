#pragma once
#include "util/uuid.hpp"
#include <map>
#include <memory>
#include "nlohmann/json_fwd.hpp"
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <glm/glm.hpp>
#include "util/file_version.hpp"
#include "entity/entity_and_point.hpp"

namespace dune3d {
using json = nlohmann::json;
class Entity;
class Constraint;
class Group;
class Body;
class GroupReference;
class Component;
enum class GroupType;

struct ItemsToDelete {
    std::set<UUID> entities;
    std::set<UUID> groups;
    std::set<UUID> constraints;

    void append(const ItemsToDelete &other);
    void subtract(const ItemsToDelete &other);
    bool empty() const;
    size_t size() const;
};

class Document {
public:
    Document();
    explicit Document(const UUID &reference_group_uuid);
    explicit Document(const json &j, const std::filesystem::path &containing_dir);
    static Document new_from_file(const std::filesystem::path &path);
    Document(const Document &other);

    std::map<UUID, std::unique_ptr<Entity>> m_entities;
    std::map<UUID, std::unique_ptr<Constraint>> m_constraints;

    FileVersion m_version;
    static unsigned int get_app_version();

    template <typename T> T &add_entity(const UUID &uu)
    {
        auto en = std::make_unique<T>(uu);
        auto p = en.get();
        m_entities.emplace(uu, std::move(en));
        return *p;
    }

    template <typename T = Entity> T &get_entity(const UUID &uu)
    {
        auto it = m_entities.find(uu);
        if (it == m_entities.end())
            throw std::out_of_range("entity UUID not found: " + static_cast<std::string>(uu));
        return dynamic_cast<T &>(*it->second);
    }

    template <typename T = Entity> T *get_entity_ptr(const UUID &uu)
    {
        auto it = m_entities.find(uu);
        if (it == m_entities.end())
            return nullptr;
        else
            return dynamic_cast<T *>(it->second.get());
    }

    template <typename T> T &get_or_add_entity(const UUID &uu, bool *was_added = nullptr)
    {
        if (m_entities.count(uu)) {
            if (was_added)
                *was_added = false;
            return dynamic_cast<T &>(*m_entities.at(uu));
        }
        else {
            if (was_added)
                *was_added = true;
            return add_entity<T>(uu);
        }
    }

    template <typename T = Entity> const T &get_entity(const UUID &uu) const
    {
        auto it = m_entities.find(uu);
        if (it == m_entities.end())
            throw std::out_of_range("entity UUID not found: " + static_cast<std::string>(uu));
        return dynamic_cast<const T &>(*it->second);
    }

    template <typename T = Entity> const T *get_entity_ptr(const UUID &uu) const
    {
        auto it = m_entities.find(uu);
        if (it == m_entities.end())
            return nullptr;
        else
            return dynamic_cast<const T *>(it->second.get());
    }

    template <typename T = Constraint> const T &get_constraint(const UUID &uu) const
    {
        return dynamic_cast<const T &>(*m_constraints.at(uu));
    }

    template <typename T = Constraint> T &get_constraint(const UUID &uu)
    {
        return dynamic_cast<T &>(*m_constraints.at(uu));
    }

    template <typename T = Constraint> T *get_constraint_ptr(const UUID &uu)
    {
        auto it = m_constraints.find(uu);
        if (it == m_constraints.end())
            return nullptr;
        else
            return dynamic_cast<T *>(it->second.get());
    }

    template <typename T = Constraint> const T *get_constraint_ptr(const UUID &uu) const
    {
        auto it = m_constraints.find(uu);
        if (it == m_constraints.end())
            return nullptr;
        else
            return dynamic_cast<const T *>(it->second.get());
    }

    template <typename T> T &add_constraint(const UUID &uu)
    {
        auto en = std::make_unique<T>(uu);
        auto p = en.get();
        m_constraints.emplace(uu, std::move(en));
        return *p;
    }

    const auto &get_groups() const
    {
        return m_groups;
    }

    // Only the root Document (the one an open tab/IDocumentInfo owns) has a
    // meaningfully populated m_components -- a Component's own m_document
    // always leaves this empty. Nesting is expressed by EntityOccurrences
    // referencing other entries of this one flat, UUID-keyed map, never by
    // physically nesting Documents. Code resolving an occurrence's target
    // must be passed the *root* Document explicitly (see Renderer's
    // m_component_registry) rather than assuming "the current Document" is
    // the registry, since that's only true when m_occurrence_path is empty.
    const auto &get_components() const
    {
        return m_components;
    }

    template <typename T = Component> T &get_component(const UUID &uu)
    {
        return dynamic_cast<T &>(*m_components.at(uu));
    }

    template <typename T = Component> const T &get_component(const UUID &uu) const
    {
        return dynamic_cast<const T &>(*m_components.at(uu));
    }

    Component *get_component_ptr(const UUID &uu);
    const Component *get_component_ptr(const UUID &uu) const;

    Component &add_component(const UUID &uu);
    Component &add_component(const UUID &uu, const UUID &reference_group_uuid);

    template <typename T = Group> const T &get_group(const UUID &uu) const
    {
        return dynamic_cast<const T &>(*m_groups.at(uu));
    }

    template <typename T = Group> T &get_group(const UUID &uu)
    {
        return dynamic_cast<T &>(*m_groups.at(uu));
    }

    template <typename T> T &add_group(const UUID &uu)
    {
        auto en = std::make_unique<T>(uu);
        auto p = en.get();
        m_groups.emplace(uu, std::move(en));
        update_groups_sorted();
        return *p;
    }

    template <typename T> T &insert_group(const UUID &uu, const UUID &after)
    {
        auto en = std::make_unique<T>(uu);
        auto p = en.get();
        insert_group(std::move(en), after);
        return *p;
    }

    glm::dvec3 get_point(const EntityAndPoint &ep) const;
    bool is_valid_point(const EntityAndPoint &ep) const;

    const std::vector<Group *> &get_groups_sorted();
    const std::vector<const Group *> &get_groups_sorted() const;

    void accumulate_first_group(const Group *&first_group, const UUID &group_uu) const;

    class BodyGroups {
    public:
        BodyGroups(const Body &b) : body(b)
        {
        }
        const Body &body;
        const Group &get_group() const
        {
            return *groups.front();
        }
        std::vector<const Group *> groups;
    };
    std::vector<BodyGroups> get_groups_by_body() const;
    // The BodyGroups span containing current_group, if any -- current_group
    // need not itself be the body-owning group, just somewhere in its span.
    std::optional<BodyGroups> find_body_groups(const UUID &current_group) const;
    UUID get_group_rel(const UUID &group, int delta) const;

    // The transitive closure of seed under IGroupSourceGroup edges in BOTH
    // directions (what each member depends on, and what depends on each
    // member), minus the Reference group, ordered by get_groups_sorted().
    // Used to move a group (and everything that would otherwise dangle) into
    // another Document via extract_groups() without leaving a broken
    // reference behind on either side -- see workspace browser drag-and-drop.
    std::vector<UUID> compute_move_closure(std::set<UUID> seed) const;

    // Every component_uu (and everything it, in turn, places an occurrence
    // of) reachable from component_uu -- i.e. "components that would end up
    // nested inside a new occurrence of component_uu". The result always
    // includes component_uu itself. Used to refuse an occurrence placement
    // (ToolInsertOccurrence) or a drag-and-drop nesting move (workspace
    // browser) that would make a component contain itself, directly or
    // transitively.
    std::set<UUID> collect_contained_components(const UUID &component_uu) const;

    void erase_invalid();
    void update_pending(const UUID &last_group = UUID(), const std::vector<EntityAndPoint> &dragged = {});

    // Moves the given groups (and the entities/constraints owned by them)
    // out of this Document and into dest, appended after dest's existing
    // groups, re-indexed to keep dest's timeline contiguous. Used by "New
    // Component from Selection" to splice a body's groups into a new
    // Component's own Document. group_uuids order is preserved.
    void extract_groups(const std::vector<UUID> &group_uuids, Document &dest);

    void set_group_generate_pending(const UUID &group);
    void set_group_solve_pending(const UUID &group);
    void set_group_update_solid_model_pending(const UUID &group);

    enum class MoveGroup { UP, DOWN, END_OF_BODY, END_OF_DOCUMENT };
    UUID get_group_after(const UUID &group, MoveGroup dir) const;

    bool reorder_group(const UUID &group, const UUID &after);

    ItemsToDelete get_additional_items_to_delete(const ItemsToDelete &items) const;
    void delete_items(const ItemsToDelete &items);

    std::set<const Constraint *> find_constraints(const std::set<EntityAndPoint> &enps) const;

    std::string find_next_group_name(GroupType type) const;

    const GroupReference &get_reference_group() const;
    GroupReference &get_reference_group();

    json serialize() const;

    ~Document();

private:
    std::map<UUID, std::unique_ptr<Component>> m_components;
    std::map<UUID, std::unique_ptr<Group>> m_groups;
    std::vector<Group *> m_groups_sorted;
    std::vector<const Group *> m_groups_sorted_const;
    void update_groups_sorted();

    UUID m_first_group_generate;
    UUID m_first_group_solve;
    UUID m_first_group_update_solid_model;

    void generate_group(Group &group);
    void solve_group(Group &group, const std::vector<EntityAndPoint> &dragged);
    void update_solid_model(Group &group);

    void update_group_if_less(UUID &uu, const UUID &new_group);

    void insert_group(std::unique_ptr<Group> group, const UUID &after);
    bool apply_version_upgrades();
};
} // namespace dune3d
