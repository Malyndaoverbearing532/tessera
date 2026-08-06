#pragma once

#include "core/Math.h"
#include "gfx/opengl/GLCommon.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace tessera::gfx {

/// A linked GL program with a uniform-location cache.
class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    /// `geometry` is optional; pass an empty view to skip that stage.
    bool build(std::string_view vertex, std::string_view fragment, std::string_view geometry,
               std::string_view debugName, std::string& error);

    void bind() const;

    [[nodiscard]] bool valid() const { return program_ != 0; }
    [[nodiscard]] GLuint id() const { return program_; }

    /// Resolves a uniform location once so a hot loop can call glUniform*
    /// directly instead of hashing a name per draw.
    [[nodiscard]] GLint locationOf(std::string_view name) { return location(name); }

    void set(std::string_view name, int value);
    void set(std::string_view name, float value);
    void set(std::string_view name, const vec2& value);
    void set(std::string_view name, const vec3& value);
    void set(std::string_view name, const vec4& value);
    void set(std::string_view name, const mat3& value);
    void set(std::string_view name, const mat4& value);

private:
    GLint location(std::string_view name);
    void destroy();

    /// Transparent hashing so a string_view can be looked up without first
    /// materialising a std::string. Without it every uniform assignment
    /// allocates, which at fifteen uniforms per draw call is tens of thousands
    /// of allocations per frame on a scene with many meshes.
    struct TransparentHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const noexcept {
            return std::hash<std::string_view>{}(text);
        }
    };

    GLuint program_ = 0;
    std::unordered_map<std::string, GLint, TransparentHash, std::equal_to<>> locations_;
    std::string name_;
};

}  // namespace tessera::gfx
