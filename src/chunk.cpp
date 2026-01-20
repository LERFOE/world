#include "chunk.h"

#include <array>
#include <numeric>

namespace {
constexpr glm::ivec3 faceOffsets[6] = {
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
};

constexpr glm::vec3 normals[6] = {
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
};

constexpr std::array<glm::vec3, 4> faceVertices[6] = {
    std::array<glm::vec3, 4>{glm::vec3(1, 0, 1), glm::vec3(1, 0, 0), glm::vec3(1, 1, 0), glm::vec3(1, 1, 1)}, // +X (Right)
    std::array<glm::vec3, 4>{glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 1), glm::vec3(0, 1, 0)}, // -X (Left)
    std::array<glm::vec3, 4>{glm::vec3(0, 1, 1), glm::vec3(1, 1, 1), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0)}, // +Y (Top)
    std::array<glm::vec3, 4>{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(1, 0, 1), glm::vec3(0, 0, 1)}, // -Y (Bottom)
    std::array<glm::vec3, 4>{glm::vec3(0, 0, 1), glm::vec3(1, 0, 1), glm::vec3(1, 1, 1), glm::vec3(0, 1, 1)}, // +Z (Front)
    std::array<glm::vec3, 4>{glm::vec3(1, 0, 0), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), glm::vec3(1, 1, 0)}, // -Z (Back)
};

constexpr std::array<glm::vec2, 4> baseUV = {
    glm::vec2(0.0f, 0.0f),
    glm::vec2(1.0f, 0.0f),
    glm::vec2(1.0f, 1.0f),
    glm::vec2(0.0f, 1.0f),
};

constexpr float faceLight[6] = {0.92f, 0.92f, 1.2f, 0.7f, 1.0f, 1.0f};

inline unsigned int vertexIndex(int x, int y, int z) {
    return static_cast<unsigned int>(y * Chunk::SIZE * Chunk::SIZE + z * Chunk::SIZE + x);
}

inline int vertexSign(float value) {
    return value > 0.5f ? 1 : -1;
}

float vertexAO(const glm::ivec3& blockPos,
               int face,
               int vert,
               const BlockRegistry& registry,
               const std::function<BlockId(const glm::ivec3&)>& sampler) {
    const glm::ivec3 faceOffset = faceOffsets[face];
    const glm::vec3& v = faceVertices[face][vert];
    int sx = vertexSign(v.x);
    int sy = vertexSign(v.y);
    int sz = vertexSign(v.z);

    glm::ivec3 side1(0);
    glm::ivec3 side2(0);
    if (face == 0 || face == 1) { // +/-X
        side1 = glm::ivec3(0, sy, 0);
        side2 = glm::ivec3(0, 0, sz);
    } else if (face == 2 || face == 3) { // +/-Y
        side1 = glm::ivec3(sx, 0, 0);
        side2 = glm::ivec3(0, 0, sz);
    } else { // +/-Z
        side1 = glm::ivec3(sx, 0, 0);
        side2 = glm::ivec3(0, sy, 0);
    }

    glm::ivec3 base = blockPos + faceOffset;
    bool sideOcc1 = registry.occludes(sampler(base + side1));
    bool sideOcc2 = registry.occludes(sampler(base + side2));
    bool cornerOcc = registry.occludes(sampler(base + side1 + side2));

    int occlusion = 0;
    if (sideOcc1) ++occlusion;
    if (sideOcc2) ++occlusion;
    if (cornerOcc) ++occlusion;
    if (sideOcc1 && sideOcc2) {
        occlusion = 3;
    }

    return 1.0f - static_cast<float>(occlusion) * 0.25f;
}

void addQuad(std::vector<RenderVertex>& vertices,
             std::vector<unsigned int>& indices,
             const glm::vec3& base,
             const std::array<glm::vec3, 4>& verts,
             const glm::vec3& normal,
             const glm::vec2 uv[4],
             const glm::vec3& tint,
             const std::array<float, 4>& lights,
             float material,
             float emission,
             const glm::vec3& animData) {
    unsigned int startIndex = static_cast<unsigned int>(vertices.size());
    for (int i = 0; i < 4; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        RenderVertex v{};
        v.pos = base + verts[idx];
        v.normal = normal;
        v.uv = uv[idx];
        v.color = tint + glm::vec3(emission);
        v.light = lights[idx];
        v.material = material;
        v.anim = animData;
        vertices.push_back(v);
    }
    indices.push_back(startIndex + 0);
    indices.push_back(startIndex + 1);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex + 2);
    indices.push_back(startIndex + 3);
    indices.push_back(startIndex + 0);
}

