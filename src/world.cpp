#include "world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtc/noise.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

// 树生成参数（保持 generateTerrain 与 growTree 一致，避免树冠被 chunk 边界裁切）
constexpr int kOakCanopyRadius = 4;      // growTree 水平最大扩展半径
constexpr int kOakCanopyHalfHeight = 3;  // growTree 叶子层上下高度（dy: -2..2）

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

// AnimalMesh: 分部件的四足动物网格（head/body/leg），使用专用动物贴图
struct World::AnimalMesh {
    struct PartMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei indexCount = 0;

        void destroy() {
            if (vao) {
                glDeleteVertexArrays(1, &vao);
                glDeleteBuffers(1, &vbo);
                glDeleteBuffers(1, &ebo);
                vao = 0;
                vbo = 0;
                ebo = 0;
                indexCount = 0;
            }
        }

        void draw() const {
            glBindVertexArray(vao);
            glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        }
    };

    static void initBoxPart(PartMesh& part,
                            const glm::vec3& minP,
                            const glm::vec3& maxP,
                            const AnimalUVLayout::Box& uv,
                            const glm::vec3& bodyColor) {
        RenderVertex verts[24];
        unsigned int indices[36];

        auto makeVertex = [&](int index, const glm::vec3& pos, const glm::vec3& normal, const glm::vec2& uvCoord) {
            verts[index].pos = pos;
            verts[index].normal = normal;
            verts[index].uv = uvCoord;
            verts[index].color = bodyColor;
            verts[index].light = 1.0f;
            verts[index].material = 4.0f; // animal
            verts[index].anim = glm::vec3(0.0f);
        };

        const float minX = minP.x;
        const float minY = minP.y;
        const float minZ = minP.z;
        const float maxX = maxP.x;
        const float maxY = maxP.y;
        const float maxZ = maxP.z;

        // +Z front
        makeVertex(0,  {minX, minY, maxZ}, {0.0f, 0.0f, 1.0f}, uv.front.bl);
        makeVertex(1,  {maxX, minY, maxZ}, {0.0f, 0.0f, 1.0f}, uv.front.br);
        makeVertex(2,  {maxX, maxY, maxZ}, {0.0f, 0.0f, 1.0f}, uv.front.tr);
        makeVertex(3,  {minX, maxY, maxZ}, {0.0f, 0.0f, 1.0f}, uv.front.tl);
        // -Z back
        makeVertex(4,  {maxX, minY, minZ}, {0.0f, 0.0f, -1.0f}, uv.back.bl);
        makeVertex(5,  {minX, minY, minZ}, {0.0f, 0.0f, -1.0f}, uv.back.br);
        makeVertex(6,  {minX, maxY, minZ}, {0.0f, 0.0f, -1.0f}, uv.back.tr);
        makeVertex(7,  {maxX, maxY, minZ}, {0.0f, 0.0f, -1.0f}, uv.back.tl);
        // +X right
        makeVertex(8,  {maxX, minY, maxZ}, {1.0f, 0.0f, 0.0f}, uv.right.bl);
        makeVertex(9,  {maxX, minY, minZ}, {1.0f, 0.0f, 0.0f}, uv.right.br);
        makeVertex(10, {maxX, maxY, minZ}, {1.0f, 0.0f, 0.0f}, uv.right.tr);
        makeVertex(11, {maxX, maxY, maxZ}, {1.0f, 0.0f, 0.0f}, uv.right.tl);
        // -X left
        makeVertex(12, {minX, minY, minZ}, {-1.0f, 0.0f, 0.0f}, uv.left.bl);
        makeVertex(13, {minX, minY, maxZ}, {-1.0f, 0.0f, 0.0f}, uv.left.br);
        makeVertex(14, {minX, maxY, maxZ}, {-1.0f, 0.0f, 0.0f}, uv.left.tr);
        makeVertex(15, {minX, maxY, minZ}, {-1.0f, 0.0f, 0.0f}, uv.left.tl);
        // +Y top
        makeVertex(16, {minX, maxY, maxZ}, {0.0f, 1.0f, 0.0f}, uv.top.bl);
        makeVertex(17, {maxX, maxY, maxZ}, {0.0f, 1.0f, 0.0f}, uv.top.br);
        makeVertex(18, {maxX, maxY, minZ}, {0.0f, 1.0f, 0.0f}, uv.top.tr);
        makeVertex(19, {minX, maxY, minZ}, {0.0f, 1.0f, 0.0f}, uv.top.tl);
        // -Y bottom
        makeVertex(20, {minX, minY, minZ}, {0.0f, -1.0f, 0.0f}, uv.bottom.bl);
        makeVertex(21, {maxX, minY, minZ}, {0.0f, -1.0f, 0.0f}, uv.bottom.br);
        makeVertex(22, {maxX, minY, maxZ}, {0.0f, -1.0f, 0.0f}, uv.bottom.tr);
        makeVertex(23, {minX, minY, maxZ}, {0.0f, -1.0f, 0.0f}, uv.bottom.tl);

        auto quad = [&](int base, int i) {
            indices[i + 0] = base + 0;
            indices[i + 1] = base + 1;
            indices[i + 2] = base + 2;
            indices[i + 3] = base + 2;
            indices[i + 4] = base + 3;
            indices[i + 5] = base + 0;
        };
        quad(0,  0);
        quad(4,  6);
        quad(8,  12);
        quad(12, 18);
        quad(16, 24);
        quad(20, 30);

        part.destroy();
        part.indexCount = 36;
        glGenVertexArrays(1, &part.vao);
        glGenBuffers(1, &part.vbo);
        glGenBuffers(1, &part.ebo);

        glBindVertexArray(part.vao);
        glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.ebo);
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
        glEnableVertexAttribArray(kAnimLocation);
        glVertexAttribPointer(kAnimLocation, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(offsetof(RenderVertex, anim)));
        glBindVertexArray(0);
    }

    AnimalMesh(AnimalType type, const AnimalUVLayout& uvLayout, const glm::vec3& bodyColor) {
        const float unit = 1.0f / 16.0f;

        int legW = 4;
        int legD = 4;
        int legH = 12;
        int bodyW = 12;
        int bodyH = 10;
        int bodyD = 8;
        int headW = 8;
        int headH = 8;
        int headD = 8;

        switch (type) {
            case AnimalType::Pig:
                legH = 6;
                bodyW = 10;
                bodyH = 8;
                bodyD = 8;
                break;
            case AnimalType::Cow:
                legH = 12;
                bodyW = 12;
                bodyH = 10;
                bodyD = 8;
                break;
            case AnimalType::Sheep:
                legH = 12;
                bodyW = 8;
                bodyH = 8;
                bodyD = 8;
                break;
        }

        const glm::vec3 bodyHalf(bodyW * 0.5f * unit, bodyH * 0.5f * unit, bodyD * 0.5f * unit);
        const glm::vec3 headHalf(headW * 0.5f * unit, headH * 0.5f * unit, headD * 0.5f * unit);
        const glm::vec3 legHalf(legW * 0.5f * unit, legH * 0.5f * unit, legD * 0.5f * unit);

        // body/head 都用中心为原点的 box
        initBoxPart(body,
                    {-bodyHalf.x, -bodyHalf.y, -bodyHalf.z},
                    { bodyHalf.x,  bodyHalf.y,  bodyHalf.z},
                    uvLayout.body,
                    bodyColor);
        initBoxPart(head,
                    {-headHalf.x, -headHalf.y, -headHalf.z},
                    { headHalf.x,  headHalf.y,  headHalf.z},
                    uvLayout.head,
                    bodyColor);
        // leg 的枢轴在顶部中心：y 从 [-H, 0]
        initBoxPart(leg,
                    {-legHalf.x, -static_cast<float>(legH) * unit, -legHalf.z},
                    { legHalf.x,  0.0f,                         legHalf.z},
                    uvLayout.leg,
                    bodyColor);

        const float legTopY = legH * unit;
        bodyPos = glm::vec3(0.0f, legTopY + bodyHalf.y, 0.0f);
        headPos = glm::vec3(0.0f, legTopY + (bodyH == 8 ? 6.0f * unit : (bodyHalf.y + 3.0f * unit)), bodyHalf.z + headHalf.z);

        float legX = glm::max(0.0f, bodyHalf.x - legHalf.x);
        float legZ = glm::max(0.0f, bodyHalf.z - legHalf.z);
        legPos[0] = glm::vec3(-legX, legTopY,  legZ); // front-left
        legPos[1] = glm::vec3( legX, legTopY,  legZ); // front-right
        legPos[2] = glm::vec3(-legX, legTopY, -legZ); // back-left
        legPos[3] = glm::vec3( legX, legTopY, -legZ); // back-right
    }

    ~AnimalMesh() {
        head.destroy();
        body.destroy();
        leg.destroy();
    }

    void drawBody() const { body.draw(); }
    void drawHead() const { head.draw(); }
    void drawLeg() const { leg.draw(); }

    PartMesh head;
    PartMesh body;
    PartMesh leg;

    glm::vec3 headPos{0.0f};
    glm::vec3 bodyPos{0.0f};
    std::array<glm::vec3, 4> legPos{glm::vec3(0.0f)};
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
            verts[i].material = 5.0f; // material==5 专用于太阳/灯光渲染（与动物分开）
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

