// ============================================================
// CAMERA MODULE
// Third-person follow camera with smoothing and collision
// ============================================================

#include "camera.h"
#include "mapLoader.h"

// Death camera zoom amount (how much closer camera gets during death)
#define CAM_DEATH_ZOOM_AMOUNT 60.0f

// ============================================================
// INITIALIZATION
// ============================================================

void camera_init(CameraState* cam, float playerX, float playerY, float playerZ) {
    cam->zOffset = CAM_Z_OFFSET;  // Default to single-player offset

    cam->pos.v[0] = playerX;
    cam->pos.v[1] = playerY + CAM_Y_OFFSET;
    cam->pos.v[2] = playerZ + cam->zOffset;

    cam->target.v[0] = playerX;
    cam->target.v[1] = playerY;
    cam->target.v[2] = playerZ;

    cam->smoothX = playerX;
    cam->smoothY = playerY + CAM_Y_OFFSET;
    cam->smoothTargetX = playerX;
    cam->chargePullback = 0.0f;

    // Initialize collision smoothing
    cam->collisionSmoothX = playerX;
    cam->collisionSmoothY = playerY + CAM_Y_OFFSET;
    cam->collisionSmoothZ = playerZ + cam->zOffset;
}

// ============================================================
// Z OFFSET SETTER
// ============================================================

void camera_set_z_offset(CameraState* cam, float zOffset) {
    cam->zOffset = zOffset;
}

// ============================================================
// UPDATE
// ============================================================

void camera_update(CameraState* cam,
                   float playerX, float playerY, float playerZ,
                   float velX, float velZ,
                   bool isCharging, float chargeRatio, float arcEndX) {
    // Camera lead - offset target based on velocity to see ahead
    float leadX = velX * CAM_LEAD_AMOUNT;
    float leadZ = velZ * CAM_LEAD_AMOUNT;

    float targetX = playerX + leadX;
    float targetY = playerY + CAM_Y_OFFSET;

    // Charge pullback (for jump aim preview)
    float desiredPullback = 0.0f;
    if (isCharging && chargeRatio > 0.0f) {
        desiredPullback = chargeRatio * CAM_CHARGE_PULLBACK;
    }
    cam->chargePullback += (desiredPullback - cam->chargePullback) * 0.1f;

    // Smooth camera follow
    cam->smoothX += (targetX - cam->smoothX) * CAM_SMOOTH_X;
    cam->smoothY += (targetY - cam->smoothY) * CAM_SMOOTH_Y;

    // Update camera position
    cam->pos.v[0] = cam->smoothX;
    cam->pos.v[1] = cam->smoothY;
    cam->pos.v[2] = playerZ + cam->zOffset + cam->chargePullback + leadZ;

    // When charging, look toward the arc direction
    float desiredTargetX = playerX;
    if (isCharging && chargeRatio > 0.0f) {
        float arcOffsetX = arcEndX - playerX;
        desiredTargetX = playerX + arcOffsetX * 0.3f;  // Look 30% toward arc end
    }
    cam->smoothTargetX += (desiredTargetX - cam->smoothTargetX) * 0.15f;

    // Update camera target
    cam->target.v[0] = cam->smoothTargetX;
    cam->target.v[1] = playerY;
    cam->target.v[2] = playerZ;
}

// ============================================================
// UPDATE WITH COLLISION
// ============================================================

