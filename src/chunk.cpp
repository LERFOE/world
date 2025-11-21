#include "chunk.h"

#include <array>
#include <numeric>

namespace {
constexpr glm::ivec3 faceOffsets[6] = {
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
};

constexpr glm::vec3 normals[6] = {
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
};

constexpr std::array<glm::vec3, 4> faceVertices[6] = {
    std::array<glm::vec3, 4>{glm::vec3(1, 0, 1), glm::vec3(1, 0, 0), glm::vec3(1, 1, 0), glm::vec3(1, 1, 1)}, // +X (Right)
    std::array<glm::vec3, 4>{glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 1), glm::vec3(0, 1, 0)}, // -X (Left)
    std::array<glm::vec3, 4>{glm::vec3(0, 1, 1), glm::vec3(1, 1, 1), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0)}, // +Y (Top)
    std::array<glm::vec3, 4>{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(1, 0, 1), glm::vec3(0, 0, 1)}, // -Y (Bottom)
    std::array<glm::vec3, 4>{glm::vec3(0, 0, 1), glm::vec3(1, 0, 1), glm::vec3(1, 1, 1), glm::vec3(0, 1, 1)}, // +Z (Front)
    std::array<glm::vec3, 4>{glm::vec3(1, 0, 0), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), glm::vec3(1, 1, 0)}, // -Z (Back)
};

constexpr std::array<glm::vec2, 4> baseUV = {
    glm::vec2(0.0f, 0.0f),
    glm::vec2(1.0f, 0.0f),
    glm::vec2(1.0f, 1.0f),
    glm::vec2(0.0f, 1.0f),
};

constexpr float faceLight[6] = {0.92f, 0.92f, 1.2f, 0.7f, 1.0f, 1.0f};

inline unsigned int vertexIndex(int x, int y, int z) {
    return static_cast<unsigned int>(y * Chunk::SIZE * Chunk::SIZE + z * Chunk::SIZE + x);
}

void addQuad(std::vector<RenderVertex>& vertices,
             std::vector<unsigned int>& indices,
             const glm::vec3& base,
             const std::array<glm::vec3, 4>& verts,
             const glm::vec3& normal,
             const glm::vec2 uv[4],
             const glm::vec3& tint,
             float light,
             float material,
             float emission,
             const glm::vec3& animData) {
    unsigned int startIndex = static_cast<unsigned int>(vertices.size());
    for (int i = 0; i < 4; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        RenderVertex v{};
        v.pos = base + verts[idx];
        v.normal = normal;
        v.uv = uv[idx];
        v.color = tint + glm::vec3(emission);
        v.light = light;
        v.material = material;
        v.anim = animData;
        vertices.push_back(v);
    }
    indices.push_back(startIndex + 0);
    indices.push_back(startIndex + 1);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex + 3);
    indices.push_back(startIndex + 0);
}

void buildBillboard(const glm::vec3& center,
                    const glm::vec3& tint,
                    float material,
                    float emission,
                    std::vector<RenderVertex>& vertices,
                    std::vector<unsigned int>& indices,
                    float tileIndex,
                    const BlockAnimation& blockAnim) {
    const glm::vec2 uvPairs[4] = {
        baseUV[0],
        baseUV[1],
        baseUV[2],
        baseUV[3],
    };

    const glm::vec3 offsets[4] = {
        {-0.5f, 0.0f, 0.0f},
        {0.5f, 0.0f, 0.0f},
        {0.5f, 1.0f, 0.0f},
        {-0.5f, 1.0f, 0.0f},
    };

    const glm::vec3 offsetsB[4] = {
        {0.0f, 0.0f, -0.5f},
        {0.0f, 0.0f, 0.5f},
        {0.0f, 1.0f, 0.5f},
        {0.0f, 1.0f, -0.5f},
    };

    auto emitQuad = [&](const glm::vec3 (&local)[4]) {
        unsigned int start = static_cast<unsigned int>(vertices.size());
        for (int i = 0; i < 4; ++i) {
            RenderVertex v{};
            v.pos = center + local[i];
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.uv = uvPairs[i];
            v.color = tint + glm::vec3(emission);
            v.light = 1.0f;
            v.material = material;
            float frames = blockAnim.frames > 0 ? static_cast<float>(blockAnim.frames) : 1.0f;
            float speed = blockAnim.frames > 1 ? blockAnim.speed : 0.0f;
            v.anim = glm::vec3(tileIndex, frames, speed);
            vertices.push_back(v);
        }
        indices.push_back(start + 0);
        indices.push_back(start + 1);
        indices.push_back(start + 2);
        indices.push_back(start + 2);
        indices.push_back(start + 3);
        indices.push_back(start + 0);
    };

    emitQuad(offsets);
    emitQuad(offsetsB);
};
}

