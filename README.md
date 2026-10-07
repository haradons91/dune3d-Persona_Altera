# Dune3D Persona Altera

**This is not the Dune 3D project.** It is a personal, heavily modified
derivative of it, developed for one person's own use.

## What this is

Dune3D Persona Altera started as a fork of [Dune 3D](https://github.com/dune3d/dune3d),
the open-source parametric 3D CAD application created by
[carrotIndustries](https://github.com/carrotIndustries). Since forking it, this
project has diverged substantially in both code and direction, through an
extended series of AI-assisted development sessions (using Claude/Claude
Code) directed by its owner. Large parts of the codebase have been rewritten,
replaced, or added outright by that process, well beyond what would normally
be called a "patch" or "contribution."

Some of what's changed relative to upstream Dune 3D:

 - A different UI: a ribbon-style toolbar, document tabs, and a group
   timeline strip replacing the original menu/toolbar layout.
 - A component/occurrence system for nesting and instancing sub-assemblies.
 - Mesh import (STL, 3MF) and mesh-to-body conversion (sewing a triangle
   mesh into a solid, with multiple algorithms and a cancellable progress
   dialog).
 - Support for building against newer OpenCASCADE releases (8.0.1), plus a
   number of rendering/performance fixes uncovered along the way.
 - Various workflow and interaction changes (snapping, drag-and-drop of
   sketches/bodies between components, live cut previews while extruding,
   and more) that reflect this fork owner's own preferences rather than
   upstream's.

Because of that divergence, this repository should be treated as its own
thing, not as a lightly-patched copy of Dune 3D, and not as a place to look
for what upstream Dune 3D currently does or intends.

## AI-assisted development and no upstream contribution

This project is openly built with AI assistance as a matter of course, not
as an occasional aid. Treat any given file, commit, or feature here as
likely to have been written or substantially modified by an AI acting under
this fork owner's direction, rather than hand-written line by line.

This fork exists purely for its owner's own use and experimentation. It is
**not** intended to be upstreamed, merged, or otherwise contributed back to
the original Dune 3D project, and it is not intended to be a source of
contributions to any other project either. If you're comparing this against
upstream Dune 3D's own contribution policy: that policy is about *their*
project, not this one, and nothing here is headed in that direction.

## License

Like upstream Dune 3D, this project is licensed under the GNU General
Public License v3.0 -- see [LICENSE](LICENSE). All credit for the original
design and the vast majority of the pre-fork codebase belongs to
[carrotIndustries](https://github.com/carrotIndustries) and the Dune 3D
contributors; see the [upstream project](https://github.com/dune3d/dune3d)
for that history.

## How to build

The general build process still follows upstream Dune 3D's own
[build instructions](https://docs.dune3d.org/en/latest/build-linux.html)
(CMake/meson, OpenCASCADE, gtkmm4), but this fork specifically targets
**OpenCASCADE 8.0.1 built from source**, not whatever older OCCT release
your distro happens to package -- version-conditional support for older
OCCT releases has been removed from the code. See `meson.build` for the
rpath/dtags linker flags a from-source OCCT install needs.

## TODO

Known loose ends in this fork, not yet acted on:

 - **Screenshot**: the README no longer references one (it used to show
   upstream Dune 3D's own screenshot, which doesn't match this fork's
   current UI). Needs a real screenshot of this fork's actual interface.
 - **Full internal rebrand**: the app still uses the upstream GTK
   application ID (`org.dune3d.dune3d`), the `~/.config/dune3d` settings
   folder, and the `dune3d` binary/icon names internally -- only
   user-visible text was renamed so far (see the "Rename the app's display
   name" commit for why the rest was deliberately left alone).
 - **This fork's own changelog**: `CHANGELOG.md` only covers upstream
   Dune 3D's pre-fork release history. Everything since forking only
   exists in this repo's own `git log`, with no user-facing changelog of
   its own yet.
 - **All 13 of the ribbon's "Modify" dropdown items are now implemented**
   (Scale, Simplify, Remove, Shell, Offset Face, Draft, Move/Copy,
   Press/Pull, Align, Split Face, Split Body, Replace Face, and
   Silhouette Split --
   GroupScale/GroupSimplify/GroupRemove/GroupShell/GroupOffsetFace/GroupDraft/GroupMoveCopy/GroupPressPull/GroupAlign/GroupSplitFace/GroupSplitBody/GroupReplaceFace/GroupSilhouetteSplit).
   These started out as permanently disabled ribbon placeholders (present
   in the menu so it showed its intended full shape, but not real
   features); a few scope notes from implementing them:
   - Move/Copy's "Copy" checkbox still isn't wired up: it reports an
     explicit error instead of silently just moving. The document/body-
     management plumbing it would need now exists (see Split Body
     below), but nothing's hooked it up to Move/Copy's checkbox yet.
   - Press/Pull is mechanically identical to Offset Face -- same
     BRepOffset_MakeOffset/SetOffsetOnFace operation, exposed under both
     ribbon entry points.
   - Align aligns a single selected planar face to a fixed global XY
     target plane rather than to a second user-picked body/face,
     matching the fixed-origin convention Scale/Draft already use.
   - Split Face/Split Body both cut along a user-edited plane (point +
     normal) rather than a picked reference entity -- same
     fixed-parameter convention, see GroupSolidModelOperation's
     GroupButton/SelectGroupDialog pattern for how a future pass could
     let the cutting tool be picked instead.
   - **Split Body is this fork's first group type that creates a
     second, genuinely separate body** (GroupSplitBodyResult, paired
     with GroupSplitBody and always created together) -- see
     src/document/group/group_split_body.hpp for how `Group::m_body`
     makes this work with no changes needed to body derivation, the
     workspace tree, or the renderer, all of which already treated
     multiple bodies per document as a first-class case before this.
   - Replace Face picks its reference plane by clicking an existing
     EntityWorkplane in the 3D view (ToolSetReplaceFacePlane, mirroring
     ToolSetWorkplane) rather than a fixed numeric plane or a picked
     group -- and only supports trimming material away, not adding it
     past the selected face's current position, since that would need a
     bounded extrusion+fuse instead of a half-space boolean; it reports
     an explicit error for the unsupported "grow" direction rather than
     silently no-opping.
   - Silhouette Split projects the previous body's own outline, as seen
     along a user-edited direction (HLRBRep_Algo for the outline,
     BRepProj_Projection to project it onto the target face,
     BRepFeat_SplitShape to add it), so e.g. a cylindrical boss standing
     on a flat face casts a circular split line when viewed down its
     own axis. It always uses the current body as its own silhouette
     source rather than letting a second body/sketch be picked as the
     silhouette source -- the same fixed-single-body-input scope as
     Scale/Simplify/Shell/etc.)
 - **Same pattern in the "Insert" dropdown**: items like "Insert SVG" and
   "Insert Derive" exist in the menu as disabled placeholders too.
 - **CI still doesn't build against OCCT 8.0.1**: this project now
   targets OCCT 8.0.1 exclusively (see below), but every CI workflow
   under `.github/workflows/` still installs whatever OCCT version each
   distro/package-manager happens to ship (Windows CI specifically
   pinned OCCT 7.9.2 via a prebuilt mingw package, which has been
   removed since there's no 8.0.1 equivalent available). None of them
   build OCCT 8.0.1 from source, so CI currently can't be trusted to
   reflect what actually gets built and tested locally.

### Known incomplete features

 - Fix the sketch Mirror implementation.
 - Fix the default Create menu's Mirror implementation (the plain
   "Mirror" item, i.e. CREATE_GROUP_MIRROR_HORIZONTAL -- distinct from the
   sketch Mirror item above).
 - Implement Timeline functionality: the timeline strip exists and lets
   you click a feature to jump to it, but has none of the other things a
   CAD timeline implies (no rollback bar, reordering, or double-click to
   edit a feature).
 - Implement the Create menu's remaining items: Rib, Web, Hole, Thread,
   and Emboss are all still placeholders that just re-trigger a plain
   Extrude instead of doing anything distinct.
 - Wire up logic behind the tree's Document Settings/Named Views/Origin
   children: the rows exist (Document Settings -> Units, Part Design;
   Named Views -> Top, Front, Right, Home; Origin -> 0, x, y, z, xy, xz,
   yz) but none of them do anything yet. At minimum, Named Views' Top/
   Front/Right/Home presumably want the same camera change as clicking
   those faces/the Home icon on the nav cube, and Origin's seven children
   presumably want their own visibility toggles.
 - **The sketch ribbon's "Modify" dropdown is implemented** (Fillet,
   Chamfer with its own 3-variant submenu, Blend Curve, Offset, Trim,
   Extend, Break, Sketch Scale, Move, Copy -- the standalone Fillet/
   Chamfer buttons stay too, same "quick buttons + full dropdown"
   convention the solid Modify group already uses). A few scope notes:
   - Fillet/Chamfer's equal-distance variant are the pre-existing
     ToolSketchFillet/ToolSketchChamfer (live cursor-drag, unchanged).
     Every other item uses a different, simpler interaction: a modal
     datum-entry dialog (`show_enter_datum_window`, the same mechanism
     `ToolRotate`/`ToolEnterDatum` already use) instead of a live drag,
     since matching Fillet's exact drag-and-preview fidelity for 7
     brand-new tools wasn't practical to build *and verify* without any
     GUI automation in this environment -- only the underlying geometry
     math was checked against hand-computed values (standalone `glm`
     test programs), plus a clean build and crash-free startup. Live
     interactive feel (does the preview track smoothly, does
     hover/click selection work right) has not been verified.
   - Chamfer's Distance-Distance/Distance-Angle submenu variants are a
     separate new tool (`ToolSketchChamferAdvanced`) rather than
     changes to the existing equal-distance tool, to avoid risking
     regressions in that already-working one.
   - Trim and Extend share one implementation: given two selected
     lines, both just move each line's nearer endpoint to where their
     underlying infinite lines intersect -- whether that shortens or
     lengthens either line is a consequence of the input geometry, not
     a different algorithm. Only straight lines are supported (no
     arcs/circles).
   - Break is the one tool with live cursor tracking (which point along
     the curve to split at); every other new tool is selection + a
     fixed dialog value.
   - Sketch Scale scales about the active workplane's origin (fixed,
     matching `GroupScale`'s convention from the solid-modeling Modify
     dropdown) -- no center-point picking.
   - Move/Copy's offset is entered as two sequential dialogs (X then Y)
     since there's no existing two-field dialog. "Copy" clones the
     selection first (`tool_paste.cpp`'s clone + constraint-remap
     pattern) then moves the clones -- unlike the solid-modeling Move/
     Copy's "Copy" checkbox, this one is fully implemented, since 2D
     sketch entities have none of the document/body complexity that
     left that one unimplemented.
   - Offset creates new parallel entities rather than modifying the
     originals, tied back to them with `ConstraintParallel` (lines) or
     a pinned `ConstraintRadius` (arcs/circles).
   - Blend Curve connects the nearest endpoints of two selected lines
     (which may be far apart, unlike Fillet's shared corner) with a
     tangent-continuous cubic Bezier, "bulge" entered via the same
     dialog mechanism; only straight lines are supported.

## Questions

This is a personal fork with no separate community, discussion board, or
support channel of its own. For general questions about Dune 3D itself (not
this fork's own changes), upstream's
[documentation](https://docs.dune3d.org/), [matrix room](https://matrix.to/#/#dune3d:selfnet.de)
and [GitHub Discussions](https://github.com/dune3d/dune3d/discussions) are
the right place to ask -- they are upstream's channels, not this fork's.
