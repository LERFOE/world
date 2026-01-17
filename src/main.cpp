#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "voxel_block.h"
#include "camera.h"
#include "shader.h"
#include "texture_atlas.h"
#include "world.h"

#include <stb_image.h>

namespace {
struct ResourcePaths {
    std::filesystem::path root;
    std::filesystem::path shaderDir;
    std::filesystem::path textureDir;
};

ResourcePaths locateResources() {
    std::filesystem::path current = std::filesystem::current_path();
    ResourcePaths paths;
    bool found = false;
    for (int i = 0; i < 6; ++i) {
        if (std::filesystem::exists(current / "shaders") &&
            std::filesystem::exists(current / "Faithful 64x - September 2025 Release")) {
            paths.root = current;
            found = true;
            break;
        }
        if (current.has_parent_path()) {
            current = current.parent_path();
        }
    }
    if (!found) {
        paths.root = std::filesystem::path(MYCRAFT_PROJECT_DIR);
    }
    paths.shaderDir = paths.root / "shaders";
    paths.textureDir = paths.root / "Faithful 64x - September 2025 Release/assets/minecraft/textures/block";
    return paths;
}

std::vector<AtlasTexture> buildTextureList(const ResourcePaths& paths) {
    auto tex = [&](const std::string& key, const std::string& file, float speed) {
        return AtlasTexture{key, paths.textureDir / file, speed};
    };
    return {
        tex("grass_top", "grass_block_top.png", 1.0f),
        tex("grass_side", "grass_block_side.png", 1.0f),
        tex("dirt", "dirt.png", 1.0f),
        tex("stone", "stone.png", 1.0f),
        tex("sand", "sand.png", 1.0f),
        tex("gravel", "gravel.png", 1.0f),
        tex("snow", "snow.png", 1.0f),
        tex("water", "water_still.png", 0.6f),
        tex("oak_log", "oak_log.png", 1.0f),
        tex("oak_log_top", "oak_log_top.png", 1.0f),
        tex("oak_leaves", "oak_leaves.png", 1.0f),
        tex("oak_planks", "oak_planks.png", 1.0f),
        tex("glass", "glass.png", 1.0f),
        tex("poppy", "poppy.png", 1.0f),
        tex("tall_grass", "tall_grass_top.png", 1.0f),
    };
}

std::string blockName(BlockId id) {
    switch (id) {
        case BlockId::Grass: return "Grass";
        case BlockId::Dirt: return "Dirt";
        case BlockId::Stone: return "Stone";
        case BlockId::Sand: return "Sand";
        case BlockId::Gravel: return "Gravel";
        case BlockId::Snow: return "Snow";
        case BlockId::Water: return "Water";
        case BlockId::OakLog: return "Oak Log";
        case BlockId::OakLeaves: return "Oak Leaves";
        case BlockId::OakPlanks: return "Oak Planks";
        case BlockId::Glass: return "Glass";
        case BlockId::Flower: return "Poppy";
        default: return "Air";
    }
}

struct MiningState {
    glm::ivec3 block{0};
    float progress = 0.0f;
    bool active = false;
};

struct PlayerState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    bool onGround = false;
};

constexpr int kShadowMapSize = 2048;
constexpr float kPlayerRadius = 0.3f;
constexpr float kPlayerHeight = 1.8f;
constexpr float kEyeHeight = 1.62f;
constexpr float kCollisionEps = 0.001f;

void glfwErrorCallback(int code, const char* desc) {
    std::cerr << "GLFW Error (" << code << "): " << desc << std::endl;
}

// 简单 2D 纹理加载器：为动物贴图使用，直接返回 OpenGL 纹理句柄，并可输出尺寸
GLuint loadTexture2D(const std::filesystem::path& path, int* outW = nullptr, int* outH = nullptr) {
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(true);
    stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!data) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    if (outW) *outW = w;
    if (outH) *outH = h;
    stbi_image_free(data);
    return tex;
}

// 把贴图上的一个像素矩形（以原版 64x32 为基准坐标）映射到一个面（四个角）
static World::AnimalUVLayout::Quad quadScaled(int texW, int texH, float baseX, float baseY, float baseW, float baseH) {
    if (texW <= 0) texW = 64;
    if (texH <= 0) texH = texW / 2;

    const float scale = static_cast<float>(texW) / 64.0f; // 以宽度为准，适配 16x/32x/64x 等贴图

    float x0 = baseX * scale;
    float y0 = baseY * scale;
    float x1 = (baseX + baseW) * scale;
    float y1 = (baseY + baseH) * scale;

    float u0 = x0 / static_cast<float>(texW);
    float u1 = x1 / static_cast<float>(texW);
    float vTop = 1.0f - y0 / static_cast<float>(texH);
    float vBottom = 1.0f - y1 / static_cast<float>(texH);

    return {
        {u0, vBottom}, // bl
        {u1, vBottom}, // br
        {u1, vTop},    // tr
        {u0, vTop},    // tl
    };
}

