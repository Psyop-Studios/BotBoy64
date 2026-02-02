#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <math.h>

// ============================================================
// GAME ENGINE CONSTANTS
// Centralized magic numbers for easier tuning and maintenance
// ============================================================

// ------------------------------------------------------------
// FPS AND TIMING
// ------------------------------------------------------------
#define TARGET_FPS 30                   // Target frame rate
#define DELTA_TIME (1.0f / TARGET_FPS)  // Time per frame in seconds
#define FPS_SCALE 2.0f                  // Scale factor (60/30 = 2)
#define FPS_SCALE_SQRT 1.41421356f      // sqrt(FPS_SCALE) for velocity scaling

// ------------------------------------------------------------
// PLAYER CONSTANTS
// ------------------------------------------------------------
#define PLAYER_RADIUS 8.0f              // Collision cylinder radius
#define PLAYER_HEIGHT 20.0f             // Collision cylinder height
#define PLAYER_CAMERA_OFFSET_Y -100.0f  // Camera Y offset from player

// ------------------------------------------------------------
// PLAYER PHYSICS (scaled for 30 FPS)
// Base values are tuned for 60 FPS, then scaled
// ------------------------------------------------------------
#define GRAVITY_BASE 0.3f                                   // Base gravity (60 FPS)
#define GRAVITY (GRAVITY_BASE * FPS_SCALE)                  // Scaled gravity for 30 FPS
#define JUMP_VELOCITY_BASE 8.0f                             // Base jump velocity (60 FPS)
#define JUMP_VELOCITY (JUMP_VELOCITY_BASE * FPS_SCALE_SQRT) // Scaled jump velocity
#define TERMINAL_VELOCITY (-20.0f * FPS_SCALE)              // Maximum falling speed

// ------------------------------------------------------------
// COLLISION CULLING
// ------------------------------------------------------------
#define COLLISION_CULL_DISTANCE 200.0f  // Skip decorations farther than this (XZ) - increased for large decos like conveyor
#define COLLISION_MARGIN 2.0f           // AABB early-out margin for triangles
#define GROUND_SEARCH_MARGIN 20.0f      // How far above player to search for ground
#define WALL_NORMAL_THRESHOLD 0.7f      // ny threshold for floor vs wall (abs(ny) > 0.7 = floor)
#define WALL_PUSH_EPSILON 0.1f          // Extra push distance to prevent stuck-in-wall

// ------------------------------------------------------------
// SLOPE-BASED PHYSICS
// Based on floor normal.y (cosine of angle from vertical)
// Lower normal.y = steeper slope
// ------------------------------------------------------------

// Steep floor threshold - slopes steeper than this force sliding
// cos(45°) = 0.707 - only really steep slopes force sliding
#define SLOPE_STEEP_THRESHOLD 0.707f       // cos(45°) - below this, floor is "steep"

// Wall threshold - steeper than this is treated as a wall, not floor
#define SLOPE_WALL_THRESHOLD 0.5f          // cos(60°) - too steep to stand on at all

// Slope slide physics (per frame at 30 FPS)
// Higher accel + less friction = more noticeable acceleration feeling
#define SLIDE_ACCEL 25.0f                  // Base acceleration (scaled by steepness)
#define SLIDE_ACCEL_SLIPPERY 35.0f         // Slippery surface acceleration
#define SLIDE_FRICTION 0.97f               // Less friction on slopes (lose 3% per frame)
#define SLIDE_FRICTION_SLIPPERY 0.99f      // Almost no friction on oil
#define SLIDE_FRICTION_FLAT 0.80f          // Very strong friction on flat/gentle ground to stop
#define SLIDE_MAX_SPEED 120.0f             // Higher cap for faster slides
#define SLIDE_STOP_THRESHOLD 5.0f          // Stop sliding below this speed on gentle slopes
#define SLIDE_SPEED 4.0f                   // Legacy constant (still used in some places)

// ------------------------------------------------------------
// ANIMATION TIMING (in frames @ 30fps)
// ------------------------------------------------------------
#define IDLE_WAIT_FRAMES 210            // 7 seconds before fidget animation (210 frames @ 30fps)
#define HURT_ANIMATION_DURATION 0.8f    // How long hurt animation plays (seconds)

// ------------------------------------------------------------
// LEVEL TRANSITION TIMING (in seconds)
// ------------------------------------------------------------
#define TRANSITION_FADE_OUT_TIME 0.5f   // Time to fade to black
#define TRANSITION_HOLD_TIME 0.5f       // Time to stay black while loading
#define TRANSITION_FADE_IN_TIME 0.5f    // Time to fade back in

// ------------------------------------------------------------
// DEATH & RESPAWN TIMING (in seconds)
// ------------------------------------------------------------
#define DEATH_ANIMATION_TIME 1.5f       // Time to play death animation before fade
#define DEATH_FADE_OUT_TIME 1.0f        // Time to fade to black on death
#define RESPAWN_HOLD_TIME 1.5f          // Time to stay black before respawning
#define RESPAWN_FADE_IN_TIME 1.0f       // Time to fade back in after respawn

