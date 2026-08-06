Another performance release. Scenes made of many separate meshes draw about
**five times faster** than 0.2.0, and **thirteen times faster** than 0.1.0.

## Install

Download `tessera-0.3.0-macOS-universal.dmg`, open it, and drag Tessera to
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

## Mesh batching

Opaque meshes that share a material are now merged into a single buffer at load
and drawn together.

All three versions measured in one session, interleaved, on 4000 meshes at
1280x800 on an M1 Pro:

| Version | Frame time | Draw calls |
| --- | --- | --- |
| 0.1.0 | 17.34 ms (58 fps) | 4002 |
| 0.2.0 | 6.41 ms (156 fps) | 4002 |
| **0.3.0** | **1.32 ms (757 fps)** | **10** |

A scene of one 1.2M triangle mesh is unchanged at 2.9 ms across all three
releases, which is the expected result rather than a disappointment: that scene
is limited by the GPU and always was. Everything here is about the cost of
submitting work, not doing it.

Two side effects worth mentioning. Loading is about six times quicker, because
the same geometry now needs eight buffer allocations instead of four thousand.
And memory is unchanged, since a merged mesh does not also keep a copy of its
own.

Hiding, isolating and selecting individual parts all still work. Each mesh keeps
an index range inside its batch, so those operations pick which ranges to draw
rather than rebuilding anything.

In practice this matters most for CAD assemblies and other files made of
thousands of small parts. A typical glTF or FBX model has tens to hundreds of
meshes and was already fast.

## Groundwork for other graphics APIs

No user-visible change, but worth recording: the `IRenderBackend` contract is
now specified rather than implied, before Vulkan, Metal, OptiX or CUDA are
written against it. Resize ownership, whether offscreen rendering is
synchronous, how a backend reports missing hardware as distinct from broken
hardware, threading rules, and what happens when a frame fails.

The one behavioural consequence: if a graphics device is ever lost mid-session,
Tessera now exits with an explanation instead of continuing to draw nothing. The
OpenGL backend cannot detect that case, so this changes nothing today.

Details in [docs/BACKENDS.md](../docs/BACKENDS.md).

## Verify your download

```bash
shasum -a 256 -c SHA256SUMS.txt
```