World::World(TextureAtlas& atlas,
             BlockRegistry& registry,
             const AnimalUVLayout& pigUV,
             const AnimalUVLayout& cowUV,
             const AnimalUVLayout& sheepUV)
    : atlas_(atlas),
      registry_(registry),
      clouds_(std::make_unique<CloudLayer>()),
      sunMesh_(std::make_unique<SunMesh>()),
    pigMesh_(std::make_unique<AnimalMesh>(AnimalType::Pig, pigUV, glm::vec3(1.0f))),   // 颜色用于轻微调节贴图色调
    cowMesh_(std::make_unique<AnimalMesh>(AnimalType::Cow, cowUV, glm::vec3(1.0f))),
    sheepMesh_(std::make_unique<AnimalMesh>(AnimalType::Sheep, sheepUV, glm::vec3(1.0f)))
{
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
    // 更新动物 AI（漫游与绕行玩家）
    updateAnimals(dt);
}

void World::render(const Shader& shader) const {
    // 渲染所有非透明（solid）的 chunk
    for (const auto& [coord, chunk] : chunks_) {
        if (!chunk || chunk->empty()) {
            continue;
        }
        chunk->renderSolid();
    }

    // 渲染动物
    renderAnimals(shader);
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
            // 在该 chunk 中生成一些动物（猪/牛/羊）
            spawnAnimalsForChunk(*chunk);
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

    // 先生成地形（高度图），再生成植被。
    // 这样可以：
    // 1) 避免树的间距过滤受 (x,z) 扫描顺序影响而偏向同一角落
    // 2) 让“边界预留”与树冠半径保持一致，避免树总是缺一半
    std::array<std::array<int, Chunk::SIZE>, Chunk::SIZE> heights{};

    for (int z = 0; z < Chunk::SIZE; ++z) {
        for (int x = 0; x < Chunk::SIZE; ++x) {
            int worldX = origin.x + x;
            int worldZ = origin.z + z;
            glm::vec2 uv = glm::vec2(worldX, worldZ) * 0.0035f + glm::vec2(seed_);
            float base = fbm(uv, 4, 2.03f, 0.55f);
            float ridges = std::abs(fbm(uv * 1.7f, 3, 2.2f, 0.52f));
            int height = static_cast<int>(35 + base * 18.0f + ridges * 14.0f);
            if (height < 8) height = 8;
            heights[z][x] = height;

            for (int y = 0; y < Chunk::HEIGHT; ++y) {
                BlockId id = BlockId::Air;
                if (y <= height) {
                    if (y == height) {
                        id = BlockId::Grass;
                    } else if (y >= height - 3) {
                        id = BlockId::Dirt;
                    } else {
                        id = BlockId::Stone;
                    }
                } else if (y <= waterLevel_) {
                    id = BlockId::Water;
                }
                chunk.setBlock(x, y, z, id);
            }

            if (height <= waterLevel_) {
                chunk.setBlock(x, height, z, BlockId::Sand);
            }
            if (height > 60) {
                chunk.setBlock(x, height, z, BlockId::Snow);
            }
        }
    }

    // 植被生成：为避免“扫描顺序偏置”，对候选格做确定性打乱。
    std::vector<std::pair<int, int>> cells;
    cells.reserve(static_cast<std::size_t>(Chunk::SIZE * Chunk::SIZE));
    for (int z = 0; z < Chunk::SIZE; ++z) {
        for (int x = 0; x < Chunk::SIZE; ++x) {
            cells.emplace_back(x, z);
        }
    }
    // 基于 chunk 坐标的确定性 Fisher–Yates shuffle
    for (int i = static_cast<int>(cells.size()) - 1; i > 0; --i) {
        float r = noiseRand(origin.x, origin.z, 4200 + i);
        int j = static_cast<int>(r * static_cast<float>(i + 1));
        if (j < 0) j = 0;
        if (j > i) j = i;
        std::swap(cells[static_cast<std::size_t>(i)], cells[static_cast<std::size_t>(j)]);
    }

    const int spacing = 7; // 树之间的最小间距（方形邻域半径）
    const int margin = kOakCanopyRadius; // 确保树冠不会越出 chunk

    for (const auto& cell : cells) {
        int x = cell.first;
        int z = cell.second;
        int height = heights[z][x];

        int worldX = origin.x + x;
        int worldZ = origin.z + z;

        // 树：水面以上才生成
        if (height > waterLevel_ + 2) {
            // 预留足够边界，避免树冠跨出 chunk 被裁切（导致树“缺一半”且方向一致）
            bool inside = (x >= margin && x < Chunk::SIZE - margin && z >= margin && z < Chunk::SIZE - margin);
            if (inside) {
                float treeMask = glm::perlin(glm::vec2(worldX, worldZ) * 0.0045f + seed_ * 0.53f);
                float density = glm::clamp(treeMask * 0.5f + 0.5f, 0.0f, 1.0f);
                float treeProb = glm::mix(0.005f, 0.01f, density);
                bool treeChance = noiseRand(worldX, worldZ, 911) < treeProb;

                if (treeChance) {
                    bool hasNeighborTree = false;
                    // 在一个小的垂直范围内找树干/树叶，避免不同地形高度时漏判
                    int y0 = height + 1;
                    int y1 = glm::min(height + 10, Chunk::HEIGHT - 1);
                    for (int dz = -spacing; dz <= spacing && !hasNeighborTree; ++dz) {
                        for (int dx = -spacing; dx <= spacing && !hasNeighborTree; ++dx) {
                            int nx = x + dx;
                            int nz = z + dz;
                            if (nx < 0 || nx >= Chunk::SIZE || nz < 0 || nz >= Chunk::SIZE) continue;
                            for (int y = y0; y <= y1; ++y) {
                                BlockId neighbor = chunk.block(nx, y, nz);
                                if (neighbor == BlockId::OakLog || neighbor == BlockId::OakLeaves) {
                                    hasNeighborTree = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!hasNeighborTree) {
                        growTree(chunk, x, z, worldX, worldZ, height);
                        continue; // 本格子生成了树，不再生成花/草
                    }
                }
            }
        }

        // 花/草（仅在水面以上）
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

// 在给定 chunk 内随机生成若干动物（猪/牛/羊）
void World::spawnAnimalsForChunk(const Chunk& chunk) {
    ChunkCoord coord = chunk.coord();
    // 基于 chunk 坐标的噪声决定本 chunk 大致动物数量（0~3 只）
    float noise = noiseRand(coord.x * 13, coord.z * 17, 1337);
    int maxAnimals = 0;
    if (noise > 0.8f) {
        maxAnimals = 2;
    } else if (noise > 0.55f) {
        maxAnimals = 2;
    } else if (noise > 0.4f) {
        maxAnimals = 1;
    }
    if (maxAnimals == 0) return;

    glm::ivec3 origin = chunk.worldOrigin();
    for (int i = 0; i < maxAnimals; ++i) {
        // 在 chunk 内挑一个相对安全的位置（避开边界 2 格）
        float rx = noiseRand(coord.x * 31 + i * 7, coord.z * 29 + i * 5, 200);
        float rz = noiseRand(coord.x * 37 + i * 11, coord.z * 23 + i * 3, 400);
        int localX = 2 + static_cast<int>(rx * (Chunk::SIZE - 4));
        int localZ = 2 + static_cast<int>(rz * (Chunk::SIZE - 4));

        // 向下扫描找到地面高度（忽略空气和水）
        int groundY = -1;
        for (int y = Chunk::HEIGHT - 2; y >= 1; --y) {
            BlockId id = chunk.block(localX, y, localZ);
            if (id != BlockId::Air && id != BlockId::Water) {
                groundY = y;
                break;
            }
        }
        if (groundY <= waterLevel_ + 1) {
            continue; // 水面附近不生成
        }

        int worldX = origin.x + localX;
        int worldZ = origin.z + localZ;

        Animal animal{};
        float typeR = noiseRand(worldX, worldZ, 777);
        if (typeR < 0.33f) {
            animal.type = AnimalType::Pig;
        } else if (typeR < 0.66f) {
            animal.type = AnimalType::Cow;
        } else {
            animal.type = AnimalType::Sheep;
        }

        animal.position = glm::vec3(static_cast<float>(worldX) + 0.5f,
                                    static_cast<float>(groundY) + 1.0f,
                                    static_cast<float>(worldZ) + 0.5f);
        animal.yaw = noiseRand(worldX, worldZ, 888) * glm::two_pi<float>();
        animal.speed = 1.2f + noiseRand(worldX, worldZ, 999) * 0.4f;
        animal.wanderTimer = 2.0f + noiseRand(worldX, worldZ, 123) * 3.0f;
        animal.seedX = worldX;
        animal.seedZ = worldZ;
        animal.wanderStep = 0;

        animals_.push_back(animal);
    }
}

// biomeColor: 根据世界位置计算生物群系颜色（用于草/叶的染色）
// worldPos: 世界空间位置（通常取方块中心）
glm::vec3 World::biomeColor(const glm::vec3& worldPos) const {
    glm::vec2 pos = glm::vec2(worldPos.x, worldPos.z) * 0.0022f + glm::vec2(seed_ * 0.13f);
    float temperature = glm::clamp(glm::perlin(pos * 0.8f + 13.7f) * 0.5f + 0.5f, 0.0f, 1.0f);
    float moisture = glm::clamp(glm::perlin(pos * 1.4f - 17.3f) * 0.5f + 0.5f, 0.0f, 1.0f);
    float elevation = glm::clamp(worldPos.y / 120.0f, 0.0f, 1.0f);

    // 各类基色，用于混合出不同生物群系的草地颜色（使用更接近原版的颜色）
    glm::vec3 plains(0.57f, 0.74f, 0.35f);    // 标准平原/森林绿
    glm::vec3 desert(0.93f, 0.86f, 0.52f);    // 沙漠/热带草原黄
    glm::vec3 swamp(0.28f, 0.32f, 0.22f);     // 沼泽暗绿
    glm::vec3 mountain(0.6f, 0.65f, 0.55f);   // 山地/冷色调

    // 先基于湿度混合出基础绿色（从平原到沼泽）
    glm::vec3 baseColor = glm::mix(plains, swamp, glm::smoothstep(0.4f, 0.8f, moisture));
    // 基于温度混合沙漠色
    baseColor = glm::mix(baseColor, desert, glm::smoothstep(0.5f, 0.9f, temperature));
    // 最后基于海拔混合山地色
    baseColor = glm::mix(baseColor, mountain, glm::smoothstep(0.5f, 0.9f, elevation));

    return glm::clamp(baseColor, glm::vec3(0.0f), glm::vec3(1.0f));
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

// 更新所有动物的简单 AI：在地面上漫游，并在靠近玩家时尝试绕开玩家
void World::updateAnimals(float dt) {
    if (animals_.empty()) return;

    const float avoidRadius = 6.0f;      // 与玩家保持的最小距离
    const float maxTurnRate = 2.5f;      // 每秒最大转向角速度（弧度）

    glm::vec2 playerXZ(cameraPos_.x, cameraPos_.z);

    for (Animal& a : animals_) {
        glm::vec2 posXZ(a.position.x, a.position.z);
        glm::vec2 toPlayer = playerXZ - posXZ;
        float dist = glm::length(toPlayer);

        bool avoiding = false;
        if (dist > 0.001f && dist < avoidRadius) {
            // 靠近玩家：朝与玩家相反的方向缓慢转向
            avoiding = true;
            glm::vec2 away = -toPlayer / dist;
            float targetYaw = std::atan2(away.x, away.y); // yaw=0 对应 +Z 方向
            float diff = targetYaw - a.yaw;
            // 把角度差规范到 [-pi, pi]
            while (diff > glm::pi<float>()) diff -= glm::two_pi<float>();
            while (diff < -glm::pi<float>()) diff += glm::two_pi<float>();
            float maxStep = maxTurnRate * dt;
            diff = glm::clamp(diff, -maxStep, maxStep);
            a.yaw += diff;
        } else {
            // 普通漫游：隔一段时间随机换一个方向
            a.wanderTimer -= dt;
            if (a.wanderTimer <= 0.0f) {
                float dirRand = noiseRand(a.seedX, a.seedZ, 500 + a.wanderStep);
                float durRand = noiseRand(a.seedX, a.seedZ, 700 + a.wanderStep);
                a.wanderStep++;
                a.yaw = (dirRand * 2.0f - 1.0f) * glm::pi<float>(); // [-pi, pi]
                a.wanderTimer = 2.0f + durRand * 4.0f;              // 2~6 秒
            }
        }

        float moveSpeed = avoiding ? (a.speed * 1.6f) : a.speed;
        glm::vec3 dir(std::sin(a.yaw), 0.0f, std::cos(a.yaw));
        glm::vec3 newPos = a.position + dir * moveSpeed * dt;
        newPos.y = a.position.y; // 保持在同一高度（简单地跟随原始地面）

        // 简单的地形碰撞和边缘检测：确保脚下有方块、身位是空气
        auto canStandAt = [&](const glm::vec3& p) -> bool {
            int bx = static_cast<int>(std::floor(p.x));
            int bz = static_cast<int>(std::floor(p.z));
            int by = static_cast<int>(std::floor(p.y));
            if (by <= 1 || by >= Chunk::HEIGHT) return false;

            BlockId below = blockAt(glm::ivec3(bx, by - 1, bz));
            BlockId at    = blockAt(glm::ivec3(bx, by, bz));

            if (below == BlockId::Air || below == BlockId::Water) return false;
            if (at != BlockId::Air && at != BlockId::TallGrass && at != BlockId::Flower) return false;
            return true;
        };

        if (canStandAt(newPos)) {
            float distMoved = glm::length(glm::vec2(newPos.x - a.position.x, newPos.z - a.position.z));
            a.position = newPos;

            // 行走动画：用“移动距离”驱动相位，保证与匀速移动同步（与帧率无关）
            float strideLen = 0.45f;
            switch (a.type) {
                case AnimalType::Pig:   strideLen = 0.35f; break;
                case AnimalType::Cow:   strideLen = 0.50f; break;
                case AnimalType::Sheep: strideLen = 0.45f; break;
            }
            if (distMoved > 1e-5f) {
                float phasePerMeter = glm::two_pi<float>() / strideLen;
                a.walkPhase += distMoved * phasePerMeter;
                if (a.walkPhase > glm::two_pi<float>()) {
                    a.walkPhase = std::fmod(a.walkPhase, glm::two_pi<float>());
                }
            }
        } else {
            // 受阻时，尽快重新选择行走方向
            a.wanderTimer = 0.0f;
        }
    }
}

// 渲染所有动物的小立方体模型
void World::renderAnimals(const Shader& shader) const {
    if (animals_.empty()) {
        return;
    }
    if (!pigMesh_ && !cowMesh_ && !sheepMesh_) {
        return;
    }

    for (const Animal& a : animals_) {
        const AnimalMesh* mesh = nullptr;
        int kind = 0;
        float maxLegAngle = glm::radians(32.0f);
        switch (a.type) {
            case AnimalType::Pig:
                mesh = pigMesh_.get();
                kind = 0;
                maxLegAngle = glm::radians(36.0f);
                break;
            case AnimalType::Cow:
                mesh = cowMesh_.get();
                kind = 1;
                maxLegAngle = glm::radians(30.0f);
                break;
            case AnimalType::Sheep:
                mesh = sheepMesh_.get();
                kind = 2;
                maxLegAngle = glm::radians(32.0f);
                break;
        }
        if (!mesh) continue;

        shader.setInt("uAnimalKind", kind);

        glm::mat4 base(1.0f);
        base = glm::translate(base, a.position);
        base = glm::rotate(base, a.yaw, glm::vec3(0.0f, 1.0f, 0.0f));

        float swing = std::sin(a.walkPhase) * maxLegAngle;
        float swingOpp = -swing;

        // body
        shader.setMat4("uModel", base * glm::translate(glm::mat4(1.0f), mesh->bodyPos));
        mesh->drawBody();
        // head
        shader.setMat4("uModel", base * glm::translate(glm::mat4(1.0f), mesh->headPos));
        mesh->drawHead();

        auto drawLeg = [&](int index, float angle) {
            glm::mat4 m = base;
            m = glm::translate(m, mesh->legPos[static_cast<size_t>(index)]);
            m = glm::rotate(m, angle, glm::vec3(1.0f, 0.0f, 0.0f));
            shader.setMat4("uModel", m);
            mesh->drawLeg();
        };

        // 0 FL, 1 FR, 2 BL, 3 BR
        drawLeg(0, swing);
        drawLeg(1, swingOpp);
        drawLeg(2, swingOpp);
        drawLeg(3, swing);
    }

    // 恢复单位矩阵，避免影响后续渲染
    shader.setMat4("uModel", glm::mat4(1.0f));
}

// growTree: 在指定位置生成树干与树冠（简单体素树）
// 参数说明见函数签名：chunk（目标 chunk），localX/localZ（chunk 局部 x/z），
// worldX/worldZ（世界 x/z，用于随机函数），groundHeight（地表高度）
void World::growTree(Chunk& chunk, int localX, int localZ, int worldX, int worldZ, int groundHeight) {
    int baseY = groundHeight + 1; // 树干从地表上方一格开始
    if (baseY + 10 >= Chunk::HEIGHT) {
        return; // 防止超出 chunk 高度范围
    }

    // 树干高度：4~6 格，保持直立
    int trunkHeight = glm::clamp(5 + static_cast<int>(std::round(gaussian01(worldX, worldZ, 5) * 2.0f)), 5, 8);

    // 直树干：仅在目标为空气/草/花/叶子时放置
    for (int i = 0; i < trunkHeight && baseY + i < Chunk::HEIGHT; ++i) {
        BlockId target = chunk.block(localX, baseY + i, localZ);
        if (target == BlockId::Air || target == BlockId::TallGrass || target == BlockId::Flower || target == BlockId::OakLeaves) {
            chunk.setBlock(localX, baseY + i, localZ, BlockId::OakLog);
        } else {
            trunkHeight = i;
            break;
        }
    }

    int topY = baseY + trunkHeight - 1;

    // 简单的“球形”树冠（按层缩小的方形近似）
    const int radius = kOakCanopyRadius;
    for (int dy = -kOakCanopyHalfHeight; dy <= kOakCanopyHalfHeight; ++dy) {
        int y = topY + dy;
        if (y < 0 || y >= Chunk::HEIGHT) continue;
        int layerRadius = radius - (dy == 0 ? 0 : 1); // 中间层稍大，上下层稍小
        for (int dx = -layerRadius; dx <= layerRadius; ++dx) {
            int x = localX + dx;
            if (x < 0 || x >= Chunk::SIZE) continue;
            for (int dz = -layerRadius; dz <= layerRadius; ++dz) {
                int z = localZ + dz;
                if (z < 0 || z >= Chunk::SIZE) continue;
                // 仅在可替换方块处放叶子，避免覆盖树干/石头等
                BlockId target = chunk.block(x, y, z);
                if (target == BlockId::Air || target == BlockId::TallGrass || target == BlockId::Flower || target == BlockId::OakLeaves) {
                    chunk.setBlock(x, y, z, BlockId::OakLeaves);
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
    if (timeOfDay_ >= 1.0f) {
        timeOfDay_ -= 1.0f;
    } else if (timeOfDay_ < 0.0f) {
        timeOfDay_ += 1.0f;
    }

    // 0.0 = 午夜，0.25 = 日出，0.5 = 正午，0.75 = 日落
    float angle = (timeOfDay_ - 0.25f) * glm::two_pi<float>();

    // 太阳在一个略微倾斜的平面上做圆弧运动（更自然的日出日落轨迹）
    glm::vec3 dir(0.25f, std::sin(angle), std::cos(angle));
    sunDir_ = glm::normalize(dir); // world->sun 方向

    // 基于太阳高度（y 分量）计算直射光与环境光
    float height = glm::clamp(sunDir_.y, -0.2f, 1.0f);

    // 直射光强度：太阳在地平线以下时迅速衰减到 0
    float direct = glm::smoothstep(0.0f, 0.25f, height);

    // 太阳颜色：地平线附近偏暖色，正午接近白色
    glm::vec3 horizonColor(0.98f, 0.72f, 0.45f);
    glm::vec3 noonColor(1.0f, 0.98f, 0.90f);
    float warmMix = glm::clamp((height + 0.2f) / 1.2f, 0.0f, 1.0f);
    sunColor_ = glm::mix(horizonColor, noonColor, warmMix) * direct;

    // 环境光：从夜晚的深蓝过渡到白天的淡蓝
    glm::vec3 nightAmbient(0.02f, 0.04f, 0.08f);
    glm::vec3 dayAmbient(0.35f, 0.43f, 0.54f);
    float ambientFactor = glm::smoothstep(-0.3f, 0.2f, height);
    ambientColor_ = glm::mix(nightAmbient, dayAmbient, ambientFactor);

    // 天空颜色：与环境光保持一致的日夜渐变
    glm::vec3 nightSky(0.01f, 0.015f, 0.03f);
    glm::vec3 daySky(0.55f, 0.72f, 0.92f);
    skyColor_ = glm::mix(nightSky, daySky, ambientFactor);

    // 雾：夜晚和太阳很低时更浓，白天更稀
    float fogDay = 0.0015f;
    float fogNight = 0.0035f;
    fogDensity_ = glm::mix(fogNight, fogDay, ambientFactor);
}
