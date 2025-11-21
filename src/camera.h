#pragma once

#include <glm/glm.hpp>

class Camera {
public:
    Camera();
    explicit Camera(const glm::vec3& pos);

    void setPerspective(float fovDeg, float aspect, float nearPlane, float farPlane);
    void setAspect(float aspect);

    void move(const glm::vec3& dir, float dt, float speed);
    void processMouse(float deltaX, float deltaY);

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix() const;
    glm::mat4 viewProjectionMatrix() const;

    glm::vec3 forward() const { return front_; }
    glm::vec3 right() const { return right_; }
    glm::vec3 up() const { return up_; }

    const glm::vec3& position() const { return position_; }
    void setPosition(const glm::vec3& pos) { position_ = pos; }

private:
    void updateVectors();

    glm::vec3 position_{0.0f, 0.0f, 3.0f};
    glm::vec3 front_{0.0f, 0.0f, -1.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    glm::vec3 worldUp_{0.0f, 1.0f, 0.0f};

    float yaw_ = -90.0f;
    float pitch_ = 0.0f;
    float fov_ = 60.0f;
    float aspect_ = 16.0f / 9.0f;
    float near_ = 0.1f;
    float far_ = 800.0f;
};
