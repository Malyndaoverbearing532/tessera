# Render backends

Tessera's renderer sits behind `gfx::IRenderBackend`. Everything above it is
graphics-API agnostic, so a new backend is one directory under `src/gfx/` and
one line in the registry.

```bash
tessera --list-backends
tessera model.glb --backend opengl
```

## State of each

| Backend | CMake option | State |
| --- | --- | --- |
| `opengl` | `TESSERA_BACKEND_OPENGL` | **Complete.** GL 3.3 core, the reference implementation |
| `vulkan` | `TESSERA_BACKEND_VULKAN` | Device detection only |
| `metal` | `TESSERA_BACKEND_METAL` | Device detection only |
| `optix` | `TESSERA_BACKEND_OPTIX` | Device detection only |
| `cuda` | `TESSERA_BACKEND_CUDA` | Device detection only |

The four unfinished ones inherit `gfx::UnimplementedBackend`. They report the
hardware they actually find and refuse at `initialize()` with a message naming
what is missing, rather than pretending. `--list-backends` distinguishes three
states that are genuinely different: not compiled in, compiled in but no usable
device, and ready.

Only OpenGL is on by default. The others are opt-in, which means half-finished
backend code cannot affect a default build or a released binary. That is
compile-time isolation, and it is stronger than remembering not to merge.

## The contract

To finish a backend, override these. The OpenGL implementation in
`src/gfx/opengl/` is a working reference for every one of them.

| Method | Responsibility |
| --- | --- |
| `info()` | Name, and whether a usable device exists right now |
| `windowRequirements()` | What GLFW must be told **before** the window exists |
| `initialize()` | Bring up device resources. `window` is null when headless |
| `shutdown()` | Release everything, while the context is still current |
| `setScene()` | Upload GPU resources; null releases them |
| `render()` | Draw one frame |
| `present()` | Show it. Separate from `render()` so the UI can draw in between |
| `renderToImage()` | Render offscreen, return RGBA8, bottom row first |

`windowRequirements()` is queried from a freshly constructed backend before the
window is created, because GLFW has to be told up front whether to make a
context at all. Vulkan and Metal manage their own surface and ask for
`GLFW_NO_API`.

## Semantics

The signatures above do not say enough to implement against. These rules do, and
they are binding: a backend that breaks one of them will appear to work on the
author's machine and fail somewhere else. They were written before the second
backend existed, deliberately, because changing them afterwards means changing
every implementation at once.

### Ownership and resize

**`initialize()` owns everything device-side**: the instance or context, the
physical device selection, the logical device, the surface, the swapchain, the
render targets, and any pipeline objects. Nothing is created lazily on the first
frame except geometry upload.

**`render()` receives the target size on every call and must tolerate it
changing between any two calls.** There is deliberately no `resize()` method.
The window can be resized, moved between monitors of different scale factors, or
made fullscreen, and none of that reaches the backend as an event. The backend
compares the size it was handed against what it built and recreates internally
when they differ.

**Both `render()` and `present()` may rebuild the swapchain**, because Vulkan can
report `VK_ERROR_OUT_OF_DATE_KHR` at acquire time or at present time, and the
backend cannot know which in advance. Neither reports that to the caller.
**Dropping a single frame during recreation is acceptable**; failing the call is
not. The application has no sensible recovery to offer, so do not ask it for one.

`present()` receives the same window that was passed to `initialize()`. Passing a
different one is a programming error, not a supported mode.

### `renderToImage()` is synchronous

**When it returns true, `rgba` is complete and no GPU work is outstanding.** The
backend must have waited: `glFinish`, `vkQueueWaitIdle`, or
`waitUntilCompleted`, whichever applies. No handles, no futures, no
"finished by the time you use it".

This is not a default chosen by inertia. Every caller writes the pixels to a PNG
on the very next line, in `--render`, in the screenshot action, and in batch
thumbnailing where the process may exit immediately afterwards. An asynchronous
contract would buy nothing and would turn a missed synchronisation into a
corrupt or empty image.

Three further requirements:

- It must work with **no window at all**. Headless mode passes `nullptr` to
  `initialize()`, and this is the only drawing entry point that runs there.
- It must not leave state that breaks the next `render()`. In practice `render()`
  binds its own target first, so an offscreen target left bound is harmless, but
  do not rely on the caller to clean up after you.
- The buffer is tightly packed RGBA8, **bottom row first**, which is what
  `glReadPixels` produces and what the PNG writer expects to flip. Vulkan and
  Metal produce top-down images, so **those backends must flip before
  returning.** This is the single easiest thing to get wrong here, and the
  symptom is an upside-down thumbnail that no test currently catches.

### Reporting why a backend will not run

Three states are genuinely different and must not be collapsed:

| State | Where it is reported | Example |
| --- | --- | --- |
| Not compiled in | the registry, without constructing anything | built without `TESSERA_BACKEND_VULKAN` |
| No usable device | `info().available == false`, with a reason | no Vulkan loader, no CUDA device, no NVIDIA GPU |
| Device present, cannot initialise | `initialize()` returns false | out of memory, shader compile failure, surface creation failed |

