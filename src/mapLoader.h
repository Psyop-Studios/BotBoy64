#ifndef MAP_LOADER_H
#define MAP_LOADER_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include "collision.h"
#include "collision_registry.h"
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define MAX_MAP_SEGMENTS 32
#define MAX_CACHED_MODELS 16
#define MAX_COLLISION_CHUNKS 8  // Max collision chunks per segment

// Simplified definition - just specify the model path
// Collision is auto-looked up, scale defaults to 1.0
typedef struct {
    const char* modelPath;           // ROM path (e.g., "rom:/test_map.t3dm")
    float scaleX, scaleY, scaleZ;    // Scale (default 1.0)
} MapSegmentDef;

// Model cache entry
typedef struct {
    const char* path;
    T3DModel* model;
} CachedModel;

// Runtime map segment
typedef struct {
    T3DModel* model;
    CollisionMesh* collision;           // Single collision (if not chunked)
    CollisionMesh* collisionChunks[MAX_COLLISION_CHUNKS];  // Chunked collision
    int collisionChunkCount;            // 0 = use single collision, >0 = use chunks
    float posX, posY, posZ;
    float scaleX, scaleY, scaleZ;
    float rotY;  // Extra Y rotation in radians (added to base 90 degrees)
    float collisionOffX, collisionOffZ;  // Collision offset (may differ from posX/Z for chunks)
    float collisionScaleX, collisionScaleZ;  // Collision scale (may differ from scaleX/Z for chunks)
    float width;
    float extentMin, extentMax;  // X extent after rotation (from model's Z AABB)
    // World-space 3D bounds for visibility culling (computed from model AABB or collision data)
    float worldMinX, worldMaxX;
    float worldMinY, worldMaxY;
    float worldMinZ, worldMaxZ;
    bool isChunk;  // True if this segment is a chunk (has baked transforms)
    bool active;
    bool loaded;
} MapSegment;

// Map loader state
typedef struct {
    MapSegment segments[MAX_MAP_SEGMENTS];
    int count;
    float visibilityRange;
    T3DMat4FP* matrixFP;
    int fbCount;

    // Model cache to avoid duplicate loads
    CachedModel modelCache[MAX_CACHED_MODELS];
    int cacheCount;

    // Level-specific extra rotation (for collision - visuals handle separately)
    // This is set from LevelData.mapRotY and used by collision rebuild
    float mapRotY;
} MapLoader;

// ============================================================
// MODEL CACHING
// ============================================================

static inline T3DModel* maploader_get_cached_model(MapLoader* loader, const char* path) {
    // Check if already loaded
    for (int i = 0; i < loader->cacheCount; i++) {
        if (strcmp(loader->modelCache[i].path, path) == 0) {
            return loader->modelCache[i].model;
        }
    }

    // Load new model
    if (loader->cacheCount >= MAX_CACHED_MODELS) {
        debugf("MapLoader: Model cache full!\n");
        return NULL;
    }

    T3DModel* model = t3d_model_load(path);
    if (model) {
        loader->modelCache[loader->cacheCount].path = path;
        loader->modelCache[loader->cacheCount].model = model;
        loader->cacheCount++;
        debugf("MapLoader: Loaded model %s\n", path);
    }
    return model;
}

// ============================================================
// CORE FUNCTIONS
// ============================================================

static inline void maploader_init(MapLoader* loader, int fbCount, float visibilityRange) {
    loader->count = 0;
    loader->fbCount = fbCount;
    loader->visibilityRange = visibilityRange;
    loader->matrixFP = NULL;
    loader->cacheCount = 0;
    loader->mapRotY = 0.0f;

    for (int i = 0; i < MAX_MAP_SEGMENTS; i++) {
        loader->segments[i].model = NULL;
        loader->segments[i].collision = NULL;
        loader->segments[i].collisionChunkCount = 0;
        for (int j = 0; j < MAX_COLLISION_CHUNKS; j++) {
            loader->segments[i].collisionChunks[j] = NULL;
        }
        loader->segments[i].collisionOffX = 0.0f;
        loader->segments[i].collisionOffZ = 0.0f;
        loader->segments[i].collisionScaleX = 1.0f;
        loader->segments[i].collisionScaleZ = 1.0f;
        loader->segments[i].worldMinX = -99999.0f;
        loader->segments[i].worldMaxX = 99999.0f;
        loader->segments[i].worldMinY = -99999.0f;
        loader->segments[i].worldMaxY = 99999.0f;
        loader->segments[i].worldMinZ = -99999.0f;
        loader->segments[i].worldMaxZ = 99999.0f;
        loader->segments[i].isChunk = false;
        loader->segments[i].loaded = false;
        loader->segments[i].active = false;
    }

    for (int i = 0; i < MAX_CACHED_MODELS; i++) {
        loader->modelCache[i].path = NULL;
        loader->modelCache[i].model = NULL;
    }
}

