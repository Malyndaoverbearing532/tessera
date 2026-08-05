#pragma once

#include <format>
#include <string>
#include <string_view>

namespace tessera::log {

enum class Level { Trace, Info, Warn, Error };

void setLevel(Level level);
void write(Level level, std::string_view message);

template <class... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace tessera::log
