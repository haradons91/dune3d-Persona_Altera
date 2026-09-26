#pragma once

#include "group_sketch.hpp"

namespace dune3d {

// Imported STL meshes are reference-only (no solid-model participation --
// see the STL import plan), but still get their own group type so they are
// represented as STL objects in the tree, timeline, and serialized document,
// same as GroupStep does for imported STEP bodies.
class GroupSTL : public GroupSketch {
public:
    explicit GroupSTL(const UUID &uu) : GroupSketch(uu)
    {
    }
    explicit GroupSTL(const UUID &uu, const json &j) : GroupSketch(uu, j)
    {
    }
    explicit GroupSTL(const GroupSketch &other) : GroupSketch(other)
    {
    }

    static constexpr Type s_type = Type::STL;
    Type get_type() const override
    {
        return s_type;
    }

    std::unique_ptr<Group> clone() const override
    {
        return std::make_unique<GroupSTL>(*this);
    }
};

} // namespace dune3d