// Load maps in order - auto-align along -X axis
// Collision is automatically looked up by model name
static inline void maploader_load(MapLoader* loader, MapSegmentDef* defs, int count) {
    if (count > MAX_MAP_SEGMENTS) count = MAX_MAP_SEGMENTS;

    char nameBuffer[64];

    for (int i = 0; i < count; i++) {
        MapSegment* seg = &loader->segments[i];

        // Load model (using cache)
        seg->model = maploader_get_cached_model(loader, defs[i].modelPath);

        // Set scale
        seg->scaleX = defs[i].scaleX > 0 ? defs[i].scaleX : 1.0f;
        seg->scaleY = defs[i].scaleY > 0 ? defs[i].scaleY : 1.0f;
        seg->scaleZ = defs[i].scaleZ > 0 ? defs[i].scaleZ : 1.0f;

        // Auto-lookup collision by model name
        extract_model_name(defs[i].modelPath, nameBuffer, sizeof(nameBuffer));

        // Check if this is a chunk segment (name contains "_chunk")
        bool isChunkName = (strstr(nameBuffer, "_chunk") != NULL);

        if (isChunkName) {
            // For chunk segments, find the single matching collision chunk
            seg->collision = collision_find_single_chunk(nameBuffer);
            seg->collisionChunkCount = 0;  // Single mesh, not using chunk array
            if (seg->collision) {
                debugf("MapLoader: Found single chunk collision for %s (%d triangles)\n",
                    nameBuffer, seg->collision->count);
            } else {
                debugf("MapLoader: No chunk collision found for %s\n", nameBuffer);
            }
        } else {
            // For non-chunk segments, try chunked collision group first
            seg->collisionChunkCount = collision_find_chunks(nameBuffer, seg->collisionChunks, MAX_COLLISION_CHUNKS);

            if (seg->collisionChunkCount > 0) {
                // Using chunked collision
                seg->collision = NULL;
                int totalTris = 0;
                for (int c = 0; c < seg->collisionChunkCount; c++) {
                    totalTris += seg->collisionChunks[c]->count;
                }
                debugf("MapLoader: Found %d collision chunks for %s (%d total triangles)\n",
                    seg->collisionChunkCount, nameBuffer, totalTris);
            } else {
                // Fall back to single collision mesh
                seg->collision = collision_find(nameBuffer);
                if (seg->collision) {
                    debugf("MapLoader: Found collision for %s (%d triangles)\n",
                        nameBuffer, seg->collision->count);
                } else {
                    debugf("MapLoader: No collision found for %s\n", nameBuffer);
                }
            }
        }

        seg->loaded = true;

        // Chunks are positioned at origin since they have world-space vertices
        seg->isChunk = isChunkName;

        // Calculate extents from model AABB
        if (seg->model != NULL) {
            debugf("MapLoader: %s AABB X[%d,%d] Y[%d,%d] Z[%d,%d]\n",
                nameBuffer,
                seg->model->aabbMin[0], seg->model->aabbMax[0],
                seg->model->aabbMin[1], seg->model->aabbMax[1],
                seg->model->aabbMin[2], seg->model->aabbMax[2]);

            if (seg->isChunk) {
                // Chunks have 0° effective rotation (rotY cancels base 90°)
                // World X = model X directly (no rotation)
                seg->extentMin = seg->model->aabbMin[0] * seg->scaleX;
                seg->extentMax = seg->model->aabbMax[0] * seg->scaleX;
            } else {
                // Normal segments: 90° Y rotation - local Z becomes world -X
                seg->extentMin = -seg->model->aabbMax[2] * seg->scaleZ;
                seg->extentMax = -seg->model->aabbMin[2] * seg->scaleZ;
            }
            seg->width = seg->extentMax - seg->extentMin;
            debugf("MapLoader: %s extents [%.1f, %.1f] width=%.1f\n",
                nameBuffer, seg->extentMin, seg->extentMax, seg->width);
        } else {
            seg->extentMin = -50.0f;
            seg->extentMax = 50.0f;
            seg->width = 100.0f;
        }

        // Position segment so edges align properly
        if (seg->isChunk) {
            // Chunks are rendered at origin (no offset needed)
            seg->posX = 0.0f;
            seg->posY = 0.0f;
            seg->posZ = 0.0f;
            seg->rotY = 0.0f;  // No extra rotation beyond base 90°

            // Chunk world bounds will be computed in maploader_rebuild_collision_grids
            // after the rotation is known (from loader->mapRotY)
            debugf("MapLoader: [%d] %s is a chunk\n", i, nameBuffer);
        } else if (i == 0) {
            // First segment: position so right edge is at X=0
            seg->posX = -seg->extentMax;
            debugf("MapLoader: [%d] %s first segment, posX = -extentMax = %.1f\n",
                i, nameBuffer, seg->posX);
        } else {
            // Subsequent segments: right edge touches previous left edge
            MapSegment* prev = &loader->segments[i - 1];
            float prevLeftEdge = prev->posX + prev->extentMin;
            seg->posX = prevLeftEdge - seg->extentMax;
            debugf("MapLoader: [%d] %s prev[%d] leftEdge=%.1f, posX = %.1f - %.1f = %.1f\n",
                i, nameBuffer, i-1, prevLeftEdge, prevLeftEdge, seg->extentMax, seg->posX);
        }
        if (!seg->isChunk) {
            seg->posY = 0.0f;
            seg->posZ = 0.0f;
        }

        debugf("MapLoader: [%d] %s FINAL pos=(%.1f, %.1f, %.1f) edges=[%.1f, %.1f]\n",
            i, nameBuffer, seg->posX, seg->posY, seg->posZ,
            seg->posX + seg->extentMin, seg->posX + seg->extentMax);

        // Build spatial grid now that we know the final position
        // All segments use 90° rotation to match T3D coordinate system
        float collisionRotY = T3D_DEG_TO_RAD(90.0f);

        // Set collision offset and scale - same as visual position
        seg->collisionOffX = seg->posX;
        seg->collisionOffZ = seg->posZ;
        seg->collisionScaleX = seg->scaleX;
        seg->collisionScaleZ = seg->scaleZ;

        if (seg->collisionChunkCount > 0) {
            // Build grid for each chunk - use collision scale, not visual scale
            for (int c = 0; c < seg->collisionChunkCount; c++) {
                if (seg->collisionChunks[c] && seg->collisionChunks[c]->count > 0) {
                    collision_build_spatial_grid(seg->collisionChunks[c], seg->collisionOffX, seg->collisionOffZ, seg->collisionScaleX, seg->collisionScaleZ, collisionRotY);
                }
            }
            debugf("MapLoader: Built spatial grids for %d chunks with rot=%.1f deg, offset=%.1f, scale=%.2f\n", seg->collisionChunkCount, collisionRotY * 180.0f / 3.14159f, seg->collisionOffX, seg->collisionScaleX);
        } else if (seg->collision && seg->collision->count > 0) {
            collision_build_spatial_grid(seg->collision, seg->collisionOffX, seg->collisionOffZ, seg->collisionScaleX, seg->collisionScaleZ, collisionRotY);
            debugf("MapLoader: Built spatial grid for %s (%d tris)\n", nameBuffer, seg->collision->count);
        }
    }

    loader->count = count;

    // Allocate matrices
    if (loader->matrixFP != NULL) {
        free_uncached(loader->matrixFP);
    }
    loader->matrixFP = malloc_uncached(sizeof(T3DMat4FP) * loader->fbCount * count);
}

