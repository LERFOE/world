#include "world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtc/noise.hpp>
#include <glm/gtx/norm.hpp>

#include <glad/glad.h>

#include "voxel_block.h"
#include "shader.h"
#include "texture_atlas.h"

/*
  说明：
  下面对文件中出现的类型、成员变量和局部变量添加了注释，解释它们在程序中的含义和用途。
  注释尽量放在定义或首次使用处，便于阅读和维护。
*/

namespace {
// floorDiv: 把任意整数坐标转换为以 Chunk::SIZE 为基数的整除（向下取整）除法，
// 能正确处理负数坐标（世界坐标向负方向时也按格子切分）。
inline int floorDiv(int value, int divisor) {
    int div = value / divisor;
    int rem = value % divisor;
    if ((rem != 0) && ((rem < 0) != (divisor < 0))) {
        --div;
    }
    return div;
}

// fbm: 分形布朗运动（fractal brownian motion），用于生成地形高度噪声。
// 参数：uv（采样坐标），octaves（迭代层数），lacunarity（频率倍增），gain（振幅衰减）。
float fbm(glm::vec2 uv, int octaves, float lacunarity, float gain) {
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float sum = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * glm::perlin(uv * frequency);
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return sum;
}

} // namespace

// CloudLayer: 负责生成、更新和绘制天空云层的简单网格与偏移动画。
// 成员说明见结构体内部注释。
struct World::CloudLayer {
    CloudLayer();
    ~CloudLayer();
    void update(float dt); // 每帧更新偏移与时间
    void draw() const;     // 绘制云层四边形

    // offset: 当前云层在 uv 空间上的偏移（用于让云动起来）
    glm::vec2 offset{0.0f};
    // wind: 云层的速度向量（u, v），可以调整云流的方向与速率
    glm::vec2 wind{0.008f, 0.003f};
    // time: 云层内部的时间累计，用于 shader 或程序内的时间驱动
    float time = 0.0f;
    // vao/vbo/ebo: OpenGL 缓存句柄，用于存储云层顶点/索引数据
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
};

// CloudLayer 构造中会填充一个覆盖大范围平面的四边形。
// 顶点包含 pos/normal/uv/color/light/material/anim（RenderVertex）。
World::CloudLayer::CloudLayer() {
    const float half = 1024.0f;     // 云层在 X/Z 方向上的半径（世界单位）
    const float height = 90.0f;     // 云层高度（世界单位，Y）
    RenderVertex verts[4];
    unsigned int indices[6] = {0, 1, 2, 2, 3, 0};
    glm::vec3 positions[4] = {
        {-half, height, -half},
        {half, height, -half},
        {half, height, half},
        {-half, height, half},
    };
    for (int i = 0; i < 4; ++i) {
        verts[i].pos = positions[i];
        // normal 指示朝下，以便 shader 能区分材质分支（material==2 为云）
        verts[i].normal = glm::vec3(0.0f, -1.0f, 0.0f);
        // uv 使用世界坐标缩放，确保云纹理在大片区域内平铺
        verts[i].uv = glm::vec2(positions[i].x, positions[i].z) * 0.0025f;
        verts[i].color = glm::vec3(1.0f); // 顶点漫反射色（通常为白）
        verts[i].light = 1.0f;            // 灯光权重（0-1），这里云层尽量受光
        verts[i].material = 2.0f;         // material==2 在 shader 中走云分支
        verts[i].anim = glm::vec3(0.0f);  // anim 打包字段，云层不使用动画帧索引
    }
    // OpenGL VAO/VBO/EBO 设置略（用于 draw）
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // 顶点属性布局：位置/法线/uv/颜色/光照/材质/anim
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
}

World::CloudLayer::~CloudLayer() {
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
    }
}

void World::CloudLayer::update(float dt) {
    // dt: 每帧时间（秒）。time 和 offset 用于在 shader/CPU 上更新云的动画状态。
    time += dt;
    offset += wind * dt;
}

void World::CloudLayer::draw() const {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
}