Chunk::Chunk(ChunkCoord coord) : coord_(coord) {
    blocks_.resize(SIZE * HEIGHT * SIZE, BlockId::Air);
}

Chunk::~Chunk() {
    destroyMesh(solid_);
    destroyMesh(alpha_);
}

Chunk::Chunk(Chunk&& other) noexcept {
    *this = std::move(other);
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    coord_ = other.coord_;
    blocks_ = std::move(other.blocks_);
    dirty_ = other.dirty_;
    empty_ = other.empty_;
    solid_ = other.solid_;
    alpha_ = other.alpha_;
    other.solid_ = {};
    other.alpha_ = {};
    other.dirty_ = true;
    return *this;
}

BlockId Chunk::block(int x, int y, int z) const {
    if (x < 0 || x >= SIZE || y < 0 || y >= HEIGHT || z < 0 || z >= SIZE) {
        return BlockId::Air;
    }
    return blocks_[vertexIndex(x, y, z)];
}

void Chunk::setBlock(int x, int y, int z, BlockId id) {
    if (x < 0 || x >= SIZE || y < 0 || y >= HEIGHT || z < 0 || z >= SIZE) {
        return;
    }
    blocks_[vertexIndex(x, y, z)] = id;
    dirty_ = true;
}

void Chunk::buildMesh(const BlockRegistry& registry,
                      const std::function<BlockId(const glm::ivec3&)>& sampler,
                      const std::function<glm::vec3(const glm::vec3&, const BlockInfo&)>& colorSampler) {
    std::vector<RenderVertex> solidVerts;
    std::vector<RenderVertex> alphaVerts;
    std::vector<unsigned int> solidIndices;
    std::vector<unsigned int> alphaIndices;
    solidVerts.reserve(4096);
    alphaVerts.reserve(1024);

    glm::ivec3 chunkOrigin = worldOrigin();

    for (int y = 0; y < HEIGHT; ++y) {
        for (int z = 0; z < SIZE; ++z) {
            for (int x = 0; x < SIZE; ++x) {
                BlockId id = block(x, y, z);
                if (id == BlockId::Air) {
                    continue;
                }
                const BlockInfo& info = registry.info(id);
                glm::vec3 base = glm::vec3(chunkOrigin.x + x, y, chunkOrigin.z + z);
                glm::vec3 tint = colorSampler(base + glm::vec3(0.5f), info);

                if (info.billboard) {
                    std::vector<RenderVertex>& targetVerts = alphaVerts;
                    std::vector<unsigned int>& targetIdx = alphaIndices;
                    float tileIndex = static_cast<float>(info.faces[2]);
                    glm::vec3 billboardTint = colorSampler(base + glm::vec3(0.5f), info);
                    buildBillboard(base + glm::vec3(0.5f, 0.0f, 0.5f),
                                   billboardTint,
                                   info.material,
                                   info.emission,
                                   targetVerts,
                                   targetIdx,
                                   tileIndex,
                                   info.animation);
                    continue;
                }

                for (int face = 0; face < 6; ++face) {
                    glm::ivec3 neighbor = glm::ivec3(chunkOrigin.x + x, y, chunkOrigin.z + z) + faceOffsets[face];
                    BlockId neighborId = sampler(neighbor);
                    if (registry.occludes(neighborId) && !registry.info(id).liquid) {
                        continue;
                    }

                    glm::vec2 uvs[4] = {
                        baseUV[0],
                        baseUV[1],
                        baseUV[2],
                        baseUV[3],
                    };

                    std::vector<RenderVertex>& targetVerts = info.transparent || info.liquid ? alphaVerts : solidVerts;
                    std::vector<unsigned int>& targetIdx = info.transparent || info.liquid ? alphaIndices : solidIndices;

                    float shading = faceLight[face] + info.emission;
                    shading = glm::clamp(shading, 0.5f, 1.4f);
                    float frames = info.animation.frames > 0 ? static_cast<float>(info.animation.frames) : 1.0f;
                    float speed = info.animation.frames > 1 ? info.animation.speed : 0.0f;
                    float startIndex = static_cast<float>(info.faces[static_cast<std::size_t>(face)]);
                    glm::vec3 animData(startIndex, frames, speed);
                    addQuad(targetVerts,
                            targetIdx,
                            base,
                            faceVertices[face],
                            normals[face],
                            uvs,
                            tint,
                            shading,
                            info.material,
                            info.emission,
                            animData);
                }
            }
        }
    }

    empty_ = solidVerts.empty() && alphaVerts.empty();
    uploadMesh(solidVerts, solidIndices, solid_);
    uploadMesh(alphaVerts, alphaIndices, alpha_);
    dirty_ = false;
}