// Convenience: load with default scale (1.0)
static inline void maploader_load_simple(MapLoader* loader, const char* const* modelPaths, int count) {
    MapSegmentDef defs[MAX_MAP_SEGMENTS];
    for (int i = 0; i < count && i < MAX_MAP_SEGMENTS; i++) {
        defs[i].modelPath = modelPaths[i];
        defs[i].scaleX = 1.0f;
        defs[i].scaleY = 1.0f;
        defs[i].scaleZ = 1.0f;
    }
    maploader_load(loader, defs, count);
}

// Rebuild spatial grids with correct rotation (call after setting loader->mapRotY)
// Uses loader->mapRotY as the extra rotation (set by level_load from LevelData.mapRotY)
// Also computes world-space 3D bounds for visibility culling (for non-chunk segments)
static inline void maploader_rebuild_collision_grids(MapLoader* loader) {
    // Collision data is always in original model coordinates (not rotated)
    // so it needs the base 90° rotation plus any level-specific extra rotation
    float collisionRotY = T3D_DEG_TO_RAD(90.0f) + loader->mapRotY;
    float cosR = fast_cos(collisionRotY);
    float sinR = fast_sin(collisionRotY);

    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];

        // For chunk segments, compute world bounds from MODEL AABB (not collision)
        // This is for VISUAL culling, so use the same transform as rendering
        if (seg->isChunk) {
            // Build collision grid if we have collision data
            if (seg->collision && seg->collision->count > 0) {
                collision_build_spatial_grid(seg->collision, seg->collisionOffX, seg->collisionOffZ,
                    seg->collisionScaleX, seg->collisionScaleZ, collisionRotY);
            }

            // Compute visibility bounds from model AABB using VISUAL transform
            // This matches what maploader_draw does: totalRotY = 90° + seg->rotY
            // For chunks with level rotation (mapRotY), total is 90° + mapRotY
            if (seg->model) {
                // Model AABB in local coordinates
                float aabbMinX = seg->model->aabbMin[0] * seg->scaleX;
                float aabbMaxX = seg->model->aabbMax[0] * seg->scaleX;
                float aabbMinY = seg->model->aabbMin[1] * seg->scaleY;
                float aabbMaxY = seg->model->aabbMax[1] * seg->scaleY;
                float aabbMinZ = seg->model->aabbMin[2] * seg->scaleZ;
                float aabbMaxZ = seg->model->aabbMax[2] * seg->scaleZ;

                // Use same rotation as collision (which includes level-specific mapRotY)
                // This ensures visibility bounds match the actual rendered position
                // Apply rotation to all 4 corners of the XZ bounding box
                seg->worldMinX = 99999.0f;
                seg->worldMaxX = -99999.0f;
                seg->worldMinZ = 99999.0f;
                seg->worldMaxZ = -99999.0f;

                // Corner 0: minX, minZ
                float rx = aabbMinX * cosR - aabbMinZ * sinR + seg->posX;
                float rz = aabbMinX * sinR + aabbMinZ * cosR + seg->posZ;
                if (rx < seg->worldMinX) seg->worldMinX = rx;
                if (rx > seg->worldMaxX) seg->worldMaxX = rx;
                if (rz < seg->worldMinZ) seg->worldMinZ = rz;
                if (rz > seg->worldMaxZ) seg->worldMaxZ = rz;

                // Corner 1: minX, maxZ
                rx = aabbMinX * cosR - aabbMaxZ * sinR + seg->posX;
                rz = aabbMinX * sinR + aabbMaxZ * cosR + seg->posZ;
                if (rx < seg->worldMinX) seg->worldMinX = rx;
                if (rx > seg->worldMaxX) seg->worldMaxX = rx;
                if (rz < seg->worldMinZ) seg->worldMinZ = rz;
                if (rz > seg->worldMaxZ) seg->worldMaxZ = rz;

                // Corner 2: maxX, minZ
                rx = aabbMaxX * cosR - aabbMinZ * sinR + seg->posX;
                rz = aabbMaxX * sinR + aabbMinZ * cosR + seg->posZ;
                if (rx < seg->worldMinX) seg->worldMinX = rx;
                if (rx > seg->worldMaxX) seg->worldMaxX = rx;
                if (rz < seg->worldMinZ) seg->worldMinZ = rz;
                if (rz > seg->worldMaxZ) seg->worldMaxZ = rz;

                // Corner 3: maxX, maxZ
                rx = aabbMaxX * cosR - aabbMaxZ * sinR + seg->posX;
                rz = aabbMaxX * sinR + aabbMaxZ * cosR + seg->posZ;
                if (rx < seg->worldMinX) seg->worldMinX = rx;
                if (rx > seg->worldMaxX) seg->worldMaxX = rx;
                if (rz < seg->worldMinZ) seg->worldMinZ = rz;
                if (rz > seg->worldMaxZ) seg->worldMaxZ = rz;

                seg->worldMinY = aabbMinY + seg->posY;
                seg->worldMaxY = aabbMaxY + seg->posY;

                debugf("Chunk %d bounds (from model AABB, rot=%.0f°): X[%.0f,%.0f] Z[%.0f,%.0f]\n",
                    i, collisionRotY * 180.0f / 3.14159f, seg->worldMinX, seg->worldMaxX, seg->worldMinZ, seg->worldMaxZ);
            } else {
                // No model - use infinite bounds (always visible)
                seg->worldMinX = -99999.0f;
                seg->worldMaxX = 99999.0f;
                seg->worldMinY = -99999.0f;
                seg->worldMaxY = 99999.0f;
                seg->worldMinZ = -99999.0f;
                seg->worldMaxZ = 99999.0f;

                debugf("Chunk %d bounds: no model, always visible\n", i);
            }
            continue;
        }

        // Reset world bounds for non-chunk segments (will be computed from collision data)
        seg->worldMinX = 99999.0f;
        seg->worldMaxX = -99999.0f;
        seg->worldMinY = 99999.0f;
        seg->worldMaxY = -99999.0f;
        seg->worldMinZ = 99999.0f;
        seg->worldMaxZ = -99999.0f;

        if (seg->collisionChunkCount > 0) {
            // Rebuild grid for each chunk - use collision scale, not visual scale
            for (int c = 0; c < seg->collisionChunkCount; c++) {
                CollisionMesh* chunk = seg->collisionChunks[c];
                if (chunk && chunk->count > 0) {
                    collision_build_spatial_grid(chunk, seg->collisionOffX, seg->collisionOffZ, seg->collisionScaleX, seg->collisionScaleZ, collisionRotY);

                    // Compute world 3D bounds from this chunk's collision data
                    for (int t = 0; t < chunk->count; t++) {
                        CollisionTriangle* tri = &chunk->triangles[t];
                        // Apply rotation and offset to get world coordinates
                        float x0 = (tri->x0 * cosR - tri->z0 * sinR) * seg->collisionScaleX + seg->collisionOffX;
                        float y0 = tri->y0 * seg->scaleY + seg->posY;
                        float z0 = (tri->x0 * sinR + tri->z0 * cosR) * seg->collisionScaleZ + seg->collisionOffZ;
                        float x1 = (tri->x1 * cosR - tri->z1 * sinR) * seg->collisionScaleX + seg->collisionOffX;
                        float y1 = tri->y1 * seg->scaleY + seg->posY;
                        float z1 = (tri->x1 * sinR + tri->z1 * cosR) * seg->collisionScaleZ + seg->collisionOffZ;
                        float x2 = (tri->x2 * cosR - tri->z2 * sinR) * seg->collisionScaleX + seg->collisionOffX;
                        float y2 = tri->y2 * seg->scaleY + seg->posY;
                        float z2 = (tri->x2 * sinR + tri->z2 * cosR) * seg->collisionScaleZ + seg->collisionOffZ;

                        if (x0 < seg->worldMinX) seg->worldMinX = x0;
                        if (x0 > seg->worldMaxX) seg->worldMaxX = x0;
                        if (x1 < seg->worldMinX) seg->worldMinX = x1;
                        if (x1 > seg->worldMaxX) seg->worldMaxX = x1;
                        if (x2 < seg->worldMinX) seg->worldMinX = x2;
                        if (x2 > seg->worldMaxX) seg->worldMaxX = x2;

                        if (y0 < seg->worldMinY) seg->worldMinY = y0;
                        if (y0 > seg->worldMaxY) seg->worldMaxY = y0;
                        if (y1 < seg->worldMinY) seg->worldMinY = y1;
                        if (y1 > seg->worldMaxY) seg->worldMaxY = y1;
                        if (y2 < seg->worldMinY) seg->worldMinY = y2;
                        if (y2 > seg->worldMaxY) seg->worldMaxY = y2;

                        if (z0 < seg->worldMinZ) seg->worldMinZ = z0;
                        if (z0 > seg->worldMaxZ) seg->worldMaxZ = z0;
                        if (z1 < seg->worldMinZ) seg->worldMinZ = z1;
                        if (z1 > seg->worldMaxZ) seg->worldMaxZ = z1;
                        if (z2 < seg->worldMinZ) seg->worldMinZ = z2;
                        if (z2 > seg->worldMaxZ) seg->worldMaxZ = z2;
                    }
                }
            }
            debugf("MapLoader: Segment %d rebuilt %d chunks, bounds X[%.0f,%.0f] Z[%.0f,%.0f]\n",
                i, seg->collisionChunkCount, seg->worldMinX, seg->worldMaxX, seg->worldMinZ, seg->worldMaxZ);
        } else if (seg->collision && seg->collision->count > 0) {
            collision_build_spatial_grid(seg->collision, seg->collisionOffX, seg->collisionOffZ, seg->collisionScaleX, seg->collisionScaleZ, collisionRotY);

            // Compute world 3D bounds from collision data
            for (int t = 0; t < seg->collision->count; t++) {
                CollisionTriangle* tri = &seg->collision->triangles[t];
                float x0 = (tri->x0 * cosR - tri->z0 * sinR) * seg->collisionScaleX + seg->collisionOffX;
                float y0 = tri->y0 * seg->scaleY + seg->posY;
                float z0 = (tri->x0 * sinR + tri->z0 * cosR) * seg->collisionScaleZ + seg->collisionOffZ;
                float x1 = (tri->x1 * cosR - tri->z1 * sinR) * seg->collisionScaleX + seg->collisionOffX;
                float y1 = tri->y1 * seg->scaleY + seg->posY;
                float z1 = (tri->x1 * sinR + tri->z1 * cosR) * seg->collisionScaleZ + seg->collisionOffZ;
                float x2 = (tri->x2 * cosR - tri->z2 * sinR) * seg->collisionScaleX + seg->collisionOffX;
                float y2 = tri->y2 * seg->scaleY + seg->posY;
                float z2 = (tri->x2 * sinR + tri->z2 * cosR) * seg->collisionScaleZ + seg->collisionOffZ;

                if (x0 < seg->worldMinX) seg->worldMinX = x0;
                if (x0 > seg->worldMaxX) seg->worldMaxX = x0;
                if (x1 < seg->worldMinX) seg->worldMinX = x1;
                if (x1 > seg->worldMaxX) seg->worldMaxX = x1;
                if (x2 < seg->worldMinX) seg->worldMinX = x2;
                if (x2 > seg->worldMaxX) seg->worldMaxX = x2;

                if (y0 < seg->worldMinY) seg->worldMinY = y0;
                if (y0 > seg->worldMaxY) seg->worldMaxY = y0;
                if (y1 < seg->worldMinY) seg->worldMinY = y1;
                if (y1 > seg->worldMaxY) seg->worldMaxY = y1;
                if (y2 < seg->worldMinY) seg->worldMinY = y2;
                if (y2 > seg->worldMaxY) seg->worldMaxY = y2;

                if (z0 < seg->worldMinZ) seg->worldMinZ = z0;
                if (z0 > seg->worldMaxZ) seg->worldMaxZ = z0;
                if (z1 < seg->worldMinZ) seg->worldMinZ = z1;
                if (z1 > seg->worldMaxZ) seg->worldMaxZ = z1;
                if (z2 < seg->worldMinZ) seg->worldMinZ = z2;
                if (z2 > seg->worldMaxZ) seg->worldMaxZ = z2;
            }
            debugf("MapLoader: Segment %d rebuilt, bounds X[%.0f,%.0f] Z[%.0f,%.0f]\n",
                i, seg->worldMinX, seg->worldMaxX, seg->worldMinZ, seg->worldMaxZ);
        }
    }
}