// 生成与 Minecraft ModelBox（Java 版）一致的 box UV 展开。
// 注意：由于本项目的 +X/-X 面定义与贴图“左右面”命名相反，这里按当前网格构建方式做了 left/right 对调。
static World::AnimalUVLayout::Box makeModelBoxUV(int texW, int texH, float u, float v, float w, float h, float d) {
    World::AnimalUVLayout::Box box;
    // side strip: [left][front][right][back]
    box.front  = quadScaled(texW, texH, u + d,         v + d, w, h);
    box.back   = quadScaled(texW, texH, u + d + w + d, v + d, w, h);
    // +X 使用贴图条带最左侧（与原 ModelBox 的 left 对应）
    box.right  = quadScaled(texW, texH, u,             v + d, d, h);
    box.left   = quadScaled(texW, texH, u + d + w,     v + d, d, h);
    // top/bottom
    box.top    = quadScaled(texW, texH, u + d,         v,     w, d);
    box.bottom = quadScaled(texW, texH, u + d + w,     v,     w, d);
    return box;
}

static World::AnimalUVLayout buildPigUV(int texW, int texH) {
    World::AnimalUVLayout layout;
    layout.head = makeModelBoxUV(texW, texH, 0.0f, 0.0f, 8.0f, 8.0f, 8.0f);
    layout.body = makeModelBoxUV(texW, texH, 28.0f, 8.0f, 10.0f, 8.0f, 8.0f);
    layout.leg  = makeModelBoxUV(texW, texH, 0.0f, 16.0f, 4.0f, 6.0f, 4.0f);
    return layout;
}

static World::AnimalUVLayout buildCowUV(int texW, int texH) {
    World::AnimalUVLayout layout;
    layout.head = makeModelBoxUV(texW, texH, 0.0f, 0.0f, 8.0f, 8.0f, 8.0f);
    layout.body = makeModelBoxUV(texW, texH, 18.0f, 4.0f, 12.0f, 10.0f, 8.0f);
    layout.leg  = makeModelBoxUV(texW, texH, 0.0f, 16.0f, 4.0f, 12.0f, 4.0f);
    return layout;
}

static World::AnimalUVLayout buildSheepUV(int texW, int texH) {
    World::AnimalUVLayout layout;
    layout.head = makeModelBoxUV(texW, texH, 0.0f, 0.0f, 8.0f, 8.0f, 8.0f);
    layout.body = makeModelBoxUV(texW, texH, 28.0f, 8.0f, 8.0f, 8.0f, 8.0f);
    layout.leg  = makeModelBoxUV(texW, texH, 0.0f, 16.0f, 4.0f, 12.0f, 4.0f);
    return layout;
}

glm::mat4 buildLightSpaceMatrix(const glm::vec3& center, const glm::vec3& sunDir, int renderDistance) {
    float range = static_cast<float>(renderDistance * Chunk::SIZE);
    range = glm::max(range, 64.0f);
    float distance = range * 1.5f;
    glm::vec3 lightDir = glm::normalize(-sunDir);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(lightDir, up)) > 0.95f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    glm::mat4 lightView = glm::lookAt(center - lightDir * distance, center, up);
    float nearPlane = glm::max(0.1f, distance - range);
    float farPlane = distance + range;
    glm::mat4 lightProj = glm::ortho(-range, range, -range, range, nearPlane, farPlane);
    return lightProj * lightView;
}

int floorToInt(float value) {
    return static_cast<int>(std::floor(value));
}

bool isSolidBlock(const World& world, const BlockRegistry& registry, int x, int y, int z) {
    BlockId id = world.blockAt(glm::ivec3(x, y, z));
    if (id == BlockId::Air) {
        return false;
    }
    return registry.info(id).solid;
}

