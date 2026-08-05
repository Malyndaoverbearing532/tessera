#include "core/Log.h"

#include <cstdio>
#include <mutex>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace tessera::log {
namespace {

Level g_level = Level::Info;
std::mutex g_mutex;

constexpr const char* kPrefix[] = {"[trace] ", "[info ] ", "[warn ] ", "[error] "};

// ANSI colour is only emitted when the stream is a terminal, so redirected
// output stays clean for scripts.
constexpr const char* kColour[] = {"\033[90m", "\033[0m", "\033[33m", "\033[31m"};

bool useColour() {
    static const bool value = [] {
#if defined(_WIN32)
        return false;
#else
        return isatty(2) != 0;
#endif
    }();
    return value;
}

}  // namespace

void setLevel(Level level) { g_level = level; }

void write(Level level, std::string_view message) {
    if (static_cast<int>(level) < static_cast<int>(g_level)) return;
    const auto index = static_cast<std::size_t>(level);

    std::scoped_lock lock(g_mutex);
    if (useColour()) {
        std::fprintf(stderr, "%s%s%.*s\033[0m\n", kColour[index], kPrefix[index],
                     static_cast<int>(message.size()), message.data());
    } else {
        std::fprintf(stderr, "%s%.*s\n", kPrefix[index], static_cast<int>(message.size()),
                     message.data());
    }
}

}  // namespace tessera::log
