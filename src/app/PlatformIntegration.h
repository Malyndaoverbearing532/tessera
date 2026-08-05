#pragma once

#include <string>

namespace tessera::app::platform {

/// Starts listening for "open this document with tessera" requests from the
/// desktop environment. Call once, after the window system is up.
///
/// On macOS a double-clicked file does not arrive in argv: Launch Services
/// sends the running process an Apple Event instead, which GLFW does not
/// forward. Everywhere else this is a no-op and files arrive as arguments or
/// via drag and drop.
void installOpenFileHandler();

/// Pops one queued path, or returns false when there is nothing waiting.
/// Polled from the main loop so the handler never touches renderer state from
/// another callback.
bool takePendingOpenFile(std::string& path);

}  // namespace tessera::app::platform
