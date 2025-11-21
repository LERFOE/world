#include <array>
#include <algorithm>
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

void glfwErrorCallback(int code, const char* desc) {
    std::cerr << "GLFW Error (" << code << "): " << desc << std::endl;
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

    World world(atlas, registry);
    Camera camera({0.0f, 70.0f, 0.0f});
    camera.setPerspective(60.0f, static_cast<float>(initialWidth) / initialHeight, 0.1f, 1000.0f);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    bool cursorCaptured = true;

    double lastTime = glfwGetTime();
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;
    bool firstMouse = true;
    bool wireframe = false;
    bool showChunkBounds = false;
    bool showClouds = true;

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

        for (int i = 0; i < static_cast<int>(hotbar.size()); ++i) {
            if (glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS) {
                selectedSlot = i;
            }
        }

        world.update(camera.position(), dt);

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
        blockShader.setVec3("uSunColor", world.sunColor());
        blockShader.setVec3("uAmbient", world.ambientColor());
        blockShader.setVec3("uEyePos", camera.position());
        blockShader.setFloat("uFogDensity", world.fogDensity());
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

        // Draw Sun first (behind everything, but we use depth test so it's fine if it's far)
        // Actually, to be safe, disable depth write for sun or just draw it far away.
        // Since it's at 400.0f and far plane is 1000.0f, it should be fine.
        world.renderSun(blockShader);

        atlas.bind(0);
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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
