#include "group_threemf.hpp"

namespace dune3d {
// GroupThreeMF currently shares GroupSketch's solid-model implementation
// (which finds nothing to build, since EntityThreeMF isn't recognized by
// solid_model_sketch.cpp -- reference-only by design). The separate
// translation unit keeps it ready for 3MF-specific behavior.
}
