#pragma once

#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "chunk.h"
#include "raycast.h"

class Shader;
class TextureAtlas;
class BlockRegistry;

class World {
public:
    World(TextureAtlas& atlas, BlockRegistry& registry);
    ~World();
    
    void update(const glm::vec3& cameraPos, float dt);

    void render(const Shader& shader) const;
    void renderTransparent(const Shader& shader) const;
    void renderChunkBounds(const Shader& shader);
    void renderClouds(const Shader& shader, bool enabled) const;
    void renderSun(const Shader& shader) const;

    RayHit raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance) const;

    bool removeBlock(const glm::ivec3& pos);
    bool placeBlock(const glm::ivec3& pos, BlockId id);

    BlockId blockAt(const glm::ivec3& pos) const;

    glm::vec3 sunDirection() const { return sunDir_; }
    glm::vec3 sunColor() const { return sunColor_; }
    glm::vec3 ambientColor() const { return ambientColor_; }
    glm::vec3 skyColor() const { return skyColor_; }
    float fogDensity() const { return fogDensity_; }
    glm::vec2 cloudOffset() const;
    float cloudTime() const;

    int chunkCount() const { return static_cast<int>(chunks_.size()); }
    int renderDistance() const { return renderDistance_; }

private:
    struct CloudLayer;
    struct SunMesh;

    Chunk* findChunk(const ChunkCoord& coord);
    const Chunk* findChunk(const ChunkCoord& coord) const;
    void updateSun(float dt);
    void ensureChunksAround(const glm::vec3& cameraPos);
    void rebuildMeshes(int maxPerFrame = 2);
    void cleanupChunks(const glm::vec3& cameraPos);
    void generateTerrain(Chunk& chunk);
    void markNeighborsDirty(const glm::ivec3& pos);

    bool setBlockInternal(const glm::ivec3& pos, BlockId id);
    glm::ivec3 toLocal(const glm::ivec3& pos, const ChunkCoord& coord) const;
    ChunkCoord worldToChunk(int x, int z) const;
    glm::vec3 biomeColor(const glm::vec3& worldPos) const;
    glm::vec3 sampleTint(const glm::vec3& worldPos, const BlockInfo& info) const;
    float noiseRand(int x, int z, int salt) const;
    float gaussian01(int x, int z, int salt) const;
    void growTree(Chunk& chunk, int localX, int localZ, int worldX, int worldZ, int groundHeight);

    TextureAtlas& atlas_;
    BlockRegistry& registry_;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> chunks_;
    std::deque<ChunkCoord> meshQueue_;
    std::unique_ptr<CloudLayer> clouds_;
    std::unique_ptr<SunMesh> sunMesh_;

    glm::vec3 cameraPos_{0.0f};
    glm::vec3 sunDir_{0.5f, 0.8f, 0.2f};
    glm::vec3 sunColor_{1.0f};
    glm::vec3 ambientColor_{0.2f};
    glm::vec3 skyColor_{0.55f, 0.72f, 0.92f};
    float fogDensity_ = 0.002f;
    float timeOfDay_ = 0.3f; // 0.0 - 1.0
    float daySpeed_ = 0.0033f;
    int renderDistance_ = 8;
    int seed_ = 12345;
    int waterLevel_ = 32;

    GLuint boundsVao_ = 0;
    GLuint boundsVbo_ = 0;
    std::vector<RenderVertex> boundsVertices_;
};
struct vec3
{
    int x,y,z;
};