`info()` **must be callable before `initialize()`, must be cheap, and must leave
nothing behind.** It may create a throwaway instance to enumerate devices, as the
Vulkan and Metal stubs already do, but it must destroy it before returning.
`--list-backends` calls it on every backend, so it runs even when the user asked
for something else entirely.

The message discipline matters more than it looks, because these failures are
reported by users who cannot debug them. Name the specific missing capability:

- "no Vulkan loader found" and "Vulkan 1.2 present, but no device supports
  `VK_KHR_swapchain`" send a reader to completely different places
- On macOS, "MoltenVK requires `VK_KHR_portability_subset`" is actionable;
  "Vulkan initialisation failed" is not
- "NVIDIA driver has no OptiX support" is different from "no CUDA-capable
  device", and a user with an RTX card hitting the first message needs to know
  it is their driver rather than their hardware

### Threading

**Every method is called from the thread that created the window, and backends
may assume that.** There is no locking, and none is planned.

This is a deliberate constraint rather than an oversight, and it is forced from
three directions at once: GLFW documents that window creation and event polling
are main-thread only, Dear ImGui is not reentrant, and anything touching
`CAMetalLayer` or an AppKit view has to be on the main thread regardless of what
the graphics API allows.

A backend may use worker threads internally, for shader compilation or buffer
staging, **provided it joins or otherwise synchronises before returning from the
method that started the work.** No IRenderBackend method may return while
touching data the caller can now mutate.

### Lifetime

**`shutdown()` must release every device object while the context or device is
still current.** Anything left to a member destructor runs after the backend is
destroyed, which is after the window and its context are gone. This is not
theoretical: it shipped once, macOS ignored it, and Mesa turned it into a
segfault immediately after a successful render.

The application drops the backend before terminating GLFW, and both the
interactive and headless paths do so explicitly so the ordering cannot drift.

- `initialize()` is not called twice without an intervening `shutdown()`.
- `setScene(nullptr)` releases GPU resources and is safe to call repeatedly.
- `setScene()` is never called during `render()`.
- `stats()` describes the most recently completed frame and is valid at any time
  after the first.

## Two costs that are easy to underestimate

**Dear ImGui needs a matching backend per renderer.** The UI currently goes
through `imgui_impl_opengl3`, and `supportsImGui()` returns true only for
OpenGL. A backend without an ImGui integration runs the viewer keyboard-only
rather than drawing a broken interface, which is deliberate but means Vulkan
needs `imgui_impl_vulkan` and Metal needs `imgui_impl_metal` before either is
really usable.

**The shaders multiply.** The GLSL lives in `src/gfx/opengl/Shaders.h`. Vulkan
wants SPIR-V, compiled ahead of time or at runtime with shaderc; Metal wants
MSL. Keeping several shader sources producing the *same* image is an ongoing
maintenance cost, not a one-time port.

## Notes per backend

### Metal

**Does not require Xcode.** The frameworks ship with macOS and the backend
compiles with the Command Line Tools alone. Only the offline `metal` shader
compiler is Xcode-only, which is why the design calls for building the pipeline
with `newLibraryWithSource:` and compiling MSL at runtime. CMake reports which
of the two situations it found at configure time.

Apple only, obviously. Device detection already works and reports the real GPU.

### Vulkan

Needs the LunarG SDK. On macOS it runs through MoltenVK rather than natively,
and the instance must be created with `VK_KHR_portability_enumeration` or the
driver refuses to load.

The largest rasteriser effort of the four by some distance, mostly boilerplate:
instance, device, swapchain, render passes, descriptor sets, pipeline objects,
synchronisation. Also the highest payoff, since one backend covers Linux,
Windows and macOS.

### CUDA and OptiX

**NVIDIA only, and therefore Linux or Windows only.** Apple has shipped no CUDA
driver since macOS 10.13 and none exists for Apple silicon, so CMake hard
disables both on Apple platforms rather than letting them fail at runtime.

OptiX additionally needs its SDK, which sits behind NVIDIA's developer login and
so can never be fetched by the build. Point `TESSERA_OPTIX_ROOT` at it.

Two things worth being clear-eyed about before starting either:

**A CUDA rasteriser will be slower than OpenGL, not faster.** CUDA and OpenGL
run on the same silicon, but the graphics pipeline reaches dedicated hardware
that CUDA cannot: the rasteriser itself, hierarchical-Z rejection, ROPs, texture
filtering units. A CUDA rasteriser reimplements all of it in shader cores.
NVIDIA's own published work on software rasterisation lands several times slower
than the fixed-function path. If you want CUDA in this project, the honest fit
is non-rendering compute: BVH construction for picking, normal and tangent
generation, decimation on large imports.

**OptiX is not a port, it is a different renderer.** It is a ray tracer, so that
backend would be a progressive path tracer: acceleration structures, ray
generation, closest-hit and miss programs, and an accumulation buffer that
resets when the camera moves. Nothing in `Shaders.h` carries over. It is also
the one backend that does something rasterisation fundamentally cannot, since it
reaches the RT cores.
