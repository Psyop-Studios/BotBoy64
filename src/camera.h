#ifndef CAMERA_H
#define CAMERA_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <stdbool.h>

// Include mapLoader for collision checking functions
#include "mapLoader.h"

// ============================================================
// CAMERA MODULE
// Third-person follow camera with smoothing and collision
// ============================================================

// Camera constants
#define CAM_Y_OFFSET 49.0f           // Height above player
#define CAM_Z_OFFSET -150.0f         // Distance behind player (single-player)
#define CAM_Z_OFFSET_MULTIPLAYER -60.0f  // Distance behind player (multiplayer - 50% closer)
#define CAM_CHARGE_PULLBACK -30.0f   // Extra pullback when charging
#define CAM_LEAD_AMOUNT 3.0f         // Velocity-based lead multiplier
#define CAM_SMOOTH_X 0.08f           // Horizontal smoothing factor
#define CAM_SMOOTH_Y 0.06f           // Vertical smoothing factor
#define CAM_COLLISION_SMOOTH 0.06f   // Collision adjustment smoothing

// Camera state for per-player camera
typedef struct {
    T3DVec3 pos;          // Current camera position
    T3DVec3 target;       // Current look-at target
    float smoothX;        // Smoothed X position
    float smoothY;        // Smoothed Y position
    float smoothTargetX;  // Smoothed target X (for arc aiming)
    float chargePullback; // Smoothed charge pullback
    float zOffset;        // Z offset (distance behind player, negative = behind)
    // Collision smoothing (prevents camera jumping when clipping walls)
    float collisionSmoothX;
    float collisionSmoothY;
    float collisionSmoothZ;
} CameraState;

// Initialize camera state
void camera_init(CameraState* cam, float playerX, float playerY, float playerZ);

// Set camera Z offset (use CAM_Z_OFFSET_MULTIPLAYER for splitscreen)
void camera_set_z_offset(CameraState* cam, float zOffset);

// Update camera to follow player
// playerX/Y/Z: Player world position
// velX/velZ: Player velocity for camera lead
// isCharging: Whether player is charging a jump
// chargeRatio: 0-1 ratio of charge progress
// arcEndX: X position of arc end (for camera look direction)
void camera_update(CameraState* cam,
                   float playerX, float playerY, float playerZ,
                   float velX, float velZ,
                   bool isCharging, float chargeRatio, float arcEndX);

// Update camera with collision checking
// Same as camera_update but also checks wall collision and pushes camera back if needed
// mapLoader: MapLoader for collision raycast (can be NULL to skip collision)
// deathZoom: 0-1 amount of death camera zoom (0 = normal, 1 = fully zoomed in)
void camera_update_with_collision(CameraState* cam,
                                  float playerX, float playerY, float playerZ,
                                  float velX, float velZ,
                                  bool isCharging, float chargeRatio, float arcEndX,
                                  MapLoader* mapLoader, float deathZoom);

// Get camera position and target (for T3D)
void camera_get_vectors(CameraState* cam, T3DVec3* outPos, T3DVec3* outTarget);

// Apply screen shake offset to camera
void camera_apply_shake(CameraState* cam, float shakeX, float shakeY);

// Reset camera to directly behind player (no smoothing)
void camera_snap(CameraState* cam, float playerX, float playerY, float playerZ);

#endif // CAMERA_H
