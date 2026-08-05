// NVIDIA OptiX backend.
//
// OptiX is a ray-tracing framework, not a rasteriser, so this backend would be
// a progressive path tracer rather than a port of the OpenGL renderer: it needs
// an acceleration structure over the scene, .cu programs for ray generation,
// closest-hit and miss, and an accumulation buffer that resets whenever the
// camera moves. Nothing in gfx/opengl/Shaders.h carries over.
//
// It requires an NVIDIA RTX GPU and the OptiX SDK, which sits behind NVIDIA's
// developer login and so can never be fetched by the build. Device detection
// below is real; the renderer is not written yet.

#include "gfx/UnimplementedBackend.h"

#include <cuda_runtime.h>
#include <optix.h>
#include <optix_stubs.h>

#include <format>
#include <string>

namespace tessera::gfx {
namespace {

class OptixBackend final : public UnimplementedBackend {
public:
    OptixBackend()
        : UnimplementedBackend(BackendId::Optix, "optix", "NVIDIA OptiX (path tracing)") {}

protected:
    [[nodiscard]] std::string detectStatus() const override {
        int count = 0;
        if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) {
            return "no CUDA-capable device";
        }

        cudaDeviceProp properties{};
        if (cudaGetDeviceProperties(&properties, 0) != cudaSuccess) {
            return "CUDA device present, properties unavailable";
        }

        // optixInit loads the driver's ray-tracing entry points; failure here
        // means the GPU or driver predates OptiX support.
        if (optixInit() != OPTIX_SUCCESS) {
            return std::format("{}: driver has no OptiX support", properties.name);
        }
        return std::format("{} (OptiX available, renderer not implemented)", properties.name);
    }
};

}  // namespace

BackendPtr makeOptixBackend() { return std::make_unique<OptixBackend>(); }

}  // namespace tessera::gfx