void camera_update_with_collision(CameraState* cam,
                                  float playerX, float playerY, float playerZ,
                                  float velX, float velZ,
                                  bool isCharging, float chargeRatio, float arcEndX,
                                  MapLoader* mapLoader, float deathZoom) {
    // Camera lead - offset target based on velocity to see ahead
    float leadX = velX * CAM_LEAD_AMOUNT;
    float leadZ = velZ * CAM_LEAD_AMOUNT;

    float targetX = playerX + leadX;
    float targetY = playerY + CAM_Y_OFFSET;

    // Charge pullback (for jump aim preview)
    float desiredPullback = 0.0f;
    if (isCharging && chargeRatio > 0.0f) {
        desiredPullback = chargeRatio * CAM_CHARGE_PULLBACK;
    }
    cam->chargePullback += (desiredPullback - cam->chargePullback) * 0.1f;

    // Smooth camera follow
    cam->smoothX += (targetX - cam->smoothX) * CAM_SMOOTH_X;
    cam->smoothY += (targetY - cam->smoothY) * CAM_SMOOTH_Y;

    // Calculate desired camera position (before collision)
    float deathZoomOffset = deathZoom * CAM_DEATH_ZOOM_AMOUNT;
    float desiredCamX = cam->smoothX;
    float desiredCamY = cam->smoothY;
    float desiredCamZ = playerZ + cam->zOffset + cam->chargePullback + leadZ + deathZoomOffset;

    // === CAMERA COLLISION ===
    // If camera clips into a wall, push it back away from walls
    if (mapLoader != NULL) {
        float playerCenterY = playerY + 10.0f;

        // Check if camera's current position is inside geometry
        if (maploader_raycast_blocked(mapLoader, playerX, playerCenterY, playerZ,
                                      desiredCamX, desiredCamY, desiredCamZ)) {
            // Camera clips into wall - try pushing it further back
            float pushBackAmount = 20.0f;
            float bestCamZ = desiredCamZ;
            bool foundClear = false;

            for (int i = 1; i <= 4; i++) {
                float testZ = desiredCamZ - (pushBackAmount * i / 4.0f);

                if (!maploader_raycast_blocked(mapLoader, playerX, playerCenterY, playerZ,
                                               desiredCamX, desiredCamY, testZ)) {
                    bestCamZ = testZ;
                    foundClear = true;
                    break;
                }
            }

            if (foundClear) {
                desiredCamZ = bestCamZ - 5.0f;  // Buffer to stay clear of wall
            }
        }
    }

    // Smooth collision adjustment to prevent camera jumping
    cam->collisionSmoothX += (desiredCamX - cam->collisionSmoothX) * CAM_COLLISION_SMOOTH;
    cam->collisionSmoothY += (desiredCamY - cam->collisionSmoothY) * CAM_COLLISION_SMOOTH;
    cam->collisionSmoothZ += (desiredCamZ - cam->collisionSmoothZ) * CAM_COLLISION_SMOOTH;

    // Set final camera position
    cam->pos.v[0] = cam->collisionSmoothX;
    cam->pos.v[1] = cam->collisionSmoothY;
    cam->pos.v[2] = cam->collisionSmoothZ;

    // When charging, look toward the arc direction
    float desiredTargetX = playerX;
    if (isCharging && chargeRatio > 0.0f) {
        float arcOffsetX = arcEndX - playerX;
        desiredTargetX = playerX + arcOffsetX * 0.3f;
    }
    cam->smoothTargetX += (desiredTargetX - cam->smoothTargetX) * 0.15f;

    // Update camera target
    cam->target.v[0] = cam->smoothTargetX;
    cam->target.v[1] = playerY;
    cam->target.v[2] = playerZ;
}

// ============================================================
// ACCESSORS
// ============================================================

void camera_get_vectors(CameraState* cam, T3DVec3* outPos, T3DVec3* outTarget) {
    if (outPos) {
        outPos->v[0] = cam->pos.v[0];
        outPos->v[1] = cam->pos.v[1];
        outPos->v[2] = cam->pos.v[2];
    }
    if (outTarget) {
        outTarget->v[0] = cam->target.v[0];
        outTarget->v[1] = cam->target.v[1];
        outTarget->v[2] = cam->target.v[2];
    }
}

void camera_apply_shake(CameraState* cam, float shakeX, float shakeY) {
    cam->pos.v[0] += shakeX;
    cam->pos.v[1] += shakeY;
}

void camera_snap(CameraState* cam, float playerX, float playerY, float playerZ) {
    cam->pos.v[0] = playerX;
    cam->pos.v[1] = playerY + CAM_Y_OFFSET;
    cam->pos.v[2] = playerZ + cam->zOffset;

    cam->target.v[0] = playerX;
    cam->target.v[1] = playerY;
    cam->target.v[2] = playerZ;

    cam->smoothX = playerX;
    cam->smoothY = playerY + CAM_Y_OFFSET;
    cam->smoothTargetX = playerX;
    cam->chargePullback = 0.0f;

    // Reset collision smoothing
    cam->collisionSmoothX = playerX;
    cam->collisionSmoothY = playerY + CAM_Y_OFFSET;
    cam->collisionSmoothZ = playerZ + cam->zOffset;
}
