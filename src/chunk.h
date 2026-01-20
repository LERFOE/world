#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "voxel_block.h"
#include "mesh.h"
#include "texture_atlas.h"

struct ChunkCoord {
    int x = 0;
    int z = 0;

    bool operator==(const ChunkCoord& other) const noexcept {
        return x == other.x && z == other.z;
    }
};

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const noexcept {
        return (static_cast<std::size_t>(coord.x) * 73856093u) ^ (static_cast<std::size_t>(coord.z) * 19349663u);
    }
};

namespace std {
    template <>
    struct hash<ChunkCoord> {
        std::size_t operator()(const ChunkCoord& coord) const noexcept {
            return ChunkCoordHash{}(coord);
        }
    };
}

class Chunk {
public:
    static constexpr int SIZE = 16;
    static constexpr int HEIGHT = 128;

    explicit Chunk(ChunkCoord coord);
    ~Chunk();

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    Chunk(Chunk&& other) noexcept;
    Chunk& operator=(Chunk&& other) noexcept;

    BlockId block(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockId id);

    glm::ivec3 worldOrigin() const { return glm::ivec3(coord_.x * SIZE, 0, coord_.z * SIZE); }
    ChunkCoord coord() const { return coord_; }

    void buildMesh(const BlockRegistry& registry,
                   const std::function<BlockId(const glm::ivec3&)>& sampler,
                   const std::function<glm::vec3(const glm::vec3&, BlockId, int)>& colorSampler);

    void renderSolid() const;
    void renderAlpha() const;

    bool dirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }

    bool empty() const { return empty_; }

private:
    struct MeshBuffers {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei indexCount = 0;
        bool ready = false;
    };

    void uploadMesh(const std::vector<RenderVertex>& vertices,
                    const std::vector<unsigned int>& indices,
                    MeshBuffers& dst);
    void destroyMesh(MeshBuffers& mesh);

    ChunkCoord coord_{};
    std::vector<BlockId> blocks_;
    bool dirty_ = true;
    bool empty_ = false;

    MeshBuffers solid_{};
    MeshBuffers alpha_{};
};
