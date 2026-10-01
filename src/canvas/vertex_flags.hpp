#pragma once
#include <stdint.h>
#include "bitmask_operators.hpp"


namespace dune3d {
enum class CanvasVertexFlags : uint32_t {
    DEFAULT = 0,
    SELECTED = (1 << 0),
    HOVER = (1 << 1),
    INACTIVE = (1 << 2),
    CONSTRAINT = (1 << 3),
    CONSTRUCTION = (1 << 4),
    HIGHLIGHT = (1 << 5),
    SCREEN = (1 << 6),
    LINE_THIN = (1 << 7),
    LINE_THINNER = (1 << 8),
    HOVER_ONLY = (1 << 9),
    // Icon-only: the icon shader normally canonicalizes a direction vector
    // to one of two equivalent orientations, correct for a symmetric icon
    // (a horizontal/vertical constraint bar looks the same either way) but
    // wrong for a genuinely directional one (an arrow, which must keep
    // pointing the way it was actually told to). See icon-vertex.glsl.
    ICON_EXACT_DIRECTION = (1 << 10),
    // Icon-only: drawn a second time in IconRenderer::render(), with depth
    // testing off, so it stays visible through occluding geometry (e.g. the
    // extrude handle arrow when dragging the extrusion into existing solid
    // material) instead of disappearing behind it like every other icon.
    ICON_ALWAYS_VISIBLE = (1 << 11),
    // Line-only: discard fragments outside a fixed pixel-space on/off pattern
    // in the fragment shader, driven by the smooth per-fragment distance
    // varying the geometry shader computes for this purpose. See
    // line-geometry.glsl/line-fragment.glsl.
    DASHED = (1 << 12),
    COLOR_MASK = SELECTED | HOVER | INACTIVE | CONSTRAINT | CONSTRUCTION | HIGHLIGHT,
};
}

template <> struct enable_bitmask_operators<dune3d::CanvasVertexFlags> {
    static constexpr bool enable = true;
};
