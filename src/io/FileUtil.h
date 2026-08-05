#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tessera::io {

/// Reads a whole file into memory. The native parsers work on this buffer
/// rather than streaming, which is what makes them fast on large meshes.
bool readFile(const std::filesystem::path& path, std::vector<char>& out, std::string& error);

/// Lower-case extension without the leading dot ("" when there is none).
std::string extensionOf(const std::filesystem::path& path);

std::string toLower(std::string_view text);

/// Human readable byte count, e.g. "12.4 MB".
std::string formatBytes(std::uint64_t bytes);

// ---------------------------------------------------------------------------
// Zero-allocation scanning helpers for the ASCII parsers.
// ---------------------------------------------------------------------------

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

inline void skipSpaces(const char*& p, const char* end) {
    while (p < end && isSpace(*p)) ++p;
}

inline void skipLine(const char*& p, const char* end) {
    while (p < end && *p != '\n') ++p;
    if (p < end) ++p;
}

/// Next whitespace-delimited token on the current line (never crosses '\n').
inline std::string_view nextToken(const char*& p, const char* end) {
    skipSpaces(p, end);
    const char* start = p;
    while (p < end && !isSpace(*p) && *p != '\n') ++p;
    return {start, static_cast<std::size_t>(p - start)};
}

/// Parses a float in place. Falls back to strtod for exotic spellings that
/// libc++'s from_chars for floating point may not accept.
bool parseFloat(const char*& p, const char* end, float& value);

/// Parses a signed integer in place.
bool parseInt(const char*& p, const char* end, long& value);

}  // namespace tessera::io
