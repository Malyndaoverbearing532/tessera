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
