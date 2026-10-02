#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"
#include <set>
#include <list>
#include <memory>

namespace dune3d {

class Document;
class SolidModel;

// Shared base for "local operation" groups that select FACES of the
// previous body rather than edges -- the face-selection equivalent of
// GroupLocalOperation (Fillet/Chamfer's shared base), for Remove and (a
// natural later addition) Shell. Faces are already individually selectable
// in normal rendering (SelectableRef::Type::SOLID_MODEL_FACE, tagged with a
// stable per-body face index -- see Renderer's per-current-document body
// rendering), unlike edges, so no analog of edge-select-mode is needed:
// ToolSelectFaces just reads that selection directly.
class GroupFaceOperation : public Group, public IGroupSolidModel {
public:
    explicit GroupFaceOperation(const UUID &uu);
    explicit GroupFaceOperation(const UUID &uu, const json &j);

    std::set<unsigned int> m_faces;

    // No union/difference/intersection concept of its own -- always exactly
    // one previous body as input -- same fixed-placeholder convention as
    // GroupLocalOperation's.
    Operation get_operation() const override
    {
        return Operation::UNION;
    }
    void set_operation(Operation op) override
    {
    }

    json serialize() const override;

    std::list<GroupStatusMessage> m_face_operation_messages;
    std::list<GroupStatusMessage> get_messages() const override;

    std::shared_ptr<const SolidModel> m_solid_model;
    const SolidModel *get_solid_model() const override;
};

} // namespace dune3d
