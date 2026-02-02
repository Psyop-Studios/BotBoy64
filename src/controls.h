#ifndef CONTROLS_H
#define CONTROLS_H

#include <libdragon.h>
#include <stdbool.h>

// Slope type enum (based on floor steepness)
typedef enum {
    SLOPE_FLAT,       // Walkable ground - can walk freely (normal.y >= 0.866)
    SLOPE_STEEP,      // Too steep to walk up - forces sliding (normal.y < 0.866)
    SLOPE_WALL        // Treated as wall, not floor (normal.y < 0.5)
} SlopeType;

// Player state structure for controls
typedef struct {
    float velX, velY, velZ;
    float playerAngle;
    bool isGrounded;
    int groundedFrames;   // How many consecutive frames we've been grounded
    bool isOnSlope;       // Currently on a slope (not perfectly flat)
    bool isSliding;       // Currently in sliding state
    SlopeType slopeType;  // Type of slope we're on
    float steepSlopeTimer; // Time spent on steep slope (for struggle-then-slide)
    float slopeNormalX;   // Ground normal X component
    float slopeNormalY;   // Ground normal Y component
    float slopeNormalZ;   // Ground normal Z component
    int currentJumps;
    bool canMove;
    bool canRotate;
    bool canJump;
    bool hasAirControl;   // Can steer while in the air (arms/legs mode)
    bool isGliding;       // Currently gliding (reduced gravity, e.g., arms spin in air)
    bool onOilPuddle;     // Currently standing on oil (slippery!)

    // Slide velocity (separate from velX/Z for smoother transitions)
    float slideVelX, slideVelZ;
    float slideYaw;       // Direction facing while sliding (radians)

    // Wall collision state (for wall kick)
    bool hitWall;              // Hit a wall this frame
    float wallNormalX;         // Wall normal X component
    float wallNormalZ;         // Wall normal Z component
    int wallHitTimer;          // Frames since wall hit (for wall kick window)
} PlayerState;

// Control configuration
typedef struct {
    float moveSpeed;
    float jumpForce;
    float gravity;
    int totalJumps;
    float flySpeed;
} ControlConfig;

// Debug fly mode flag
extern bool debugFlyMode;

// Flag to disable normal jump (for charge-jump-only modes)
extern bool disableNormalJump;

// Analog stick deadzone (0.0 to 1.0, values below this are treated as 0)
#define STICK_DEADZONE 0.15f

// Apply deadzone to a stick axis value (-1.0 to 1.0)
// Returns 0 if within deadzone, otherwise rescales to use full range
static inline float apply_deadzone(float value) {
    if (value > STICK_DEADZONE) {
        return (value - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
    } else if (value < -STICK_DEADZONE) {
        return (value + STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
    }
    return 0.0f;
}

// Initialize default control config
void controls_init(ControlConfig* config);

// Process player input and update state
// Returns true if player is moving
bool controls_update(PlayerState* state, ControlConfig* config, joypad_port_t port);

// Process replay input and update state (for ghost/replay playback)
// Uses provided stick and button values instead of reading from joypad
bool controls_update_replay(PlayerState* state, ControlConfig* config, int8_t stickX, int8_t stickY, uint16_t buttons);

#endif // CONTROLS_H