void buildBillboard(const glm::vec3& center,
                    const glm::vec3& tint,
                    float material,
                    float emission,
                    std::vector<RenderVertex>& vertices,
                    std::vector<unsigned int>& indices,
                    float tileIndex,
                    const BlockAnimation& blockAnim) {
    const glm::vec2 uvPairs[4] = {
        baseUV[0],
        baseUV[1],
        baseUV[2],
        baseUV[3],
    };

    const glm::vec3 offsets[4] = {
        {-0.5f, 0.0f, 0.0f},
        {0.5f, 0.0f, 0.0f},
        {0.5f, 1.0f, 0.0f},
        {-0.5f, 1.0f, 0.0f},
    };

    const glm::vec3 offsetsB[4] = {
        {0.0f, 0.0f, -0.5f},
        {0.0f, 0.0f, 0.5f},
        {0.0f, 1.0f, 0.5f},
        {0.0f, 1.0f, -0.5f},
    };

    auto emitQuad = [&](const glm::vec3 (&local)[4]) {
        unsigned int start = static_cast<unsigned int>(vertices.size());
        for (int i = 0; i < 4; ++i) {
            RenderVertex v{};
            v.pos = center + local[i];
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.uv = uvPairs[i];
            v.color = tint + glm::vec3(emission);
            v.light = 1.0f;
            v.material = material;
            float frames = blockAnim.frames > 0 ? static_cast<float>(blockAnim.frames) : 1.0f;
            float speed = blockAnim.frames > 1 ? blockAnim.speed : 0.0f;
            v.anim = glm::vec3(tileIndex, frames, speed);
            vertices.push_back(v);
        }
        indices.push_back(start + 0);
        indices.push_back(start + 1);
        indices.push_back(start + 2);
        indices.push_back(start + 2);
        indices.push_back(start + 3);
        indices.push_back(start + 0);
    };

    emitQuad(offsets);
    emitQuad(offsetsB);
};
}

Chunk::Chunk(ChunkCoord coord) : coord_(coord) {
    blocks_.resize(SIZE * HEIGHT * SIZE, BlockId::Air);
}

Chunk::~Chunk() {
    destroyMesh(solid_);
    destroyMesh(alpha_);
}

Chunk::Chunk(Chunk&& other) noexcept {
    *this = std::move(other);
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    coord_ = other.coord_;
    blocks_ = std::move(other.blocks_);
    dirty_ = other.dirty_;
    empty_ = other.empty_;
    solid_ = other.solid_;
    alpha_ = other.alpha_;
    other.solid_ = {};
    other.alpha_ = {};
    other.dirty_ = true;
    return *this;
}

BlockId Chunk::block(int x, int y, int z) const {
    if (x < 0 || x >= SIZE || y < 0 || y >= HEIGHT || z < 0 || z >= SIZE) {
        return BlockId::Air;
    }
    return blocks_[vertexIndex(x, y, z)];
}

void Chunk::setBlock(int x, int y, int z, BlockId id) {
    if (x < 0 || x >= SIZE || y < 0 || y >= HEIGHT || z < 0 || z >= SIZE) {
        return;
    }
    blocks_[vertexIndex(x, y, z)] = id;
    dirty_ = true;
}

// Greedy Meshing Helper Struct
struct MaskEntry {
    BlockId id;
    int normal; // Normal index for back-face culling logic if needed, or just boolean
    bool visible;
    
    // For comparing if we can merge
    bool operator==(const MaskEntry& other) const {
        return visible == other.visible && id == other.id;
    }
    
    bool operator!=(const MaskEntry& other) const {
        return !(*this == other);
    }
};