// ------------------------------------------------------------
// SCREEN DIMENSIONS (runtime values for resolution independence)
// ------------------------------------------------------------
#define SCREEN_WIDTH  display_get_width()
#define SCREEN_HEIGHT display_get_height()
#define SCREEN_CENTER_X (SCREEN_WIDTH / 2)
#define SCREEN_CENTER_Y (SCREEN_HEIGHT / 2)

// ------------------------------------------------------------
// RENDERING
// ------------------------------------------------------------
#define VISIBILITY_RANGE 1125.0f         // Map segment visibility range for chunk loading/unloading
#define DECO_VISIBILITY_RANGE 900.0f    // Decoration visibility range
#define COG_VISIBILITY_RANGE 1575.0f    // Cogs visible from further (always spinning)
#define DEBUG_VISIBILITY_RANGE 2250.0f  // Debug mode extended visibility range

// ------------------------------------------------------------
// ENEMY AI CONSTANTS
// ------------------------------------------------------------
#define ENEMY_RADIUS 8.0f               // Enemy collision cylinder radius
#define ENEMY_HEIGHT 15.0f              // Enemy collision cylinder height
#define RAT_AGGRO_RANGE 100.0f          // Distance at which rat becomes aggressive
#define RAT_DEAGGRO_RANGE 200.0f        // Distance at which rat loses aggro
#define RAT_ATTACK_RANGE 35.0f          // Distance at which rat attacks
#define RAT_WALK_SPEED 75.0f            // Rat movement speed when chasing (units/sec)
#define RAT_PATROL_SPEED 60.0f          // Rat movement speed when patrolling (units/sec)
#define RAT_ATTACK_COOLDOWN 1.0f        // Seconds between rat attacks
#define RAT_ATTACK_DAMAGE_THRESHOLD 0.5f // Animation progress threshold for damage (0.5 = halfway)
#define RAT_POUNCE_RANGE 75.0f          // Distance at which rat pounces (half of aggro range)
#define RAT_POUNCE_MIN_RANGE 50.0f      // Minimum distance to pounce (don't pounce if too close)
#define RAT_POUNCE_DURATION 0.833f      // Pounce animation duration in seconds
#define RAT_POUNCE_DISTANCE 70.0f       // Distance rat travels during pounce
#define RAT_POUNCE_COOLDOWN 2.0f        // Seconds between pounces
// Rat animation indices (order from rat.glb)
#define RAT_WALK_ANIM 0                 // rat_running animation (0.375s)
#define RAT_ATTACK_ANIM 1               // rat_attack animation (0.833s)
#define RAT_POUNCE_ANIM 2               // rat_pounce animation (0.833s)
#define RAT_IDLE_ANIM 3                 // rat_idle animation (4.958s)
#define RAT_MAX_STEP_DOWN 20.0f         // Max drop before rat refuses to walk (cliff detection)

// ------------------------------------------------------------
// PICKUP COLLISION SCALING
// ------------------------------------------------------------
#define BOLT_COLLISION_SCALE 2.0f       // Bolts are easier to collect (2x radius)

// ------------------------------------------------------------
// ELEVATOR CONSTANTS
// ------------------------------------------------------------
#define ELEVATOR_RISE_HEIGHT 100.0f     // How high elevator rises (units)
#define ELEVATOR_SPEED 30.0f            // Elevator movement speed (units/sec)
#define ELEVATOR_STOP_THRESHOLD 0.5f    // Distance threshold to snap to target

// ------------------------------------------------------------
// FALLBACK GROUND HEIGHT
// ------------------------------------------------------------
#define FALLBACK_GROUND_Y -100.0f       // Emergency ground height if no collision found
#define INVALID_GROUND_Y -105.0f       // Sentinel value for "no ground found"
#define INVALID_CEILING_Y 99999.0f     // Sentinel value for "no ceiling found"
#define CEILING_SEARCH_MARGIN 200.0f   // How far above player to search for ceiling

// ------------------------------------------------------------
// FAST MATH LOOKUP TABLES (RAM for CPU tradeoff)
// ------------------------------------------------------------
#define SIN_TABLE_SIZE 256  // 256 entries = 1.4° resolution