// SunMesh: 用来绘制天空中太阳（或镜面光源的 billboard quad），仅包含一个小四边形。
struct World::SunMesh {
    SunMesh() {
        RenderVertex verts[4];
        unsigned int indices[6] = {0, 1, 2, 2, 3, 0};
        // quad 大小：16x16 单位，中心在 0,0 平面
        glm::vec3 positions[4] = {
            {-15.0f, -15.0f, 0.0f},
            {15.0f, -15.0f, 0.0f},
            {15.0f, 15.0f, 0.0f},
            {-15.0f, 15.0f, 0.0f},
        };
        for (int i = 0; i < 4; ++i) {
            verts[i].pos = positions[i];
            // 法线指向屏幕外（Z正），用于某些 shader 分支判断
            verts[i].normal = glm::vec3(0.0f, 0.0f, 1.0f);
            // 将 -15..15 映射到 0..1 的 uv，用于太阳简单采样
            verts[i].uv = glm::vec2(positions[i].x, positions[i].y) * 0.033f + 0.5f;
            // 颜色偏暖，太阳色
            verts[i].color = glm::vec3(1.0f, 0.95f, 0.8f);
            verts[i].light = 1.0f;    // 太阳作为光照来源
            verts[i].material = 4.0f; // material==4 专用于太阳/灯光渲染（shader 需识别）
        }
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

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
    }

    ~SunMesh() {
        if (vao) {
            glDeleteVertexArrays(1, &vao);
            glDeleteBuffers(1, &vbo);
            glDeleteBuffers(1, &ebo);
        }
    }

    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }

    // VAO/VBO/EBO: OpenGL 缓存句柄
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
};

// World 构造函数：传入 atlas（纹理图集）和 registry（方块信息表）
// 成员变量（class World）说明（在 world.h 中实际声明，这里给出含义）：
/*
  atlas_        : 引用到全局纹理图集，用于查 UV/动画帧索引等。
  registry_     : 引用到方块注册表，包含每种 BlockId 的元信息（是否透明、贴图索引等）。
  clouds_       : CloudLayer 的唯一指针，管理云层。
  sunMesh_      : SunMesh 的唯一指针，绘制太阳 billboard。
  boundsVao_/Vbo_: 用于调试时绘制 chunk 边界线的 OpenGL 缓冲。
  chunks_       : 存放当前加载的 chunk 的 unordered_map，键为 ChunkCoord，值为 unique_ptr<Chunk>。
  meshQueue_    : 需要重建 mesh 的 chunk 坐标队列（按帧处理一定数量）。
  cameraPos_    : 当前相机在世界坐标系的位置（x,y,z）。
  renderDistance_: 渲染半径（以 chunk 为单位）。
  sunDir_       : 太阳方向（单位向量）。
  sunColor_/ambientColor_/skyColor_: 光照与天空颜色基调，由 updateSun 计算。
  timeOfDay_/daySpeed_: 用于控制太阳位置随时间变化的参数（timeOfDay_ 范围 0..1）。
  fogDensity_   : 雾浓度，用于雾化计算。
*/

World::World(TextureAtlas& atlas, BlockRegistry& registry)
    : atlas_(atlas), registry_(registry), clouds_(std::make_unique<CloudLayer>()), sunMesh_(std::make_unique<SunMesh>()) {
    (void)atlas_;
    // 创建用于绘制 chunk 边界线的 VAO/VBO 并设置顶点布局（位置/法线/uv/color/light/material/anim）
    glGenVertexArrays(1, &boundsVao_);
    glGenBuffers(1, &boundsVbo_);
    glBindVertexArray(boundsVao_);
    glBindBuffer(GL_ARRAY_BUFFER, boundsVbo_);
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
    glBindVertexArray(0);
}

World::~World() {
    if (boundsVao_) {
        glDeleteVertexArrays(1, &boundsVao_);
        glDeleteBuffers(1, &boundsVbo_);
    }
}

void World::update(const glm::vec3& cameraPos, float dt) {
    // cameraPos: 摄像机世界坐标（用于决定哪些 chunk 需要加载/卸载）
    cameraPos_ = cameraPos;
    // 更新太阳相关（太阳方向、颜色、环境光等）
    updateSun(dt);
    // 确保相机周围一定范围内的 chunk 被生成/存在
    ensureChunksAround(cameraPos_);
    // 重建需要更新的 chunk 网格（每帧限制数量）
    rebuildMeshes();
    // 清理远处不需要的 chunk
    cleanupChunks(cameraPos_);
    // 更新云层动画
    if (clouds_) {
        clouds_->update(dt);
    }
}

