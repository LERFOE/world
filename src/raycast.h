#pragma once

#include <functional>

#include <glm/glm.hpp>

#include "voxel_block.h"

struct RayHit {
    bool hit = false;
    glm::ivec3 block{0};
    glm::ivec3 normal{0};
    glm::vec3 position{0.0f};
    BlockId id = BlockId::Air;
};

RayHit RaycastBlocks(const glm::vec3& origin,
                     const glm::vec3& direction,
                     float maxDistance,
                     const std::function<BlockId(const glm::ivec3&)>& sampler,
                     const BlockRegistry& registry);
