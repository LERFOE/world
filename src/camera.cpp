#include "camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

Camera::Camera() {
    updateVectors();
}

Camera::Camera(const glm::vec3& pos) : position_(pos) {
    updateVectors();
}

void Camera::setPerspective(float fovDeg, float aspect, float nearPlane, float farPlane) {
    fov_ = fovDeg;
    aspect_ = aspect;
    near_ = nearPlane;
    far_ = farPlane;
}

void Camera::setAspect(float aspect) {
    aspect_ = aspect;
}

void Camera::move(const glm::vec3& dir, float dt, float speed) {
    position_ += dir * speed * dt;
}

void Camera::processMouse(float deltaX, float deltaY) {
    constexpr float sensitivity = 0.08f;
    yaw_ += deltaX * sensitivity;
    pitch_ -= deltaY * sensitivity;
    const float limit = 89.0f;
    if (pitch_ > limit) pitch_ = limit;
    if (pitch_ < -limit) pitch_ = -limit;
    updateVectors();
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position_, position_ + front_, up_);
}

glm::mat4 Camera::projectionMatrix() const {
    return glm::perspective(glm::radians(fov_), aspect_, near_, far_);
}

glm::mat4 Camera::viewProjectionMatrix() const {
    return projectionMatrix() * viewMatrix();
}

void Camera::updateVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front.y = sin(glm::radians(pitch_));
    front.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front_ = glm::normalize(front);
    right_ = glm::normalize(glm::cross(front_, worldUp_));
    up_ = glm::normalize(glm::cross(right_, front_));
}
