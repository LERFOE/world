#include "shader.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include <glad/glad.h>

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
    load(vertexPath, fragmentPath);
}

Shader::~Shader() {
    if (programId_ != 0) {
        glDeleteProgram(programId_);
    }
}

bool Shader::load(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vertexSource = readFile(vertexPath);
    std::string fragmentSource = readFile(fragmentPath);
    if (vertexSource.empty() || fragmentSource.empty()) {
        std::cerr << "[Shader] Failed to read source files." << std::endl;
        return false;
    }

    unsigned int vertex = compile(GL_VERTEX_SHADER, vertexSource);
    unsigned int fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);

    if (!vertex || !fragment) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return false;
    }

    bool linked = link(vertex, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return linked;
}

void Shader::use() const {
    glUseProgram(programId_);
}

void Shader::setMat4(const std::string& name, const glm::mat4& value) const {
    int loc = glGetUniformLocation(programId_, name.c_str());
    glUniformMatrix4fv(loc, 1, GL_FALSE, &value[0][0]);
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
    int loc = glGetUniformLocation(programId_, name.c_str());
    glUniform3fv(loc, 1, &value[0]);
}

void Shader::setVec2(const std::string& name, const glm::vec2& value) const {
    int loc = glGetUniformLocation(programId_, name.c_str());
    glUniform2fv(loc, 1, &value[0]);
}

void Shader::setFloat(const std::string& name, float value) const {
    int loc = glGetUniformLocation(programId_, name.c_str());
    glUniform1f(loc, value);
}

void Shader::setInt(const std::string& name, int value) const {
    int loc = glGetUniformLocation(programId_, name.c_str());
    glUniform1i(loc, value);
}

unsigned int Shader::compile(unsigned int type, const std::string& source) {
    unsigned int shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[Shader] Compile error: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::link(unsigned int vertex, unsigned int fragment) {
    unsigned int program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[Shader] Link error: " << log << std::endl;
        glDeleteProgram(program);
        return false;
    }

    if (programId_ != 0) {
        glDeleteProgram(programId_);
    }
    programId_ = program;
    return true;
}

std::string Shader::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Shader] Cannot open file: " << path << std::endl;
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
