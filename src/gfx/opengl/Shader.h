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

    GLuint program_ = 0;
    std::unordered_map<std::string, GLint> locations_;
    std::string name_;
};

}  // namespace tessera::gfx
