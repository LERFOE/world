#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

enum class BlockId : uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Sand,
    Gravel,
    Snow,
    Water,
    OakLog,
    OakLeaves,
    OakPlanks,
    Glass,
    Flower,     // Poppy (Red)
    Dandelion,  // Yellow
    TallGrass,
    DeadBush,
    BlueOrchid,
    Allium,
    AzureBluet,
    RedTulip,
    OrangeTulip,
    WhiteTulip,
    PinkTulip,
    OxeyeDaisy,
    Cornflower,
    LilyOfTheValley,
    Cactus,
    Count
};

struct BlockAnimation {
    int start = -1;
    int frames = 1;
    float speed = 0.0f;

    bool animated() const { return frames > 1 && start >= 0; }
};

struct BlockInfo {
    bool solid = false;
    bool transparent = false;
    bool selectable = false;
    bool liquid = false;
    bool billboard = false;
    bool biomeTint = false;
    std::array<int, 6> faces{};
    glm::vec3 tint{1.0f};
    float emission = 0.0f;
    float material = 0.0f;
    BlockAnimation animation;
};

class TextureAtlas;

class BlockRegistry {
public:
    BlockRegistry();

    void build(const TextureAtlas& atlas);
    const BlockInfo& info(BlockId id) const { return blocks_[static_cast<std::size_t>(id)]; }
    bool occludes(BlockId id) const;

private:
    BlockInfo& slot(BlockId id) { return blocks_[static_cast<std::size_t>(id)]; }
    std::array<BlockInfo, static_cast<int>(BlockId::Count)> blocks_;
};
