# Adding a format

Adding a format means writing one file and one registration line. Nothing else
in the project changes: the UI, the file browser's filter, `--formats`, the
converter and the renderer all pick it up automatically.

That is the whole point of `scene::Scene`. An importer's only job is to fill in
that structure. It never learns how anything is drawn or saved.

## How reading a file currently works

```bash
tessera --formats          # ~70 readable
tessera --export-formats   # 16 writable
```

**OBJ**, **STL** and **PLY** have hand-written readers. They read the whole file
into memory and parse numbers in place rather than streaming, which is what
makes them quick on the multi-million-triangle dumps those three formats
attract. Everything else goes through [Assimp](https://github.com/assimp/assimp).

Importers are tried in **priority order**. The native readers register at 100,
Assimp at 0. If a native reader declines a file, the general one still gets a
turn, so an exotic variant of a format never fails just because the specialised
path was too strict.

## Writing an importer

```cpp
class MyImporter final : public IImporter {
public:
    std::string name() const override { return "myfmt"; }

    std::vector<FormatInfo> formats() const override {
        return {{"myfmt", "My Format"}};
    }

    // Native readers use 100; the general-purpose fallback uses 0.
    int priority() const override { return 100; }

    bool load(const std::filesystem::path& path, const ImportOptions& options,
              scene::Scene& out, std::string& error) override {
        // Fill out.meshes / out.materials / out.images / out.nodes.
        // Return false with a filled-in `error` if this is not your format,
        // and a lower-priority importer will be tried instead.
        return true;
    }
};
```

Register it in `registerBuiltinImporters()` (in `io/ImporterRegistry.cpp`) and
add the file to `src/CMakeLists.txt`.

Exporters work identically through `io::IExporter` and
`registerBuiltinExporters()`.

## What you do not have to do

`Scene::finalize()` runs after every successful load, so do not reimplement any
of this:

- **Normals**, generated when a mesh has none, area-weighted
- **Tangents**, generated when a material actually uses a normal map
- **Bounds**, computed per mesh
- **Orphaned meshes**, adopted under the root if you did not attach them to a node
- **Statistics** for the UI

So the minimum viable importer fills `out.meshes` with positions and indices and
returns true. Everything else is optional refinement.

## Things that catch people out

**Report failure honestly.** Returning false with a clear `error` is not a
defeat: it is how the priority chain works. The registry collects every
attempt's message, so a file that no importer can read produces a diagnosis
rather than a shrug.

**Texture paths are rarely usable as written.** Files reference textures with
Windows separators, absolute paths from another machine, or bare file names.
`io::resolveTexturePath()` already handles the common cases, including a
case-insensitive match in the model's own directory. Use it rather than opening
the string directly.

**Decide what a "mesh" is deliberately.** One mesh per material is usually
right, because it maps one-to-one onto a draw call. The OBJ reader buckets by
`(object, material)` for this reason. Splitting more finely costs draw calls;
splitting less loses per-part selection in the outliner.

**Vertex colours and point clouds are real cases.** A file with no faces should
produce `Topology::Points` rather than an empty mesh; that is the normal shape
of LiDAR and photogrammetry exports, and the renderer handles it.

**Respect `ImportOptions`.** At minimum `flipUVs`, since roughly half the
formats in the world disagree about which way V points.

## Testing it

Add a small fixture to `tests/models/` and a case to `tests/CMakeLists.txt`:

```cmake
tessera_convert_test(myfmt_to_stl ${MODELS}/sample.myfmt ${SCRATCH}/sample.stl)
```

That runs the importer, writes the result through a different exporter, and
fails if the output is missing or suspiciously small, which catches the common
failure of a file that parses without producing any geometry.

Keep fixtures small and, where the format allows, human readable. They are
committed, and a reviewer should be able to see what a test is actually
exercising.

```bash
ctest --test-dir build -LE render     # no graphics context needed
```

The import and export path needs no GPU, so these run anywhere, including CI on
Windows.
