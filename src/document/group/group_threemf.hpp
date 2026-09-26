#pragma once

#include "group_sketch.hpp"

namespace dune3d {

// Imported 3MF meshes are reference-only (no solid-model participation --
// see the 3MF import plan), but still get their own group type so they are
// represented as 3MF objects in the tree, timeline, and serialized
// document, same as GroupStep/GroupSTL do for their own formats.
class GroupThreeMF : public GroupSketch {
public:
    explicit GroupThreeMF(const UUID &uu) : GroupSketch(uu)
    {
    }
    explicit GroupThreeMF(const UUID &uu, const json &j) : GroupSketch(uu, j)
    {
    }
    explicit GroupThreeMF(const GroupSketch &other) : GroupSketch(other)
    {
    }

    static constexpr Type s_type = Type::THREE_MF;
    Type get_type() const override
    {
        return s_type;
    }

    std::unique_ptr<Group> clone() const override
    {
        return std::make_unique<GroupThreeMF>(*this);
    }
};

} // namespace dune3d