void World::render(const Shader&) const {
    // 渲染所有非透明（solid）的 chunk
    for (const auto& [coord, chunk] : chunks_) {
        if (!chunk || chunk->empty()) {
            continue;
        }
        chunk->renderSolid();
    }
}

void World::renderTransparent(const Shader&) const {
    // 透明物体需按距离逆序渲染：先计算每个 chunk 到相机在 XZ 平面的平方距离
    std::vector<std::pair<float, Chunk*>> transparent;
    transparent.reserve(chunks_.size());
    for (const auto& [coord, chunk] : chunks_) {
        if (!chunk) continue;
        // 使用 squared distance 避免开方开销
        transparent.emplace_back(glm::length2(glm::vec2(cameraPos_.x - coord.x * Chunk::SIZE,
                                                        cameraPos_.z - coord.z * Chunk::SIZE)),
                                 chunk.get());
    }
    // 按距离从远到近排序
    std::sort(transparent.begin(), transparent.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    for (const auto& pair : transparent) {
        pair.second->renderAlpha();
    }
}

void World::renderChunkBounds(const Shader&) {
    if (!boundsVao_) {
        return;
    }
    // boundsVertices_：用于存储所有 chunk 边界线的顶点集合（临时，每帧重建）
    boundsVertices_.clear();
    auto pushLine = [&](const glm::vec3& a, const glm::vec3& b) {
        RenderVertex va{};
        // va.pos/ vb.pos: 两端点位置
        va.pos = a;
        // 法线这里无实际意义，仅填充避免未定义行为
        va.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        // uv 不被使用
        va.uv = glm::vec2(0.0f);
        // color: 调试线的颜色
        va.color = glm::vec3(1.0f, 0.4f, 0.1f);
        va.light = 1.0f;
        // material==3 表示调试线框（shader 需识别）
        va.material = 3.0f;
        va.anim = glm::vec3(0.0f);
        RenderVertex vb = va;
        vb.pos = b;
        boundsVertices_.push_back(va);
        boundsVertices_.push_back(vb);
    };

    // 遍历所有 chunk，构建对应的 12 条边的线段
    for (const auto& [coord, chunk] : chunks_) {
        if (!chunk) continue;
        glm::vec3 min = glm::vec3(coord.x * Chunk::SIZE, 0.0f, coord.z * Chunk::SIZE);
        glm::vec3 max = min + glm::vec3(Chunk::SIZE, Chunk::HEIGHT, Chunk::SIZE);
        glm::vec3 corners[8] = {
            {min.x, min.y, min.z},
            {max.x, min.y, min.z},
            {max.x, min.y, max.z},
            {min.x, min.y, max.z},
            {min.x, max.y, min.z},
            {max.x, max.y, min.z},
            {max.x, max.y, max.z},
            {min.x, max.y, max.z},
        };
        int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        for (auto& edge : edges) {
            pushLine(corners[edge[0]], corners[edge[1]]);
        }
    }

    if (boundsVertices_.empty()) {
        return;
    }

    // 把构建好的线段顶点上传到 GPU 并绘制
    glBindVertexArray(boundsVao_);
    glBindBuffer(GL_ARRAY_BUFFER, boundsVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(boundsVertices_.size() * sizeof(RenderVertex)),
                 boundsVertices_.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(boundsVertices_.size()));
}

void World::renderClouds(const Shader&, bool enabled) const {
    if (!enabled || !clouds_) {
        return;
    }
    clouds_->draw();
}

void World::renderSun(const Shader& shader) const {
    if (!sunMesh_) return;

    // sunDir_: 当前太阳方向（单位向量，世界空间）
    // sunPos: 把太阳放在相机前方一定距离以实现屏幕空间恒定大小的太阳
    glm::vec3 sunPos = cameraPos_ + sunDir_ * 400.0f;
    // 构建 billboard 的局部坐标基（right/up/forward）
    glm::vec3 forward = -sunDir_;
    glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), forward));
    glm::vec3 up = glm::cross(forward, right);

    // model 矩阵：把 SunMesh 的本地 quad 放置到世界空间的 sunPos 并对齐朝向
    glm::mat4 model = glm::mat4(1.0f);
    model[0] = glm::vec4(right, 0.0f);
    model[1] = glm::vec4(up, 0.0f);
    model[2] = glm::vec4(forward, 0.0f);
    model[3] = glm::vec4(sunPos, 1.0f);

    // 设置 shader 的 uModel uniform，然后绘制
    shader.setMat4("uModel", model);
    sunMesh_->draw();
    // 恢复默认模型矩阵（避免影响后续 draw）
    shader.setMat4("uModel", glm::mat4(1.0f));
}

