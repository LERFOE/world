#pragma once

#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

class Shader {
public:
    Shader() = default;
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    bool load(const std::string& vertexPath, const std::string& fragmentPath);
    void use() const;

    void setMat4(const std::string& name, const glm::mat4& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setVec2(const std::string& name, const glm::vec2& value) const;
    void setFloat(const std::string& name, float value) const;
    void setInt(const std::string& name, int value) const;

    unsigned int id() const { return programId_; }

private:
    unsigned int programId_ = 0;

    unsigned int compile(unsigned int type, const std::string& source);
    bool link(unsigned int vertex, unsigned int fragment);
    static std::string readFile(const std::string& path);
};
