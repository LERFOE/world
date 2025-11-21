#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

struct AtlasTexture {
    std::string key;
    std::filesystem::path file;
    float speed = 1.0f;
};

struct AtlasAnimation {
    int startIndex = -1;
    int frameCount = 1;
    float speed = 0.0f;

    bool animated() const { return frameCount > 1 && startIndex >= 0; }
};

class TextureAtlas {
public:
    TextureAtlas() = default;
    ~TextureAtlas();

    bool build(const std::vector<AtlasTexture>& textures);
    void bind(int unit = 0) const;

    glm::vec4 tileUV(int index) const;
    int tileIndex(const std::string& key) const;
    AtlasAnimation animationInfo(const std::string& key) const;

    int tileSize() const { return tileSize_; }
    unsigned int id() const { return textureId_; }
    int atlasWidth() const { return atlasWidth_; }
    int atlasHeight() const { return atlasHeight_; }

private:
    unsigned int textureId_ = 0;
    int tileSize_ = 0;
    int atlasWidth_ = 0;
    int atlasHeight_ = 0;

    std::vector<glm::vec4> uvRects_;
    std::unordered_map<std::string, int> keyToIndex_;
    std::unordered_map<std::string, AtlasAnimation> animations_;
};