// Update visibility based on player X and Z position
// Checks if player is within the chunk's XZ bounds (with margin)
static inline void maploader_update_visibility(MapLoader* loader, float checkX, float checkZ) {
    float visMargin = loader->visibilityRange;

    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (!seg->loaded) continue;

        bool wasActive = seg->active;

        // Check both X and Z bounds - chunk is active if player is within both
        bool inX = (checkX >= seg->worldMinX - visMargin && checkX <= seg->worldMaxX + visMargin);
        bool inZ = (checkZ >= seg->worldMinZ - visMargin && checkZ <= seg->worldMaxZ + visMargin);

        seg->active = (inX && inZ);

        if (seg->active != wasActive) {
            if (seg->active) {
                debugf("Chunk %d LOADED (pos: %.0f,%.0f in X[%.0f,%.0f] Z[%.0f,%.0f])\n",
                    i, checkX, checkZ, seg->worldMinX, seg->worldMaxX,
                    seg->worldMinZ, seg->worldMaxZ);
            } else {
                debugf("Chunk %d UNLOADED (pos: %.0f,%.0f outside X[%.0f,%.0f] Z[%.0f,%.0f])\n",
                    i, checkX, checkZ, seg->worldMinX, seg->worldMaxX,
                    seg->worldMinZ, seg->worldMaxZ);
            }
        }
    }
}