// raycast: 使用 RaycastBlocks 辅助函数在世界坐标中射线检测方块
// origin: 射线起点（世界空间）
// dir: 单位方向向量
// maxDistance: 最大检测距离
RayHit World::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance) const {
    auto sampler = [this](const glm::ivec3& pos) { return blockAt(pos); };
    return RaycastBlocks(origin, dir, maxDistance, sampler, registry_);
}

// setBlockInternal / placeBlock / removeBlock:
// 这些函数处理世界坐标到 chunk/local 的映射，修改 chunk 内容并把对应 chunk 标记为 dirty。
// meshQueue_ 会在下帧被处理以重新构建 mesh，从而把修改反映到渲染数据上。

bool World::removeBlock(const glm::ivec3& pos) {
    return setBlockInternal(pos, BlockId::Air);
}

bool World::placeBlock(const glm::ivec3& pos, BlockId id) {
    return setBlockInternal(pos, id);
}

// blockAt: 在世界坐标 pos 处查询方块 ID。
// 如果坐标不在已加载的 chunk 中则返回 Air（空气）。
BlockId World::blockAt(const glm::ivec3& pos) const {
    if (pos.y < 0 || pos.y >= Chunk::HEIGHT) {
        return BlockId::Air;
    }
    // worldToChunk: 把世界 x,z 投影为 chunk 坐标（整格）
    ChunkCoord coord = worldToChunk(pos.x, pos.z);
    auto it = chunks_.find(coord);
    if (it == chunks_.end() || !it->second) {
        return BlockId::Air;
    }
    glm::ivec3 local = toLocal(pos, coord); // toLocal: 把世界坐标转为 chunk 内部局部坐标
    return it->second->block(local.x, pos.y, local.z);
}

// cloudOffset / cloudTime: 提供给渲染模块的云层偏移和时间
glm::vec2 World::cloudOffset() const {
    return clouds_ ? clouds_->offset : glm::vec2(0.0f);
}

float World::cloudTime() const {
    return clouds_ ? clouds_->time : 0.0f;
}

// findChunk: 返回指向已加载 chunk 的裸指针，若不存在则返回 nullptr
Chunk* World::findChunk(const ChunkCoord& coord) {
    auto it = chunks_.find(coord);
    if (it == chunks_.end()) {
        return nullptr;
    }
    return it->second.get();
}

const Chunk* World::findChunk(const ChunkCoord& coord) const {
    auto it = chunks_.find(coord);
    if (it == chunks_.end()) {
        return nullptr;
    }
    return it->second.get();
}

// ensureChunksAround: 根据相机位置加载一定范围内的 chunk
// cameraPos: 世界空间相机位置，使用其 x/z 分量计算中心 chunk
void World::ensureChunksAround(const glm::vec3& cameraPos) {
    ChunkCoord center = worldToChunk(static_cast<int>(std::floor(cameraPos.x)), static_cast<int>(std::floor(cameraPos.z)));
    for (int dz = -renderDistance_; dz <= renderDistance_; ++dz) {
        for (int dx = -renderDistance_; dx <= renderDistance_; ++dx) {
            ChunkCoord coord{center.x + dx, center.z + dz};
            if (chunks_.find(coord) != chunks_.end()) {
                continue;
            }
            auto chunk = std::make_unique<Chunk>(coord);
            // generateTerrain: 在未加载的 chunk 中生成地形与植被
            generateTerrain(*chunk);
            meshQueue_.push_back(coord); // 标记需要构建 mesh
            chunks_.emplace(coord, std::move(chunk));
        }
    }
}

// rebuildMeshes: 每帧处理一定数量的 meshQueue_ 项，避免一帧内阻塞过久
// maxPerFrame: 最大处理数量（默认值在声明中），built 用于计数
void World::rebuildMeshes(int maxPerFrame) {
    int built = 0;
    while (!meshQueue_.empty() && built < maxPerFrame) {
        ChunkCoord coord = meshQueue_.front();
        meshQueue_.pop_front();
        Chunk* chunk = findChunk(coord);
        if (!chunk || !chunk->dirty()) {
            continue;
        }
        // sampler: 提供给 Chunk::buildMesh 的函数，用于按世界位置采样方块 id
        auto sampler = [this](const glm::ivec3& pos) { return blockAt(pos); };
        // tintSampler: 给方块提供基于生物群系的 tint（颜色）采样器
        auto tintSampler = [this](const glm::vec3& pos, const BlockInfo& info) {
            return sampleTint(pos, info);
        };
        chunk->buildMesh(registry_, sampler, tintSampler);
        ++built;
    }
}

