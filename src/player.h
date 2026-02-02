#ifndef PLAYER_H
#define PLAYER_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include "controls.h"
#include "constants.h"
#include "mapLoader.h"

// Forward declarations
struct MapRuntime;

// Robot body parts - determines animation set and abilities
typedef enum {
    PLAYER_PART_HEAD,
    PLAYER_PART_TORSO,
    PLAYER_PART_ARMS,
    PLAYER_PART_LEGS,
    PLAYER_PART_COUNT
} PlayerPart;

// Player structure - contains all per-player state
typedef struct Player {
    // Position (world coordinates)
    float x, y, z;
    float groundLevel;

    // Physics (from controls.h)
    PlayerState physics;
    ControlConfig config;

    // Facing direction (radians)
    float angle;

    // Animation system
    T3DModel* model;
    T3DSkeleton skeleton;
    T3DSkeleton skeletonBlend;
    T3DAnim* currentAnim;

    // Torso mode animations
    T3DAnim animIdle;
    T3DAnim animWalk;
    T3DAnim animJumpCharge;
    T3DAnim animJumpLaunch;
    T3DAnim animJumpLand;
    T3DAnim animWait;
    T3DAnim animPain1;
    T3DAnim animPain2;
    T3DAnim animDeath;
    T3DAnim animSlideFront;
    T3DAnim animSlideFrontRecover;
    T3DAnim animSlideBack;
    T3DAnim animSlideBackRecover;
    bool torsoHasAnims;

    // Arms mode animations
    T3DAnim animArmsIdle;
    T3DAnim animArmsWalk1;
    T3DAnim animArmsWalk2;
    T3DAnim animArmsJump;
    T3DAnim animArmsJumpLand;
    T3DAnim animArmsSpin;
    T3DAnim animArmsWhip;
    T3DAnim animArmsDeath;
    T3DAnim animArmsPain1;
    T3DAnim animArmsPain2;
    T3DAnim animArmsSlide;
    bool armsHasAnims;

    // Head mode animations
    T3DAnim animHeadIdle;
    T3DAnim animHeadWalk;
    bool headHasAnims;

    // Fullbody mode additional animations (beyond torso slots)
    T3DAnim fbAnimRun;
    T3DAnim fbAnimCrouch;
    T3DAnim fbAnimCrouchJump;
    T3DAnim fbAnimCrouchJumpHover;
    T3DAnim fbAnimSpinAir;
    T3DAnim fbAnimSpinAtk;
    T3DAnim fbAnimSpinCharge;
    T3DAnim fbAnimRunNinja;
    T3DAnim fbAnimCrouchAttack;
    bool fbHasAnims;

    // Body part mode
    PlayerPart currentPart;
    int partSwitchCooldown;
    bool isArmsMode;

    // Jump charge state (torso mode)
    bool isCharging;
    bool isJumping;
    bool isLanding;
    bool jumpAnimPaused;
    float jumpChargeTime;
    float jumpAimX, jumpAimY;
    float lastValidAimX, lastValidAimY;
    float aimGraceTimer;
    float jumpPeakY;
    float jumpArcEndX;  // For camera targeting during charge

    // Arms mode combat state
    bool armsIsSpinning;
    bool armsIsWhipping;
    bool armsIsGliding;
    bool armsHasDoubleJumped;
    bool armsIsWallJumping;
    float armsSpinTime;
    float armsWhipTime;
    float armsWallJumpAngle;
    float spinHitCooldown;
    uint64_t spinHitEnemies;  // Bitfield tracking which decorations were hit this spin

    // Fullbody mode state (crouch mechanics)
    bool fbIsCrouching;       // Currently holding crouch (Z)
    bool fbIsLongJumping;     // In long jump state
    bool fbIsBackflipping;    // In backflip state
    float fbLongJumpSpeed;    // Forward speed during long jump
    bool fbIsHovering;        // Crouch hover active (B while crouching)
    float fbHoverTime;        // Time spent hovering
    bool fbCrouchJumpWindup;  // In crouch jump wind-up phase (before launch)
    float fbCrouchJumpWindupTime; // Wind-up timer
    bool fbCrouchJumpRising;  // In crouch jump rising phase (play fb_crouch_jump anim)
    float fbHoverTiltX;       // Current tilt angle X component (degrees)
    float fbHoverTiltZ;       // Current tilt angle Z component (degrees)
    float fbHoverTiltVelX;    // Tilt angular velocity X
    float fbHoverTiltVelZ;    // Tilt angular velocity Z

    // Fullbody spin attack state
    bool fbIsSpinning;        // Ground spin active
    bool fbIsSpinningAir;     // Air spin active
    float fbSpinTime;         // Animation timer
    bool fbIsCharging;        // Spin charge active (Z+B hold)
    float fbChargeTime;       // Charge duration
    bool fbIsCrouchAttacking; // Crouch attack active
    float fbCrouchAttackTime; // Crouch attack timer

    // Fullbody wall jump state
    bool fbIsWallJumping;     // Wall jump active
    float fbWallJumpAngle;    // Angle to face during wall jump

    // Fullbody tracking state
    bool fbWasCrouchingPrev;  // Was crouching last frame
    bool fbWasAirborne;       // Was airborne last frame (for landing detection)
    bool fbJustLandedFromSpecial;  // Prevents immediate jump after landing from long jump/hover
    int fbFramesSinceGrounded; // Grace period for wall jumps

    // Wall jump state (torso mode)
    bool torsoIsWallJumping;
    float torsoWallJumpAngle;
    int wallJumpInputBuffer;  // Frames since A was pressed for wall jump buffering

    // Squash and stretch (visual feedback)
    float squashScale;
    float squashVelocity;
    float landingSquash;
    float chargeSquash;

    // Coyote time and jump buffering
    float coyoteTimer;
    float jumpBufferTimer;
    bool isBufferedCharge;

    // Triple jump combo tracking (TORSO mode)
    int jumpComboCount;      // 0=first, 1=second, 2=third jump
    float jumpComboTimer;    // Grace period to continue combo after landing

    // Idle fidget animation
    int idleFrames;
    bool playingFidget;
    float fidgetPlayTime;

    // Slide animation state
    bool wasSliding;
    bool isSlidingFront;
    bool isSlideRecovering;
    float slideRecoverTime;

    // Health and damage
    int health;
    int maxHealth;
    bool isDead;
    bool isHurt;
    float hurtAnimTime;
    T3DAnim* currentPainAnim;
    float invincibilityTimer;
    int invincibilityFlashFrame;

    // Death and respawn
    float deathTimer;
    bool isRespawning;
    float respawnDelayTimer;

    // Spawn point
    float spawnX, spawnY, spawnZ;

    // Camera state (per-player for splitscreen)
    T3DVec3 camPos;
    T3DVec3 camTarget;
    float smoothCamX, smoothCamY;
    float smoothCamTargetX;  // For arc preview camera adjustment
    float smoothCollisionCamX, smoothCollisionCamY, smoothCollisionCamZ;  // Collision smoothing

    // Death iris effect (per-player for splitscreen)
    float irisRadius;
    float irisCenterX, irisCenterY;
    float irisTargetX, irisTargetY;
    bool irisActive;
    float irisPauseTimer;
    bool irisPaused;
    float fadeAlpha;  // Screen fade (0=transparent, 1=black)

    // Input
    joypad_port_t port;
    joypad_buttons_t prevHeld;

    // Visual
    color_t tint;
    int playerIndex;

    // Transform matrix for rendering
    T3DMat4FP* matFP;

    // Animation tracking (avoid expensive re-attach)
    T3DAnim* attachedAnim;
} Player;

