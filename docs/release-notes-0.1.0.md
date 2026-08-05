First release of Tessera, a small 3D model viewer that reads about seventy
formats, renders them with physically-based shading, and doubles as a format
converter and a headless thumbnailer.

## Install

Download `tessera-0.1.0-macOS-universal.dmg`, open it, and drag Tessera to
Applications.

**First launch.** This build is signed ad-hoc rather than with an Apple
Developer ID, so macOS will refuse it once with *"Tessera cannot be opened
because the developer cannot be verified."* This is expected and does not mean
the download is damaged.

Right-click (or Control-click) the app → **Open** → confirm. Once only. Or from
a terminal:

```bash
xattr -dr com.apple.quarantine /Applications/Tessera.app
```

Universal binary. Runs natively on Apple silicon and Intel. Requires macOS 13.3
or later.

## What it does

- **Views** ~70 formats: OBJ, STL, PLY, glTF/GLB, FBX, COLLADA, 3DS, BLEND,
  DXF, X3D, STEP and more
- **Converts** between 16 writable formats: `tessera part.step -o part.stl`
- **Renders headlessly**, with no window, for batch thumbnails:
  `tessera model.fbx -r thumb.png -s 512x512`

Hand-written readers for OBJ, STL and PLY on a fast path; Assimp for the rest.
PLY includes point clouds, so LiDAR and photogrammetry exports open too.

Ten shading modes (PBR, clay, base colour, normals, tangents, UV, metallic,
roughness, occlusion, vertex colour), wireframe / normal / bounding-box
overlays, an infinite ground grid, click-to-select, and a measurement tool
backed by a BVH.

Double-clicking an associated model file opens it in Tessera.

## Command line

The bundled binary is the same one the CLI modes use:

```bash
ln -s /Applications/Tessera.app/Contents/MacOS/Tessera /usr/local/bin/tessera
tessera --help
```

## Known limitations

- **macOS only.** The Linux and Windows build paths exist in the source but have
  never been compiled. Reports welcome.
- **The OpenGL backend is the only complete one.** Vulkan, Metal, OptiX and CUDA
  detect hardware and report it, but have no renderer. `tessera --list-backends`
  tells you exactly what your build and your machine support.
- **Animations are not played.** Rigged files load and display in their bind
  pose.

## Verify your download

```bash
shasum -a 256 -c SHA256SUMS.txt
```