// cleanupChunks: 卸载距离相机过远的 chunk，避免占用过多内存
void World::cleanupChunks(const glm::vec3& cameraPos) {
    ChunkCoord center = worldToChunk(static_cast<int>(std::floor(cameraPos.x)), static_cast<int>(std::floor(cameraPos.z)));
    std::vector<ChunkCoord> toRemove;
    int limit = renderDistance_ + 2;
    for (const auto& [coord, chunk] : chunks_) {
        int dx = coord.x - center.x;
        int dz = coord.z - center.z;
        if (std::abs(dx) > limit || std::abs(dz) > limit) {
            toRemove.push_back(coord);
        }
    }
    for (const auto& coord : toRemove) {
        chunks_.erase(coord);
    }
}

// generateTerrain: 在指定 chunk 上生成地形高度、表面方块、水线、树木与花等
void World::generateTerrain(Chunk& chunk) {
    glm::ivec3 origin = chunk.worldOrigin(); // chunk 在世界坐标系的左下角（最小 x,z）位置
    for (int z = 0; z < Chunk::SIZE; ++z) {
        for (int x = 0; x < Chunk::SIZE; ++x) {
            int worldX = origin.x + x; // 将局部 x 转为 世界 x
            int worldZ = origin.z + z; // 同理 z
            // uv 用于噪声采样，seed_ 用作随机偏移保证每次运行高度图差异
            glm::vec2 uv = glm::vec2(worldX, worldZ) * 0.0035f + glm::vec2(seed_);
            float base = fbm(uv, 4, 2.03f, 0.55f);
            float ridges = std::abs(fbm(uv * 1.7f, 3, 2.2f, 0.52f));
            // 基于 fbm 与 ridge 生成的地形高度（大致值）
            int height = static_cast<int>(35 + base * 18.0f + ridges * 14.0f);
            if (height < 8) height = 8;

            // 填充该列的方块：高度以下为实体（草/泥/石），高度以上为空气或水
            for (int y = 0; y < Chunk::HEIGHT; ++y) {
                BlockId id = BlockId::Air;
                if (y <= height) {
                    if (y == height) {
                        id = BlockId::Grass; // 地表顶层为草方块
                    } else if (y >= height - 3) {
                        id = BlockId::Dirt;  // 地表下几层为泥土
                    } else {
                        id = BlockId::Stone; // 更深处为石头
                    }
                } else if (y <= waterLevel_) {
                    id = BlockId::Water; // 水面高度之下为水
                }
                chunk.setBlock(x, y, z, id);
            }

            // 当地表在水面线之下时把表面设为沙子（沙滩）
            if (height <= waterLevel_) {
                chunk.setBlock(x, height, z, BlockId::Sand);
            }

            // 高海拔处覆盖雪
            if (height > 60) {
                chunk.setBlock(x, height, z, BlockId::Snow);
            }

            // 生长树/花/草的概率逻辑（基于噪声）
            float treeMask = glm::perlin(glm::vec2(worldX, worldZ) * 0.012f + seed_ * 0.53f);
            if (treeMask > 0.6f && height > waterLevel_ + 2 && x > 2 && x < Chunk::SIZE - 3 && z > 2 && z < Chunk::SIZE - 3) {
                growTree(chunk, x, z, worldX, worldZ, height);
            } else {
                float flowerNoise = glm::perlin(glm::vec2(worldX * 1.2f, worldZ * 0.9f) * 0.07f + seed_ * 0.24f);
                if (flowerNoise > 0.65f && height > waterLevel_ + 1) {
                    chunk.setBlock(x, height + 1, z, BlockId::Flower);
                } else if (flowerNoise > 0.3f && height > waterLevel_ + 1) {
                    if (chunk.block(x, height + 1, z) == BlockId::Air) {
                        chunk.setBlock(x, height + 1, z, BlockId::TallGrass);
                    }
                }
            }
        }
    }
}