// Update visibility for multiple player positions (multiplayer)
// Chunk stays active if ANY player is within range
static inline void maploader_update_visibility_multi(MapLoader* loader,
    float* checkXs, float* checkZs, int numPlayers) {
    float visMargin = loader->visibilityRange;

    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (!seg->loaded) continue;

        bool wasActive = seg->active;
        bool anyPlayerInRange = false;

        // Check if ANY player is within this chunk's bounds
        for (int p = 0; p < numPlayers; p++) {
            float checkX = checkXs[p];
            float checkZ = checkZs[p];
            bool inX = (checkX >= seg->worldMinX - visMargin && checkX <= seg->worldMaxX + visMargin);
            bool inZ = (checkZ >= seg->worldMinZ - visMargin && checkZ <= seg->worldMaxZ + visMargin);
            if (inX && inZ) {
                anyPlayerInRange = true;
                break;
            }
        }

        seg->active = anyPlayerInRange;

        if (seg->active != wasActive) {
            if (seg->active) {
                debugf("Chunk %d LOADED (multiplayer)\n", i);
            } else {
                debugf("Chunk %d UNLOADED (multiplayer)\n", i);
            }
        }
    }
}

// Helper: Check if segment has any collision (single or chunks)
static inline bool maploader_segment_has_collision(MapSegment* seg) {
    if (seg->collisionChunkCount > 0) return true;
    return (seg->collision != NULL && seg->collision->count > 0);
}

