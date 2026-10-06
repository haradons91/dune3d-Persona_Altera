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
 - **Unimplemented items in the ribbon's "Modify" dropdown**: these are
   present in the menu (so it shows its intended full shape) but wired up
   as permanently disabled placeholders, not real features yet:
   Press/Pull, Replace Face, Split Face,
   Split Body, Silhouette Split, Move/Copy, Align. (Scale, Simplify,
   Remove, Shell, Offset Face, and Draft are implemented --
   GroupScale/GroupSimplify/GroupRemove/GroupShell/GroupOffsetFace/GroupDraft.)
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

## Questions

This is a personal fork with no separate community, discussion board, or
support channel of its own. For general questions about Dune 3D itself (not
this fork's own changes), upstream's
[documentation](https://docs.dune3d.org/), [matrix room](https://matrix.to/#/#dune3d:selfnet.de)
and [GitHub Discussions](https://github.com/dune3d/dune3d/discussions) are
the right place to ask -- they are upstream's channels, not this fork's.
