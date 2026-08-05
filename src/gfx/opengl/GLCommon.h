#pragma once

// glad must come before any other GL header.
#include <glad/glad.h>

#include <string_view>

namespace tessera::gfx {

/// Drains the GL error queue and logs anything found. Compiled to a no-op in
/// release builds so the hot path stays clean.
void checkGlErrors(std::string_view where);

#if defined(NDEBUG)
#define TESSERA_GL_CHECK(where) ((void)0)
#else
#define TESSERA_GL_CHECK(where) ::tessera::gfx::checkGlErrors(where)
#endif

}  // namespace tessera::gfx
