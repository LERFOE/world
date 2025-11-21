#include "raycast.h"

#include <cmath>

#include <glm/gtc/epsilon.hpp>

namespace {
inline float safeInverse(float value) {
    const float epsilon = 1e-6f;
    if (std::abs(value) < epsilon) {
        return 1e6f;
    }
    return 1.0f / value;
}
}

RayHit RaycastBlocks(const glm::vec3& origin,
                     const glm::vec3& direction,
                     float maxDistance,
                     const std::function<BlockId(const glm::ivec3&)>& sampler,
                     const BlockRegistry& registry) {
    RayHit hit;
    glm::vec3 dir = glm::normalize(direction);
    glm::vec3 pos = origin;
    glm::ivec3 block = glm::ivec3(glm::floor(pos));
    glm::ivec3 lastNormal(0);

    glm::vec3 deltaDist = glm::abs(glm::vec3(safeInverse(dir.x), safeInverse(dir.y), safeInverse(dir.z)));
    glm::ivec3 step = glm::ivec3(dir.x >= 0.0f ? 1 : -1,
                                 dir.y >= 0.0f ? 1 : -1,
                                 dir.z >= 0.0f ? 1 : -1);

    glm::vec3 sideDist;
    sideDist.x = (dir.x >= 0.0f) ? (block.x + 1.0f - pos.x) * deltaDist.x : (pos.x - block.x) * deltaDist.x;
    sideDist.y = (dir.y >= 0.0f) ? (block.y + 1.0f - pos.y) * deltaDist.y : (pos.y - block.y) * deltaDist.y;
    sideDist.z = (dir.z >= 0.0f) ? (block.z + 1.0f - pos.z) * deltaDist.z : (pos.z - block.z) * deltaDist.z;

    float traveled = 0.0f;
    constexpr int maxSteps = 512;

    for (int i = 0; i < maxSteps && traveled <= maxDistance; ++i) {
        BlockId current = sampler(block);
        if (current != BlockId::Air && registry.info(current).selectable) {
            hit.hit = true;
            hit.block = block;
            hit.position = origin + dir * traveled;
            hit.normal = lastNormal;
            hit.id = current;
            return hit;
        }

        if (sideDist.x < sideDist.y) {
            if (sideDist.x < sideDist.z) {
                block.x += step.x;
                traveled = sideDist.x;
                sideDist.x += deltaDist.x;
                lastNormal = glm::ivec3(-step.x, 0, 0);
            } else {
                block.z += step.z;
                traveled = sideDist.z;
                sideDist.z += deltaDist.z;
                lastNormal = glm::ivec3(0, 0, -step.z);
            }
        } else {
            if (sideDist.y < sideDist.z) {
                block.y += step.y;
                traveled = sideDist.y;
                sideDist.y += deltaDist.y;
                lastNormal = glm::ivec3(0, -step.y, 0);
            } else {
                block.z += step.z;
                traveled = sideDist.z;
                sideDist.z += deltaDist.z;
                lastNormal = glm::ivec3(0, 0, -step.z);
            }
        }
    }

    hit.hit = false;
    hit.normal = glm::ivec3(0);
    return hit;
}
