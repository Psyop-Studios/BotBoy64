#ifndef COLLISION_LOADER_H
#define COLLISION_LOADER_H

/**
 * Runtime collision loader - loads .col files from ROM (DFS filesystem)
 *
 * Binary format (.col):
 *   - 4 bytes: magic "COL1"
 *   - 4 bytes: triangle count (uint32 big-endian)
 *   - N * 36 bytes: triangles (9 floats each, big-endian)
 *
 * This moves collision data out of the ELF and into the ROM,
 * significantly reducing RAM usage.
 */

#include <libdragon.h>
#include <asset.h>
#include <stdlib.h>
#include <string.h>
#include "collision.h"

// Maximum collision meshes that can be loaded at once
// Level 4 needs ~40+ meshes due to many decoration types
#define MAX_LOADED_COLLISION 48

// Magic header for .col files
#define COL_MAGIC 0x434F4C31  // "COL1" in big-endian

// Loaded collision entry
typedef struct {
    char name[32];              // Model name (e.g., "level1_chunk0")
    CollisionMesh mesh;         // The collision mesh data
    CollisionTriangle* data;    // Pointer to triangle data (for freeing)
    bool loaded;
} LoadedCollision;

// Global collision storage - defined in collision_loader.c
// Using extern to avoid multiple definitions across translation units
extern LoadedCollision g_loadedCollision[MAX_LOADED_COLLISION];
extern int g_loadedCollisionCount;
extern bool g_collisionLoaderInitialized;

// Initialize the collision loader
static inline void collision_loader_init(void) {
    if (g_collisionLoaderInitialized) return;

    for (int i = 0; i < MAX_LOADED_COLLISION; i++) {
        g_loadedCollision[i].loaded = false;
        g_loadedCollision[i].data = NULL;
        g_loadedCollision[i].mesh.triangles = NULL;
        g_loadedCollision[i].mesh.count = 0;
        g_loadedCollision[i].mesh.grid.built = false;
    }
    g_loadedCollisionCount = 0;
    g_collisionLoaderInitialized = true;
    debugf("Collision loader initialized\n");
}

// Load a collision file from ROM
// Returns pointer to CollisionMesh, or NULL on failure
// The mesh is cached - subsequent calls with same name return cached version
static inline CollisionMesh* collision_load(const char* name) {
    if (!g_collisionLoaderInitialized) {
        collision_loader_init();
    }

    // Check if already loaded
    for (int i = 0; i < g_loadedCollisionCount; i++) {
        if (g_loadedCollision[i].loaded &&
            strcmp(g_loadedCollision[i].name, name) == 0) {
            return &g_loadedCollision[i].mesh;
        }
    }

    // Find free slot
    if (g_loadedCollisionCount >= MAX_LOADED_COLLISION) {
        debugf("Collision loader: too many meshes loaded (max %d)\n", MAX_LOADED_COLLISION);
        return NULL;
    }

    LoadedCollision* slot = &g_loadedCollision[g_loadedCollisionCount];

    // Build ROM path
    char romPath[64];
    snprintf(romPath, sizeof(romPath), "rom:/%s.col", name);

    // Load entire file into memory using asset_load (handles DFS properly)
    int fileSize = 0;
    uint8_t* fileData = asset_load(romPath, &fileSize);
    if (!fileData) {
        debugf("Collision loader: failed to load %s\n", romPath);
        return NULL;
    }

    // Verify minimum size (header = 8 bytes)
    if (fileSize < 8) {
        debugf("Collision loader: %s too small (%d bytes)\n", romPath, fileSize);
        free(fileData);
        return NULL;
    }

    // Read and verify magic header
    uint32_t magic = *(uint32_t*)fileData;
    if (magic != COL_MAGIC) {
        debugf("Collision loader: invalid magic in %s (got 0x%08X)\n", romPath, magic);
        free(fileData);
        return NULL;
    }

    // Read triangle count
    uint32_t triCount = *(uint32_t*)(fileData + 4);

    if (triCount == 0) {
        debugf("Collision loader: %s has 0 triangles\n", romPath);
        free(fileData);
        return NULL;
    }

    // Verify file size matches expected
    int expectedSize = 8 + triCount * sizeof(CollisionTriangle);
    if (fileSize < expectedSize) {
        debugf("Collision loader: %s truncated (got %d, expected %d)\n", romPath, fileSize, expectedSize);
        free(fileData);
        return NULL;
    }

    // The triangle data follows the header directly
    // We need to allocate separate storage since fileData will be freed
    CollisionTriangle* triangles = malloc(sizeof(CollisionTriangle) * triCount);
    if (!triangles) {
        debugf("Collision loader: failed to allocate %d triangles for %s\n", triCount, name);
        free(fileData);
        return NULL;
    }

    // Copy triangle data from file buffer
    memcpy(triangles, fileData + 8, sizeof(CollisionTriangle) * triCount);

    // Free the file buffer
    free(fileData);

    // Store in slot
    strncpy(slot->name, name, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
    slot->data = triangles;
    slot->mesh.triangles = triangles;
    slot->mesh.count = triCount;
    slot->mesh.grid.built = false;
    slot->loaded = true;

    g_loadedCollisionCount++;

    debugf("Collision loaded: %s (%d triangles, %d bytes)\n",
           name, triCount, (int)(sizeof(CollisionTriangle) * triCount));

    return &slot->mesh;
}

// Free all loaded collision data
static inline void collision_loader_free_all(void) {
    for (int i = 0; i < g_loadedCollisionCount; i++) {
        if (g_loadedCollision[i].loaded && g_loadedCollision[i].data) {
            free(g_loadedCollision[i].data);
            g_loadedCollision[i].data = NULL;
            g_loadedCollision[i].mesh.triangles = NULL;
            g_loadedCollision[i].mesh.count = 0;
            g_loadedCollision[i].loaded = false;
        }
    }
    g_loadedCollisionCount = 0;
    debugf("Collision loader: freed all meshes\n");
}

// Free a specific collision mesh by name
static inline void collision_loader_free(const char* name) {
    for (int i = 0; i < g_loadedCollisionCount; i++) {
        if (g_loadedCollision[i].loaded &&
            strcmp(g_loadedCollision[i].name, name) == 0) {
            if (g_loadedCollision[i].data) {
                free(g_loadedCollision[i].data);
            }
            g_loadedCollision[i].data = NULL;
            g_loadedCollision[i].mesh.triangles = NULL;
            g_loadedCollision[i].mesh.count = 0;
            g_loadedCollision[i].loaded = false;
            debugf("Collision loader: freed %s\n", name);
            return;
        }
    }
}

#endif // COLLISION_LOADER_H
