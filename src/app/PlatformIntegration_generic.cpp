// Windows and Linux hand opened files to the process as command line
// arguments, and GLFW already delivers drag and drop, so there is nothing to
// hook here.

#include "app/PlatformIntegration.h"

namespace tessera::app::platform {

void installOpenFileHandler() {}

bool takePendingOpenFile(std::string&) { return false; }

}  // namespace tessera::app::platform
