#include "gfx/opengl/Shader.h"

#include "core/Log.h"

#include <format>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace tessera::gfx {
namespace {

bool compileStage(GLenum type, std::string_view source, std::string_view debugName,
                  GLuint& outShader, std::string& error) {
    outShader = glCreateShader(type);
    const auto* data = source.data();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(outShader, 1, &data, &length);
    glCompileShader(outShader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(outShader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return true;

    GLint logLength = 0;
    glGetShaderiv(outShader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(static_cast<std::size_t>(std::max(logLength, 1)));
    glGetShaderInfoLog(outShader, logLength, nullptr, log.data());

    const char* stageName = type == GL_VERTEX_SHADER     ? "vertex"
                            : type == GL_FRAGMENT_SHADER ? "fragment"
                                                         : "geometry";
    error = std::format("{} ({} stage): {}", debugName, stageName, log.data());
    glDeleteShader(outShader);
    outShader = 0;
    return false;
}

}  // namespace

void checkGlErrors(std::string_view where) {
    for (GLenum code = glGetError(); code != GL_NO_ERROR; code = glGetError()) {
        const char* text = "unknown";
        switch (code) {
            case GL_INVALID_ENUM: text = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE: text = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: text = "GL_INVALID_OPERATION"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: text = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
            case GL_OUT_OF_MEMORY: text = "GL_OUT_OF_MEMORY"; break;
            default: break;
        }
        log::error("GL error {} at {}", text, where);
    }
}

Shader::~Shader() { destroy(); }

Shader::Shader(Shader&& other) noexcept
    : program_(other.program_), locations_(std::move(other.locations_)), name_(std::move(other.name_)) {
    other.program_ = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        destroy();
        program_ = other.program_;
        locations_ = std::move(other.locations_);
        name_ = std::move(other.name_);
        other.program_ = 0;
    }
    return *this;
}

void Shader::destroy() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    locations_.clear();
}

bool Shader::build(std::string_view vertex, std::string_view fragment, std::string_view geometry,
                   std::string_view debugName, std::string& error) {
    destroy();
    name_ = std::string(debugName);

    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    GLuint geometryShader = 0;

    if (!compileStage(GL_VERTEX_SHADER, vertex, debugName, vertexShader, error)) return false;
    if (!compileStage(GL_FRAGMENT_SHADER, fragment, debugName, fragmentShader, error)) {
        glDeleteShader(vertexShader);
        return false;
    }
    if (!geometry.empty() &&
        !compileStage(GL_GEOMETRY_SHADER, geometry, debugName, geometryShader, error)) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, fragmentShader);
    if (geometryShader != 0) glAttachShader(program_, geometryShader);
    glLinkProgram(program_);

    // The shader objects are reference-counted by the program; drop ours now.
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (geometryShader != 0) glDeleteShader(geometryShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(static_cast<std::size_t>(std::max(logLength, 1)));
        glGetProgramInfoLog(program_, logLength, nullptr, log.data());
        error = std::format("{} (link): {}", debugName, log.data());
        destroy();
        return false;
    }
    return true;
}

void Shader::bind() const { glUseProgram(program_); }

GLint Shader::location(std::string_view name) {
    // The lookup is the hot path and must not allocate; only a first-time miss
    // pays for the string.
    if (const auto it = locations_.find(name); it != locations_.end()) return it->second;

    std::string key(name);
    const GLint value = glGetUniformLocation(program_, key.c_str());
    locations_.emplace(std::move(key), value);
    return value;
}

void Shader::set(std::string_view name, int value) { glUniform1i(location(name), value); }
void Shader::set(std::string_view name, float value) { glUniform1f(location(name), value); }
void Shader::set(std::string_view name, const vec2& value) {
    glUniform2fv(location(name), 1, glm::value_ptr(value));
}
void Shader::set(std::string_view name, const vec3& value) {
    glUniform3fv(location(name), 1, glm::value_ptr(value));
}
void Shader::set(std::string_view name, const vec4& value) {
    glUniform4fv(location(name), 1, glm::value_ptr(value));
}
void Shader::set(std::string_view name, const mat3& value) {
    glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}
void Shader::set(std::string_view name, const mat4& value) {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

}  // namespace tessera::gfx