// biomeColor: 根据世界位置计算生物群系颜色（用于草/叶的染色）
// worldPos: 世界空间位置（通常取方块中心）
glm::vec3 World::biomeColor(const glm::vec3& worldPos) const {
    glm::vec2 pos = glm::vec2(worldPos.x, worldPos.z) * 0.0022f + glm::vec2(seed_ * 0.13f);
    float temperature = glm::clamp(glm::perlin(pos * 0.8f + 13.7f) * 0.5f + 0.5f, 0.0f, 1.0f);
    float moisture = glm::clamp(glm::perlin(pos * 1.4f - 17.3f) * 0.5f + 0.5f, 0.0f, 1.0f);
    float elevation = glm::clamp(worldPos.y / 120.0f, 0.0f, 1.0f);

    // 各类基色，用于混合出不同生物群系的草地颜色
    glm::vec3 forestLow(0.37f, 0.72f, 0.46f);
    glm::vec3 forestHigh(0.38f, 0.85f, 0.62f);
    glm::vec3 desert(0.92f, 0.88f, 0.45f);
    glm::vec3 swamp(0.25f, 0.48f, 0.32f);
    glm::vec3 mountain(0.66f, 0.8f, 0.7f);

    glm::vec3 baseColor = glm::mix(forestLow, forestHigh, moisture);
    if (temperature > 0.65f && moisture < 0.35f) {
        baseColor = glm::mix(baseColor, desert, temperature);
    } else if (moisture > 0.7f) {
        baseColor = glm::mix(baseColor, swamp, moisture - 0.5f);
    }
    baseColor = glm::mix(baseColor, mountain, elevation * elevation);
    baseColor.b += 0.05f; // 轻微提亮蓝色通道以获得更自然的色调
    return glm::clamp(baseColor, glm::vec3(0.1f), glm::vec3(1.0f));
}

// sampleTint: 根据方块信息和世界位置返回最终的 tint（顶点颜色乘积）
// worldPos: 用于采样生物群系颜色（如果 info.biomeTint 为 true）
// info.tint: BlockInfo 中存储的基础 tint（可被生物群系调制）
glm::vec3 World::sampleTint(const glm::vec3& worldPos, const BlockInfo& info) const {
    glm::vec3 base = info.tint;
    if (info.biomeTint) {
        base *= biomeColor(worldPos);
    }
    return base;
}

// noiseRand / gaussian01: 用于随机性与高斯随机生成，辅助植被/地形
float World::noiseRand(int x, int z, int salt) const {
    glm::vec2 p = glm::vec2(x + seed_ * 0.11f + salt * 1.37f, z - seed_ * 0.17f - salt * 0.73f);
    float value = glm::sin(glm::dot(p, glm::vec2(12.9898f, 78.233f))) * 43758.5453f;
    return glm::fract(value);
}

float World::gaussian01(int x, int z, int salt) const {
    float u1 = glm::max(noiseRand(x, z, salt), 1e-4f);
    float u2 = glm::max(noiseRand(x, z, salt + 29), 1e-4f);
    float z0 = glm::sqrt(-2.0f * glm::log(u1)) * glm::cos(2.0f * glm::pi<float>() * u2);
    return glm::clamp(z0 * 0.25f + 0.5f, 0.0f, 1.0f);
}

