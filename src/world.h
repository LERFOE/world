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
    struct AnimalUVLayout {
        struct Quad {
            glm::vec2 bl{0.0f};
            glm::vec2 br{0.0f};
            glm::vec2 tr{0.0f};
            glm::vec2 tl{0.0f};
        };

        struct Box {
            Quad front;
            Quad back;
            Quad left;
            Quad right;
            Quad top;
            Quad bottom;
        };

        // 以原版 64x32（四足动物）布局为基准：head/body/leg 均为独立 box 的 UV 展开。
        Box head;
        Box body;
        Box leg;
    };

    World(TextureAtlas& atlas,
          BlockRegistry& registry,
          const AnimalUVLayout& pigUV,
          const AnimalUVLayout& cowUV,
          const AnimalUVLayout& sheepUV,
          int seed);
    ~World();

    int getSeed() const { return seed_; }
    
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
    float daySpeed() const { return daySpeed_; }
    void setDaySpeed(float speed) { daySpeed_ = speed; }
    glm::vec2 cloudOffset() const;
    float cloudTime() const;

    int chunkCount() const { return static_cast<int>(chunks_.size()); }
    int renderDistance() const { return renderDistance_; }

    void setAoStrength(float v) { aoStrength_ = v; }
    float aoStrength() const { return aoStrength_; }
    
    void setShadowStrength(float v) { shadowStrengthVal_ = v; }
    float shadowStrength() const { return shadowStrengthVal_; }

    void setFogDensity(float v) { fogDensity_ = v; }

private:
    struct CloudLayer;
    struct SunMesh;
    struct AnimalMesh;

    enum class AnimalType {
        Pig = 0,
        Cow = 1,
        Sheep = 2
    };

    struct Animal {
        AnimalType type = AnimalType::Pig;
        glm::vec3 position{0.0f};
        float yaw = 0.0f;          // 朝向（绕 Y 轴旋转，弧度）
        float speed = 1.2f;        // 漫游移动速度
        float wanderTimer = 0.0f;  // 还要直行多久后重新选方向
        float walkPhase = 0.0f;    // 行走动画相位（随移动距离推进）
        int seedX = 0;             // 用于伪随机的种子（出生点）
        int seedZ = 0;
        int wanderStep = 0;        // 随每次换向递增，保证噪声采样不同
    };

    Chunk* findChunk(const ChunkCoord& coord);
    const Chunk* findChunk(const ChunkCoord& coord) const;
    void updateSun(float dt);
    void ensureChunksAround(const glm::vec3& cameraPos);
    void rebuildMeshes(int maxPerFrame = 2);
    void cleanupChunks(const glm::vec3& cameraPos);
    void generateTerrain(Chunk& chunk);
    void spawnAnimalsForChunk(const Chunk& chunk);
    void updateAnimals(float dt);
    void renderAnimals(const Shader& shader) const;
    void markNeighborsDirty(const glm::ivec3& pos);

    bool setBlockInternal(const glm::ivec3& pos, BlockId id);
    glm::ivec3 toLocal(const glm::ivec3& pos, const ChunkCoord& coord) const;
    ChunkCoord worldToChunk(int x, int z) const;
    glm::vec3 biomeColor(const glm::vec3& worldPos) const;
    glm::vec3 sampleTint(const glm::vec3& worldPos, BlockId id, int face) const;
    float noiseRand(int x, int z, int salt) const;
    float gaussian01(int x, int z, int salt) const;
    void growTree(Chunk& chunk, int localX, int localZ, int worldX, int worldZ, int groundHeight);

    TextureAtlas& atlas_;
    BlockRegistry& registry_;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> chunks_;
    std::deque<ChunkCoord> meshQueue_;
    std::unique_ptr<CloudLayer> clouds_;
    std::unique_ptr<SunMesh> sunMesh_;
    std::unique_ptr<AnimalMesh> pigMesh_;
    std::unique_ptr<AnimalMesh> cowMesh_;
    std::unique_ptr<AnimalMesh> sheepMesh_;

    glm::vec3 cameraPos_{0.0f};
    glm::vec3 sunDir_{0.5f, 0.8f, 0.2f};
    glm::vec3 sunColor_{1.0f};
    glm::vec3 ambientColor_{0.2f};
    glm::vec3 skyColor_{0.55f, 0.72f, 0.92f};
    float fogDensity_ = 0.002f;
    float timeOfDay_ = 0.3f; // 0.0 - 1.0
    float daySpeed_ = 0.0033f;
    
    float aoStrength_ = 1.0f;
    float shadowStrengthVal_ = 0.3f;
    
    int renderDistance_ = 8;
    int seed_ = 12345;
    int waterLevel_ = 32;

    GLuint boundsVao_ = 0;
    GLuint boundsVbo_ = 0;
    std::vector<RenderVertex> boundsVertices_;
    std::vector<Animal> animals_;
};