void Chunk::buildMesh(const BlockRegistry& registry,
                      const std::function<BlockId(const glm::ivec3&)>& sampler,
                      const std::function<glm::vec3(const glm::vec3&, BlockId, int)>& colorSampler) {
    std::vector<RenderVertex> solidVerts;
    std::vector<RenderVertex> alphaVerts;
    std::vector<unsigned int> solidIndices;
    std::vector<unsigned int> alphaIndices;
    solidVerts.reserve(4096);
    alphaVerts.reserve(1024);

    glm::ivec3 chunkOrigin = worldOrigin();

    // Axes mapping for 6 faces:
    // 0: +X (Right) -> Axes: Y, Z. Direction: X
    // 1: -X (Left)  -> Axes: Y, Z. Direction: X
    // 2: +Y (Top)   -> Axes: X, Z. Direction: Y
    // 3: -Y (Bottom)-> Axes: X, Z. Direction: Y
    // 4: +Z (Front) -> Axes: X, Y. Direction: Z
    // 5: -Z (Back)  -> Axes: X, Y. Direction: Z
    
    // Dimensions of the slice grid (u, v) and the sweep axis (d)
    // For vertical faces (Front, Back, Right, Left): u=SIZE (or HEIGHT?), v=SIZE/HEIGHT?
    // It's easier to handle each axis pair specifically or genericize.
    
    for (int face = 0; face < 6; ++face) {
        int uAxis, vAxis, dAxis;
        int uSize, vSize, dSize;
        
        if (face == 0 || face == 1) { // +/- X
            dAxis = 0; uAxis = 2; vAxis = 1; // Sweep X. Slice is Z-Y (or Y-Z)
            dSize = SIZE; uSize = SIZE; vSize = HEIGHT; 
        } else if (face == 2 || face == 3) { // +/- Y
            dAxis = 1; uAxis = 0; vAxis = 2; // Sweep Y. Slice is X-Z
            dSize = HEIGHT; uSize = SIZE; vSize = SIZE;
        } else { // +/- Z
            dAxis = 2; uAxis = 0; vAxis = 1; // Sweep Z. Slice is X-Y
            dSize = SIZE; uSize = SIZE; vSize = HEIGHT;
        }

        std::vector<MaskEntry> mask(uSize * vSize);

        // Sweep through the chunk along the dimension axis
        // q[0], q[1], q[2] is the cursor position. q[dAxis] = i
        int q[3] = {0, 0, 0};
        
        // Offset to check neighbor: current face normal
        glm::ivec3 faceDir = faceOffsets[face];

        for (int i = 0; i < dSize; ++i) {
            q[dAxis] = i;

            // 1. Populate Mask for this slice
            int n = 0;
            for (int v = 0; v < vSize; ++v) {
                q[vAxis] = v;
                for (int u = 0; u < uSize; ++u) {
                    q[uAxis] = u;
                    
                    BlockId id = block(q[0], q[1], q[2]);
                    bool visible = false;
                    
                    if (id != BlockId::Air) {
                        const BlockInfo& info = registry.info(id);
                        
                        // Check if face is visible (occlusion culling)
                        // If info.billboard, we skip greedy mesh for it (handled separately usually, 
                        // but here loop covers all. Billboards usually are cross models, handled separately?
                        // The original code handled billboards separately.)
                        
                        if (info.billboard) {
                            // Billboards don't participate in greedy face merging usually
                            // We can emit them immediately or ignore them here and do a separate pass?
                            // Original code did billboards in the main nested loop.
                            // We will skip billboards in the mask and add them later/separately?
                            // Or simpler: handle billboards in a separate pass entirely to keep greedy logic clean.
                            // Let's mark them invisible in mask.
                            visible = false;
                        } else {
                            glm::ivec3 neighborPos = glm::ivec3(chunkOrigin.x + q[0], q[1], chunkOrigin.z + q[2]) + faceDir;
                            BlockId neighborId = sampler(neighborPos);
                            
                            // Visibility check
                            bool occluded = registry.occludes(neighborId) && !registry.info(id).liquid;
                            visible = !occluded;
                        }
                    }

                    mask[n++] = {id, face, visible};
                }
            }

            // 2. Greedy Meshing on Mask
            n = 0;
            for (int v = 0; v < vSize; ++v) {
                for (int u = 0; u < uSize; ++u) {
                    if (mask[n].visible) {
                        // Start of a potential quad
                        BlockId id = mask[n].id;
                        int width = 1;
                        int height = 1;

                        // Compute width
                        while (u + width < uSize && mask[n + width] == mask[n]) {
                            width++;
                        }

                        // Compute height
                        bool done = false;
                        for (; v + height < vSize; ++height) {
                            for (int k = 0; k < width; ++k) {
                                if (mask[n + k + height * uSize] != mask[n]) {
                                    done = true;
                                    break;
                                }
                            }
                            if (done) break;
                        }

                        // Add Quad
                        const BlockInfo& info = registry.info(id);
                        glm::ivec3 blockPos(q[0], q[1], q[2]); // Current 'q' has q[uAxis]=u already? No, loop 'u' is local.
                        // We need to reconstruct the position of the Bottom-Left block of the quad.
                        int pos[3];
                        pos[dAxis] = i;
                        pos[uAxis] = u;
                        pos[vAxis] = v;
                        
                        glm::vec3 base = glm::vec3(chunkOrigin.x + pos[0], pos[1], chunkOrigin.z + pos[2]);
                        
                        // Quad size in world space
                        glm::vec3 wDir(0), hDir(0);
                        wDir[uAxis] = 1.0f;
                        hDir[vAxis] = 1.0f;
                        
                        std::array<glm::vec3, 4> verts;
                        // Original faceVertices matching:
                        // 0: +X -> (1,0,1), (1,0,0), (1,1,0), (1,1,1) -> Y, Z axes?
                        // Wait, my uAxis/vAxis choice must match faceVertices order or UVs will be rotated.
                        // Let's rely on standard quad generation and stretch it.
                        // Standard 'faceVertices' are 1x1.
                        // We construct 4 corners relative to 'base'.
                         
                        // Vertices order in addQuad: BL, BR, TR, TL (usually 0,1,2,3 from faceVertices)
                        // It depends on the face definition in global scope.
                        // Let's map u/v size to the specific face geometry.
                        // Face 0 (+X): Z is u? Y is v?
                        // faceVertices[0]: (1,0,1), (1,0,0), (1,1,0), (1,1,1)
                        // u=0,v=0 -> (1,0,1) ? No, that's Z=1.
                        // u=1,v=0 -> (1,0,0) ? That's Z=0. So u goes along -Z?
                        // This indicates my uAxis mapping needs to align with texture direction.
                        
                        // Simplification: Just generate the 4 standard corners for the *merged* quad
                        // by checking how faceVertices scale.
                        // 0: Right (+X). Verts: (1,0,1) (1,0,0) (1,1,0) (1,1,1).
                        //   V0(1,0,1): Bottom-Front.
                        //   V1(1,0,0): Bottom-Back.
                        //   V2(1,1,0): Top-Back.
                        //   V3(1,1,1): Top-Front.
                        //   Width (u) likely along -Z (1->0). Height (v) along +Y (0->1).
                        //   Quad width stretches Z. Quad height stretches Y.
                        
                        // We need to pass correct position to vertexAO.
                        // BL block: pos
                        // BR block: pos + (width-1)*uDir
                        // TL block: pos + (height-1)*vDir
                        // TR block: ...
                        
                        // UVs will be (0, 0), (width, 0), (width, height), (0, height)
                        // to allow tiling.
                        
                        // Construct the 4 vertices manually for the greedy quad
                        std::array<glm::vec3, 4> greedyVerts = faceVertices[face];
                        // Expand the 1x1 template to width/height
                        // We need to know which component of the vertex corresponds to U and V.
                        // Hack: check differences in faceVertices[face]
                        // abs(v1-v0), abs(v2-v0)...
                        
                        glm::vec3 uVec = glm::vec3(0);
                        glm::vec3 vVec = glm::vec3(0);
                        
                        // Determine expansion vectors based on face
                        if (face == 0 || face == 1) { // X faces. U is Z, V is Y?
                            // 0: (1,0,1)->(1,0,0) is -Z. (1,0,1)->(1,1,1) is +Y.
                            // Let's strictly follow the axis we iterated.
                            // My uAxis=2 (Z), vAxis=1 (Y).
                            // If face 0: U is -Z?
                            if (face == 0) {
                                // V0(1,0,1) -> V1(1,0,0). Z goes 1->0. U is along -Z.
                                // If width increases, we extend along -Z.
                                uVec = glm::vec3(0, 0, -1);
                                vVec = glm::vec3(0, 1, 0);
                            } else {
                                // Face 1 (-X): (0,0,0) -> (0,0,1). Z goes 0->1. U is along +Z.
                                uVec = glm::vec3(0, 0, 1);
                                vVec = glm::vec3(0, 1, 0);
                            }
                        } else if (face == 2 || face == 3) { // Y faces. U is X, V is Z?
                            if (face == 2) { // +Y
                                // (0,1,1)->(1,1,1). X goes 0->1. U is +X.
                                // (0,1,1)->(0,1,0). Z goes 1->0. V is -Z.
                                uVec = glm::vec3(1, 0, 0);
                                vVec = glm::vec3(0, 0, -1);
                            } else { // -Y
                                // (0,0,0)->(1,0,0). X goes 0->1. U is +X.
                                // (0,0,0)->(0,0,1). Z goes 0->1. V is +Z.
                                uVec = glm::vec3(1, 0, 0);
                                vVec = glm::vec3(0, 0, 1);
                            }
                        } else { // Z faces. U is X, V is Y?
                            if (face == 4) { // +Z
                                // (0,0,1)->(1,0,1). X goes 0->1. +X
                                // (0,0,1)->(0,1,1). Y goes 0->1. +Y
                                uVec = glm::vec3(1, 0, 0);
                                vVec = glm::vec3(0, 1, 0);
                            } else { // -Z
                                // (1,0,0)->(0,0,0). X goes 1->0. -X
                                uVec = glm::vec3(-1, 0, 0);
                                vVec = glm::vec3(0, 1, 0);
                            }
                        }
                        
                        // Apply expansion to the vertices (0,1,2,3 standard order: BL, BR, TR, TL)
                        // Wait, faceVertices order is weird in the original file.
                        // Let's just create the quad based on 'base' and uVec/vVec.
                        // Standard quad: 
                        // v0 = base + offset + (0,0)
                        // v1 = base + offset + (width * uVec)
                        // v2 = base + offset + (width * uVec + height * vVec)
                        // v3 = base + offset + (height * vVec)
                        // But need to respect the exact face setup for normals/winding.
                        
                        // Let's use the faceVertices template and scale it.
                        // v0 is "Origin" of the face.
                        // v1 is v0 + uDir
                        // v3 is v0 + vDir
                        // v2 is v0 + uDir + vDir
                        // Check logic for Face 0:
                        // v0(1,0,1), v1(1,0,0). D = (0,0,-1). uVec should be (0,0,-1).
                        // v3(1,1,1). D = (0,1,0). vVec should be (0,1,0).
                        // So:
                        greedyVerts[0] = faceVertices[face][0];
                        greedyVerts[1] = faceVertices[face][0] + uVec * (float)width;
                        greedyVerts[2] = faceVertices[face][0] + uVec * (float)width + vVec * (float)height;
                        greedyVerts[3] = faceVertices[face][0] + vVec * (float)height;
                        
                        // AO calculation involves checking the NEIGHBORS of the corners.
                        // We need the block coordinate of the corners.
                        // Corner 0: (chunkOrigin + pos + faceOffset) is the air block.
                        // We want the block that *casts* the shade.
                        // vertexAO uses 'blockPos' (solid block) and 'face' and 'vert'.
                        
                        // Vert 0 uses block at (pos)
                        // Vert 1 uses block at (pos + width_step)
                        // Vert 2 uses block at (pos + width_step + height_step)
                        // Vert 3 uses block at (pos + height_step)
                        
                        // BUT, uVec might be negative (chunk coord system). 
                        // E.g. Face 5 (-Z). uVec is (-1, 0, 0).
                        // If we are at x=10, width=2. We cover x=10 and x=9?
                        // No, our loop q[uAxis] goes 0..SIZE.
                        // If uVec is negative, it means the texture maps backwards along the axis, but the blocks are still contiguous in memory?
                        // Actually, the loop 'u' always increments.
                        // If Face 5 (-X direction?), 'u' goes 0..SIZE.
                        // If uVec is -1, it means texture U aligns with -X.
                        // But the blocks in the quad are at pos[uAxis], pos[uAxis]+1...
                        // So the block corresponding to "Vertex 1" (u=1) is at `pos[uAxis] + 1` relative to iteration direction?
                        // Wait, if uVec is negative, then `vertex 1` (u=1) is physically at `base - 1`.
                        // But our loop found blocks at `base, base+1, ...`
                        // So if uVec is negative, the "U=0" vertex is actually the one with higher coordinate?
                        // Let's re-verify Face 5.
                        // blocks: (0,0,0). Face -Z.
                        // v0(1,0,0), v1(0,0,0).
                        // Texture U=0 is at x=1. U=1 is at x=0.
                        // If we merge block x=1 and x=0.
                        // u=0 covers x=1. u=1 covers x=0.
                        // Loop 'u': 0..SIZE.
                        // mask[0] is x=0? No, mask index matches q[uAxis].
                        // If q[uAxis] is x.
                        // We check mask[0] (x=0), mask[1] (x=1).
                        // If we merge 0 and 1. Width=2.
                        // Face 5 vertices: V0(1,0,0).
                        // Does V0 correspond to x=0 or x=1?
                        // (1,0,0) is the corner of block at (0,0,0).
                        // So for block at x=0, V0 is at x=1.
                        // For block at x=1, V0 is at x=2.
                        // So V0 shifts by +1X for each +1 index?
                        // Yes, coordinates shift by +1.
                        // But uVec is (-1, 0, 0).
                        // So v1 = v0 + (-1,0,0) = (0,0,0). Correct.
                        // So if width=2.
                        // Greedy V1 = V0 + 2*(-1,0,0) = (-1, 0, 0).
                        // Block (0,0,0): v0=(1,0,0), v1=(0,0,0).
                        // Block (1,0,0): v0=(2,0,0), v1=(1,0,0).
                        // If we merge x=0, x=1?
                        // The quad should go from x=2 to x=-1?? No.
                        // The quad should cover x=0 and x=1. Range [0, 2] in coords.
                        // Vertices should be (2,0,0) and (0,0,0). Distance 2.
                        // Start Position:
                        // The loop iterates u. u=0 (x=0).
                        // We start at x=0.
                        // "Base" V0 for x=0 is (1,0,0).
                        // If we merge 2 blocks (x=0, x=1), we want to end at the far side of x=1.
                        // The far side of x=1 is V1 of block x=1? No, V1 is the "end" of the edge.
                        // V1 of block 0 is (0,0,0).
                        // V1 of block 1 is (1,0,0).
                        // Wait, Face 5 is -X directed U.
                        // block 0: 1->0.
                        // block 1: 2->1.
                        // Together: 2->0.
                        // So we start at "Start Block's V0"? No.
                        // Start block is u=0 (x=0) or u=1 (x=1)?
                        // Mask loop finds `mask[n]` is valid. `n` corresponds to `u`.
                        // If we find u=0 and u=1 are mergeable.
                        // We start at u=0.
                        // If uVec is negative, "Start" (U=0) corresponds to the HIGHEST coordinate in the sequence?
                        // No, texture U=0 is usually the "Start".
                        // Face 5 faceVertices: (1,0,0) is index 0. Is index 0 UV(0,0)?
                        // baseUV[0] = (0,0). Yes.
                        // So (1,0,0) corresponds to U=0.
                        // (0,0,0) corresponds to U=1.
                        // So U axis points -X.
                        // If we have blocks at x=0, x=1.
                        // The geometric union accounts for x in [0, 2].
                        // U=0 should be at x=2?
                        // U=Width should be at x=0?
                        // So we need to start the quad at the block with the "highest" X?
                        // Only if we want the texture to tile continuously across the world?
                        // The standard is: Each block has 0..1.
                        // If we greedy mesh, we want 0..Width.
                        // So we just map the geometry of the UNION.
                        // The geometry is from x=start to x=start+width.
                        // But Face 5 draws U backwards.
                        // So Quad V0 should be at x=start+width (max X).
                        // Quad V1 should be at x=start (min X).
                        // The code `v1 = v0 + width * uVec` does exactly this!
                        // v0 of "Block at u" is (u+1).
                        // If width=2. uVec=-1.
                        // v1 = (u+1) - 2 = u-1.
                        // Start block u=0. v0 at 1. v1 at -1?
                        
                        // Something is fishy with relative coords.
                        // Block vertices are relative to block origin (0,0,0).
                        // faceVertices are constants 0..1.
                        // My `base` adds block position.
                        
                        // Let's reset: faceVertices[5] is for a block at (0,0,0).
                        // V0=(1,0,0). V1=(0,0,0).
                        // Block at (10,0,0). Base=(10,0,0).
                        // V0=(11,0,0). V1=(10,0,0).
                        // Block at (11,0,0). Base=(11,0,0).
                        // V0=(12,0,0). V1=(11,0,0).
                        // Merged (10 and 11). Width=2.
                        // We want V0=(12,0,0) and V1=(10,0,0).
                        // My code:
                        // base = (10,0,0).
                        // greedyVerts[0] = base + faceVertices[0] = (11,0,0). WRONG. We need (12,0,0).
                        
                        // Issue: For negative uVec faces, "Base Block" (lowest index) is not the "UV Origin Block" (highest index).
                        // If uVec is negative, the "Origin" of the quad traverses +1 for each +1 block index, but the V0 relative to block does not.
                        
                        // Okay, SIMPLER APPROACH:
                        // Just iterate the steps for verts 0,1,2,3.
                        // Vert 0 (BL): corresponds to coordinate (u, v) in the greedy grid.
                        // If uVec is positive: Vert0 is at `u`. Vert1 is at `u + width`.
                        // If uVec is negative: Vert0 is at `u`. Vert1 is at `u + width`?
                        // Wait, negative uVec means V1 < V0.
                        // Block (0): V0=1, V1=0.
                        // Block (1): V0=2, V1=1.
                        // We want range 2..0.
                        // V0 of quad comes from Block(1). V1 of quad comes from Block(0)? NO.
                        // We want the Quad to cover the extent.
                        // V0 = Base(at u) + FaceV0 ? -> (11,0,0).
                        // V1 = Base(at u) + FaceV1 + (width-1)*Step?
                        
                        // Let's just blindly trust that correct UV mapping implies geometry follows.
                        // If I map UV (0..width), and `uVec` is geometric direction of U.
                        // Then `StartPos + Width * uVec` should be correct.
                        // `flow`:
                        // Base = (10,0,0).
                        // uVec = (-1,0,0).
                        // V0 = (11,0,0). (From template).
                        // V1 = V0 + 2 * (-1,0,0) = (9,0,0).
                        // We wanted (10,0,0). Why 9?
                        // Because V1 in template is (0,0,0). V0 is (1,0,0). Dist is 1.
                        // Formula: `V_new = V_base + (width-1) * axis_step` ??
                        
                        // Let's use the `uVec` derived from `faceVertices`.
                        // V1 - V0 = (-1, 0, 0).
                        // So `Width * uVec` scales the edge.
                        // Base V0 is (11,0,0).
                        // Target V1 is (10,0,0).
                        // V0 + Width*uVec = 11 + 2*(-1) = 9. Still 9.
                        // Because standard block checks `Base + (V1-V0)`.
                        // Our width is 2 blocks.
                        // The geometry is 2 units long.
                        // V0 is at 11? No, V0 of block 10 is at 11.
                        // V1 of block 11 is at 11.
                        // Wait, Block 10: 10..11. (Map 1->0).
                        // Block 11: 11..12. (Map 1->0).
                        // Total: 10..12. (Map 2->0).
                        // V0 needs to be at 12. V1 needs to be at 10.
                        // My calculated V0 using Block 10 base was 11. It's short by 1.
                        
                        // Conclusion: For faces with negative axes, the "Origin" block for geometry is the one with the higher index?
                        // OR, we just shift the base.
                        
                        // Actually, I can just compute the 3D bounds of the quad and pick corners.
                        // MinPos = (10,0,0). MaxPos = (12,1,1).
                        // Face 5: Back face (Z=0?). No, Face 5 is -Z? (normal -1).
                        // Bounds on X: 10..12. Y: 0..1. Z: 0.
                        // Corners are (12,0,0), (10,0,0), (10,1,0), (12,1,0).
                        // Order (0,1,2,3) depends on faceVertices.
                        // 0:(1,0,0), 1:(0,0,0)...
                        // So 0 -> MaxX, MinY.
                        // 1 -> MinX, MinY.
                        // 2 -> MinX, MaxY.
                        // 3 -> MaxX, MaxY.
                        
                        // Correct logic:
                        // Use `uVec` and `vVec` as +1/-1 step direction.
                        // `minP` = blockPos. `maxP` = blockPos + (width, height, 1).
                        // Interpolate!
                        // Or simpler:
                        // RenderVertex 0: base + faceVert[0] + (uVec > 0 ? 0 : (width-1)*uVec?)
                        // Too complex.
                        
                        // Universal way:
                        // `blockPos` is the "anchor" block (the one detected by loop, u,v).
                        // For this block, `faceVertices` gives relative corners.
                        // If we expand `width` in `uAxis`.
                        // If `faceVertices` uses `+` uAxis, we extend `+`.
                        // If `faceVertices` uses `-` uAxis, we extend `-`.
                        // In Face 5, uAxis is X, but vector is -X.
                        // So for block 10, corner 0 is at X=11.
                        // If we add block 11, we want corner 0 to move to X=12.
                        // So we ADD `(width-1) * abs(uVec)` ?
                        // 11 + 1 = 12. Correct.
                        // What about Corner 1? (0,0,0) -> X=10.
                        // If we add block 11, corner 1 stays at X=10?
                        // Yes.
                        // So: corners that are on the "positive" side of expansion get moved.
                        // Corners on "anchor" side stay.
                        
                        glm::vec3 expansionX = uVec * (float)(width - 1);
                        glm::vec3 expansionY = vVec * (float)(height - 1);
                        
                        // But wait, uVec was (-1, 0, 0).
                        // expansionX = (-1, 0, 0).
                        // Cor 0 (1,0,0) -> add expansion -> (0,0,0). Wrong. We wanted (2,0,0).
                        // So if uVec is negative, we assume "Growth" is opposite to U?
                        // Actually, just rely on this:
                        // "Growth" is always in +X/+Y/+Z direction of the CHUNK.
                        // Because we iterate u=0..size.
                        // So we are growing in +uAxis direction.
                        // We just need to check if the vertex component in that axis is 0 or 1.
                        // If it's 1, it moves. If it's 0, it stays?
                        // Face 5: V0 is (1,0,0). X=1. It moves.
                        // V1 is (0,0,0). X=0. It stays.
                        // YES.
                        // Just check the component corresponding to the axis.
                        
                        // Let's implement this generic vertex mover.

                        // Correct base for the starting block
                        glm::vec3 startBase = base; 

                        for(int k=0; k<4; ++k) {
                            glm::vec3 v = faceVertices[face][k];
                            glm::vec3 pos = startBase + v;
                            
                            // Check if we push this vertex along uAxis
                            // The vertex component is either 0 or 1.
                            // If we grow along +X.
                            // Vertices with local X=1 should move by +width-1.
                            // Vertices with local X=0 stay.
                            if (v[uAxis] > 0.5f) pos[uAxis] += (width - 1);
                            if (v[vAxis] > 0.5f) pos[vAxis] += (height - 1);
                            
                            greedyVerts[k] = pos;
                        }

                        // Calculate AO for the 4 resulting corners
                        // For this we need the BLOCK coordinate of the shading corner.
                        // V0 is at `greedyVerts[0]`.
                        // Its "block neighbor check" should be at the block containing that corner?
                        // Or reusing `vertexAO`?
                        // vertexAO(blockPos, face, v).
                        // It uses `blockPos` as reference and adds `faceOffsets` and `side1/side2`.
                        // For the merged quad, V0 might be far away from `blockPos`.
                        // We need `vertexAO( cornerBlockPos, ... )`.
                        // To find `cornerBlockPos`:
                        // It is the block in the quad closest to that vertex.
                        // If `v[uAxis] > 0.5`, we used block at `u + width - 1`.
                        // If `v[uAxis] < 0.5`, we used block at `u`.
                        
                        std::array<float, 4> lights{};
                        for(int k=0; k<4; ++k) {
                            glm::ivec3 aoBlock = glm::ivec3(q[0], q[1], q[2]); // start at anchor
                            glm::vec3 v = faceVertices[face][k];
                            
                            if (v[uAxis] > 0.5f) aoBlock[uAxis] += (width - 1);
                            if (v[vAxis] > 0.5f) aoBlock[vAxis] += (height - 1);
                            
                            // Reconstruct the full coordinate
                            glm::ivec3 worldAOBlock = chunkOrigin + aoBlock;
                            
                            lights[k] = glm::clamp(faceLight[face] * vertexAO(worldAOBlock, face, k, registry, sampler) + info.emission, 0.2f, 1.0f);
                        }

                        // UVs: (0,0), (Width, 0), (Width, Height), (0, Height)
                        // But we need to match the face winding.
                        // faceVertices[0] -> baseUV[0] (0,0)
                        // This implies standard mapping.
                        // We just scale the UVs max values.
                        // U goes to width, V goes to height.
                        glm::vec2 uvs[4];
                        for(int k=0; k<4; ++k) {
                            uvs[k] = baseUV[k];
                            // Scale 1.0 to width/height
                            if (uvs[k].x > 0.5f) uvs[k].x = (float)width;
                            if (uvs[k].y > 0.5f) uvs[k].y = (float)height;
                        }
                        
                        glm::vec3 tint = colorSampler(startBase + glm::vec3(0.5f), id, face); // Tint of first block
                        
                        std::vector<RenderVertex>& targetVerts = info.transparent || info.liquid ? alphaVerts : solidVerts;
                        std::vector<unsigned int>& targetIdx = info.transparent || info.liquid ? alphaIndices : solidIndices;
                        
                        // Animation Data
                        float frames = info.animation.frames > 0 ? static_cast<float>(info.animation.frames) : 1.0f;
                        float speed = info.animation.frames > 1 ? info.animation.speed : 0.0f;
                        float startIndex = static_cast<float>(info.faces[face]);
                        glm::vec3 animData(startIndex, frames, speed);
                        
                        // ADD QUAD
                        // Ensure we use our transformed greedyVerts
                        // Copy addQuad logic inline or modify addQuad?
                        // addQuad is local static function, I can't modify it easily without full replace.
                        // I will just push manually here.
                        
                        unsigned int vStart = static_cast<unsigned int>(targetVerts.size());
                        for (int k = 0; k < 4; ++k) {
                            RenderVertex rv{};
                            rv.pos = greedyVerts[k];
                            rv.normal = normals[face];
                            rv.uv = uvs[k];
                            rv.color = tint + glm::vec3(info.emission);
                            rv.light = lights[k];
                            rv.material = info.material;
                            rv.anim = animData;
                            targetVerts.push_back(rv);
                        }
                        targetIdx.push_back(vStart + 0);
                        targetIdx.push_back(vStart + 1);
                        targetIdx.push_back(vStart + 2);
                        targetIdx.push_back(vStart + 2);
                        targetIdx.push_back(vStart + 3);
                        targetIdx.push_back(vStart + 0);
                        
                        // Mark covered
                        for(int h=0; h<height; ++h) {
                            for(int w=0; w<width; ++w) {
                                mask[n + w + h * uSize].visible = false;
                            }
                        }
                    }
                    n++;
                }
            }
        }
    }
    
    // Also build Billboards (Cross models) - Regular naive loop for them
    // as passed over in greedy loop
     for (int y = 0; y < HEIGHT; ++y) {
        for (int z = 0; z < SIZE; ++z) {
            for (int x = 0; x < SIZE; ++x) {
                BlockId id = block(x, y, z);
                if (id == BlockId::Air) continue;
                const BlockInfo& info = registry.info(id);
                if (info.billboard) {
                     glm::ivec3 blockPos(chunkOrigin.x + x, y, chunkOrigin.z + z);
                     glm::vec3 base = glm::vec3(blockPos);
                     float tileIndex = static_cast<float>(info.faces[2]); // Use top face texture? or dedicated?
                     // Use face 2 (Top) for billboards to get biome tint if applicable
                     glm::vec3 billboardTint = colorSampler(base + glm::vec3(0.5f), id, 2);
                     buildBillboard(base + glm::vec3(0.5f, 0.0f, 0.5f),
                                   billboardTint,
                                   info.material,
                                   info.emission,
                                   alphaVerts,
                                   alphaIndices,
                                   tileIndex,
                                   info.animation);
                }
            }
        }
     }

    empty_ = solidVerts.empty() && alphaVerts.empty();
    uploadMesh(solidVerts, solidIndices, solid_);
    uploadMesh(alphaVerts, alphaIndices, alpha_);
    dirty_ = false;
}