// growTree: 在指定位置生成树干与树冠（简单体素树）
// 参数说明见函数签名：chunk（目标 chunk），localX/localZ（chunk 局部 x/z），
// worldX/worldZ（世界 x/z，用于随机函数），groundHeight（地表高度）
void World::growTree(Chunk& chunk, int localX, int localZ, int worldX, int worldZ, int groundHeight) {
    int baseY = groundHeight + 1; // 树干从地表上方一格开始
    if (baseY + 10 >= Chunk::HEIGHT) {
        return; // 防止超出 chunk 高度范围
    }
    
    // 1. 修改树干高度：增加高度以容纳弯曲部分 (6~9)
    int trunkHeight = glm::clamp(6 + static_cast<int>(std::round(gaussian01(worldX, worldZ, 5) * 3.0f)), 6, 9);

    // 2. 定义直树干的高度：保证下方 4 个方块是直的
    const int straightHeight = 4;

    int currentX = localX;
    int currentZ = localZ;
    
    // 记录树顶位置，用于生成树冠（因为树干可能歪了）
    int topX = localX;
    int topZ = localZ;
    std::vector<vec3> directions = {
        {0, 0, 1}, {0, 0, -1}, // 上下
        {1, 0, 0}, {-1, 0, 0}, // 东西
    };
    // 下面循环在 trunkHeight 范围内尝试放置树干
    for (int i = 0; i < trunkHeight && baseY + i < Chunk::HEIGHT; ++i) {
        // 3. 超过直树干高度后，允许随机偏移
        if (i >= straightHeight) {
            // 使用噪声决定是否偏移 (salt 随高度变化)
            float drift = noiseRand(worldX, worldZ, i * 23 + 100);
            if (drift > 0.5f) { // 50% 概率发生偏移
                float dir = noiseRand(worldX, worldZ, i * 17 + 200);
                if (dir < 0.25f) currentX--;
                else if (dir < 0.5f) currentX++;
                else if (dir < 0.75f) currentZ--;
                else currentZ++;
            }
        }

        // 边界检查：如果偏移出了 Chunk，停止生成（简化处理，防止越界）
        if (currentX < 0 || currentX >= Chunk::SIZE || currentZ < 0 || currentZ >= Chunk::SIZE) {
            trunkHeight = i; // 截断高度
            break;
        }

        // 检查目标位置是否为空或可替换（草/花/叶子），避免嵌入石头或其他树干
        BlockId target = chunk.block(currentX, baseY + i, currentZ);
        if (target == BlockId::Air || target == BlockId::TallGrass || target == BlockId::Flower || target == BlockId::OakLeaves) {
            int flag=0;
            for(const auto&[dx,dy,dz]:directions){
                if (chunk.block(currentX+dx,baseY+i+dy,currentZ+dz)==BlockId::Air)
                {
                flag++;
                }
            }
            if(flag==2){
            chunk.setBlock(currentX, baseY + i, currentZ, BlockId::OakLog);
            topX = currentX;
            topZ = currentZ;
            }
        } else {
            // 遇到阻碍（如其他树干），停止生长
            trunkHeight = i;
            break;
        }
    }

    // 树冠生成：crownStart 为树冠开始的最低层 y
    int crownStart = baseY + trunkHeight - 2;
    // baseRadius: 树冠基准半径（会随位置随机扰动）
    float baseRadius = 2.5f + gaussian01(worldX, worldZ, 11) * 1.2f;
    
    // 4. 围绕树顶 (topX, topZ) 生成叶子
    for (int dy = -2; dy <= 3; ++dy) {
        float radius = baseRadius - std::abs(static_cast<float>(dy)) * 0.6f;
        radius = glm::max(radius, 1.0f);
        int range = static_cast<int>(std::ceil(radius)) + 1;

        for (int dx = -range; dx <= range; ++dx) {
            for (int dz = -range; dz <= range; ++dz) {
                int lx = topX + dx; // 使用 topX 而不是 localX
                int lz = topZ + dz; // 使用 topZ 而不是 localZ
                int ly = crownStart + dy;
                
                // 边界检查
                if (lx < 0 || lz < 0 || lx >= Chunk::SIZE || lz >= Chunk::SIZE || ly < 0 || ly >= Chunk::HEIGHT) {
                    continue;
                }
                
                // dist: 在水平面上的距离，用于判定是否落在树冠半径内
                float dist = glm::length(glm::vec2(dx, dz));
                // jitter: 小扰动，使叶子边缘更不规则
                float jitter = (noiseRand(worldX + dx, worldZ + dz, 150 + dy) - 0.5f) * 0.4f;
                
                if (dist + jitter <= radius) {
                    // 仅在空气处生成叶子，不覆盖树干
                    if (chunk.block(lx, ly, lz) == BlockId::Air) {
                        chunk.setBlock(lx, ly, lz, BlockId::OakLeaves);
                    }
                }
            }
        }
    }
}

// toLocal: 将世界坐标转换为指定 chunk 的局部坐标（chunk 内索引）
// pos: 世界坐标，coord: chunk 的 chunk 坐标（以 chunk 为单位的格子位置）
glm::ivec3 World::toLocal(const glm::ivec3& pos, const ChunkCoord& coord) const {
    return glm::ivec3(pos.x - coord.x * Chunk::SIZE, pos.y, pos.z - coord.z * Chunk::SIZE);
}

