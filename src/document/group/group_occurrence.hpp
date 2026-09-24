#pragma once
#include "group.hpp"
#include "igroup_generate.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class Document;

// Hosts exactly one EntityOccurrence, giving a placed Component instance its
// own first-class timeline/body slot -- modeled on GroupStep, which exists
// for the same reason (a single imported entity needs its own body row
// rather than living inside an arbitrary existing group). m_body is set so
// Document::get_groups_by_body() picks this up as its own body-span with no
// changes to body derivation.
class GroupOccurrence : public Group, public IGroupGenerate {
public:
    explicit GroupOccurrence(const UUID &uu);
    explicit GroupOccurrence(const UUID &uu, const json &j);

    static constexpr Type s_type = Type::OCCURRENCE;
    Type get_type() const override
    {
        return s_type;
    }

    // Which Component this places -- stable once created (not user-retargetable
    // in this first iteration; retargeting would mean delete-and-replace, same
    // as Fusion). Copied onto the hosted EntityOccurrence's m_component every
    // generate(); the placement (origin/normal) lives only on the entity and
    // is preserved across regenerate calls, since it's user-editable.
    UUID m_component;

    UUID get_entity_uuid() const;

    void generate(Document &doc) override;

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;
};

} // namespace dune3d
