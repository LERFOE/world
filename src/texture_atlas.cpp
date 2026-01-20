#include "texture_atlas.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <glad/glad.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

TextureAtlas::~TextureAtlas() {
    if (textureId_ != 0) {
        glDeleteTextures(1, &textureId_);
    }
}

bool TextureAtlas::build(const std::vector<AtlasTexture>& textures) {
    if (textures.empty()) {
        std::cerr << "[TextureAtlas] No textures provided." << std::endl;
        return false;
    }

    stbi_set_flip_vertically_on_load(true);

    keyToIndex_.clear();
    animations_.clear();
    uvRects_.clear();

    struct LoadedTexture {
        AtlasTexture desc;
        int width = 0;
        int height = 0;
        int frames = 1;
        stbi_uc* data = nullptr;
    };

    std::vector<LoadedTexture> loaded;
    loaded.reserve(textures.size());
    auto cleanup = [&]() {
        for (auto& entry : loaded) {
            if (entry.data) {
                stbi_image_free(entry.data);
                entry.data = nullptr;
            }
        }
    };

    int totalFrames = 0;
    for (const auto& tex : textures) {
        int w = 0, h = 0, channels = 0;
        stbi_uc* data = stbi_load(tex.file.string().c_str(), &w, &h, &channels, STBI_rgb_alpha);
        if (!data) {
            std::cerr << "[TextureAtlas] Failed to load " << tex.file << std::endl;
            cleanup();
            return false;
        }

        if (tileSize_ == 0) {
            tileSize_ = w;
        }
        if (w != tileSize_) {
            std::cerr << "[TextureAtlas] Unexpected width for " << tex.file << std::endl;
            stbi_image_free(data);
            cleanup();
            return false;
        }
        if (h % tileSize_ != 0) {
            std::cerr << "[TextureAtlas] Height must be multiple of tile size for " << tex.file << std::endl;
            stbi_image_free(data);
            cleanup();
            return false;
        }

        int frames = h / tileSize_;
        if (frames <= 0) {
            frames = 1;
        }

        LoadedTexture entry;
        entry.desc = tex;
        entry.width = w;
        entry.height = h;
        entry.frames = frames;
        entry.data = data;
        loaded.push_back(entry);
        totalFrames += frames;
    }

    if (totalFrames == 0) {
        cleanup();
        return false;
    }

    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(totalFrames))));
    int rows = static_cast<int>(std::ceil(totalFrames / static_cast<float>(cols)));
    atlasWidth_ = cols * tileSize_;
    atlasHeight_ = rows * tileSize_;
    
    // Switch to GL_TEXTURE_2D_ARRAY
    if (textureId_ == 0) {
        glGenTextures(1, &textureId_);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureId_);
    
    // Allocate 3D storage: width, height, layers
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, tileSize_, tileSize_, totalFrames, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    int cursor = 0;
    uvRects_.resize(totalFrames); // Just to keep size consistent, though we won't use UV Rects for sub-regions anymore

    for (auto& entry : loaded) {
        int startIndex = cursor;
        
        // Upload each frame as a layer
        for (int frame = 0; frame < entry.frames; ++frame) {
            // Source pointer for this frame
            unsigned char* src = &entry.data[(frame * tileSize_ * entry.width) * 4];
            // Upload to layer 'cursor'
            // NOTE: stbi loads rows top-to-bottom, OpenGL expects bottom-to-top usually, 
            // but we used stbi_set_flip_vertically_on_load(true) at start of function.
            // So data is correct.
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, cursor, tileSize_, tileSize_, 1, GL_RGBA, GL_UNSIGNED_BYTE, src);
            
            // UVs are always 0..1 now because each tile is a full texture layer
            uvRects_[cursor] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            ++cursor;
        }

        keyToIndex_[entry.desc.key] = startIndex;
        AtlasAnimation anim;
        anim.startIndex = startIndex;
        anim.frameCount = entry.frames;
        if (entry.frames > 1) {
            anim.speed = entry.desc.speed > 0.0f ? entry.desc.speed : 1.0f;
        } else {
            anim.speed = 0.0f;
        }
        animations_[entry.desc.key] = anim;
        
        stbi_image_free(entry.data);
        entry.data = nullptr;
    }

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    return true;
}

void TextureAtlas::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureId_);
}

glm::vec4 TextureAtlas::tileUV(int index) const {
    if (index < 0 || index >= static_cast<int>(uvRects_.size())) {
        return glm::vec4(0.0f);
    }
    return uvRects_[index];
}

int TextureAtlas::tileIndex(const std::string& key) const {
    auto it = keyToIndex_.find(key);
    if (it == keyToIndex_.end()) {
        return -1;
    }
    return it->second;
}

AtlasAnimation TextureAtlas::animationInfo(const std::string& key) const {
    auto it = animations_.find(key);
    if (it != animations_.end()) {
        return it->second;
    }
    AtlasAnimation anim;
    auto idx = keyToIndex_.find(key);
    if (idx != keyToIndex_.end()) {
        anim.startIndex = idx->second;
    }
    return anim;
}
