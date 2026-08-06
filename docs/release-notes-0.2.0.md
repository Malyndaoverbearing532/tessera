A performance release. Scenes made of many separate meshes draw about **2.2x
faster**; everything else is unchanged.

## Install

Download `tessera-0.2.0-macOS-universal.dmg`, open it, and drag Tessera to
Applications.

**First launch.** This build is signed ad-hoc rather than with an Apple
Developer ID, so macOS will refuse it once with *"Tessera cannot be opened
because the developer cannot be verified."* This is expected and does not mean
the download is damaged.

Right-click (or Control-click) the app, choose **Open**, then confirm. Once
only. Or from a terminal:

```bash
xattr -dr com.apple.quarantine /Applications/Tessera.app
```

Universal binary. Runs natively on Apple silicon and Intel. Requires macOS 13.3
or later.

## Faster on scenes with many meshes

Measured on 4000 meshes at 1280x800, median of three interleaved runs on an
M1 Pro:

| | 0.1.0 | 0.2.0 |
| --- | --- | --- |
| 4000 meshes, 48k triangles | 17.5 ms (57 fps) | **7.9 ms (127 fps)** |
| 1 mesh, 1.2M triangles | 2.9 ms (345 fps) | 2.9 ms (345 fps) |

The second row is deliberately unchanged. That scene was limited by the GPU and
still is, so nothing done to the CPU side could help it. All of the gain is in
how draws are submitted:

- Draws are grouped by material and skip rebinding one already in use, turning
  thousands of material binds into a handful.
- World transforms are folded into vertex buffers at load for meshes used at one
  place in the hierarchy, removing two matrix uploads per mesh per frame.
- The draw list is built once per scene rather than once per frame, with bounds
  and normal matrices precomputed.
- Geometry outside the view is rejected before submission.

In practice this matters most for CAD assemblies and other files made of
thousands of small parts. A typical glTF or FBX model has tens to hundreds of
meshes and was already fast.

## New: a benchmark mode

```bash
tessera model.glb --benchmark 300 -s 1280x800
```

Renders offscreen with vsync disabled and reports mean, median, best and 95th
percentile frame times. Added so renderer changes can be measured rather than
argued about, and it is what produced the table above.

## Fixes

- **Bounding box overlay** drew in the wrong place for any file with a node
  hierarchy.
- **A reserved GLSL keyword** in the fragment shader stopped it compiling on
  Mesa. Apple's compiler accepted it, so this only ever broke non-Apple drivers.
- **Shader programs outlived their OpenGL context** on shutdown, which macOS
  ignored and Mesa turned into a crash after rendering completed.
- **The OpenGL loader generator** failed on Windows, where the locale encoding
  corrupted the Khronos spec it reads.

The last three only affect people building on Linux or Windows, since those are
the only platforms they broke.

## Platform status

Linux and Windows now build and pass the test suite in CI on every push. That is
new in this release and is worth stating precisely: CI runs on headless virtual
machines with no GPU, so it proves the code compiles and the logic is right. It
does not prove the application behaves correctly on real hardware, and nobody
has yet run Tessera on a real Linux or Windows desktop.

Windows CI does not render at all, because those runners provide only
OpenGL 1.1, below what the renderer needs.

If you run it on either, there is now an issue template that asks for the
details needed to act on a report.

## Verify your download

```bash
shasum -a 256 -c SHA256SUMS.txt
```
