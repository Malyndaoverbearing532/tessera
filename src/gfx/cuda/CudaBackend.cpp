// CUDA backend.
//
// NVIDIA hardware only, so Linux or Windows only: Apple has shipped no CUDA
// driver since macOS 10.13, and none exists for Apple silicon. CMake refuses to
// enable this backend on Apple platforms for that reason.
//
// Device detection is real. The renderer is not written yet; finishing it means
// a compute rasteriser (tile binning, depth resolve via atomics) writing into a
// surface that is then blitted for presentation.

#include "gfx/UnimplementedBackend.h"

#include <cuda_runtime.h>

#include <format>
#include <string>

namespace tessera::gfx {
namespace {

class CudaBackend final : public UnimplementedBackend {
public:
    CudaBackend() : UnimplementedBackend(BackendId::Cuda, "cuda", "CUDA (compute rasteriser)") {}

protected:
    [[nodiscard]] std::string detectStatus() const override {
        int count = 0;
        const cudaError_t status = cudaGetDeviceCount(&count);
        if (status != cudaSuccess) {
            return std::format("no CUDA device: {}", cudaGetErrorString(status));
        }
        if (count == 0) return "CUDA runtime present but no devices";

        cudaDeviceProp properties{};
        if (cudaGetDeviceProperties(&properties, 0) != cudaSuccess) {
            return std::format("{} device(s), properties unavailable", count);
        }
        return std::format("{} (sm_{}{}, {} device(s), renderer not implemented)", properties.name,
                           properties.major, properties.minor, count);
    }
};

}  // namespace

BackendPtr makeCudaBackend() { return std::make_unique<CudaBackend>(); }

}  // namespace tessera::gfx