bool anySolidInRange(const World& world,
                     const BlockRegistry& registry,
                     int x0,
                     int x1,
                     int y0,
                     int y1,
                     int z0,
                     int z1) {
    if (x0 > x1 || y0 > y1 || z0 > z1) {
        return false;
    }
    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (isSolidBlock(world, registry, x, y, z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool isGrounded(const World& world, const BlockRegistry& registry, const glm::vec3& pos) {
    int x0 = floorToInt(pos.x - kPlayerRadius + kCollisionEps);
    int x1 = floorToInt(pos.x + kPlayerRadius - kCollisionEps);
    int z0 = floorToInt(pos.z - kPlayerRadius + kCollisionEps);
    int z1 = floorToInt(pos.z + kPlayerRadius - kCollisionEps);
    int yCheck = floorToInt(pos.y - 0.02f);
    return anySolidInRange(world, registry, x0, x1, yCheck, yCheck, z0, z1);
}

void resolvePlayerCollisions(const World& world, const BlockRegistry& registry, PlayerState& player, float dt) {
    glm::vec3 pos = player.position;
    glm::vec3 vel = player.velocity;

    if (vel.x != 0.0f) {
        float delta = vel.x * dt;
        float nextX = pos.x + delta;
        int y0 = floorToInt(pos.y + kCollisionEps);
        int y1 = floorToInt(pos.y + kPlayerHeight - kCollisionEps);
        int z0 = floorToInt(pos.z - kPlayerRadius + kCollisionEps);
        int z1 = floorToInt(pos.z + kPlayerRadius - kCollisionEps);
        int xCheck = delta > 0.0f ? floorToInt(nextX + kPlayerRadius) : floorToInt(nextX - kPlayerRadius);
        if (anySolidInRange(world, registry, xCheck, xCheck, y0, y1, z0, z1)) {
            if (delta > 0.0f) {
                pos.x = static_cast<float>(xCheck) - kPlayerRadius - kCollisionEps;
            } else {
                pos.x = static_cast<float>(xCheck + 1) + kPlayerRadius + kCollisionEps;
            }
            vel.x = 0.0f;
        } else {
            pos.x = nextX;
        }
    }

    if (vel.z != 0.0f) {
        float delta = vel.z * dt;
        float nextZ = pos.z + delta;
        int y0 = floorToInt(pos.y + kCollisionEps);
        int y1 = floorToInt(pos.y + kPlayerHeight - kCollisionEps);
        int x0 = floorToInt(pos.x - kPlayerRadius + kCollisionEps);
        int x1 = floorToInt(pos.x + kPlayerRadius - kCollisionEps);
        int zCheck = delta > 0.0f ? floorToInt(nextZ + kPlayerRadius) : floorToInt(nextZ - kPlayerRadius);
        if (anySolidInRange(world, registry, x0, x1, y0, y1, zCheck, zCheck)) {
            if (delta > 0.0f) {
                pos.z = static_cast<float>(zCheck) - kPlayerRadius - kCollisionEps;
            } else {
                pos.z = static_cast<float>(zCheck + 1) + kPlayerRadius + kCollisionEps;
            }
            vel.z = 0.0f;
        } else {
            pos.z = nextZ;
        }
    }

    player.onGround = false;
    if (vel.y != 0.0f) {
        float delta = vel.y * dt;
        float nextY = pos.y + delta;
        int x0 = floorToInt(pos.x - kPlayerRadius + kCollisionEps);
        int x1 = floorToInt(pos.x + kPlayerRadius - kCollisionEps);
        int z0 = floorToInt(pos.z - kPlayerRadius + kCollisionEps);
        int z1 = floorToInt(pos.z + kPlayerRadius - kCollisionEps);
        int yCheck = delta > 0.0f ? floorToInt(nextY + kPlayerHeight) : floorToInt(nextY - kCollisionEps);
        if (anySolidInRange(world, registry, x0, x1, yCheck, yCheck, z0, z1)) {
            if (delta > 0.0f) {
                pos.y = static_cast<float>(yCheck) - kPlayerHeight - kCollisionEps;
            } else {
                pos.y = static_cast<float>(yCheck + 1) + kCollisionEps;
                player.onGround = true;
            }
            vel.y = 0.0f;
        } else {
            pos.y = nextY;
        }
    }

    if (vel.y <= 0.0f && !player.onGround) {
        player.onGround = isGrounded(world, registry, pos);
    }

    player.position = pos;
    player.velocity = vel;
}

} // namespace

int main() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "无法初始化 GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    const int initialWidth = 1600;
    const int initialHeight = 900;
    GLFWwindow* window = glfwCreateWindow(initialWidth, initialHeight, "mycraft", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "无法初始化 GLAD" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");

    ResourcePaths paths = locateResources();
    TextureAtlas atlas;
    if (!atlas.build(buildTextureList(paths))) {
        std::cerr << "纹理图集构建失败" << std::endl;
        return -1;
    }

    BlockRegistry registry;
    registry.build(atlas);

    Shader blockShader((paths.shaderDir / "block.vert").string(), (paths.shaderDir / "block.frag").string());
    blockShader.use();
    blockShader.setInt("uAtlas", 0);
    glm::vec2 atlasSize = glm::vec2(static_cast<float>(atlas.atlasWidth()), static_cast<float>(atlas.atlasHeight()));
    glm::vec2 atlasInvSize = glm::vec2(1.0f / atlasSize.x, 1.0f / atlasSize.y);
    blockShader.setVec2("uAtlasSize", atlasSize);
    blockShader.setVec2("uAtlasInvSize", atlasInvSize);
    blockShader.setFloat("uAtlasTileSize", static_cast<float>(atlas.tileSize()));

    // 加载 Faithful 资源包中的猪/牛/羊贴图
    std::filesystem::path entityDir = paths.root / "Faithful 64x - September 2025 Release/assets/minecraft/textures/entity";
    int pigW = 0, pigH = 0;
    int cowW = 0, cowH = 0;
    int sheepW = 0, sheepH = 0;
    GLuint pigTex = loadTexture2D(entityDir / "pig/temperate_pig.png", &pigW, &pigH);
    if (!pigTex) pigTex = loadTexture2D(entityDir / "pig/cold_pig.png", &pigW, &pigH);
    GLuint cowTex = loadTexture2D(entityDir / "cow/temperate_cow.png", &cowW, &cowH);
    if (!cowTex) cowTex = loadTexture2D(entityDir / "cow/warm_cow.png", &cowW, &cowH);
    GLuint sheepTex = loadTexture2D(entityDir / "sheep/sheep.png", &sheepW, &sheepH);
    if (!sheepTex) sheepTex = loadTexture2D(entityDir / "sheep/sheep_wool.png", &sheepW, &sheepH);
    World::AnimalUVLayout pigUV = buildPigUV(pigW, pigH);
    World::AnimalUVLayout cowUV = buildCowUV(cowW, cowH);
    World::AnimalUVLayout sheepUV = buildSheepUV(sheepW, sheepH);

    blockShader.setInt("uPigTex", 1);
    blockShader.setInt("uCowTex", 2);
    blockShader.setInt("uSheepTex", 3);
    blockShader.setInt("uShadowMap", 4);
    blockShader.setMat4("uLightSpace", glm::mat4(1.0f));

    Shader shadowShader((paths.shaderDir / "shadow.vert").string(), (paths.shaderDir / "shadow.frag").string());
    shadowShader.use();
    shadowShader.setMat4("uModel", glm::mat4(1.0f));

    GLuint shadowFbo = 0;
    GLuint shadowMap = 0;
    glGenFramebuffers(1, &shadowFbo);
    glGenTextures(1, &shadowMap);
    glBindTexture(GL_TEXTURE_2D, shadowMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, kShadowMapSize, kShadowMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Shadow framebuffer incomplete." << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    World world(atlas, registry, pigUV, cowUV, sheepUV);
    Camera camera({0.0f, 70.0f, 0.0f});
    camera.setPerspective(60.0f, static_cast<float>(initialWidth) / initialHeight, 0.1f, 1000.0f);
    PlayerState player;
    player.position = camera.position() - glm::vec3(0.0f, kEyeHeight, 0.0f);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    bool cursorCaptured = true;

    double lastTime = glfwGetTime();
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;
    bool firstMouse = true;
    bool wireframe = false;
    bool showChunkBounds = false;
    bool showClouds = true;
    bool enablePhysics = true;
    float gravity = 20.0f;
    float jumpSpeed = 7.2f;
    float walkSpeed = 4.8f;
    float sprintMultiplier = 1.6f;
    float sunIntensity = 1.0f;
    float ambientIntensity = 1.0f;
    float fogScale = 1.0f;
    float shadowStrength = 0.85f;
    float daySpeed = world.daySpeed();

    std::array<BlockId, 8> hotbar = {
        BlockId::Grass, BlockId::Dirt, BlockId::Stone, BlockId::OakLog,
        BlockId::OakPlanks, BlockId::OakLeaves, BlockId::Glass, BlockId::Water
    };
    int selectedSlot = 0;

    MiningState mining;
    bool previousRight = false;
    bool tabPressedLast = false;

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        if (dt > 0.1f) dt = 0.1f;

        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        bool tabDown = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tabDown && !tabPressedLast) {
            cursorCaptured = !cursorCaptured;
            glfwSetInputMode(window, GLFW_CURSOR, cursorCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }
        tabPressedLast = tabDown;

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        if (fbh > 0) {
            camera.setAspect(static_cast<float>(fbw) / static_cast<float>(fbh));
        }

        double cursorX = 0.0, cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        if (firstMouse) {
            lastCursorX = cursorX;
            lastCursorY = cursorY;
            firstMouse = false;
        }
        if (cursorCaptured && !io.WantCaptureMouse) {
            camera.processMouse(static_cast<float>(cursorX - lastCursorX), static_cast<float>(cursorY - lastCursorY));
        }
        lastCursorX = cursorX;
        lastCursorY = cursorY;

        if (enablePhysics) {
            glm::vec3 forward = glm::vec3(camera.forward().x, 0.0f, camera.forward().z);
            if (glm::length(forward) > 0.001f) {
                forward = glm::normalize(forward);
            } else {
                forward = glm::vec3(0.0f, 0.0f, -1.0f);
            }
            glm::vec3 right = glm::normalize(glm::vec3(camera.right().x, 0.0f, camera.right().z));
            glm::vec3 wishDir(0.0f);
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wishDir += forward;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wishDir -= forward;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wishDir += right;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wishDir -= right;
            if (glm::length(wishDir) > 0.01f) {
                wishDir = glm::normalize(wishDir);
            }

            float speed = walkSpeed;
            if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
                speed *= sprintMultiplier;
            }
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
                speed *= 0.6f;
            }

            player.velocity.x = wishDir.x * speed;
            player.velocity.z = wishDir.z * speed;
            player.velocity.y -= gravity * dt;

            if (player.onGround && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
                player.velocity.y = jumpSpeed;
            }

            resolvePlayerCollisions(world, registry, player, dt);
            camera.setPosition(player.position + glm::vec3(0.0f, kEyeHeight, 0.0f));
        } else {
            glm::vec3 moveDir(0.0f);
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += glm::vec3(camera.forward().x, 0.0f, camera.forward().z);
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= glm::vec3(camera.forward().x, 0.0f, camera.forward().z);
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += camera.right();
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= camera.right();
            if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) moveDir.y += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) moveDir.y -= 1.0f;
            if (glm::length(moveDir) > 0.01f) {
                moveDir = glm::normalize(moveDir);
            }
            float baseSpeed = 7.5f;
            if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
                baseSpeed *= 2.0f;
            }
            camera.move(moveDir, dt, baseSpeed);
            player.position = camera.position() - glm::vec3(0.0f, kEyeHeight, 0.0f);
            player.velocity = glm::vec3(0.0f);
            player.onGround = false;
        }

        for (int i = 0; i < static_cast<int>(hotbar.size()); ++i) {
            if (glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS) {
                selectedSlot = i;
            }
        }

        world.update(camera.position(), dt);

        glm::mat4 lightSpace = buildLightSpaceMatrix(camera.position(), world.sunDirection(), world.renderDistance());
        shadowShader.use();
        shadowShader.setMat4("uLightSpace", lightSpace);
        shadowShader.setMat4("uModel", glm::mat4(1.0f));

        glViewport(0, 0, kShadowMapSize, kShadowMapSize);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);
        world.render(shadowShader);
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);

        RayHit hit = world.raycast(camera.position(), camera.forward(), 8.5f);
        bool breakInput = cursorCaptured && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !io.WantCaptureMouse;
        if (breakInput && hit.hit) {
            if (!mining.active || mining.block != hit.block) {
                mining.block = hit.block;
                mining.progress = 0.0f;
            }
            mining.active = true;
            const float destroyTime = 0.35f;
            mining.progress += dt / destroyTime;
            if (mining.progress >= 1.0f) {
                world.removeBlock(mining.block);
                mining.progress = 0.0f;
                mining.active = false;
            }
        } else {
            mining.active = false;
            mining.progress = 0.0f;
        }

        bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (rightDown && !previousRight && hit.hit && !io.WantCaptureMouse) {
            glm::ivec3 place = hit.block + hit.normal;
            world.placeBlock(place, hotbar[static_cast<std::size_t>(selectedSlot)]);
        }
        previousRight = rightDown;

        glm::vec3 sky = world.skyColor();
        glClearColor(sky.r, sky.g, sky.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        blockShader.use();
        blockShader.setMat4("uModel", glm::mat4(1.0f)); // Ensure default model matrix
        glm::mat4 view = camera.viewMatrix();
        glm::mat4 proj = camera.projectionMatrix();
        blockShader.setMat4("uViewProj", proj * view);
        blockShader.setVec3("uSunDir", world.sunDirection());
        blockShader.setVec3("uSunColor", world.sunColor() * sunIntensity);
        blockShader.setVec3("uAmbient", world.ambientColor() * ambientIntensity);
        blockShader.setVec3("uEyePos", camera.position());
        blockShader.setMat4("uLightSpace", lightSpace);
        blockShader.setFloat("uFogDensity", world.fogDensity() * fogScale);
        blockShader.setVec2("uAtlasSize", glm::vec2(atlas.atlasWidth(), atlas.atlasHeight()));
        blockShader.setVec2("uAtlasInvSize", glm::vec2(1.0f / atlas.atlasWidth(), 1.0f / atlas.atlasHeight()));
        blockShader.setFloat("uAtlasTileSize", static_cast<float>(atlas.tileSize()));
        blockShader.setVec3("uTargetBlock", hit.hit ? glm::vec3(hit.block) : glm::vec3(0.0f));
        blockShader.setFloat("uTargetActive", hit.hit ? 1.0f : 0.0f);
        blockShader.setFloat("uBreakProgress", mining.active ? glm::clamp(mining.progress, 0.0f, 1.0f) : 0.0f);
        blockShader.setFloat("uTime", static_cast<float>(now));
        blockShader.setVec2("uCloudOffset", world.cloudOffset());
        blockShader.setFloat("uCloudTime", world.cloudTime());
        blockShader.setFloat("uCloudEnabled", showClouds ? 1.0f : 0.0f);
        blockShader.setFloat("uShadowStrength", shadowStrength);

        // Draw Sun first (behind everything, but we use depth test so it's fine if it's far)
        // Actually, to be safe, disable depth write for sun or just draw it far away.
        // Since it's at 400.0f and far plane is 1000.0f, it should be fine.
        world.renderSun(blockShader);

        // 绑定方块图集和动物贴图
        atlas.bind(0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, pigTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, cowTex);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, sheepTex);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, shadowMap);

        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
        world.render(blockShader);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        world.renderTransparent(blockShader);
        if (showClouds) {
            world.renderClouds(blockShader, true);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        if (showChunkBounds) {
            glLineWidth(1.5f);
            world.renderChunkBounds(blockShader);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("mycraft HUD");
        ImGui::Text("FPS: %.1f", 1.0f / std::max(dt, 0.0001f));
        ImGui::Text("相机: %.1f %.1f %.1f", camera.position().x, camera.position().y, camera.position().z);
        ImGui::Text("区块: %d", world.chunkCount());
        ImGui::Checkbox("线框模式", &wireframe);
        ImGui::Checkbox("显示Chunk边界", &showChunkBounds);
        ImGui::Checkbox("云层", &showClouds);
        ImGui::Separator();
        ImGui::Text("环境调试");
        ImGui::SliderFloat("太阳强度", &sunIntensity, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("环境光强度", &ambientIntensity, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("雾强度", &fogScale, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("阴影强度", &shadowStrength, 0.0f, 1.5f, "%.2f");
        ImGui::SliderFloat("昼夜速度", &daySpeed, 0.0f, 0.02f, "%.4f");
        world.setDaySpeed(daySpeed);
        ImGui::Separator();
        ImGui::Text("物理/移动");
        ImGui::Checkbox("物理碰撞", &enablePhysics);
        ImGui::SliderFloat("行走速度", &walkSpeed, 1.0f, 10.0f, "%.1f");
        ImGui::SliderFloat("跳跃速度", &jumpSpeed, 2.0f, 12.0f, "%.1f");
        ImGui::SliderFloat("重力", &gravity, 5.0f, 30.0f, "%.1f");
        ImGui::Text("着地: %s", player.onGround ? "是" : "否");
        ImGui::Separator();
        ImGui::Text("方块选择");
        for (int i = 0; i < static_cast<int>(hotbar.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(blockName(hotbar[static_cast<std::size_t>(i)]).c_str(), selectedSlot == i)) {
                selectedSlot = i;
            }
            ImGui::PopID();
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    if (shadowFbo) {
        glDeleteFramebuffers(1, &shadowFbo);
    }
    if (shadowMap) {
        glDeleteTextures(1, &shadowMap);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