// Get ground height at position (with automatic rotation to match visuals)
static inline float maploader_get_ground_height(MapLoader* loader, float px, float py, float pz) {
    float bestY = -9999.0f;

    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (!seg->active || !maploader_segment_has_collision(seg)) continue;

        // Check collision chunks if available - use collision scale, not visual scale
        if (seg->collisionChunkCount > 0) {
            for (int c = 0; c < seg->collisionChunkCount; c++) {
                CollisionMesh* chunk = seg->collisionChunks[c];
                if (!chunk || chunk->count == 0 || !chunk->triangles) continue;

                // Early-out: skip chunks whose spatial grid doesn't contain the player
                if (chunk->grid.built) {
                    SpatialGrid* grid = &chunk->grid;
                    float gridMaxX = grid->gridMinX + grid->cellSizeX * SPATIAL_GRID_WIDTH;
                    float gridMaxZ = grid->gridMinZ + grid->cellSizeZ * SPATIAL_GRID_DEPTH;
                    if (px < grid->gridMinX || px > gridMaxX ||
                        pz < grid->gridMinZ || pz > gridMaxZ) {
                        continue;
                    }
                }

                float groundY = collision_get_ground_height_scaled(
                    chunk, px, py, pz,
                    seg->collisionOffX, seg->posY, seg->collisionOffZ,
                    seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ
                );
                if (groundY > bestY) bestY = groundY;
            }
        } else if (seg->collision) {
            // Early-out for single collision mesh
            CollisionMesh* mesh = seg->collision;
            if (mesh->grid.built) {
                SpatialGrid* grid = &mesh->grid;
                float gridMaxX = grid->gridMinX + grid->cellSizeX * SPATIAL_GRID_WIDTH;
                float gridMaxZ = grid->gridMinZ + grid->cellSizeZ * SPATIAL_GRID_DEPTH;
                if (px < grid->gridMinX || px > gridMaxX ||
                    pz < grid->gridMinZ || pz > gridMaxZ) {
                    continue;
                }
            }

            float groundY = collision_get_ground_height_scaled(
                mesh, px, py, pz,
                seg->collisionOffX, seg->posY, seg->collisionOffZ,
                seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ
            );
            if (groundY > bestY) bestY = groundY;
        }
    }

    return bestY;
}

// Get ceiling height at position (for head bonking)
// Returns INVALID_CEILING_Y if no ceiling found
static inline float maploader_get_ceiling_height(MapLoader* loader, float px, float py, float pz) {
    float bestY = INVALID_CEILING_Y;

    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (!seg->active || !maploader_segment_has_collision(seg)) continue;

        // Check collision chunks if available
        // NOTE: We don't use spatial grid early-out for ceiling because the grid was
        // built for floor XZ projection, and angled ceilings may not align with it
        if (seg->collisionChunkCount > 0) {
            for (int c = 0; c < seg->collisionChunkCount; c++) {
                CollisionMesh* chunk = seg->collisionChunks[c];
                if (!chunk || chunk->count == 0 || !chunk->triangles) continue;

                float ceilingY = collision_get_ceiling_height_scaled(
                    chunk, px, py, pz,
                    seg->collisionOffX, seg->posY, seg->collisionOffZ,
                    seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ
                );
                if (ceilingY < bestY) bestY = ceilingY;  // Lower ceiling is closer
            }
        } else if (seg->collision) {
            float ceilingY = collision_get_ceiling_height_scaled(
                seg->collision, px, py, pz,
                seg->collisionOffX, seg->posY, seg->collisionOffZ,
                seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ
            );
            if (ceilingY < bestY) bestY = ceilingY;  // Lower ceiling is closer
        }
    }

    return bestY;
}

// Get ground height AND normal at position (for shadow projection)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
static inline float maploader_get_ground_height_normal(MapLoader* loader, float px, float py, float pz,
    float* outNX, float* outNY, float* outNZ) {
    float bestY = -9999.0f;
    *outNX = 0.0f; *outNY = 1.0f; *outNZ = 0.0f;  // Default: up

    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (!seg->active || !maploader_segment_has_collision(seg)) continue;

        // Check collision chunks if available - use collision scale, not visual scale
        if (seg->collisionChunkCount > 0) {
            for (int c = 0; c < seg->collisionChunkCount; c++) {
                CollisionMesh* chunk = seg->collisionChunks[c];
                if (!chunk || chunk->count == 0 || !chunk->triangles) continue;

                // Early-out: skip chunks whose spatial grid doesn't contain the player
                // This is the key optimization - avoids calling collision_get_ground_height_normal
                // on chunks that can't possibly contain the player's position
                if (chunk->grid.built) {
                    SpatialGrid* grid = &chunk->grid;
                    float gridMaxX = grid->gridMinX + grid->cellSizeX * SPATIAL_GRID_WIDTH;
                    float gridMaxZ = grid->gridMinZ + grid->cellSizeZ * SPATIAL_GRID_DEPTH;
                    // Skip if player is completely outside this chunk's grid bounds
                    if (px < grid->gridMinX || px > gridMaxX ||
                        pz < grid->gridMinZ || pz > gridMaxZ) {
                        continue;
                    }
                }

                float nx, ny, nz;
                float groundY = collision_get_ground_height_normal(
                    chunk, px, py, pz,
                    seg->collisionOffX, seg->posY, seg->collisionOffZ,
                    seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ,
                    &nx, &ny, &nz
                );
                if (groundY > bestY) {
                    bestY = groundY;
                    *outNX = nx; *outNY = ny; *outNZ = nz;
                }
            }
        } else if (seg->collision) {
            // Early-out for single collision mesh too
            CollisionMesh* mesh = seg->collision;
            if (mesh->grid.built) {
                SpatialGrid* grid = &mesh->grid;
                float gridMaxX = grid->gridMinX + grid->cellSizeX * SPATIAL_GRID_WIDTH;
                float gridMaxZ = grid->gridMinZ + grid->cellSizeZ * SPATIAL_GRID_DEPTH;
                if (px < grid->gridMinX || px > gridMaxX ||
                    pz < grid->gridMinZ || pz > gridMaxZ) {
                    continue;
                }
            }

            float nx, ny, nz;
            float groundY = collision_get_ground_height_normal(
                mesh, px, py, pz,
                seg->collisionOffX, seg->posY, seg->collisionOffZ,
                seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ,
                &nx, &ny, &nz
            );
            if (groundY > bestY) {
                bestY = groundY;
                *outNX = nx; *outNY = ny; *outNZ = nz;
            }
        }
    }

    return bestY;
}
#pragma GCC diagnostic pop

