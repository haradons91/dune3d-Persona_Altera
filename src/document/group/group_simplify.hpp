#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"
#include <memory>
#include <list>

namespace dune3d {

class Document;
class SolidModel;

// Modify ribbon's "Simplify": merges coplanar faces and collinear edges of
// the previous body into fewer, larger ones (ShapeUpgrade_UnifySameDomain),
// removing now-redundant seams left behind by earlier operations (e.g. an
// array of identical cuts that happen to share edges). Doesn't change the
// body's shape, only its face/edge count -- no selection or parameters
// needed, unlike Fillet/Chamfer (GroupLocalOperation).
class GroupSimplify : public Group, public IGroupSolidModel {
public:
    explicit GroupSimplify(const UUID &uu);
    explicit GroupSimplify(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::SIMPLIFY;
    Type get_type() const override
    {
        return s_type;
    }

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    // Simplify has no union/difference/intersection concept of its own --
    // it only ever has the one previous body as input -- so this is a fixed
    // placeholder, same convention as GroupLocalOperation's.
    Operation get_operation() const override
    {
        return Operation::UNION;
    }
    void set_operation(Operation op) override
    {
    }

    const SolidModel *get_solid_model() const override;
    void update_solid_model(const Document &doc) override;

    std::shared_ptr<const SolidModel> m_solid_model;
    std::list<GroupStatusMessage> m_simplify_messages;
    std::list<GroupStatusMessage> get_messages() const override;
};

} // namespace dune3d
