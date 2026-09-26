#include "group_stl.hpp"

namespace dune3d {
// GroupSTL currently shares GroupSketch's solid-model implementation (which
// finds nothing to build, since EntitySTL isn't recognized by
// solid_model_sketch.cpp -- reference-only by design). The separate
// translation unit keeps it ready for STL-specific behavior.
}
