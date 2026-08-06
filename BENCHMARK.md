# Benchmarking

Tessera has a built-in benchmark mode so renderer changes can be measured
instead of argued about. This file records how to run it, what the numbers mean,
and what they were at the points where the renderer changed.

Read the [methodology](#methodology) before quoting any number here. Absolute
timings are a property of the machine that produced them and do not transfer.

## Running it

Generate the stress scenes first. They are produced rather than committed,
because one is 57 MB and neither is interesting to read:

```bash
python3 benchmarks/make_scenes.py
```

Then:

```bash
tessera benchmarks/scenes/many.obj  --benchmark 300 -s 1280x800 -q
tessera benchmarks/scenes/heavy.stl --benchmark 200 -s 1280x800 -q
```

`--benchmark N` renders N frames into a hidden window with vsync disabled,
orbiting the camera each frame so the driver cannot cache a static image, and
reports mean, median, best and 95th percentile. The first frames are discarded:
shader compilation and buffer residency land there and would skew the mean.

## The two scenes

They are deliberately lopsided, so a change that helps one and not the other is
immediately visible.

| Scene | Shape | What it measures |
| --- | --- | --- |
| `many.obj` | 4000 meshes, 48k triangles, 8 materials | CPU cost of submitting draws |
| `heavy.stl` | 1 mesh, 1.2M triangles | GPU cost of processing vertices |

`many.obj` is the CAD-assembly case: thousands of separate parts with almost no
geometry in each. `heavy.stl` is the scanned-or-sculpted case: one dense mesh.

The gap between them is the point. When this was first measured, 48k triangles
across 4000 meshes rendered **six times slower** than 1.2M triangles in one
mesh. Twenty-five times the geometry, a sixth of the cost. That single
comparison said the bottleneck was never the GPU.

## Methodology

**Only compare runs taken back to back, on the same machine, in the same
session.** Everything else is noise.

Measured variance on the reference machine:

- Consecutive runs of an identical binary: about **±5%**
- Across sessions, with different thermal state or background load: **20% or more**

So a change is only worth claiming if it clears roughly **10%**. Anything
smaller is indistinguishable from the machine having a different afternoon.

There is a second effect worth knowing: the noisier the measurement, the faster
the renderer. A CPU-saturated 17 ms frame reproduces to within 0.4%, while a
7 ms frame varies by 10%, because scheduling jitter is a constant that becomes
proportionally larger as the real work shrinks.

### Comparing a change against main

Build both and interleave them. Do not measure one, make the change, and measure
again an hour later.

```bash
git worktree add /tmp/tessera-before main
cmake -S /tmp/tessera-before -B /tmp/tessera-before/build \
      -DCMAKE_BUILD_TYPE=Release -DTESSERA_BUILD_TESTS=OFF
cmake --build /tmp/tessera-before/build -j

for i in 1 2 3; do
  /tmp/tessera-before/build/bin/tessera benchmarks/scenes/many.obj --benchmark 300 -q | grep median
  ./build/bin/tessera                    benchmarks/scenes/many.obj --benchmark 300 -q | grep median
done
```

If `--benchmark` did not exist yet on the branch you are comparing against,
graft the harness across rather than giving up on the comparison:

```bash
cd /tmp/tessera-before && git checkout main -- src/app src/main.cpp
```

That gives you the old renderer with the current harness, which is the
experiment you actually want.

## Recorded results

Reference machine: Apple M1 Pro, macOS 26, OpenGL 4.1 via the Metal driver.
Release build, 1280x800, median of three interleaved runs.

| Commit | `many.obj` | `heavy.stl` |
| --- | --- | --- |
| Before the draw-path work | 17.5 ms (57 fps) | 2.9 ms (345 fps) |
| After the draw-path work | 7.9 ms (127 fps) | 2.9 ms (345 fps) |

The heavy scene is unchanged, which is the expected and desired outcome: it was
GPU-bound before and is GPU-bound now. Nothing done to the CPU side could help
it, and a change there would have meant something had gone wrong.

## What each change was worth

Measured one at a time, in the order they were applied, on `many.obj`.

| Change | Frame time | Gain |
| --- | --- | --- |
| Baseline | 17.42 ms | |
| Uniform lookups stop allocating | 16.93 ms | 3%, **within noise** |
| Group draws by material, skip redundant binds | 11.55 ms | 32% |
| Cached draw list, precomputed bounds, frustum culling | 11.18 ms | 3%, **within noise** |
| World transforms baked into vertex buffers | 6.97 ms | 38% |

Two of those five are inside the noise floor and are honestly not demonstrated
wins on this benchmark. They were kept anyway, for reasons that are about
correctness of approach rather than measured gain:

- Not allocating on every uniform lookup is right regardless, and the benchmark
  machine has fast allocation; a machine with a slower allocator would show more.
- Frustum culling does nothing here **by construction**, because the benchmark
  frames the whole scene so there is nothing off-screen to reject. It pays off
  when the camera is close, which is most of the time in real use. Measuring it
  properly needs a scene the camera only partly sees.

The two large wins were both found by measurement rather than intuition. The
prediction going in was that per-uniform allocation would dominate; it was worth
3%. The decisive change came from a throwaway experiment: stubbing out the two
per-draw matrix uniforms dropped the frame to 7.29 ms, proving they alone cost
35% and pointing straight at baking transforms into the vertex buffers.

## Known headroom

The draw path still issues one call per mesh, and this platform's OpenGL costs
roughly 1.7 microseconds per bind-and-submit pair. On `many.obj` that is most of
the remaining 7 ms.

The next real step is merging meshes that share a material into single buffers
with per-mesh index ranges, taking 4002 draws down to about 8. It touches
selection, per-mesh visibility and culling granularity, so it deserves to be
done deliberately rather than bolted on.

Worth keeping in perspective: 4000 separate meshes is a deliberate worst case. A
typical glTF or FBX model has tens to hundreds of meshes and is already
comfortably under a millisecond.
