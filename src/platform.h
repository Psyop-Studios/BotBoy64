#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <math.h>
#include "collision.h"
#include "constants.h"

// ============================================================
// PLATFORM DISPLACEMENT SYSTEM
// ============================================================
// Moving platform displacement tracking system
//
// Key concepts:
// 1. Player tracks which platform they're standing on (current platform reference)
// 2. Each frame, platform displacement is applied BEFORE other physics
// 3. Platforms provide velocity + rotation, physics runs normally after
// 4. Floor finding returns a surface reference that can point to a platform
//
// Our implementation:
// - Single function to check all platforms and get displacement
// - Platforms register themselves, no scattered special cases
// - Clean separation: platforms update themselves, then provide displacement
// ============================================================

// Forward declarations
struct DecoInstance;
struct MapRuntime;

// Platform types - different platforms can have different behaviors
typedef enum {
    PLATFORM_NONE = 0,
    PLATFORM_ELEVATOR,      // Moves up/down only
    PLATFORM_COG,           // Rotates around Z axis (ferris wheel style)
} PlatformType;

// Platform displacement result - what to apply to player
typedef struct {
    float deltaX, deltaY, deltaZ;   // Position change to apply
    bool onPlatform;                 // Is player on a platform?
    bool overrideGroundPhysics;      // Skip normal ground collision?
    PlatformType type;               // Which type of platform
    struct DecoInstance* platform;   // Reference to the platform (for future use)
    float wallPushX, wallPushZ;      // Wall collision push (for rotating platforms)
    bool hitWall;                    // Did player hit platform wall?
} PlatformResult;

// ============================================================
// PLATFORM API - Implemented in mapData.h after DecoInstance is defined
// ============================================================

// Main entry point: Check all platforms, return displacement
// Call this ONCE per frame, BEFORE movement and collision
// struct PlatformResult platform_get_displacement(struct MapRuntime* map, float px, float py, float pz);

// Initialize result to "no platform"
static inline void platform_result_init(PlatformResult* result) {
    result->deltaX = 0.0f;
    result->deltaY = 0.0f;
    result->deltaZ = 0.0f;
    result->onPlatform = false;
    result->overrideGroundPhysics = false;
    result->type = PLATFORM_NONE;
    result->platform = NULL;
    result->wallPushX = 0.0f;
    result->wallPushZ = 0.0f;
    result->hitWall = false;
}

#endif // PLATFORM_H