// ============================================================
// LIFECYCLE FUNCTIONS
// ============================================================

// Initialize a player with model and spawn position
// model: The T3DModel* to use (shared between players)
// index: Player index (0 = P1, 1 = P2)
// x, y, z: Initial spawn position
// bodyPart: Which body part mode (determines animation set)
void player_init(Player* p, int index, T3DModel* model, float x, float y, float z, PlayerPart bodyPart);

// Free player resources
void player_deinit(Player* p);

// Reset player to spawn point (after death, etc.)
void player_reset(Player* p);

// ============================================================
// UPDATE FUNCTIONS
// ============================================================

// Main update - call every frame
// map: MapLoader for collision
// runtime: MapRuntime for decorations/triggers
// dt: Delta time (DELTA_TIME constant)
// inputEnabled: false during countdown, cutscenes, etc.
void player_update(Player* p, MapLoader* map, struct MapRuntime* runtime,
                   float dt, bool inputEnabled);

// Load/reload animations from model (called by player_init)
// bodyPart determines which animation set to load:
// - PLAYER_PART_HEAD/PLAYER_PART_TORSO: loads torso_* and head_* animations
// - PLAYER_PART_ARMS: loads arms_* animations
// - PLAYER_PART_LEGS: loads fb_* (fullbody) animations
void player_load_animations(Player* p, T3DModel* model, PlayerPart bodyPart);

// ============================================================
// COMBAT FUNCTIONS
// ============================================================

// Apply damage to player
// Returns true if damage was applied (not invincible)
// Note: Named player_apply_damage to avoid conflict with game.c legacy function
bool player_apply_damage(Player* p, int damage);