// Lookup table for sin (covers 0 to 2*PI)
// Access: fast_sin(angle) where angle is in radians
static const float SIN_TABLE[SIN_TABLE_SIZE] = {
    0.000000f, 0.024541f, 0.049068f, 0.073565f, 0.098017f, 0.122411f, 0.146730f, 0.170962f,
    0.195090f, 0.219101f, 0.242980f, 0.266713f, 0.290285f, 0.313682f, 0.336890f, 0.359895f,
    0.382683f, 0.405241f, 0.427555f, 0.449611f, 0.471397f, 0.492898f, 0.514103f, 0.534998f,
    0.555570f, 0.575808f, 0.595699f, 0.615232f, 0.634393f, 0.653173f, 0.671559f, 0.689541f,
    0.707107f, 0.724247f, 0.740951f, 0.757209f, 0.773010f, 0.788346f, 0.803208f, 0.817585f,
    0.831470f, 0.844854f, 0.857729f, 0.870087f, 0.881921f, 0.893224f, 0.903989f, 0.914210f,
    0.923880f, 0.932993f, 0.941544f, 0.949528f, 0.956940f, 0.963776f, 0.970031f, 0.975702f,
    0.980785f, 0.985278f, 0.989177f, 0.992480f, 0.995185f, 0.997290f, 0.998795f, 0.999699f,
    1.000000f, 0.999699f, 0.998795f, 0.997290f, 0.995185f, 0.992480f, 0.989177f, 0.985278f,
    0.980785f, 0.975702f, 0.970031f, 0.963776f, 0.956940f, 0.949528f, 0.941544f, 0.932993f,
    0.923880f, 0.914210f, 0.903989f, 0.893224f, 0.881921f, 0.870087f, 0.857729f, 0.844854f,
    0.831470f, 0.817585f, 0.803208f, 0.788346f, 0.773010f, 0.757209f, 0.740951f, 0.724247f,
    0.707107f, 0.689541f, 0.671559f, 0.653173f, 0.634393f, 0.615232f, 0.595699f, 0.575808f,
    0.555570f, 0.534998f, 0.514103f, 0.492898f, 0.471397f, 0.449611f, 0.427555f, 0.405241f,
    0.382683f, 0.359895f, 0.336890f, 0.313682f, 0.290285f, 0.266713f, 0.242980f, 0.219101f,
    0.195090f, 0.170962f, 0.146730f, 0.122411f, 0.098017f, 0.073565f, 0.049068f, 0.024541f,
    0.000000f,-0.024541f,-0.049068f,-0.073565f,-0.098017f,-0.122411f,-0.146730f,-0.170962f,
   -0.195090f,-0.219101f,-0.242980f,-0.266713f,-0.290285f,-0.313682f,-0.336890f,-0.359895f,
   -0.382683f,-0.405241f,-0.427555f,-0.449611f,-0.471397f,-0.492898f,-0.514103f,-0.534998f,
   -0.555570f,-0.575808f,-0.595699f,-0.615232f,-0.634393f,-0.653173f,-0.671559f,-0.689541f,
   -0.707107f,-0.724247f,-0.740951f,-0.757209f,-0.773010f,-0.788346f,-0.803208f,-0.817585f,
   -0.831470f,-0.844854f,-0.857729f,-0.870087f,-0.881921f,-0.893224f,-0.903989f,-0.914210f,
   -0.923880f,-0.932993f,-0.941544f,-0.949528f,-0.956940f,-0.963776f,-0.970031f,-0.975702f,
   -0.980785f,-0.985278f,-0.989177f,-0.992480f,-0.995185f,-0.997290f,-0.998795f,-0.999699f,
   -1.000000f,-0.999699f,-0.998795f,-0.997290f,-0.995185f,-0.992480f,-0.989177f,-0.985278f,
   -0.980785f,-0.975702f,-0.970031f,-0.963776f,-0.956940f,-0.949528f,-0.941544f,-0.932993f,
   -0.923880f,-0.914210f,-0.903989f,-0.893224f,-0.881921f,-0.870087f,-0.857729f,-0.844854f,
   -0.831470f,-0.817585f,-0.803208f,-0.788346f,-0.773010f,-0.757209f,-0.740951f,-0.724247f,
   -0.707107f,-0.689541f,-0.671559f,-0.653173f,-0.634393f,-0.615232f,-0.595699f,-0.575808f,
   -0.555570f,-0.534998f,-0.514103f,-0.492898f,-0.471397f,-0.449611f,-0.427555f,-0.405241f,
   -0.382683f,-0.359895f,-0.336890f,-0.313682f,-0.290285f,-0.266713f,-0.242980f,-0.219101f,
   -0.195090f,-0.170962f,-0.146730f,-0.122411f,-0.098017f,-0.073565f,-0.049068f,-0.024541f,
};

// Fast sin using lookup table (angle in radians)
static inline float fast_sin(float angle) {
    // Normalize angle to 0..2PI range
    const float TWO_PI = 6.28318530718f;
    while (angle < 0.0f) angle += TWO_PI;
    while (angle >= TWO_PI) angle -= TWO_PI;
    // Convert to table index
    int idx = (int)(angle * (SIN_TABLE_SIZE / TWO_PI)) & (SIN_TABLE_SIZE - 1);
    return SIN_TABLE[idx];
}

// Fast cos using sin table (cos(x) = sin(x + PI/2))
static inline float fast_cos(float angle) {
    return fast_sin(angle + 1.57079632679f);  // PI/2
}

#endif // CONSTANTS_H
