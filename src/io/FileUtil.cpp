#include "io/FileUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace tessera::io {

bool readFile(const std::filesystem::path& path, std::vector<char>& out, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = std::format("cannot stat '{}': {}", path.string(), ec.message());
        return false;
    }

    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (!file) {
        error = std::format("cannot open '{}'", path.string());
        return false;
    }

    out.resize(static_cast<std::size_t>(size));
    const std::size_t read = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);

    if (read != out.size()) {
        error = std::format("short read on '{}' ({} of {} bytes)", path.string(), read, out.size());
        return false;
    }
    return true;
}

std::string extensionOf(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    return toLower(ext);
}

std::string toLower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string formatBytes(std::uint64_t bytes) {
    constexpr const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0 ? std::format("{} B", bytes) : std::format("{:.1f} {}", value, kUnits[unit]);
}

namespace {

/// Falls back to strtod on a bounded copy, so we never run off the end of the
/// buffer. Only used for spellings the fast path deliberately does not handle.
bool parseFloatSlow(const char*& p, const char* end, float& value) {
    char scratch[64];
    const std::size_t n =
        std::min<std::size_t>(sizeof(scratch) - 1, static_cast<std::size_t>(end - p));
    std::copy_n(p, n, scratch);
    scratch[n] = '\0';

    char* stop = nullptr;
    const double parsed = std::strtod(scratch, &stop);
    if (stop == scratch) return false;
    p += (stop - scratch);
    value = static_cast<float>(parsed);
    return true;
}

}  // namespace

bool parseFloat(const char*& p, const char* end, float& value) {
    skipSpaces(p, end);
    if (p >= end || *p == '\n') return false;

    // Deliberately not std::from_chars: libc++ only exposes the floating-point
    // overload from macOS 26 onwards, and it lives in the dylib rather than the
    // headers. This hand-rolled scanner is portable, locale-independent (strtod
    // is not) and measurably faster on the multi-million-line ASCII meshes the
    // OBJ, STL and PLY readers exist to chew through.
    const char* start = p;

    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    } else if (*p == '+') {
        ++p;
    }

    // Accumulate every digit into one integer and track the decimal point, so
    // there is a single rounding step at the end instead of one per digit.
    std::uint64_t mantissa = 0;
    int exponent = 0;
    int digits = 0;
    bool sawDigit = false;

    for (; p < end && *p >= '0' && *p <= '9'; ++p) {
        if (digits < 19) {
            mantissa = mantissa * 10 + static_cast<std::uint64_t>(*p - '0');
            ++digits;
        } else {
            ++exponent;  // beyond the precision of the accumulator
        }
        sawDigit = true;
    }

    if (p < end && *p == '.') {
        ++p;
        for (; p < end && *p >= '0' && *p <= '9'; ++p) {
            if (digits < 19) {
                mantissa = mantissa * 10 + static_cast<std::uint64_t>(*p - '0');
                ++digits;
                --exponent;
            }
            sawDigit = true;
        }
    }

    if (!sawDigit) {
        // Could be "nan" or "inf"; let strtod decide.
        p = start;
        return parseFloatSlow(p, end, value);
    }

    if (p < end && (*p == 'e' || *p == 'E')) {
        const char* afterMantissa = p;
        ++p;
        bool negativeExponent = false;
        if (p < end && (*p == '-' || *p == '+')) {
            negativeExponent = (*p == '-');
            ++p;
        }
        if (p < end && *p >= '0' && *p <= '9') {
            int written = 0;
            for (; p < end && *p >= '0' && *p <= '9'; ++p) {
                if (written < 5) {  // clamp; anything larger overflows the range anyway
                    written = written * 10 + (*p - '0');
                }
            }
            exponent += negativeExponent ? -written : written;
        } else {
            p = afterMantissa;  // a stray 'e' that is not an exponent
        }
    }

    double result = static_cast<double>(mantissa);
    if (exponent != 0) {
        // Exact for |exponent| <= 22, where the power of ten is representable;
        // outside that range hand the whole token to strtod for correct rounding.
        if (exponent >= -22 && exponent <= 22) {
            static constexpr double kPowersOfTen[] = {
                1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
            result = exponent > 0 ? result * kPowersOfTen[exponent]
                                  : result / kPowersOfTen[-exponent];
        } else {
            p = start;
            return parseFloatSlow(p, end, value);
        }
    }

    value = static_cast<float>(negative ? -result : result);
    return true;
}

bool parseInt(const char*& p, const char* end, long& value) {
    skipSpaces(p, end);
    if (p >= end || *p == '\n') return false;

    const bool negative = (*p == '-');
    if (negative || *p == '+') ++p;

    if (p >= end || *p < '0' || *p > '9') return false;
    long result = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        result = result * 10 + (*p - '0');
        ++p;
    }
    value = negative ? -result : result;
    return true;
}

}  // namespace tessera::io