// worldToChunk: 根据世界 x,z 计算它属于哪个 chunk（坐标以 chunk 为单位）
// 使用 floorDiv 确保负坐标也正确映射到 chunk 网格
ChunkCoord World::worldToChunk(int x, int z) const {
    return ChunkCoord{floorDiv(x, Chunk::SIZE), floorDiv(z, Chunk::SIZE)};
}

// setBlockInternal: 在世界坐标 pos 放置方块 id（处理 chunk 查找、local 转换与 dirty 标记）
// 返回是否成功（如坐标越界或 chunk 不存在则返回 false）
bool World::setBlockInternal(const glm::ivec3& pos, BlockId id) {
    if (pos.y < 0 || pos.y >= Chunk::HEIGHT) {
        return false;
    }
    ChunkCoord coord = worldToChunk(pos.x, pos.z);
    Chunk* chunk = findChunk(coord);
    if (!chunk) {
        return false;
    }
    glm::ivec3 local = toLocal(pos, coord);
    if (chunk->block(local.x, pos.y, local.z) == id) {
        return false;
    }
    chunk->setBlock(local.x, pos.y, local.z, id);
    meshQueue_.push_back(coord); // 标记该 chunk 需要重建 mesh
    markNeighborsDirty(pos);     // 如果位于 chunk 边界，还需标记相邻 chunk
    return true;
}

// markNeighborsDirty: 如果修改位置位于 chunk 边界，则把相邻 chunk 标记为需要重建
void World::markNeighborsDirty(const glm::ivec3& pos) {
    ChunkCoord coord = worldToChunk(pos.x, pos.z);
    glm::ivec3 local = toLocal(pos, coord);
    std::array<std::pair<bool, ChunkCoord>, 4> candidates = {
        std::make_pair(local.x == 0, ChunkCoord{coord.x - 1, coord.z}),
        std::make_pair(local.x == Chunk::SIZE - 1, ChunkCoord{coord.x + 1, coord.z}),
        std::make_pair(local.z == 0, ChunkCoord{coord.x, coord.z - 1}),
        std::make_pair(local.z == Chunk::SIZE - 1, ChunkCoord{coord.x, coord.z + 1}),
    };
    for (const auto& entry : candidates) {
        if (!entry.first) continue;
        Chunk* neighbor = findChunk(entry.second);
        if (!neighbor) continue;
        neighbor->markDirty();
        meshQueue_.push_back(entry.second);
    }
}

// updateSun: 根据时间进度更新太阳方向、太阳颜色、环境光和天空颜色等
void World::updateSun(float dt) {
    // 让 timeOfDay_ 在 [0,1) 周期内循环，daySpeed_ 控制一日流逝速度
    timeOfDay_ += dt * daySpeed_;
    if (timeOfDay_ > 1.0f) {
        timeOfDay_ -= 1.0f;
    }
    float angle = timeOfDay_ * glm::two_pi<float>();
    // sunDir_ 计算方式：x/z 使用 cos/sin 混合得到一定偏移，y 使用 sin(angle) 来表示高度
    sunDir_ = glm::normalize(glm::vec3(std::cos(angle) * 0.8f, std::sin(angle), std::sin(angle * 0.7f)));
    // sunAmount: 基于太阳高度计算光照强度（0.05..1.0 范围）
    float sunAmount = glm::clamp(sunDir_.y * 0.5f + 0.5f, 0.05f, 1.0f);
    // sunColor_: 日出/日落偏暖色，正午偏白
    sunColor_ = glm::mix(glm::vec3(0.9f, 0.65f, 0.4f), glm::vec3(1.0f, 0.98f, 0.92f), sunAmount);
    // ambientColor_: 环境光颜色随太阳高度变化
    ambientColor_ = glm::mix(glm::vec3(0.1f, 0.12f, 0.2f), glm::vec3(0.38f, 0.45f, 0.55f), sunAmount);
    // skyColor_: 天空背景颜色随太阳高度渐变（夜晚较深，白天明亮）
    skyColor_ = glm::mix(glm::vec3(0.05f, 0.05f, 0.1f), glm::vec3(0.55f, 0.72f, 0.92f), sunAmount);
    // fogDensity_ 随日间减少，夜晚更浓
    fogDensity_ = glm::mix(0.0032f, 0.0015f, sunAmount);
}
