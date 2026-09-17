#include "shader.h"

#include <glad/gl.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

bool readFile(const std::string& path, std::string& outContents) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "Failed to open shader file: %s\n", path.c_str());
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    outContents = ss.str();
    return true;
}

unsigned int compile(GLenum type, const std::string& source, const std::string& debugName) {
    unsigned int shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(logLen > 0 ? logLen : 1);
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::fprintf(stderr, "Shader compile error (%s):\n%s\n", debugName.c_str(), log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}  // namespace

unsigned int loadShaderProgram(const std::string& vertPath, const std::string& fragPath) {
    std::string vertSrc, fragSrc;
    if (!readFile(vertPath, vertSrc) || !readFile(fragPath, fragSrc)) {
        return 0;
    }

    unsigned int vert = compile(GL_VERTEX_SHADER, vertSrc, vertPath);
    unsigned int frag = compile(GL_FRAGMENT_SHADER, fragSrc, fragPath);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return 0;
    }

    unsigned int program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(logLen > 0 ? logLen : 1);
        glGetProgramInfoLog(program, logLen, nullptr, log.data());
        std::fprintf(stderr, "Shader link error (%s + %s):\n%s\n", vertPath.c_str(),
                     fragPath.c_str(), log.data());
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}