void Chunk::renderSolid() const {
    if (!solid_.ready || solid_.indexCount == 0) {
        return;
    }
    glBindVertexArray(solid_.vao);
    glDrawElements(GL_TRIANGLES, solid_.indexCount, GL_UNSIGNED_INT, nullptr);
}

void Chunk::renderAlpha() const {
    if (!alpha_.ready || alpha_.indexCount == 0) {
        return;
    }
    glBindVertexArray(alpha_.vao);
    glDrawElements(GL_TRIANGLES, alpha_.indexCount, GL_UNSIGNED_INT, nullptr);
}

void Chunk::uploadMesh(const std::vector<RenderVertex>& vertices,
                       const std::vector<unsigned int>& indices,
                       MeshBuffers& dst) {
    if (!dst.vao) {
        glGenVertexArrays(1, &dst.vao);
        glGenBuffers(1, &dst.vbo);
        glGenBuffers(1, &dst.ebo);
    }

    glBindVertexArray(dst.vao);
    glBindBuffer(GL_ARRAY_BUFFER, dst.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(RenderVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dst.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                 indices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(kPositionLocation);
    glVertexAttribPointer(kPositionLocation, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, pos)));
    glEnableVertexAttribArray(kNormalLocation);
    glVertexAttribPointer(kNormalLocation, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, normal)));
    glEnableVertexAttribArray(kUVLocation);
    glVertexAttribPointer(kUVLocation, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, uv)));
    glEnableVertexAttribArray(kColorLocation);
    glVertexAttribPointer(kColorLocation, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, color)));
    glEnableVertexAttribArray(kLightLocation);
    glVertexAttribPointer(kLightLocation, 1, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, light)));
    glEnableVertexAttribArray(kMaterialLocation);
    glVertexAttribPointer(kMaterialLocation, 1, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, material)));
    glEnableVertexAttribArray(kAnimLocation);
    glVertexAttribPointer(kAnimLocation, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, anim)));

    dst.indexCount = static_cast<GLsizei>(indices.size());
    dst.ready = true;
}

void Chunk::destroyMesh(MeshBuffers& mesh) {
    if (mesh.vao) {
        glDeleteVertexArrays(1, &mesh.vao);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteBuffers(1, &mesh.ebo);
        mesh = {};
    }
}
