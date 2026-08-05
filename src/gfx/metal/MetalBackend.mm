// Metal backend (Objective-C++).
//
// Metal does not require Xcode.app: the frameworks ship with the OS, and this
// file compiles with clang from the Command Line Tools. The only piece Xcode
// would add is the offline `metal` shader compiler, which we deliberately do
// not depend on - a finished backend here should build its pipeline with
// -[MTLDevice newLibraryWithSource:options:error:], compiling MSL at runtime.
//
// Device detection below is real. The renderer is not written yet.

#include "gfx/UnimplementedBackend.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <string>

namespace tessera::gfx {
namespace {

class MetalBackend final : public UnimplementedBackend {
public:
    MetalBackend() : UnimplementedBackend(BackendId::Metal, "metal", "Metal") {}

protected:
    [[nodiscard]] std::string detectStatus() const override {
        @autoreleasepool {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (device == nil) return "no Metal device";

            std::string name = [[device name] UTF8String];
            if ([device supportsFamily:MTLGPUFamilyApple7]) {
                name += " (Apple7+)";
            } else if ([device supportsFamily:MTLGPUFamilyMac2]) {
                name += " (Mac2)";
            }
            return name + " (detected, renderer not implemented)";
        }
    }
};

}  // namespace

BackendPtr makeMetalBackend() { return std::make_unique<MetalBackend>(); }

}  // namespace tessera::gfx
