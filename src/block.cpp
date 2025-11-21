#include "voxel_block.h"

#include <stdexcept>

#include "texture_atlas.h"

BlockRegistry::BlockRegistry() {
    for (auto& block : blocks_) {
        block = BlockInfo{};
    }
}

void BlockRegistry::build(const TextureAtlas& atlas) {
    auto texture = [&](const std::string& name) {
        int index = atlas.tileIndex(name);
        if (index < 0) {
            throw std::runtime_error("缺少纹理:" + name);
        }
        return index;
    };

    auto applyAnimation = [&](BlockInfo& info, const std::string& name) {
        AtlasAnimation anim = atlas.animationInfo(name);
        if (anim.startIndex >= 0 && anim.frameCount > 1) {
            info.animation.start = anim.startIndex;
            info.animation.frames = anim.frameCount;
            info.animation.speed = anim.speed > 0.0f ? anim.speed : 1.0f;
        }
    };

    auto assign = [&](BlockInfo& info, int px, int nx, int py, int ny, int pz, int nz) {
        info.faces = {px, nx, py, ny, pz, nz};
    };

    BlockInfo& air = slot(BlockId::Air);
    air.transparent = true;
    air.selectable = false;

    BlockInfo& grass = slot(BlockId::Grass);
    grass.solid = true;
    grass.selectable = true;
    grass.biomeTint = true;
    assign(grass,
           texture("grass_side"), texture("grass_side"), texture("grass_top"), texture("dirt"),
           texture("grass_side"), texture("grass_side"));
    grass.tint = glm::vec3(0.48f, 0.65f, 0.36f);

    BlockInfo& dirt = slot(BlockId::Dirt);
    dirt.solid = true;
    dirt.selectable = true;
    assign(dirt, texture("dirt"), texture("dirt"), texture("dirt"), texture("dirt"), texture("dirt"), texture("dirt"));

    BlockInfo& stone = slot(BlockId::Stone);
    stone.solid = true;
    stone.selectable = true;
    assign(stone, texture("stone"), texture("stone"), texture("stone"), texture("stone"), texture("stone"), texture("stone"));

    BlockInfo& sand = slot(BlockId::Sand);
    sand.solid = true;
    sand.selectable = true;
    sand.tint = glm::vec3(1.0f, 0.95f, 0.82f);
    assign(sand, texture("sand"), texture("sand"), texture("sand"), texture("sand"), texture("sand"), texture("sand"));

    BlockInfo& gravel = slot(BlockId::Gravel);
    gravel.solid = true;
    gravel.selectable = true;
    assign(gravel, texture("gravel"), texture("gravel"), texture("gravel"), texture("gravel"), texture("gravel"), texture("gravel"));

    BlockInfo& snow = slot(BlockId::Snow);
    snow.solid = true;
    snow.selectable = true;
    assign(snow, texture("snow"), texture("snow"), texture("snow"), texture("snow"), texture("snow"), texture("snow"));

    BlockInfo& water = slot(BlockId::Water);
    water.solid = false;
    water.transparent = true;
    water.selectable = true;
    water.liquid = true;
    water.material = 1.0f;
    water.tint = glm::vec3(0.2f, 0.35f, 0.65f);
    assign(water, texture("water"), texture("water"), texture("water"), texture("water"), texture("water"), texture("water"));
    applyAnimation(water, "water");

    BlockInfo& log = slot(BlockId::OakLog);
    log.solid = true;
    log.selectable = true;
    assign(log, texture("oak_log"), texture("oak_log"), texture("oak_log_top"), texture("oak_log_top"), texture("oak_log"), texture("oak_log"));

    BlockInfo& leaves = slot(BlockId::OakLeaves);
    leaves.solid = true;
    leaves.transparent = false;
    leaves.selectable = true;
    leaves.biomeTint = true;
    leaves.tint = glm::vec3(1.0f);
    assign(leaves, texture("oak_leaves"), texture("oak_leaves"), texture("oak_leaves"), texture("oak_leaves"), texture("oak_leaves"), texture("oak_leaves"));

    BlockInfo& planks = slot(BlockId::OakPlanks);
    planks.solid = true;
    planks.selectable = true;
    assign(planks, texture("oak_planks"), texture("oak_planks"), texture("oak_planks"), texture("oak_planks"), texture("oak_planks"), texture("oak_planks"));

    BlockInfo& glass = slot(BlockId::Glass);
    glass.solid = true;
    glass.transparent = true;
    glass.selectable = true;
    glass.material = 1.1f;
    assign(glass, texture("glass"), texture("glass"), texture("glass"), texture("glass"), texture("glass"), texture("glass"));

    BlockInfo& flower = slot(BlockId::Flower);
    flower.solid = false;
    flower.transparent = true;
    flower.selectable = true;
    flower.billboard = true;
    flower.tint = glm::vec3(1.0f);
    assign(flower, texture("poppy"), texture("poppy"), texture("poppy"), texture("poppy"), texture("poppy"), texture("poppy"));

    BlockInfo& tallGrass = slot(BlockId::TallGrass);
    tallGrass.solid = false;
    tallGrass.transparent = true;
    tallGrass.selectable = true;
    tallGrass.billboard = true;
    tallGrass.biomeTint = true;
    tallGrass.tint = glm::vec3(1.0f);
    assign(tallGrass, texture("tall_grass"), texture("tall_grass"), texture("tall_grass"), texture("tall_grass"), texture("tall_grass"), texture("tall_grass"));
}

bool BlockRegistry::occludes(BlockId id) const {
    const BlockInfo& info = blocks_[static_cast<std::size_t>(id)];
    return info.solid && !info.transparent && !info.billboard;
}
