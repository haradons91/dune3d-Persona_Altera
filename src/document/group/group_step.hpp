#pragma once

#include "group_sketch.hpp"

namespace dune3d {

// Imported STEP bodies use the same solid-model behavior as a sketch-backed
// imported body, but have their own group type so they are represented as
// STEP objects in the tree, timeline, and serialized document.
class GroupStep : public GroupSketch {
public:
    explicit GroupStep(const UUID &uu) : GroupSketch(uu)
    {
    }
    explicit GroupStep(const UUID &uu, const json &j) : GroupSketch(uu, j)
    {
    }
    explicit GroupStep(const GroupSketch &other) : GroupSketch(other)
    {
    }

    static constexpr Type s_type = Type::STEP;
    Type get_type() const override
    {
        return s_type;
    }

    std::unique_ptr<Group> clone() const override
    {
        return std::make_unique<GroupStep>(*this);
    }
};

} // namespace dune3d