// Check wall collision (extended version with ground height for slope handling)
static inline bool maploader_check_walls_ex(MapLoader* loader,
    float px, float py, float pz, float radius, float playerHeight,
    float* outPushX, float* outPushZ, float playerGroundY) {

    *outPushX = 0.0f;
    *outPushZ = 0.0f;
    bool collided = false;

    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (!seg->active || !maploader_segment_has_collision(seg)) continue;

        // Check collision chunks if available - use collision scale, not visual scale
        if (seg->collisionChunkCount > 0) {
            for (int c = 0; c < seg->collisionChunkCount; c++) {
                CollisionMesh* chunk = seg->collisionChunks[c];
                if (!chunk || chunk->count == 0 || !chunk->triangles) continue;

                // Early-out: skip chunks whose spatial grid doesn't contain the player
                // Use radius as margin for wall collision
                if (chunk->grid.built) {
                    SpatialGrid* grid = &chunk->grid;
                    float gridMaxX = grid->gridMinX + grid->cellSizeX * SPATIAL_GRID_WIDTH;
                    float gridMaxZ = grid->gridMinZ + grid->cellSizeZ * SPATIAL_GRID_DEPTH;
                    if (px < grid->gridMinX - radius || px > gridMaxX + radius ||
                        pz < grid->gridMinZ - radius || pz > gridMaxZ + radius) {
                        continue;
                    }
                }

                float pushX = 0.0f, pushZ = 0.0f;
                if (collision_check_walls_ex(chunk,
                    px, py, pz, radius, playerHeight,
                    seg->collisionOffX, seg->posY, seg->collisionOffZ,
                    seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ,
                    &pushX, &pushZ, playerGroundY)) {
                    *outPushX += pushX;
                    *outPushZ += pushZ;
                    collided = true;
                }
            }
        } else if (seg->collision) {
            // Early-out for single collision mesh
            CollisionMesh* mesh = seg->collision;
            if (mesh->grid.built) {
                SpatialGrid* grid = &mesh->grid;
                float gridMaxX = grid->gridMinX + grid->cellSizeX * SPATIAL_GRID_WIDTH;
                float gridMaxZ = grid->gridMinZ + grid->cellSizeZ * SPATIAL_GRID_DEPTH;
                if (px < grid->gridMinX - radius || px > gridMaxX + radius ||
                    pz < grid->gridMinZ - radius || pz > gridMaxZ + radius) {
                    continue;
                }
            }

            float pushX = 0.0f, pushZ = 0.0f;
            if (collision_check_walls_ex(mesh,
                px, py, pz, radius, playerHeight,
                seg->collisionOffX, seg->posY, seg->collisionOffZ,
                seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ,
                &pushX, &pushZ, playerGroundY)) {
                *outPushX += pushX;
                *outPushZ += pushZ;
                collided = true;
            }
        }
    }

    return collided;
}

// Check wall collision (backwards compatible - no ground height)
static inline bool maploader_check_walls(MapLoader* loader,
    float px, float py, float pz, float radius, float playerHeight,
    float* outPushX, float* outPushZ) {
    return maploader_check_walls_ex(loader, px, py, pz, radius, playerHeight,
        outPushX, outPushZ, INVALID_GROUND_Y);
}

// Check if line of sight is blocked by map geometry (for decal occlusion)
static inline bool maploader_raycast_blocked(MapLoader* loader,
    float fromX, float fromY, float fromZ,
    float toX, float toY, float toZ) {

    // Total collision rotation = base 90° + level's mapRotY
    float collisionRotY = T3D_DEG_TO_RAD(90.0f) + loader->mapRotY;

    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (!seg->active || !maploader_segment_has_collision(seg)) continue;

        // Check collision chunks if available - use collision scale, not visual scale
        if (seg->collisionChunkCount > 0) {
            for (int c = 0; c < seg->collisionChunkCount; c++) {
                CollisionMesh* chunk = seg->collisionChunks[c];
                if (!chunk) continue;

                // Use rotation-aware raycast with correct total rotation
                if (collision_raycast_blocked_rotated(chunk,
                    fromX, fromY, fromZ, toX, toY, toZ,
                    seg->collisionOffX, seg->posY, seg->collisionOffZ,
                    seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ,
                    collisionRotY)) {
                    return true;
                }
            }
        } else if (seg->collision) {
            if (collision_raycast_blocked_rotated(seg->collision,
                fromX, fromY, fromZ, toX, toY, toZ,
                seg->collisionOffX, seg->posY, seg->collisionOffZ,
                seg->collisionScaleX, seg->scaleY, seg->collisionScaleZ,
                collisionRotY)) {
                return true;
            }
        }
    }
    return false;
}