void Chunk::renderSolid() const {
    if (!solid_.ready || solid_.indexCount == 0) {
        return;
    }
    glBindVertexArray(solid_.vao);
    glDrawElements(GL_TRIANGLES, solid_.indexCount, GL_UNSIGNED_INT, nullptr);
}

void Chunk::renderAlpha() const {
    if (!alpha_.ready || alpha_.indexCount == 0) {
        return;
    }
    glBindVertexArray(alpha_.vao);
    glDrawElements(GL_TRIANGLES, alpha_.indexCount, GL_UNSIGNED_INT, nullptr);
}

void Chunk::uploadMesh(const std::vector<RenderVertex>& vertices,
                       const std::vector<unsigned int>& indices,
                       MeshBuffers& dst) {
    if (!dst.vao) {
        glGenVertexArrays(1, &dst.vao);
        glGenBuffers(1, &dst.vbo);
        glGenBuffers(1, &dst.ebo);
    }

    glBindVertexArray(dst.vao);
    glBindBuffer(GL_ARRAY_BUFFER, dst.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(RenderVertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dst.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                 indices.data(),
                 GL_STATIC_DRAW);

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

    dst.indexCount = static_cast<GLsizei>(indices.size());
    dst.ready = true;
}

void Chunk::destroyMesh(MeshBuffers& mesh) {
    if (mesh.vao) {
        glDeleteVertexArrays(1, &mesh.vao);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteBuffers(1, &mesh.ebo);
        mesh = {};
    }
}
