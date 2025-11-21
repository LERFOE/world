#pragma once

#include <glm/glm.hpp>

struct RenderVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 color;
    float light;
    float material;
    glm::vec3 anim;
};

inline constexpr std::size_t kRenderVertexStride = sizeof(RenderVertex);

inline constexpr int kPositionLocation = 0;
inline constexpr int kNormalLocation = 1;
inline constexpr int kUVLocation = 2;
inline constexpr int kColorLocation = 3;
inline constexpr int kLightLocation = 4;
inline constexpr int kMaterialLocation = 5;
inline constexpr int kAnimLocation = 6;