// Filter callback for frustum culling - only draw visible objects
static bool maploader_frustum_filter(void* userData, const T3DObject *obj) {
    (void)userData;
    return obj->isVisible;
}

// Draw all active maps with frustum culling
static inline void maploader_draw_culled(MapLoader* loader, int frameIdx, T3DViewport* viewport) {
    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (!seg->active || seg->model == NULL) continue;

        int matIdx = frameIdx * loader->count + i;
        // All segments need base 90° + level rotation (same as collision)
        float totalRotY = T3D_DEG_TO_RAD(90.0f) + seg->rotY;
        t3d_mat4fp_from_srt_euler(&loader->matrixFP[matIdx],
            (float[3]){seg->scaleX, seg->scaleY, seg->scaleZ},
            (float[3]){0.0f, totalRotY, 0.0f},
            (float[3]){seg->posX, seg->posY, seg->posZ}
        );

        // Check if model has BVH for frustum culling
        const T3DBvh* bvh = t3d_model_bvh_get(seg->model);
        if (bvh && viewport) {
            // Reset visibility on all objects
            T3DModelIter it = t3d_model_iter_create(seg->model, T3D_CHUNK_TYPE_OBJECT);
            while (t3d_model_iter_next(&it)) {
                it.object->isVisible = false;
            }

            // Create model-view-projection matrix for frustum in model space
            // Model matrix: SRT with rotation and translation
            T3DMat4 modelMat, mvpMat;
            t3d_mat4_from_srt_euler(&modelMat,
                (float[3]){seg->scaleX, seg->scaleY, seg->scaleZ},
                (float[3]){0.0f, totalRotY, 0.0f},
                (float[3]){seg->posX, seg->posY, seg->posZ}
            );

            // Combine: MVP = Proj * View * Model
            T3DMat4 vmMat;
            t3d_mat4_mul(&vmMat, &viewport->matCamera, &modelMat);
            t3d_mat4_mul(&mvpMat, &viewport->matProj, &vmMat);

            // Extract frustum from MVP (this gives frustum in model space)
            T3DFrustum modelFrustum;
            t3d_mat4_to_frustum(&modelFrustum, &mvpMat);

            // Query BVH with model-space frustum
            t3d_model_bvh_query_frustum(bvh, &modelFrustum);

            // Draw with filter
            t3d_matrix_push(&loader->matrixFP[matIdx]);
            t3d_model_draw_custom(seg->model, (T3DModelDrawConf){
                .filterCb = maploader_frustum_filter
            });
            t3d_matrix_pop(1);
        } else {
            // No BVH, draw normally
            t3d_matrix_push(&loader->matrixFP[matIdx]);
            t3d_model_draw(seg->model);
            t3d_matrix_pop(1);
        }

        // Flush after each map segment to prevent RDP buffer overflow
        rspq_flush();
    }

}

// Draw all active maps (no frustum culling - legacy)
static inline void maploader_draw(MapLoader* loader, int frameIdx) {
    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (!seg->active || seg->model == NULL) continue;

        int matIdx = frameIdx * loader->count + i;
        // All segments need base 90° rotation + any extra level rotation
        float totalRotY = T3D_DEG_TO_RAD(90.0f) + seg->rotY;
        t3d_mat4fp_from_srt_euler(&loader->matrixFP[matIdx],
            (float[3]){seg->scaleX, seg->scaleY, seg->scaleZ},
            (float[3]){0.0f, totalRotY, 0.0f},
            (float[3]){seg->posX, seg->posY, seg->posZ}
        );

        t3d_matrix_push(&loader->matrixFP[matIdx]);
        t3d_model_draw(seg->model);
        t3d_matrix_pop(1);
    }
}

// Get total vertex count of active segments
static inline int maploader_get_active_verts(MapLoader* loader) {
    int total = 0;
    for (int i = 0; i < loader->count; i++) {
        MapSegment* seg = &loader->segments[i];
        if (seg->active && seg->model != NULL) {
            total += seg->model->totalVertCount;
        }
    }
    return total;
}

// Cleanup
static inline void maploader_free(MapLoader* loader) {
    // Free cached models (each only once)
    for (int i = 0; i < loader->cacheCount; i++) {
        if (loader->modelCache[i].model) {
            t3d_model_free(loader->modelCache[i].model);
            loader->modelCache[i].model = NULL;
        }
    }
    loader->cacheCount = 0;

    // Clear segments (including collision pointers to prevent stale references)
    for (int i = 0; i < loader->count; i++) {
        loader->segments[i].model = NULL;
        loader->segments[i].collision = NULL;
        for (int j = 0; j < MAX_COLLISION_CHUNKS; j++) {
            loader->segments[i].collisionChunks[j] = NULL;
        }
    }

    if (loader->matrixFP != NULL) {
        free_uncached(loader->matrixFP);
        loader->matrixFP = NULL;
    }

    loader->count = 0;
}

#endif // MAP_LOADER_H
