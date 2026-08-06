# Architecture

Tessera reads about seventy formats in roughly six thousand lines. That is only
possible because format parsing is not allowed to leak into the rest of the
program. This document explains the seams that keep it that way, and the
invariants you have to respect when changing things.

## Layout

```
src/core      logging, math, AABB, frustum, ray/triangle intersection
src/scene     Scene / Node / Mesh / Material / Image, the format-agnostic IR
src/io        IImporter + registry, native OBJ/STL/PLY, Assimp, exporters
src/gfx       IRenderBackend + one directory per graphics API
src/camera    orbit camera with Blender-style bindings
src/tools     lazily-built BVH for picking and measurement
src/ui        Dear ImGui panels
src/app       window, input, CLI, headless and benchmark modes
```

Dependencies flow one way:

```
app  ->  ui  ->  gfx  ->  scene  <-  io  ->  core
```

Nothing in `scene` or `io` knows a renderer exists. That is not a style
preference: it is what allows `tessera_core` to link without OpenGL, GLFW or a
window, so format conversion and most of the test suite run on machines with no
graphics context at all.

## The two seams

Almost every design decision follows from these.

### `scene::Scene` is plain data

Meshes, materials, images, and a node hierarchy. Importers produce it, and
everything downstream consumes it: the renderer, the exporters, the picker, the
outliner.

This is why adding a format touches exactly one file. An importer's only job is
to fill in this structure; it never learns how anything is drawn or saved.

`Scene::finalize()` handles the fixups so importers do not each reinvent them:
missing normals, tangents where a normal map needs them, per-mesh bounds,
orphaned meshes adopted under the root, and the statistics shown in the UI.

`Scene::forEachMeshInstance()` is the single traversal that every consumer uses.
If you need to walk the scene, use it rather than writing another recursion.

### `gfx::IRenderBackend` is the rendering contract

Everything above it, including importers, the scene, the camera, picking and the
UI, is graphics-API agnostic. A new backend is a directory under `src/gfx/` and
one line in the registry.

See [BACKENDS.md](BACKENDS.md) for the contract itself and the state of each
implementation.

## Invariants worth knowing

**GPU objects must be released while their context is current.** `shutdown()`
has to free everything; anything left to a member destructor is destroyed after
the backend object dies, which is after GLFW has torn the context down. macOS
silently ignores that, Mesa segfaults. Both entry points drop the backend before
terminating GLFW so the ordering cannot drift back.

**World transforms may already be baked into vertex buffers.** A mesh used at
exactly one node has its transform folded in at upload, so its `DrawItem` holds
an identity matrix. Any code combining an instance matrix with object-space data
is therefore wrong: use the precomputed `worldBounds` instead. This bit the
bounding-box overlay once already.

**The draw list is cached, and batches are built from it.** The list is rebuilt
only when the scene changes, not per frame and not when visibility changes. If
you add state that affects *which meshes exist*, set `drawListDirty_`; if it
only affects whether one is drawn, do not, or you will rebuild GPU buffers for
a checkbox.

**Opaque meshes sharing a material are merged into one buffer.** They are
concatenated in world space, so a batch draws with an identity model matrix and
no per-mesh uniforms at all. Each mesh keeps an index range inside the batch,
which is how hiding, culling and selection still address one mesh: consecutive
visible ranges are coalesced, so a fully visible batch costs a single draw call
and the worst case degrades to one per mesh. Meshes that are blended, non
triangular, or instanced at several nodes keep their own buffer.

**Picking uses CPU-side scene data, not GPU state.** `tools::Picker` builds a
BVH per mesh lazily, on first click, so opening a large model stays fast and only
meshes you actually click near pay for a tree.

## Non-obvious decisions

A few things that look arbitrary and are not. Each cost real debugging time.

**The macOS deployment target is 13.3.** libc++ keeps floating-point
`std::to_chars` in the shared library rather than the headers, annotated for
availability. Every `std::format("{:.3f}", x)` in the project calls it, so a
lower target fails to compile all of them.

**The ASCII parsers have their own float scanner.** Floating-point
`std::from_chars` is gated at macOS 26 in libc++. The replacement in
`io/FileUtil.cpp` is also faster than `strtod` and, unlike it, locale
independent, so a comma decimal separator cannot silently corrupt a mesh.

**Assimp's bundled zlib is avoided where a system one exists.** The vendored
copy tests `defined(TARGET_OS_MAC)` as though it were a classic Mac OS marker.
Modern SDKs always define it, so that copy does `#define fdopen(fd,mode) NULL`
and poisons `<stdio.h>` for every file compiled after it.

**No GLSL identifier may be named `packed`.** It is a reserved keyword. Apple's
compiler accepts it anyway; Mesa correctly refuses, so this broke every render
on Linux while looking fine on macOS. Worth remembering that the Apple GLSL
front end is unusually permissive, and CI is the only thing that catches it.

**glad's own CMake is not used.** It declares
`cmake_minimum_required(VERSION 3.0)`, which CMake 4 refuses outright. Driving
the generator directly also allows `--reproducible`, building the loader from a
pinned Khronos spec instead of whatever is on the registry today. The generator
must run under `PYTHONUTF8=1`, because it opens its XML in text mode and the
Windows locale encoding turns it into mojibake.

**Point clouds shade toward the camera.** They carry no normals, and PBR
lighting on a zero normal renders black.

## Performance

The renderer is CPU-bound on scenes with many meshes and GPU-bound on scenes
with few. [BENCHMARK.md](../BENCHMARK.md) covers how to measure it, the noise
floor, and what past changes were actually worth.