// Apply knockback force from a position
// Note: Named player_apply_knockback to avoid conflict with game.c legacy function
void player_apply_knockback(Player* p, float fromX, float fromZ, float strength);

// Trigger respawn sequence
void player_respawn(Player* p);

// Check if player is dead - use p->isDead directly
// Note: Not using player_is_dead() to avoid conflict with mapData.h declaration

// Check if player is invincible - use p->invincibilityTimer > 0.0f directly
// Note: Not using player_is_invincible() to avoid potential future conflicts

// ============================================================
// RENDERING FUNCTIONS
// ============================================================

// Draw player model
// vp: Viewport for this player's view
void player_draw(Player* p, T3DViewport* vp);

// Draw jump arc preview (torso mode charge)
// arcModel: Model to use for arc dots
// arcMat: Transform matrix for arc dots
void player_draw_arc(Player* p, MapLoader* map, T3DModel* arcModel, T3DMat4FP* arcMat);

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

// Check if player input should be blocked (hurt, dead, cutscene, etc.)
bool player_is_input_blocked(Player* p);

// Attach animation only if different from current (optimization)
void player_attach_anim(Player* p, T3DAnim* anim);

// Get current movement speed based on mode
float player_get_speed(Player* p);

// Update camera to follow player
void player_update_camera(Player* p, float dt);

// ============================================================
// CONSTANTS (Timing values)
// ============================================================

#define PLAYER_COYOTE_TIME 0.3f          // Time to still jump after leaving ground
#define PLAYER_JUMP_BUFFER_TIME 0.25f    // Time to buffer jump before landing
#define PLAYER_AIM_GRACE_THRESHOLD 0.15f  // Stick threshold for aim input

// Triple jump combo system (TORSO mode)
#define PLAYER_JUMP_COMBO_WINDOW 0.4f    // Seconds after landing to continue combo
#define PLAYER_JUMP_COMBO_CHARGE_MULT_1 1.0f   // First jump: normal charge rate
#define PLAYER_JUMP_COMBO_CHARGE_MULT_2 1.5f   // Second jump: 1.5x charge rate (reaches cap faster)
#define PLAYER_JUMP_COMBO_CHARGE_MULT_3 2.0f   // Third jump: 2x charge rate (reaches cap even faster)
#define PLAYER_JUMP_COMBO_POWER_MULT_1 1.0f    // First jump: normal velocity
#define PLAYER_JUMP_COMBO_POWER_MULT_2 1.3f    // Second jump: 1.3x velocity
#define PLAYER_JUMP_COMBO_POWER_MULT_3 1.6f    // Third jump: 1.6x velocity
#define PLAYER_AIM_GRACE_DURATION 0.1f    // Grace period for aim input
#define PLAYER_INVINCIBILITY_DURATION 1.5f // Seconds of invincibility after damage
#define PLAYER_INVINCIBILITY_FLASH_RATE 4  // Flash every N frames

// ============================================================
// TORSO JUMP PHYSICS CONSTANTS (from game.c)
// ============================================================

// Charge jump timing
#define PLAYER_HOP_THRESHOLD 0.15f               // Time threshold for hop vs charge (seconds)
#define PLAYER_MAX_CHARGE_TIME 1.5f              // Maximum charge duration

// Charge jump velocities (FPS-scaled)
#define PLAYER_CHARGE_JUMP_EARLY_BASE (3.0f * FPS_SCALE_SQRT)
#define PLAYER_CHARGE_JUMP_EARLY_MULT (2.0f * FPS_SCALE_SQRT)

// Hop velocities (small tap)
#define PLAYER_HOP_VELOCITY_Y (5.0f * FPS_SCALE_SQRT)
#define PLAYER_HOP_FORWARD_SPEED (1.0f * FPS_SCALE)

// Horizontal movement scale (reduce sideways movement)
#define PLAYER_HORIZONTAL_SCALE 0.4f

// Wall kick constants
#define PLAYER_WALL_KICK_VEL_Y 8.0f
#define PLAYER_WALL_KICK_VEL_XZ 4.0f

// Squash/stretch spring constants
#define PLAYER_SQUASH_SPRING_K 24.0f
#define PLAYER_SQUASH_DAMPING 8.0f

// Input helper - apply deadzone to stick value
static inline float player_apply_deadzone(float value) {
    const float deadzone = 0.15f;
    if (value > deadzone) return (value - deadzone) / (1.0f - deadzone);
    if (value < -deadzone) return (value + deadzone) / (1.0f - deadzone);
    return 0.0f;
}

#endif // PLAYER_H
