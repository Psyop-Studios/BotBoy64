#include <libdragon.h>
#include <rdpq_tri.h>
#include <rdpq_sprite.h>
#include <fgeom.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/tpx.h>
#include "game.h"
#include "../scene.h"
#include "../controls.h"
#include "../player.h"
#include "../mapLoader.h"
#include "../mapData.h"
#define LEVELS_AUDIO_IMPLEMENTATION
#include "../levels.h"
#define LEVEL3_SPECIAL_IMPLEMENTATION
// level3_special.h removed - using DECO_LEVEL3_STREAM decoration instead
#define DECO_RENDER_OWNER
#include "../deco_render.h"
#include "../PsyopsCode.h"
#include "../constants.h"
#include "../save.h"
#include "../ui.h"
#include "../debug_menu.h"
#include "level_select.h"
#include "level_complete.h"
#include "demo_scene.h"
#include "../demo_data.h"
#include <stddef.h>
#include <stdbool.h>
#include <math.h>
#include <malloc.h>

#define FB_COUNT 3

// Activation state array - shared across all files that include mapData.h
bool g_activationState[MAX_ACTIVATION_IDS] = {false};

// ============================================================
// DEBUG VALIDATION (Disable for performance on real hardware)
// ============================================================
// Uncomment to enable verbose debug checks
// #define DEBUG_RSP_VALIDATION

#ifdef DEBUG_RSP_VALIDATION
static uint32_t heap_check_counter = 0;
#define HEAP_CHECK_CANARY 0xDEADC0DE

static void heap_check(const char* location) {
    heap_check_counter++;
    uint32_t* test = (uint32_t*)malloc(32);
    if (!test) { debugf("[HEAP] FAIL %s: NULL\n", location); return; }
    for (int i = 0; i < 8; i++) test[i] = HEAP_CHECK_CANARY;
    for (int i = 0; i < 8; i++) {
        if (test[i] != HEAP_CHECK_CANARY) {
            debugf("[HEAP] CORRUPT %s: [%d]=0x%08lX\n", location, i, (unsigned long)test[i]);
        }
    }
    free(test);
    debugf("[H]%s\n", location);
}
#define HEAP_CHECK(loc) heap_check(loc)
#else
#define HEAP_CHECK(loc) ((void)0)
#endif

// Always-available validation (no debug prints, minimal overhead)
static bool skeleton_matrices_valid(const T3DSkeleton* skel, const char* name) {
    (void)name;  // Unused when not debugging
    if (!skel || !skel->boneMatricesFP || !skel->skeletonRef) return false;
    uint16_t boneCount = skel->skeletonRef->boneCount;
    if (boneCount == 0 || boneCount > 256) return false;
    for (uint16_t i = 0; i < boneCount; i++) {
        T3DMat4FP* mat = &skel->boneMatricesFP[i];
        uint32_t* vals32 = (uint32_t*)mat;
        for (int j = 0; j < 8; j++) {
            if (vals32[j] == 0xFEFEFEFE || vals32[j] == 0xDEADBEEF ||
                vals32[j] == 0xCAFEBABE || vals32[j] == 0xDEADC0DE) {
                return false;
            }
        }
    }
    return true;
}

// Robot parts enum
typedef enum {
    PART_HEAD,
    PART_TORSO,
    PART_ARMS,
    PART_LEGS,
    PART_COUNT
} RobotParts;

static RobotParts currentPart = PART_TORSO;
static int partSwitchCooldown = 0;

int partsObtained = 3; // 0 = head, 1 = torso, 2 = arms, 3 = legs

// Checkpoint state (extern declared in mapData.h, defined here)
bool g_checkpointActive = false;
float g_checkpointX = 0.0f;
float g_checkpointY = 0.0f;
float g_checkpointZ = 0.0f;

// Global respawn flag (mirrors player.isRespawning for external access by mapData.h)
bool g_playerIsRespawning = false;

// Get current body part for chargepad buff selection (called from mapData.h)
int get_current_body_part(void) {
    return (int)currentPart;
}

// Model paths per body part - each uses different animations
static const char* BODY_PART_MODEL_PATHS[PART_COUNT] = {
    "rom:/Robo_torso.t3dm",  // PART_HEAD - uses torso model with head_ animations
    "rom:/Robo_torso.t3dm",  // PART_TORSO - uses torso model
    "rom:/Robo_arms.t3dm",   // PART_ARMS - uses arms model
    "rom:/Robo_fb.t3dm",     // PART_LEGS (fullbody) - uses fullbody model
};

// Helper to get model path for current body part
static inline const char* get_body_part_model_path(RobotParts part) {
    if (part >= 0 && part < PART_COUNT) {
        return BODY_PART_MODEL_PATHS[part];
    }
    return BODY_PART_MODEL_PATHS[PART_TORSO];  // Fallback
}

// Sound effects
wav64_t sfxBoltCollect;
wav64_t sfxTurretFire;
wav64_t sfxTurretZap;
wav64_t sfxJumpSound;

// Slime performance profiling counters
uint32_t g_slimeGroundTicks = 0;
uint32_t g_slimeDecoGroundTicks = 0;
uint32_t g_slimeWallTicks = 0;
uint32_t g_slimeMathTicks = 0;
uint32_t g_slimeSpringTicks = 0;
int g_slimeUpdateCount = 0;

// Global render profiling (accessible from draw function)
static uint32_t g_renderMapTicks = 0;
static uint32_t g_renderDecoTicks = 0;
static uint32_t g_renderPlayerTicks = 0;
static uint32_t g_renderShadowTicks = 0;
static uint32_t g_renderHUDTicks = 0;
static uint32_t g_renderTotalTicks = 0;
static uint32_t g_audioTicks = 0;
static uint32_t g_inputTicks = 0;
static uint32_t g_cameraTicks = 0;
__attribute__((unused)) static uint32_t g_physicsTicks = 0;
static int g_lastFrameUs = 0;  // Average frame time in microseconds (for HUD)

// Performance graph (frame time history)
#define PERF_GRAPH_WIDTH 64    // Number of samples to display
#define PERF_GRAPH_HEIGHT 40   // Height in pixels
#define PERF_GRAPH_TARGET_US 33333  // Target frame time (30 FPS = 33.3ms)
static int perfGraphData[PERF_GRAPH_WIDTH] = {0};  // Frame time history in microseconds
static int perfGraphHead = 0;          // Circular buffer head
static bool perfGraphEnabled = false;  // Toggle with C-Left+C-Right (disabled - causes rdpq_debug crashes)
static bool gameSceneInitialized = false;  // Track if scene is properly initialized

static sprite_t *test_sprite = NULL;
static sprite_t *shadowSprite = NULL;

// Health HUD sprites and state (lazy-loaded on first damage)
static sprite_t *healthSprites[4] = {NULL, NULL, NULL, NULL};  // health1-4.sprite
static bool healthSpritesLoaded = false;  // Lazy load flag
static float healthHudY = -40.0f;      // Y position (negative = off screen)
static float healthHudTargetY = -40.0f; // Target Y position
static float healthFlashTimer = 0.0f;  // Flash effect timer
static bool healthHudVisible = false;  // Whether HUD should be shown
static float healthHudHideTimer = 0.0f; // Timer to auto-hide after damage
#define HEALTH_HUD_SHOW_Y 3.0f         // Y position when visible (raised 5px)
#define HEALTH_HUD_HIDE_Y -70.0f       // Y position when hidden (64px sprite at 2x scale)
#define HEALTH_HUD_SPEED 8.0f          // Animation speed
#define HEALTH_FLASH_DURATION 0.8f     // How long to flash
#define HEALTH_DISPLAY_DURATION 2.0f   // How long to show after damage

// Screw/Bolt HUD sprites and state (lazy-loaded on first bolt collection)
#define SCREW_HUD_FRAME_COUNT 6
static sprite_t *screwSprites[SCREW_HUD_FRAME_COUNT] = {NULL};
static bool screwSpritesLoaded = false;  // Lazy load flag
static float screwHudX = 270.0f;       // X position (positive = off screen right, for 256px width)
static float screwHudTargetX = 270.0f; // Target X position
static int screwAnimFrame = 0;         // Current animation frame (0-12)
static float screwAnimTimer = 0.0f;    // Timer for animation
static bool screwHudVisible = false;   // Whether HUD should be shown
static float screwHudHideTimer = 0.0f; // Timer to auto-hide after collection

// Debug: TMEM usage print timer (prints every 1 second)
static float tmemDebugTimer = 0.0f;
#define SCREW_HUD_SHOW_X 180.0f        // X position when visible (right side, for 256px width)
#define SCREW_HUD_HIDE_X 270.0f        // X position when hidden (off screen right, for 256px width)
#define SCREW_HUD_SPEED 8.0f           // Slide animation speed
#define SCREW_ANIM_FPS 8.0f            // Animation frames per second (slower spin)
#define SCREW_DISPLAY_DURATION 2.5f    // How long to show after collection

// Golden Screw HUD sprites and state (lazy-loaded on first golden screw collection)
#define GOLDEN_HUD_FRAME_COUNT 8
static sprite_t *goldenSprites[GOLDEN_HUD_FRAME_COUNT] = {NULL};
static bool goldenSpritesLoaded = false;  // Lazy load flag
static float goldenHudX = -50.0f;         // X position (negative = off screen left)
static float goldenHudTargetX = -50.0f;   // Target X position
static int goldenAnimFrame = 0;           // Current animation frame
static float goldenAnimTimer = 0.0f;      // Timer for animation
static bool goldenHudVisible = false;     // Whether HUD should be shown
static float goldenHudHideTimer = 0.0f;   // Timer to auto-hide after collection
#define GOLDEN_HUD_SHOW_X 8.0f            // X position when visible (left side)
#define GOLDEN_HUD_HIDE_X -50.0f          // X position when hidden (off screen left)
#define GOLDEN_HUD_SHOW_Y 8.0f            // Y position (same as bolt HUD)
#define GOLDEN_HUD_SPEED 8.0f             // Slide animation speed
#define GOLDEN_ANIM_FPS 10.0f             // Animation FPS (slightly faster spin for golden)
#define GOLDEN_DISPLAY_DURATION 3.0f      // How long to show after collection

// Reward popup (shown when collecting the final bolt/screw)
static bool rewardPopupActive = false;
static float rewardPopupTimer = 0.0f;
#define REWARD_POPUP_DURATION 4.0f     // How long to show the reward popup (4 seconds)

// Countdown sprites (3, 2, 1, GO!)
static sprite_t *countdownSprites[4] = {NULL, NULL, NULL, NULL};  // Three, Two, One, Go

// Rank sprites for level complete screen (D, C, B, A, S)
static sprite_t *rankSprite = NULL;  // Currently loaded rank sprite
static char rankSpriteChar = 0;      // Which rank is currently loaded

// UV scrolling now provided by deco_render.h

// Shadow quad vertices (4 corners of a quad)
static T3DVertPacked *shadowVerts = NULL;

// Decal vertices for slime oil trails (4 verts = 2 packed)
static T3DVertPacked *decalVerts = NULL;
static T3DMat4FP *decalMatFP = NULL;

// Map loader
static MapLoader mapLoader;
static LevelID currentLevel;

// Decorations
static MapRuntime mapRuntime;
static T3DMat4FP* decoMatFP;

// Runtime lighting state (can be modified by checkpoints and DECO_LIGHT_TRIGGER)
// Two-light system: Map light (static) affects only stage geometry,
//                   Entity light (dynamic) affects player and decorations
static struct {
    // Current lighting values (what's actually rendered)
    uint8_t ambientR, ambientG, ambientB;           // Ambient (affects everything)
    uint8_t directionalR, directionalG, directionalB;  // Map directional (stage only)
    // Entity-specific directional light (player & decorations only)
    uint8_t entityDirectR, entityDirectG, entityDirectB;
    float entityLightDirX, entityLightDirY, entityLightDirZ;  // Entity light direction
    // Target lighting values (what we're lerping toward)
    uint8_t targetAmbientR, targetAmbientG, targetAmbientB;
    uint8_t targetDirectionalR, targetDirectionalG, targetDirectionalB;
    uint8_t targetEntityDirectR, targetEntityDirectG, targetEntityDirectB;
    float targetEntityDirX, targetEntityDirY, targetEntityDirZ;
    // Starting values for lerp (captured when lerp begins)
    uint8_t fromAmbientR, fromAmbientG, fromAmbientB;
    uint8_t fromDirectionalR, fromDirectionalG, fromDirectionalB;
    uint8_t fromEntityDirectR, fromEntityDirectG, fromEntityDirectB;
    float fromEntityDirX, fromEntityDirY, fromEntityDirZ;
    // Lerp progress (0.0 = at from, 1.0 = at target)
    float lerpProgress;
    bool isLerping;
    // Checkpoint lighting (restored on respawn)
    uint8_t checkpointAmbientR, checkpointAmbientG, checkpointAmbientB;
    uint8_t checkpointDirectionalR, checkpointDirectionalG, checkpointDirectionalB;
    uint8_t checkpointEntityDirectR, checkpointEntityDirectG, checkpointEntityDirectB;
    float checkpointEntityDirX, checkpointEntityDirY, checkpointEntityDirZ;
    bool hasCheckpointLighting;
} lightingState;

// Party fog (color-cycling fog that creeps in when jukebox plays)
static float partyFogIntensity = 0.0f;  // 0 = no fog, 1 = full fog
static float partyFogHue = 0.0f;        // Color cycling (0-360)

// HSV to RGB for party fog color cycling (h: 0-360, s/v: 0-1)
static void hsv_to_rgb(float h, float s, float v, uint8_t* r, uint8_t* g, uint8_t* b) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;

    if (h < 60)       { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }

    *r = (uint8_t)((rf + m) * 255.0f);
    *g = (uint8_t)((gf + m) * 255.0f);
    *b = (uint8_t)((bf + m) * 255.0f);
}

// Single player instance (for multiplayer parity)
static Player player;

// ============================================================
// MACRO BRIDGE: Static globals -> Player struct
// These macros redirect legacy static globals to use the Player struct,
// enabling gradual migration to the modular multiplayer architecture.
// Remove macros as code is properly refactored to use player.* directly.
// ============================================================

// Position (cubeX/Y/Z are the canonical names used throughout game.c)
#define cubeX player.x
#define cubeY player.y
#define cubeZ player.z

// Torso jump/charge state
#define isCharging player.isCharging
#define isJumping player.isJumping
#define isLanding player.isLanding
#define jumpAnimPaused player.jumpAnimPaused
#define jumpChargeTime player.jumpChargeTime
#define jumpAimX player.jumpAimX
#define jumpAimY player.jumpAimY
#define lastValidAimX player.lastValidAimX
#define lastValidAimY player.lastValidAimY
#define aimGraceTimer player.aimGraceTimer
#define jumpPeakY player.jumpPeakY
#define jumpArcEndX player.jumpArcEndX
#define torsoIsWallJumping player.torsoIsWallJumping
#define torsoWallJumpAngle player.torsoWallJumpAngle
#define wallJumpInputBuffer player.wallJumpInputBuffer
#define isBufferedCharge player.isBufferedCharge

// Arms mode state
#define isArmsMode player.isArmsMode
#define armsIsSpinning player.armsIsSpinning
#define armsIsWhipping player.armsIsWhipping
#define armsIsGliding player.armsIsGliding
#define armsHasDoubleJumped player.armsHasDoubleJumped
#define armsIsWallJumping player.armsIsWallJumping
#define armsSpinTime player.armsSpinTime
#define armsWhipTime player.armsWhipTime
#define armsWallJumpAngle player.armsWallJumpAngle
#define spinHitCooldown player.spinHitCooldown
#define spinHitEnemies player.spinHitEnemies

// Fullbody mode state (crouch mechanics)
#define fbIsCrouching player.fbIsCrouching
#define fbIsLongJumping player.fbIsLongJumping
#define fbIsBackflipping player.fbIsBackflipping
#define fbLongJumpSpeed player.fbLongJumpSpeed
#define fbIsHovering player.fbIsHovering
#define fbHoverTime player.fbHoverTime
#define fbHoverTiltX player.fbHoverTiltX
#define fbHoverTiltZ player.fbHoverTiltZ
#define fbHoverTiltVelX player.fbHoverTiltVelX
#define fbHoverTiltVelZ player.fbHoverTiltVelZ
#define fbIsSpinning player.fbIsSpinning
#define fbIsSpinningAir player.fbIsSpinningAir
#define fbSpinTime player.fbSpinTime
#define fbIsCharging player.fbIsCharging
#define fbChargeTime player.fbChargeTime
#define fbIsCrouchAttacking player.fbIsCrouchAttacking
#define fbCrouchAttackTime player.fbCrouchAttackTime
#define fbCrouchJumpWindup player.fbCrouchJumpWindup
#define fbCrouchJumpWindupTime player.fbCrouchJumpWindupTime
#define fbCrouchJumpRising player.fbCrouchJumpRising
#define fbIsWallJumping player.fbIsWallJumping
#define fbWallJumpAngle player.fbWallJumpAngle

// Squash and stretch
#define squashScale player.squashScale
#define squashVelocity player.squashVelocity
#define landingSquash player.landingSquash
#define chargeSquash player.chargeSquash

// Coyote/buffer
#define coyoteTimer player.coyoteTimer
#define jumpBufferTimer player.jumpBufferTimer

// Fidget animation
#define idleFrames player.idleFrames
#define playingFidget player.playingFidget
#define fidgetPlayTime player.fidgetPlayTime

// Slide state
#define wasSliding player.wasSliding
#define isSlidingFront player.isSlidingFront
#define isSlideRecovering player.isSlideRecovering
#define slideRecoverTime player.slideRecoverTime

// Health and damage
// NOTE: Generic names like 'health', 'playerIsDead', 'playerDeathTimer' conflict with decoration state
// (e.g., deco->state.slime.deathTimer), so we use specific prefixed names or access directly
#define playerHealth player.health
#define maxPlayerHealth player.maxHealth
#define playerIsDead player.isDead
#define playerIsHurt player.isHurt
#define playerHurtAnimTime player.hurtAnimTime
#define playerCurrentPainAnim player.currentPainAnim
#define playerInvincibilityTimer player.invincibilityTimer
#define playerInvincibilityFlashFrame player.invincibilityFlashFrame

// Death and respawn
#define playerDeathTimer player.deathTimer
#define playerIsRespawning player.isRespawning
#define playerRespawnDelayTimer player.respawnDelayTimer

// Camera smoothing
#define smoothCamX player.smoothCamX
#define smoothCamY player.smoothCamY
#define smoothCamTargetX player.smoothCamTargetX
#define smoothCollisionCamX player.smoothCollisionCamX
#define smoothCollisionCamY player.smoothCollisionCamY
#define smoothCollisionCamZ player.smoothCollisionCamZ

// Iris effect
#define irisRadius player.irisRadius
#define irisCenterX player.irisCenterX
#define irisCenterY player.irisCenterY
#define irisTargetX player.irisTargetX
#define irisTargetY player.irisTargetY
#define irisActive player.irisActive
#define irisPauseTimer player.irisPauseTimer
#define irisPaused player.irisPaused
#define fadeAlpha player.fadeAlpha

// ============================================================
// END MACRO BRIDGE
// ============================================================

// ============================================================
// CHARGEPAD BUFF SYSTEM
// ============================================================
// Buffs granted by standing on a chargepad, based on current body part:
// - Torso: 2x jump height on next charge jump
// - Arms: 2x glide distance (reduced gravity)
// - Legs/Fullbody: 5 seconds of double speed + invincibility

static bool buffJumpActive = false;        // Torso: 2x jump height on next jump
static bool buffGlideActive = false;       // Arms: 2x glide distance
static float buffSpeedTimer = 0.0f;        // Legs: remaining time for speed buff
static bool buffInvincible = false;        // Active during speed buff (legs mode)

// Getters for mapData.h (chargepad) to set buffs
bool buff_get_jump_active(void) { return buffJumpActive; }
void buff_set_jump_active(bool active) { buffJumpActive = active; }
bool buff_get_glide_active(void) { return buffGlideActive; }
void buff_set_glide_active(bool active) { buffGlideActive = active; }
float buff_get_speed_timer(void) { return buffSpeedTimer; }
void buff_set_speed_timer(float timer) { buffSpeedTimer = timer; buffInvincible = timer > 0.0f; }
bool buff_get_invincible(void) { return buffInvincible; }

// Get current body part for chargepad buff selection
// Returns: 0=HEAD, 1=TORSO, 2=ARMS, 3=LEGS
int get_current_body_part(void);  // Forward declaration - defined after currentPart

// ============================================================
// Player controls (legacy - will be migrated to player.physics/config)
static ControlConfig controlConfig;
static PlayerState playerState;

// Player collision parameters
float playerRadius = PLAYER_RADIUS;
float playerHeight = PLAYER_HEIGHT;
T3DVec3 camTarget = {{0, PLAYER_CAMERA_OFFSET_Y, 0}};

// Camera zoom levels (0 = close, 1 = normal, 2 = far)
static int cameraZoomLevel = 1;  // Start at normal zoom
static float smoothCamZoom = -150.0f;  // Smoothly lerps toward target zoom
#define CAM_ZOOM_CLOSE -110.0f   // Closer than normal
#define CAM_ZOOM_NORMAL -150.0f  // Current default
#define CAM_ZOOM_FAR -200.0f     // More zoomed out
#define CAM_ZOOM_LERP_SPEED 0.08f  // How fast zoom transitions (0-1, lower = smoother)

T3DModel *playerModel;

// Player model and animations (loaded per body part: Robo_torso, Robo_arms, Robo_fb)
T3DModel *torsoModel;
T3DSkeleton torsoSkel;

// Buff FX texture swap system
static sprite_t* buffFxSprite = NULL;        // FX texture for buff glow
static sprite_t* playerOriginalTexture = NULL; // Original player texture (stored for swap)
static float buffFlashTimer = 0.0f;          // Timer for slow flash effect
static bool buffFlashState = false;          // Current flash state (normal/FX)

// Simple cube model for jump arc visualization
T3DModel *arcDotModel;
T3DVertPacked* arcDotVerts;  // Flat-shaded arc dots (brighter than model-based)
T3DSkeleton torsoSkelBlend;
T3DAnim torsoAnimIdle;
T3DAnim torsoAnimWalk;
T3DAnim torsoAnimWalkSlow;
T3DAnim torsoAnimJumpCharge;
T3DAnim torsoAnimJumpLaunch;
T3DAnim torsoAnimJumpDouble;   // Double jump animation (second in combo)
T3DAnim torsoAnimJumpTriple;   // Triple jump animation (third in combo)
T3DAnim torsoAnimJumpLand;
T3DAnim torsoAnimWait;
T3DAnim torsoAnimPain1;
T3DAnim torsoAnimPain2;
T3DAnim torsoAnimDeath;
T3DAnim torsoAnimSlideFront;
T3DAnim torsoAnimSlideFrontRecover;
T3DAnim torsoAnimSlideBack;
T3DAnim torsoAnimSlideBackRecover;
bool torsoHasAnims = false;

// Arms mode animations (different character/controls)
T3DAnim armsAnimIdle;
T3DAnim armsAnimWalk1;
T3DAnim armsAnimWalk2;
T3DAnim armsAnimJump;
T3DAnim armsAnimJumpLand;
T3DAnim armsAnimAtkSpin;
T3DAnim armsAnimAtkWhip;
T3DAnim armsAnimDeath;
T3DAnim armsAnimPain1;
T3DAnim armsAnimPain2;
T3DAnim armsAnimSlide;
bool armsHasAnims = false;

// Fullbody (legs) mode animations
T3DAnim fbAnimIdle;
T3DAnim fbAnimWalk;
T3DAnim fbAnimRun;
T3DAnim fbAnimJump;
T3DAnim fbAnimWait;
T3DAnim fbAnimCrouch;
T3DAnim fbAnimCrouchJump;
T3DAnim fbAnimCrouchJumpHover;
T3DAnim fbAnimLongJump;
T3DAnim fbAnimDeath;
T3DAnim fbAnimPain1;
T3DAnim fbAnimPain2;
T3DAnim fbAnimSlide;
T3DAnim fbAnimSpinAir;
T3DAnim fbAnimSpinAtk;
T3DAnim fbAnimSpinCharge;
T3DAnim fbAnimRunNinja;
T3DAnim fbAnimCrouchAttack;
bool fbHasAnims = false;

// Cutscene falloff model and animation (loaded on-demand when triggered)
static T3DModel* cutsceneModel = NULL;
static T3DSkeleton cutsceneSkel;
static T3DAnim cutsceneAnim;
static bool cutsceneModelLoaded = false;
static bool cutsceneFalloffPlaying = false;
static float cutsceneFalloffTimer = 0.0f;
static float cutsceneFalloffX = 0.0f;
static float cutsceneFalloffY = 0.0f;
static float cutsceneFalloffZ = 0.0f;
static float cutsceneFalloffRotY = 0.0f;
static bool cutscenePlayerHidden = false;

// Cutscene 2 slideshow system (double-buffered to prevent glitches)
#define CS2_FRAME_COUNT 37
bool g_cs2Triggered = false;  // Global trigger flag (extern declared in mapData.h)
static bool cs2Playing = false;
static int cs2CurrentFrame = 0;
static float cs2FrameTimer = 0.0f;
static sprite_t* cs2CurrentSprite = NULL;
static sprite_t* cs2NextSprite = NULL;  // Pre-loaded next frame to prevent glitches

// Frame durations in 30fps game frames (converted from 24fps Blender: frames * 1.25)
static const uint8_t CS2_FRAME_DURATIONS[CS2_FRAME_COUNT] = {
    33, 9, 5, 19, 5, 14, 5, 30, 38, 11,   // CS2_1 to CS2_10
    6, 8, 6, 6, 14, 6, 8, 6, 6, 25,       // CS2_11 to CS2_20
    10, 6, 19, 8, 8, 6, 5, 15, 8, 8,      // CS2_21 to CS2_30
    8, 8, 19, 21, 21, 14, 60              // CS2_31 to CS2_37
};

// Pre-load the next CS2 frame (call after displaying current frame)
static void cs2_preload_next(int nextFrameNum) {
    if (nextFrameNum >= CS2_FRAME_COUNT) return;
    if (cs2NextSprite) return;  // Already pre-loaded

    char path[32];
    snprintf(path, sizeof(path), "rom:/CS2_%d.sprite", nextFrameNum + 1);
    cs2NextSprite = sprite_load(path);
    debugf("CS2: Pre-loaded frame %d\n", nextFrameNum + 1);
}

// Load first CS2 frame and pre-load second (call at start)
static void cs2_load_frame(int frameNum) {
    // Wait for RDP to finish using any existing textures
    rspq_wait();

    // Free old sprites
    if (cs2CurrentSprite) {
        sprite_free(cs2CurrentSprite);
        cs2CurrentSprite = NULL;
    }
    if (cs2NextSprite) {
        sprite_free(cs2NextSprite);
        cs2NextSprite = NULL;
    }

    // Load current frame (1-indexed in filenames)
    char path[32];
    snprintf(path, sizeof(path), "rom:/CS2_%d.sprite", frameNum + 1);
    cs2CurrentSprite = sprite_load(path);
    debugf("CS2: Loaded frame %d\n", frameNum + 1);

    // Pre-load next frame
    cs2_preload_next(frameNum + 1);
}

// Advance to next frame (uses pre-loaded sprite for smooth transition)
static void cs2_advance_frame(void) {
    // Wait for RDP to finish using current texture before freeing
    rspq_wait();

    // Free old current sprite
    if (cs2CurrentSprite) {
        sprite_free(cs2CurrentSprite);
    }

    // Move pre-loaded to current
    cs2CurrentSprite = cs2NextSprite;
    cs2NextSprite = NULL;

    // Pre-load the next one
    cs2_preload_next(cs2CurrentFrame + 1);
}

// ============================================================
// CUTSCENE 3 (ENDING SLIDESHOW) SYSTEM
// ============================================================
// 40 segments with double-buffered sprite loading

#define CS3_SEGMENT_COUNT 40
bool g_cs3Triggered = false;  // Global trigger flag (extern declared in mapData.h)
static bool cs3Playing = false;
static int cs3CurrentSegment = 0;
static float cs3FrameTimer = 0.0f;
static sprite_t* cs3CurrentSprite = NULL;
static sprite_t* cs3NextSprite = NULL;  // Pre-loaded next frame to prevent glitches

// Frame durations for each segment (in 30fps game frames)
static const uint16_t CS3_FRAME_DURATIONS[CS3_SEGMENT_COUNT] = {
    19, 7, 17, 16, 27, 21, 7, 32, 33, 9,     // CS_3_1 to CS_3_10
    6, 19, 25, 24, 7, 17, 14, 16, 7, 5,      // CS_3_11 to CS_3_20
    16, 6, 15, 19, 13, 21, 9, 11, 7, 7,      // CS_3_21 to CS_3_30
    14, 8, 14, 18, 18, 7, 8, 8, 33, 220      // CS_3_31 to CS_3_40
};

// Pre-load the next CS3 frame (call after displaying current frame)
static void cs3_preload_next(int nextSegmentNum) {
    if (nextSegmentNum >= CS3_SEGMENT_COUNT) return;
    if (cs3NextSprite) return;  // Already pre-loaded

    char path[32];
    snprintf(path, sizeof(path), "rom:/CS_3_%d.sprite", nextSegmentNum + 1);
    cs3NextSprite = sprite_load(path);
    debugf("CS3: Pre-loaded segment %d\n", nextSegmentNum + 1);
}

// Load first CS3 frame and pre-load second (call at start)
static void cs3_load_frame(int segmentNum) {
    // Wait for RDP to finish using any existing textures
    rspq_wait();

    // Free old sprites
    if (cs3CurrentSprite) {
        sprite_free(cs3CurrentSprite);
        cs3CurrentSprite = NULL;
    }
    if (cs3NextSprite) {
        sprite_free(cs3NextSprite);
        cs3NextSprite = NULL;
    }

    // Load current segment (1-indexed in filenames)
    char path[32];
    snprintf(path, sizeof(path), "rom:/CS_3_%d.sprite", segmentNum + 1);
    cs3CurrentSprite = sprite_load(path);
    debugf("CS3: Loaded segment %d\n", segmentNum + 1);

    // Pre-load next segment
    cs3_preload_next(segmentNum + 1);
}

// Advance to next frame (uses pre-loaded sprite for smooth transition)
static void cs3_advance_frame(void) {
    // Wait for RDP to finish using current texture before freeing
    rspq_wait();

    // Free old current sprite
    if (cs3CurrentSprite) {
        sprite_free(cs3CurrentSprite);
    }

    // Move pre-loaded to current
    cs3CurrentSprite = cs3NextSprite;
    cs3NextSprite = NULL;

    // Pre-load the next one
    cs3_preload_next(cs3CurrentSegment + 1);
}

// ============================================================
// CUTSCENE 4 (CS4) - Arms explanation cutscene (2 frames, 2.5s each)
// ============================================================

#define CS4_SEGMENT_COUNT 2
bool g_cs4Triggered = false;  // Global trigger flag
static bool cs4Playing = false;
static int cs4CurrentSegment = 0;
static float cs4FrameTimer = 0.0f;
static sprite_t* cs4CurrentSprite = NULL;
static sprite_t* cs4NextSprite = NULL;

// Frame durations: 2.5 seconds each = 75 frames at 30fps
static const uint16_t CS4_FRAME_DURATIONS[CS4_SEGMENT_COUNT] = { 75, 75 };

static void cs4_preload_next(int nextSegmentNum) {
    if (nextSegmentNum >= CS4_SEGMENT_COUNT) return;
    if (cs4NextSprite) return;

    char path[32];
    snprintf(path, sizeof(path), "rom:/CS_4_%d.sprite", nextSegmentNum + 1);
    cs4NextSprite = sprite_load(path);
    debugf("CS4: Pre-loaded segment %d\n", nextSegmentNum + 1);
}

static void cs4_load_frame(int segmentNum) {
    rspq_wait();
    if (cs4CurrentSprite) { sprite_free(cs4CurrentSprite); cs4CurrentSprite = NULL; }
    if (cs4NextSprite) { sprite_free(cs4NextSprite); cs4NextSprite = NULL; }

    char path[32];
    snprintf(path, sizeof(path), "rom:/CS_4_%d.sprite", segmentNum + 1);
    cs4CurrentSprite = sprite_load(path);
    debugf("CS4: Loaded segment %d\n", segmentNum + 1);
    cs4_preload_next(segmentNum + 1);
}

static void cs4_advance_frame(void) {
    rspq_wait();
    if (cs4CurrentSprite) { sprite_free(cs4CurrentSprite); }
    cs4CurrentSprite = cs4NextSprite;
    cs4NextSprite = NULL;
    cs4_preload_next(cs4CurrentSegment + 1);
}

// Fog override system (for DECO_FOGCOLOR triggers)
static bool fogOverrideActive = false;
static uint8_t fogOverrideR = 0, fogOverrideG = 0, fogOverrideB = 0;
static float fogOverrideNear = 0.0f, fogOverrideFar = 0.0f;
static float fogOverrideBlend = 0.0f;
static uint8_t currentFogR = 0, currentFogG = 0, currentFogB = 0;
static float currentFogNear = 0.0f, currentFogFar = 0.0f;

// Background color lerping (follows fog color for smooth transitions)
static float bgLerpR = 0.0f, bgLerpG = 0.0f, bgLerpB = 0.0f;
static bool bgLerpInitialized = false;
#define BG_LERP_SPEED 2.0f  // Same speed as fog color lerp

void game_get_current_fog(uint8_t* fogR, uint8_t* fogG, uint8_t* fogB, float* fogNear, float* fogFar) {
    *fogR = currentFogR;
    *fogG = currentFogG;
    *fogB = currentFogB;
    *fogNear = currentFogNear;
    *fogFar = currentFogFar;
}

void game_set_fog_override(uint8_t fogR, uint8_t fogG, uint8_t fogB, float fogNear, float fogFar, float blend) {
    fogOverrideActive = true;
    fogOverrideR = fogR;
    fogOverrideG = fogG;
    fogOverrideB = fogB;
    fogOverrideNear = fogNear;
    fogOverrideFar = fogFar;
    fogOverrideBlend = blend;
}

void game_clear_fog_override(void) {
    fogOverrideActive = false;
    bgLerpInitialized = false;  // Reset background lerp on level change
}

// Arms mode state - NOW IN PLAYER STRUCT VIA MACRO BRIDGE
// (See macro definitions after `static Player player;`)
static const float AIM_GRACE_THRESHOLD = 0.15f;  // Stick magnitude threshold for "significant" input
static const float AIM_GRACE_DURATION = 0.1f;    // Grace period duration in seconds (100ms)

// Squash and stretch - NOW IN PLAYER STRUCT VIA MACRO BRIDGE

// Coyote time and jump buffering - NOW IN PLAYER STRUCT VIA MACRO BRIDGE
static const float COYOTE_TIME = 0.3f;       // Increased for more forgiving controls
static const float JUMP_BUFFER_TIME = 0.25f; // Increased from 0.13f to compensate for input lag
static const float BUFFERED_CHARGE_BONUS = 3.0f; // Bonus charge rate multiplier for buffered jumps

// Triple jump combo system (TORSO mode)
static int jumpComboCount = 0;           // 0=first, 1=second, 2=third jump
static float jumpComboTimer = 0.0f;      // Grace period to continue combo after landing
static const float JUMP_COMBO_WINDOW = 0.5f;  // Seconds after landing to continue combo (if not buffering)
static bool lastJumpWasAimed = false;    // True if player aimed (moved stick) on last jump - required for combo
static T3DAnim* currentLaunchAnim = NULL; // Currently active launch animation (single/double/triple)
static const float JUMP_COMBO_CHARGE_MULT_1 = 1.0f;   // First jump: normal charge rate
static const float JUMP_COMBO_CHARGE_MULT_2 = 1.5f;   // Second jump: 1.5x charge rate
static const float JUMP_COMBO_CHARGE_MULT_3 = 2.0f;   // Third jump: 2x charge rate
static const float JUMP_COMBO_POWER_MULT_1 = 1.0f;    // First jump: normal velocity
static const float JUMP_COMBO_POWER_MULT_2 = 1.3f;    // Second jump: 1.3x velocity
static const float JUMP_COMBO_POWER_MULT_3 = 1.6f;    // Third jump: 1.6x velocity
// Animation progress thresholds - how much of the jump animation plays before pausing
// Adjust these to control when the barrel roll / backflip animation freezes mid-air
static const float JUMP_ANIM_PROGRESS_1 = 0.5f;       // First jump: pause at 50% (simple hop)
static const float JUMP_ANIM_PROGRESS_2 = 0.95f;      // Second jump: pause at 95% (barrel roll)
static const float JUMP_ANIM_PROGRESS_3 = 0.95f;      // Third jump: pause at 95% (backflip)

// Slide animation state - NOW IN PLAYER STRUCT VIA MACRO BRIDGE

// Track currently attached animation to avoid expensive re-attaching every frame
static T3DAnim* currentlyAttachedAnim = NULL;

// Helper: Check if animation has valid bone targets for the given skeleton
static inline bool anim_is_safe_to_attach(T3DAnim* anim, T3DSkeleton* skel) {
    if (!anim || !anim->animRef || !skel) return false;

    // Check all channel mappings have valid bone targets
    uint16_t totalChannels = anim->animRef->channelsQuat + anim->animRef->channelsScalar;
    uint16_t boneCount = skel->skeletonRef->boneCount;

    for (uint16_t i = 0; i < totalChannels; i++) {
        uint16_t targetIdx = anim->animRef->channelMappings[i].targetIdx;
        if (targetIdx >= boneCount && targetIdx != 0xFFFF) {
            // Invalid target - bone index out of range
            debugf("ANIM INVALID: channel %d targets bone %d but skeleton has %d bones\n",
                   i, targetIdx, boneCount);
            return false;
        }
        if (targetIdx == 255 || targetIdx == 0xFFFF) {
            // Sentinel value indicating unmapped channel - skip but don't fail
            // Actually, target 255 causes the crash, so fail on it
            debugf("ANIM INVALID: channel %d has sentinel target %d\n", i, targetIdx);
            return false;
        }
    }
    return true;
}

// Helper: Check if animation is safe to update (has been properly attached with valid targets)
static inline bool anim_is_safe_to_update(T3DAnim* anim) {
    if (!anim || !anim->animRef) return false;
    // Check that animation targets have been set by t3d_anim_attach
    uint16_t quatChannels = anim->animRef->channelsQuat;
    uint16_t scalarChannels = anim->animRef->channelsScalar;
    // If we have quat channels but no targets array, animation was never attached
    if (quatChannels > 0 && !anim->targetsQuat) return false;
    // If we have scalar channels but no targets array, animation was never attached
    if (scalarChannels > 0 && !anim->targetsScalar) return false;
    return true;
}

// Helper: Only attach animation if it's different from current (avoids expensive re-attach)
static inline void attach_anim_if_different(T3DAnim* anim, T3DSkeleton* skel) {
    // Debug: trace launch animation attachment
    bool isLaunchAnim = (anim == &torsoAnimJumpLaunch || anim == &torsoAnimJumpDouble || anim == &torsoAnimJumpTriple);
    if (isLaunchAnim) {
        const char* animName = (anim == &torsoAnimJumpLaunch) ? "launch" :
                               (anim == &torsoAnimJumpDouble) ? "DOUBLE" : "TRIPLE";
        debugf("ATTACH: %s - current=%p new=%p same=%d\n", animName, currentlyAttachedAnim, anim, currentlyAttachedAnim == anim);
    }

    if (currentlyAttachedAnim != anim) {
        // Basic null check only - don't destroy animations
        if (!anim || !anim->animRef || !skel) {
            if (isLaunchAnim) debugf("  -> SKIP: null check failed (anim=%p animRef=%p skel=%p)\n", anim, anim ? anim->animRef : NULL, skel);
            return;
        }
        // Validate animation bone targets don't exceed skeleton bone count
        // This prevents "Unknown animation target N" assertion failures
        uint16_t skelBoneCount = skel->skeletonRef ? skel->skeletonRef->boneCount : 0;
        uint16_t totalChannels = anim->animRef->channelsQuat + anim->animRef->channelsScalar;
        for (uint16_t i = 0; i < totalChannels; i++) {
            uint16_t targetIdx = anim->animRef->channelMappings[i].targetIdx;
            if (targetIdx >= skelBoneCount && targetIdx < 255) {
                // Animation targets a bone that doesn't exist in skeleton - skip attach
                debugf("WARN: Anim target %d exceeds skeleton bones %d - skipping attach\n",
                       targetIdx, skelBoneCount);
                return;
            }
        }
        t3d_anim_attach(anim, skel);
        currentlyAttachedAnim = anim;
        if (isLaunchAnim) debugf("  -> ATTACHED OK\n");
    } else {
        if (isLaunchAnim) debugf("  -> SKIP: same animation already attached\n");
    }
}

// Health and damage system - NOW IN PLAYER STRUCT VIA MACRO BRIDGE
#define INVINCIBILITY_DURATION 1.5f       // Seconds of invincibility after damage
#define INVINCIBILITY_FLASH_RATE 4        // Flash every N frames (lower = faster)

// Death transition - NOW IN PLAYER STRUCT VIA MACRO BRIDGE

// Death iris effect - NOW IN PLAYER STRUCT VIA MACRO BRIDGE
#define IRIS_PAUSE_RADIUS 25.0f         // Radius at which to pause
#define IRIS_PAUSE_DURATION 0.33f       // How long to hold the small circle (seconds)

// Level transition
static bool isTransitioning = false;
static float transitionTimer = 0.0f;
static int targetTransitionLevel = 0;
static int targetTransitionSpawn = 0;
static bool celebrateReturnToMenu = false;  // User chose "Return to Menu" on level complete

// Pre-transition state (for immediate level transitions with Psyops logo)
static bool isPreTransitioning = false;
static float preTransitionTimer = 0.0f;
static int preTransitionTargetLevel = 0;
static int preTransitionTargetSpawn = 0;
#define PRE_TRANSITION_WAIT_TIME 2.0f      // Wait/fade to black before whistle
#define PRE_TRANSITION_THUD_TIME 1.5f      // Time after whistle to play crash thud
#define PRE_TRANSITION_LOGO_DELAY 0.5f     // Delay after thud before showing logo
#define PRE_TRANSITION_LOGO_TIME 1.5f      // How long to show game logo
static wav64_t sfxSlideWhistle;
static wav64_t sfxMetallicThud;
static bool preTransitionSoundsLoaded = false;
static sprite_t* preTransitionLogo = NULL;  // Game logo sprite

// ============================================================
// LEVEL COMPLETE CELEBRATION SYSTEM
// Fireworks + UI overlay before iris transition
// ============================================================

typedef enum {
    CELEBRATE_INACTIVE,
    CELEBRATE_FIREWORKS,      // Fireworks playing, gameplay paused
    CELEBRATE_UI_SHOWING,     // UI overlay visible, waiting for A press
    CELEBRATE_DONE,           // A was pressed, ready for iris transition
    CELEBRATE_CS2_SLIDESHOW   // Playing CS2 slideshow (after level 3 completion)
} CelebratePhase;

#define MAX_CELEBRATE_FIREWORKS 6
#define MAX_CELEBRATE_SPARKS 48
#define CELEBRATE_FIREWORK_INTERVAL 0.35f
#define CELEBRATE_FIREWORK_DURATION 2.5f  // How long fireworks play before UI appears

typedef struct {
    float x, y, z;           // World position
    float velY;              // Upward velocity
    bool active;
    bool exploded;
    uint8_t colorIdx;        // Color index (0-6)
} CelebrateFirework;

typedef struct {
    float x, y, z;
    float velX, velY, velZ;
    float life;
    float maxLife;
    uint8_t colorIdx;
    bool active;
} CelebrateSpark;

static CelebratePhase celebratePhase = CELEBRATE_INACTIVE;
static float celebrateTimer = 0.0f;
static float celebrateFireworkSpawnTimer = 0.0f;
static float celebrateWorldX = 0.0f;   // World position where celebration happens
static float celebrateWorldY = 0.0f;
static float celebrateWorldZ = 0.0f;
static CelebrateFirework celebrateFireworks[MAX_CELEBRATE_FIREWORKS];
static CelebrateSpark celebrateSparks[MAX_CELEBRATE_SPARKS];
static int celebrateBlinkTimer = 0;
static uint32_t celebrateRng = 54321;

// Celebration colors (bright, celebratory)
static const color_t CELEBRATE_COLORS[] = {
    {0xFF, 0x44, 0x44, 0xFF},  // Red
    {0x44, 0xFF, 0x44, 0xFF},  // Green
    {0x44, 0x88, 0xFF, 0xFF},  // Blue
    {0xFF, 0xFF, 0x44, 0xFF},  // Yellow
    {0xFF, 0x44, 0xFF, 0xFF},  // Magenta
    {0x44, 0xFF, 0xFF, 0xFF},  // Cyan
    {0xFF, 0x88, 0x44, 0xFF},  // Orange
};
#define NUM_CELEBRATE_COLORS 7

// Simple RNG for celebration
static uint32_t celebrate_rand(void) {
    celebrateRng = celebrateRng * 1103515245 + 12345;
    return (celebrateRng >> 16) & 0x7FFF;
}

static float celebrate_randf(void) {
    return (float)celebrate_rand() / 32767.0f;
}

// Cached rank for level complete display
static char celebrateRank = 'D';

// Calculate rank based on performance
// S = Perfect (no deaths, all bolts, under time limit)
// A = Great (no deaths OR all bolts and fast)
// B = Good (few deaths, decent completion)
// C = Okay (completed with some struggle)
// D = Completed (just finished)
static char calculate_level_rank(int deaths, int boltsCollected, int totalBolts, float time) {
    bool allBolts = (boltsCollected == totalBolts);
    float boltRatio = totalBolts > 0 ? (float)boltsCollected / (float)totalBolts : 1.0f;

    // S Rank: No deaths, all bolts, under 5 minutes
    if (deaths == 0 && allBolts && time < 300.0f) {
        return 'S';
    }

    // A Rank: No deaths with most bolts OR all bolts with few deaths and good time
    if ((deaths == 0 && boltRatio >= 0.75f) ||
        (allBolts && deaths <= 1 && time < 420.0f)) {
        return 'A';
    }

    // B Rank: Low deaths with decent bolt collection
    if ((deaths <= 2 && boltRatio >= 0.5f) || (deaths == 0)) {
        return 'B';
    }

    // C Rank: Moderate deaths or low bolt collection
    if (deaths <= 5 && boltRatio >= 0.25f) {
        return 'C';
    }

    // D Rank: Just completed
    return 'D';
}

// Level-local stats (for level complete screen)
static int levelDeaths = 0;           // Deaths in current level attempt
static float levelTime = 0.0f;        // Time spent in current level
static int levelBoltsCollected = 0;   // Bolts collected in this level

// Replay mode flag (set when watching a replay from menu)
bool g_replayMode = false;            // True when in replay playback mode

// Virtual input state for replay playback
// These are set from replay data and used by the game logic
static uint16_t g_replayButtonsHeld = 0;      // Current frame's held buttons
static uint16_t g_replayButtonsPrev = 0;      // Previous frame's buttons (for pressed detection)
static int8_t g_replayStickX = 0;
static int8_t g_replayStickY = 0;

// Button masks for replay (must match controls.c)
#define REPLAY_BTN_A       0x0001
#define REPLAY_BTN_B       0x0002
#define REPLAY_BTN_Z       0x0004
#define REPLAY_BTN_START   0x0008
#define REPLAY_BTN_DU      0x0010
#define REPLAY_BTN_DD      0x0020
#define REPLAY_BTN_DL      0x0040
#define REPLAY_BTN_DR      0x0080
#define REPLAY_BTN_L       0x0100
#define REPLAY_BTN_R       0x0200
#define REPLAY_BTN_CU      0x0400
#define REPLAY_BTN_CD      0x0800
#define REPLAY_BTN_CL      0x1000
#define REPLAY_BTN_CR      0x2000

// Tutorial state (forward declared for use by button helper functions)
static bool tutorialActive = false;      // Is tutorial currently showing?
static bool tutorialJustDismissed = false; // Block input for rest of frame after dismissal
static int tutorialBodyPart = -1;        // Which body part tutorial (1=torso, 2=arms, 3=fullbody)
static int tutorialPage = 0;             // Current page of multi-page tutorial
static int tutorialPageCount = 1;        // Total pages for current tutorial

// Helper to get buttons held - returns replay/demo data if in playback mode
static joypad_buttons_t get_game_buttons_held(void) {
    joypad_buttons_t btns = {0};
    // Block all button input during tutorial or frame after dismissal
    if (tutorialActive || tutorialJustDismissed) {
        return btns;
    }
    if ((g_replayMode && replay_is_playing()) || (g_demoMode && demo_is_playing())) {
        btns.a = (g_replayButtonsHeld & REPLAY_BTN_A) != 0;
        btns.b = (g_replayButtonsHeld & REPLAY_BTN_B) != 0;
        btns.z = (g_replayButtonsHeld & REPLAY_BTN_Z) != 0;
        btns.start = (g_replayButtonsHeld & REPLAY_BTN_START) != 0;
        btns.d_up = (g_replayButtonsHeld & REPLAY_BTN_DU) != 0;
        btns.d_down = (g_replayButtonsHeld & REPLAY_BTN_DD) != 0;
        btns.d_left = (g_replayButtonsHeld & REPLAY_BTN_DL) != 0;
        btns.d_right = (g_replayButtonsHeld & REPLAY_BTN_DR) != 0;
        btns.l = (g_replayButtonsHeld & REPLAY_BTN_L) != 0;
        btns.r = (g_replayButtonsHeld & REPLAY_BTN_R) != 0;
        btns.c_up = (g_replayButtonsHeld & REPLAY_BTN_CU) != 0;
        btns.c_down = (g_replayButtonsHeld & REPLAY_BTN_CD) != 0;
        btns.c_left = (g_replayButtonsHeld & REPLAY_BTN_CL) != 0;
        btns.c_right = (g_replayButtonsHeld & REPLAY_BTN_CR) != 0;
    } else {
        btns = joypad_get_buttons_held(JOYPAD_PORT_1);
    }
    return btns;
}

// Helper to get buttons pressed (just pressed this frame)
static joypad_buttons_t get_game_buttons_pressed(void) {
    joypad_buttons_t btns = {0};
    // Block all button input during tutorial or frame after dismissal
    if (tutorialActive || tutorialJustDismissed) {
        return btns;
    }
    if ((g_replayMode && replay_is_playing()) || (g_demoMode && demo_is_playing())) {
        // Pressed = held now but NOT held last frame
        uint16_t pressed = g_replayButtonsHeld & ~g_replayButtonsPrev;
        btns.a = (pressed & REPLAY_BTN_A) != 0;
        btns.b = (pressed & REPLAY_BTN_B) != 0;
        btns.z = (pressed & REPLAY_BTN_Z) != 0;
        btns.start = (pressed & REPLAY_BTN_START) != 0;
        btns.d_up = (pressed & REPLAY_BTN_DU) != 0;
        btns.d_down = (pressed & REPLAY_BTN_DD) != 0;
        btns.d_left = (pressed & REPLAY_BTN_DL) != 0;
        btns.d_right = (pressed & REPLAY_BTN_DR) != 0;
        btns.l = (pressed & REPLAY_BTN_L) != 0;
        btns.r = (pressed & REPLAY_BTN_R) != 0;
        btns.c_up = (pressed & REPLAY_BTN_CU) != 0;
        btns.c_down = (pressed & REPLAY_BTN_CD) != 0;
        btns.c_left = (pressed & REPLAY_BTN_CL) != 0;
        btns.c_right = (pressed & REPLAY_BTN_CR) != 0;
    } else {
        btns = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    }
    return btns;
}

// Helper to get stick inputs
static joypad_inputs_t get_game_inputs(void) {
    joypad_inputs_t inputs = {0};
    if ((g_replayMode && replay_is_playing()) || (g_demoMode && demo_is_playing())) {
        inputs.stick_x = g_replayStickX;
        inputs.stick_y = g_replayStickY;
    } else {
        inputs = joypad_get_inputs(JOYPAD_PORT_1);
    }
    return inputs;
}

// Pause menu
static OptionPrompt pauseMenu;
static OptionPrompt optionsMenu;  // Submenu for options
static bool isPaused = false;
static bool pauseMenuInitialized = false;
static bool optionsMenuInitialized = false;
static bool isInOptionsMenu = false;  // True when options submenu is open
static bool isQuittingToMenu = false;  // Iris out then go to menu

// Dialogue/Script system for interact triggers
static DialogueBox dialogueBox;
static DialogueScript activeScript;
static bool scriptRunning = false;
static DecoInstance* activeInteractTrigger = NULL;  // Currently interacting trigger

// Level banner (shows level name on entry)
static LevelBanner levelBanner;

// ============================================================
// CONTROLS TUTORIAL SYSTEM
// ============================================================
// Shows control hints on first play of each body part type
// (Tutorial state variables declared earlier for use by button helpers)

// Tutorial text for each body part - multi-page support
// Torso: 1 page
static const char* TUTORIAL_TORSO_PAGES[] = {
    "TORSO CONTROLS\n\n"
    "Hold A to charge jump.\n"
    "Release for single, double,\n"
    "or triple jump (shown by\n"
    "different arc colors).\n\n"
    "Tap A for a quick hop."
};
#define TUTORIAL_TORSO_PAGE_COUNT 1

// Arms: 2 pages
static const char* TUTORIAL_ARMS_PAGES[] = {
    "ARMS CONTROLS\n\n"
    "A - Jump\n"
    "B - Spin Attack\n\n"
    "Spin in the air to\n"
    "'hover' short distances!",

    "ARMS CONTROLS (2/2)\n\n"
    "Wall Jump: Jump into a\n"
    "wall, then press A again\n"
    "to bounce off!"
};
#define TUTORIAL_ARMS_PAGE_COUNT 2

// Fullbody: 2 pages
static const char* TUTORIAL_FULLBODY_PAGES[] = {
    "FULL BODY CONTROLS\n\n"
    "A - Jump\n"
    "Z - Crouch\n"
    "B - Spin Attack\n"
    "Z + C-Left - Kick Attack",

    "FULL BODY (2/2)\n\n"
    "Running + Z + A = Long Jump\n"
    "Standing + Z + A = Hover Jump"
};
#define TUTORIAL_FULLBODY_PAGE_COUNT 2

// Check if tutorial should show for current level and body part
static void tutorial_check_and_show(int levelId, int bodyPart) {
    debugf("Tutorial check: levelId=%d, bodyPart=%d, demoMode=%d\n", levelId, bodyPart, g_demoMode);

    // Skip in demo mode
    if (g_demoMode) return;

    // Torso tutorial: Level 2 (index 1), bodyPart = 1
    if (levelId == 1 && bodyPart == 1) {
        debugf("Tutorial: Torso level detected, seen=%d\n", save_has_seen_tutorial_torso());
        if (!save_has_seen_tutorial_torso()) {
            tutorialActive = true;
            tutorialBodyPart = 1;
            tutorialPage = 0;
            tutorialPageCount = TUTORIAL_TORSO_PAGE_COUNT;
            debugf("Tutorial: Showing Torso controls tutorial (%d pages)\n", tutorialPageCount);
            return;
        }
    }

    // Arms tutorial: Level 4 (index 3), bodyPart = 2
    if (levelId == 3 && bodyPart == 2) {
        debugf("Tutorial: Arms level detected, seen=%d\n", save_has_seen_tutorial_arms());
        if (!save_has_seen_tutorial_arms()) {
            tutorialActive = true;
            tutorialBodyPart = 2;
            tutorialPage = 0;
            tutorialPageCount = TUTORIAL_ARMS_PAGE_COUNT;
            debugf("Tutorial: Showing Arms controls tutorial (%d pages)\n", tutorialPageCount);
            return;
        }
    }

    // Fullbody tutorial: Level 6 (index 5), bodyPart = 3
    if (levelId == 5 && bodyPart == 3) {
        debugf("Tutorial: Fullbody level detected, seen=%d\n", save_has_seen_tutorial_fullbody());
        if (!save_has_seen_tutorial_fullbody()) {
            tutorialActive = true;
            tutorialBodyPart = 3;
            tutorialPage = 0;
            tutorialPageCount = TUTORIAL_FULLBODY_PAGE_COUNT;
            debugf("Tutorial: Showing Fullbody controls tutorial (%d pages)\n", tutorialPageCount);
            return;
        }
    }
}

// Update tutorial (check for A press to advance page or dismiss)
static bool tutorial_update(joypad_port_t port) {
    if (!tutorialActive) return false;

    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);

    if (pressed.a) {
        ui_play_press_a_sound();

        // Check if there are more pages
        if (tutorialPage < tutorialPageCount - 1) {
            // Advance to next page
            tutorialPage++;
            debugf("Tutorial: Advanced to page %d/%d\n", tutorialPage + 1, tutorialPageCount);
            return true;
        }

        // Last page - mark tutorial as seen and dismiss
        switch (tutorialBodyPart) {
            case 1: save_mark_tutorial_torso_seen(); break;
            case 2: save_mark_tutorial_arms_seen(); break;
            case 3: save_mark_tutorial_fullbody_seen(); break;
        }
        tutorialActive = false;
        tutorialJustDismissed = true;  // Block input for rest of this frame
        tutorialBodyPart = -1;
        tutorialPage = 0;
        tutorialPageCount = 1;

        // Re-enable player controls
        playerState.canMove = true;
        playerState.canJump = true;
        playerState.canRotate = true;

        debugf("Tutorial: Dismissed, controls enabled\n");
        return true;
    }

    return true; // Consume input while tutorial is showing
}

// Draw tutorial box
static void tutorial_draw(void) {
    if (!tutorialActive) return;

    // Get the correct page of text for current tutorial
    const char* text = NULL;
    switch (tutorialBodyPart) {
        case 1: text = TUTORIAL_TORSO_PAGES[tutorialPage]; break;
        case 2: text = TUTORIAL_ARMS_PAGES[tutorialPage]; break;
        case 3: text = TUTORIAL_FULLBODY_PAGES[tutorialPage]; break;
        default: return;
    }

    // Large centered box for tutorial
    int boxWidth = 230;
    int boxHeight = 145;
    int boxX = (SCREEN_WIDTH - boxWidth) / 2;
    int boxY = (SCREEN_HEIGHT - boxHeight) / 2;
    int padding = 14;
    int lineHeight = 12;

    // Draw the box using UI system
    ui_draw_box(boxX, boxY, boxWidth, boxHeight, UI_COLOR_BG, UI_COLOR_BORDER);

    // Draw text with auto word wrap
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(UI_COLOR_TEXT);

    int textX = boxX + padding;
    int textY = boxY + padding + 8;
    int maxWidth = boxWidth - padding * 2;

    // Use ui_draw_text_wrapped for auto word wrapping
    ui_draw_text_wrapped(text, (int)strlen(text), textX, textY, maxWidth, lineHeight);

    // Draw flashing "Press A" indicator at bottom right
    static int blinkTimer = 0;
    blinkTimer++;
    if (blinkTimer >= 40) blinkTimer = 0;

    if (blinkTimer < 20) {
        int indicatorX = boxX + boxWidth - 30;
        int indicatorY = boxY + boxHeight - 18;
        rdpq_text_printf(NULL, 2, indicatorX, indicatorY, "a");  // A button icon
    }
}

// ============================================================
// COUNTDOWN SYSTEM (3-2-1-GO!)
// ============================================================
#define COUNTDOWN_TIME_PER_NUMBER 0.8f  // Time each number stays on screen
#define COUNTDOWN_GO_TIME 0.6f          // Time "GO!" stays on screen

typedef enum {
    COUNTDOWN_INACTIVE,
    COUNTDOWN_3,
    COUNTDOWN_2,
    COUNTDOWN_1,
    COUNTDOWN_GO,
    COUNTDOWN_DONE
} CountdownState;

static CountdownState countdownState = COUNTDOWN_INACTIVE;
static float countdownTimer = 0.0f;     // Time in current state
static float countdownScale = 1.0f;     // Current scale for bounce effect
static float countdownAlpha = 1.0f;     // Fade out alpha
static bool countdownPending = false;   // Waiting for iris to finish before starting

// Start the countdown (or queue it if iris is active)
// DISABLED for single player - countdown only used in multiplayer scene
static void countdown_start(void) {
    return;  // No countdown in single player
}

// Queue countdown to start after iris finishes
// DISABLED for single player - countdown only used in multiplayer scene
static void countdown_queue(void) {
    return;  // No countdown in single player
}

// Check if countdown is blocking player movement
// DISABLED for single player - countdown only used in multiplayer scene
static bool countdown_is_active(void) {
    return false;  // No countdown in single player
}

// Bouncy ease-out function (overshoots then settles)
static float countdown_ease_out_back(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

// Elastic bounce for extra cartoon feel
static float countdown_ease_out_elastic(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    float p = 0.3f;
    return powf(2.0f, -10.0f * t) * sinf((t - p / 4.0f) * (2.0f * 3.14159f) / p) + 1.0f;
}

// Update countdown state (pass isPaused, playerIsRespawning, irisActive to check conditions)
// DISABLED for single player - countdown only used in multiplayer scene
static void countdown_update(float deltaTime, bool paused, bool respawning, bool irisOn) {
    return;  // No countdown in single player

    // If pending, wait for iris/respawn to finish before starting
    if (countdownPending) {
        if (!respawning && !irisOn) {
            // Iris finished, start the countdown now
            countdownPending = false;
            countdown_start();
        }
        return;
    }

    if (countdownState == COUNTDOWN_INACTIVE || countdownState == COUNTDOWN_DONE) {
        return;
    }

    countdownTimer += deltaTime;

    // Calculate bounce scale based on time in state
    float stateTime = (countdownState == COUNTDOWN_GO) ? COUNTDOWN_GO_TIME : COUNTDOWN_TIME_PER_NUMBER;
    float progress = countdownTimer / stateTime;

    if (progress < 0.4f) {
        // Pop in with elastic bounce (first 40% of state time)
        float popProgress = progress / 0.4f;
        countdownScale = countdown_ease_out_elastic(popProgress) * 1.5f;
        countdownAlpha = 1.0f;
    } else if (progress < 0.7f) {
        // Hold at full size
        countdownScale = 1.5f;
        countdownAlpha = 1.0f;
    } else {
        // Shrink and fade out (last 30%)
        float fadeProgress = (progress - 0.7f) / 0.3f;
        countdownScale = 1.5f * (1.0f - fadeProgress * 0.5f);
        countdownAlpha = 1.0f - fadeProgress;
    }

    // Transition to next state
    if (countdownTimer >= stateTime) {
        countdownTimer = 0.0f;
        switch (countdownState) {
            case COUNTDOWN_3:
                countdownState = COUNTDOWN_2;
                break;
            case COUNTDOWN_2:
                countdownState = COUNTDOWN_1;
                break;
            case COUNTDOWN_1:
                countdownState = COUNTDOWN_GO;
                break;
            case COUNTDOWN_GO:
                countdownState = COUNTDOWN_DONE;
                // Unload countdown sprites when done
                for (int i = 0; i < 4; i++) {
                    if (countdownSprites[i]) {
                        sprite_free(countdownSprites[i]);
                        countdownSprites[i] = NULL;
                    }
                }
                break;
            default:
                break;
        }
    }
}

// Draw countdown with sprites
// DISABLED for single player - countdown only used in multiplayer scene
static void countdown_draw(void) {
    return;  // No countdown in single player

    // Lazy load countdown sprites on first use
    if (countdownSprites[0] == NULL) {
        countdownSprites[0] = sprite_load("rom:/Three.sprite");  // 3
        countdownSprites[1] = sprite_load("rom:/Two.sprite");    // 2
        countdownSprites[2] = sprite_load("rom:/One.sprite");    // 1
        countdownSprites[3] = sprite_load("rom:/Go.sprite");     // GO
    }

    // Get the sprite for current state
    sprite_t* sprite = NULL;
    float baseScale = 4.0f;  // Base scale for 16x16 sprites to look good
    int spriteWidth = 16;
    int spriteHeight = 16;

    switch (countdownState) {
        case COUNTDOWN_3:
            sprite = countdownSprites[0];
            break;
        case COUNTDOWN_2:
            sprite = countdownSprites[1];
            break;
        case COUNTDOWN_1:
            sprite = countdownSprites[2];
            break;
        case COUNTDOWN_GO:
            sprite = countdownSprites[3];
            spriteWidth = 32;  // Go is 32x16
            baseScale = 2.0f;  // Scale Go at 2x so it appears same height as others
            break;
        default:
            return;
    }

    if (!sprite) return;

    // Screen center
    int centerX = 160;
    int centerY = 120;

    // Add slight wobble for extra cartoon feel
    float wobble = sinf(countdownTimer * 20.0f) * 0.03f * countdownScale;
    float displayScale = (countdownScale + wobble) * baseScale;

    // Calculate sprite position (centered)
    float scaledWidth = spriteWidth * displayScale;
    float scaledHeight = spriteHeight * displayScale;
    int spriteX = centerX - (int)(scaledWidth / 2.0f);
    int spriteY = centerY - (int)(scaledHeight / 2.0f);

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);

    rdpq_sprite_blit(sprite, spriteX, spriteY, &(rdpq_blitparms_t){
        .scale_x = displayScale,
        .scale_y = displayScale
    });
}

// ============================================================
// SCREEN SHAKE & HIT FLASH
// ============================================================
static float shakeIntensity = 0.0f;   // Current shake strength (decays over time)
static float shakeOffsetX = 0.0f;     // Current frame's X offset
static float shakeOffsetY = 0.0f;     // Current frame's Y offset
static float hitFlashTimer = 0.0f;    // Time remaining for hit flash (0 = no flash)
static float boltFlashTimer = 0.0f;   // Time remaining for bolt collect flash (white)
static float hitstopTimer = 0.0f;     // Time remaining for hitstop freeze frame (0 = no freeze)

// Trigger screen shake (call when taking damage, landing hard, etc.)
static inline void trigger_screen_shake(float intensity) {
    if (intensity > shakeIntensity) {
        shakeIntensity = intensity;
    }
}

// Trigger hit flash (call when player takes damage)
static inline void trigger_hit_flash(float duration) {
    hitFlashTimer = duration;
}

// Trigger bolt flash (call when collecting a bolt)
static inline void trigger_bolt_flash(void) {
    boltFlashTimer = 0.1f;  // Brief white flash
}

// Trigger hitstop (brief freeze frame on damage)
static inline void trigger_hitstop(float duration) {
    hitstopTimer = duration;
}

// Global wrapper for hitstop (callable from mapData.h for enemy kills)
void game_trigger_hitstop(float duration) {
    trigger_hitstop(duration);
}

// Update shake (call each frame)
static inline void update_screen_shake(float deltaTime) {
    if (shakeIntensity > 0.1f) {
        // Random offset based on intensity
        shakeOffsetX = ((rand() % 200) - 100) / 100.0f * shakeIntensity;
        shakeOffsetY = ((rand() % 200) - 100) / 100.0f * shakeIntensity;
        // Decay shake
        shakeIntensity *= 0.85f;
    } else {
        shakeIntensity = 0.0f;
        shakeOffsetX = 0.0f;
        shakeOffsetY = 0.0f;
    }

    // Decay hit flash
    if (hitFlashTimer > 0.0f) {
        hitFlashTimer -= deltaTime;
        if (hitFlashTimer < 0.0f) hitFlashTimer = 0.0f;
    }

    // Decay bolt flash
    if (boltFlashTimer > 0.0f) {
        boltFlashTimer -= deltaTime;
        if (boltFlashTimer < 0.0f) boltFlashTimer = 0.0f;
    }
}

// ============================================================
// SIMPLE PARTICLE SYSTEM (screen-space, no collision)
// ============================================================
#define MAX_PARTICLES 24

typedef struct {
    float x, y, z;           // World position
    float velX, velY, velZ;  // Velocity
    float life;              // Remaining lifetime (0 = dead)
    float maxLife;           // Initial lifetime (for fade calc)
    uint8_t r, g, b;         // Color
    float size;              // Particle size
    bool active;             // Is this slot in use?
} Particle;

static Particle g_particles[MAX_PARTICLES];

// Death decals - persistent ground splats when slimes die
#define MAX_DEATH_DECALS 8
typedef struct {
    float x, y, z;
    float scale;
    float alpha;
    bool active;
    bool isLava;  // True for lava slime (orange), false for regular slime (dark)
} DeathDecal;
static DeathDecal g_deathDecals[MAX_DEATH_DECALS];

// Spawn a death decal at position
static inline void spawn_death_decal(float x, float y, float z, float scale, bool isLava) {
    // Find oldest or inactive slot
    int slot = 0;
    float lowestAlpha = 999.0f;
    for (int i = 0; i < MAX_DEATH_DECALS; i++) {
        if (!g_deathDecals[i].active) {
            slot = i;
            break;
        }
        if (g_deathDecals[i].alpha < lowestAlpha) {
            lowestAlpha = g_deathDecals[i].alpha;
            slot = i;
        }
    }
    g_deathDecals[slot].x = x;
    g_deathDecals[slot].y = y;
    g_deathDecals[slot].z = z;
    g_deathDecals[slot].scale = scale;
    g_deathDecals[slot].alpha = 1.0f;
    g_deathDecals[slot].active = true;
    g_deathDecals[slot].isLava = isLava;
}

// Update death decals (fade out)
static inline void update_death_decals(float deltaTime) {
    for (int i = 0; i < MAX_DEATH_DECALS; i++) {
        if (g_deathDecals[i].active) {
            g_deathDecals[i].alpha -= deltaTime * 0.3f;  // Fade over ~3 seconds
            if (g_deathDecals[i].alpha <= 0.0f) {
                g_deathDecals[i].active = false;
            }
        }
    }
}

// Impact stars - cartoon stars orbiting player's head on damage/hard landing
#define IMPACT_STAR_COUNT 4
#define IMPACT_STAR_RADIUS 12.0f   // Orbit radius around head
#define IMPACT_STAR_HEIGHT 25.0f   // Height above player base
#define IMPACT_STAR_DURATION 2.0f  // How long stars show

static float g_impactStarsTimer = 0.0f;  // Time remaining for stars
static float g_impactStarsAngle = 0.0f;  // Rotation angle for orbit

// Trigger impact stars around player
static inline void spawn_impact_stars(void) {
    g_impactStarsTimer = IMPACT_STAR_DURATION;
    g_impactStarsAngle = 0.0f;
}

// Update impact stars
static inline void update_impact_stars(float deltaTime) {
    if (g_impactStarsTimer > 0.0f) {
        g_impactStarsTimer -= deltaTime;
        g_impactStarsAngle += 5.0f * deltaTime;  // Rotate around head
    }
}

// Initialize particle system (call once at startup)
static inline void init_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        g_particles[i].active = false;
        g_particles[i].life = 0.0f;
    }
    for (int i = 0; i < MAX_DEATH_DECALS; i++) {
        g_deathDecals[i].active = false;
    }
}

// Spawn particles in an arc (for slime splash)
static inline void spawn_splash_particles(float x, float y, float z, int count,
                                          uint8_t r, uint8_t g, uint8_t b) {
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            p->x = x;
            p->y = y + 2.0f;
            p->z = z;

            // Random outward velocity
            float angle = (float)(rand() % 628) / 100.0f;
            float speed = 1.5f + (float)(rand() % 100) / 50.0f;
            p->velX = cosf(angle) * speed;
            p->velZ = sinf(angle) * speed;
            p->velY = 3.0f + (float)(rand() % 100) / 50.0f;

            p->maxLife = 1.0f + (float)(rand() % 30) / 100.0f;
            p->life = p->maxLife;
            p->r = r;
            p->g = g;
            p->b = b;
            p->size = 2.0f + (float)(rand() % 15) / 10.0f;
            spawned++;
        }
    }
}

// Spawn dust puffs (big, slow, floaty - for player landing)
static inline void spawn_dust_particles(float x, float y, float z, int count) {
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            p->x = x + ((rand() % 20) - 10);  // Slight random offset
            p->y = y + 3.0f;
            p->z = z + ((rand() % 20) - 10);

            // Slow outward and upward drift
            float angle = (float)(rand() % 628) / 100.0f;
            float speed = 0.3f + (float)(rand() % 50) / 100.0f;
            p->velX = cosf(angle) * speed;
            p->velZ = sinf(angle) * speed;
            p->velY = 0.8f + (float)(rand() % 50) / 100.0f;  // Gentle upward

            p->maxLife = 0.25f + (float)(rand() % 15) / 100.0f;
            p->life = p->maxLife;
            // Brownish-gray dust color
            p->r = 140 + (rand() % 30);
            p->g = 120 + (rand() % 30);
            p->b = 100 + (rand() % 30);
            p->size = 4.0f + (float)(rand() % 30) / 10.0f;  // Big puffs
            spawned++;
        }
    }
}

// Spawn oil splash particles (brown, goopy - for slime hits)
static inline void spawn_oil_particles(float x, float y, float z, int count) {
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            p->x = x + ((rand() % 16) - 8);
            p->y = y + 5.0f;
            p->z = z + ((rand() % 16) - 8);

            // Fast outward splatter
            float angle = (float)(rand() % 628) / 100.0f;
            float speed = 2.5f + (float)(rand() % 150) / 50.0f;
            p->velX = cosf(angle) * speed;
            p->velZ = sinf(angle) * speed;
            p->velY = 4.0f + (float)(rand() % 200) / 50.0f;

            p->maxLife = 0.6f + (float)(rand() % 30) / 100.0f;
            p->life = p->maxLife;
            // Brown/dark oil colors
            p->r = 60 + (rand() % 40);
            p->g = 40 + (rand() % 30);
            p->b = 20 + (rand() % 20);
            p->size = 3.0f + (float)(rand() % 20) / 10.0f;
            spawned++;
        }
    }
}

// Spawn sparks (dramatic starburst pattern - for bolt collection)
static inline void spawn_spark_particles(float x, float y, float z, int count) {
    int spawned = 0;
    // First pass: spawn radial starburst particles (evenly spaced angles)
    int starburstCount = count / 2;
    for (int i = 0; i < MAX_PARTICLES && spawned < starburstCount; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            p->x = x;
            p->y = y;
            p->z = z;

            // Evenly spaced radial burst
            float angle = (float)spawned / (float)starburstCount * 6.283f;
            float speed = 5.0f + (float)(rand() % 100) / 50.0f;  // Faster for drama
            p->velX = cosf(angle) * speed;
            p->velZ = sinf(angle) * speed;
            p->velY = 3.0f + (float)(rand() % 100) / 50.0f;  // Upward arc

            p->maxLife = 0.5f + (float)(rand() % 20) / 100.0f;
            p->life = p->maxLife;
            // Bright yellow sparks
            p->r = 255;
            p->g = 230 + (rand() % 25);
            p->b = 100 + (rand() % 100);
            p->size = 2.0f + (float)(rand() % 15) / 10.0f;  // Slightly bigger
            spawned++;
        }
    }

    // Second pass: random scatter particles for filling
    int scatterCount = count - starburstCount;
    int scattered = 0;
    for (int i = 0; i < MAX_PARTICLES && scattered < scatterCount; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            p->x = x;
            p->y = y;
            p->z = z;

            // Random scatter
            float angle = (float)(rand() % 628) / 100.0f;
            float speed = 2.0f + (float)(rand() % 150) / 50.0f;
            p->velX = cosf(angle) * speed;
            p->velZ = sinf(angle) * speed;
            p->velY = 2.0f + (float)(rand() % 200) / 50.0f;

            p->maxLife = 0.4f + (float)(rand() % 20) / 100.0f;
            p->life = p->maxLife;
            // Yellow/orange spark colors
            p->r = 255;
            p->g = 200 + (rand() % 55);
            p->b = 50 + (rand() % 100);
            p->size = 1.5f + (float)(rand() % 10) / 10.0f;
            scattered++;
        }
    }
}

// Spawn electricity/lightning particles (small, fast, jagged - for spin attack)
static inline void spawn_electric_particles(float x, float y, float z, int count) {
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;

            // Random offset from center (around player)
            float angle = (float)(rand() % 628) / 100.0f;
            float radius = 8.0f + (float)(rand() % 100) / 10.0f;  // 8-18 units from center
            p->x = x + cosf(angle) * radius;
            p->y = y + (float)(rand() % 200) / 10.0f;  // 0-20 units up
            p->z = z + sinf(angle) * radius;

            // Jagged, erratic velocity (zigzag pattern)
            float zigzag = ((rand() % 2) * 2 - 1) * 3.0f;  // -3 or +3
            p->velX = ((float)(rand() % 100) / 50.0f - 1.0f) * 2.0f + zigzag;
            p->velZ = ((float)(rand() % 100) / 50.0f - 1.0f) * 2.0f - zigzag;
            p->velY = 1.0f + (float)(rand() % 100) / 50.0f;  // Small upward drift

            p->maxLife = 0.15f + (float)(rand() % 10) / 100.0f;  // Very short lived (0.15-0.25s)
            p->life = p->maxLife;

            // Electric blue/cyan/white colors
            int colorType = rand() % 3;
            if (colorType == 0) {
                // Bright cyan
                p->r = 100 + (rand() % 50);
                p->g = 200 + (rand() % 55);
                p->b = 255;
            } else if (colorType == 1) {
                // White core
                p->r = 230 + (rand() % 25);
                p->g = 240 + (rand() % 15);
                p->b = 255;
            } else {
                // Electric blue
                p->r = 80 + (rand() % 40);
                p->g = 150 + (rand() % 50);
                p->b = 255;
            }
            p->size = 1.0f + (float)(rand() % 10) / 10.0f;  // Small sparks
            spawned++;
        }
    }
}

// Spawn vapor trail particles (for rail turret projectiles)
// Small, slow-fading particles with minimal velocity
static inline void spawn_trail_particles(float x, float y, float z, int count) {
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            
            // Slight random offset for volume
            float offsetX = ((float)(rand() % 100) / 100.0f - 0.5f) * 2.0f;
            float offsetY = ((float)(rand() % 100) / 100.0f - 0.5f) * 2.0f;
            float offsetZ = ((float)(rand() % 100) / 100.0f - 0.5f) * 2.0f;
            p->x = x + offsetX;
            p->y = y + offsetY;
            p->z = z + offsetZ;
            
            // Minimal velocity (mostly static - just spreads slowly)
            p->velX = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.5f;
            p->velY = 0.2f + (float)(rand() % 50) / 100.0f;  // Slight upward drift
            p->velZ = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.5f;
            
            p->maxLife = 0.3f + (float)(rand() % 20) / 100.0f;  // Short lived (0.3-0.5s)
            p->life = p->maxLife;
            
            // White to light-blue vapor colors
            int colorType = rand() % 2;
            if (colorType == 0) {
                // Pure white vapor
                p->r = 240 + (rand() % 15);
                p->g = 245 + (rand() % 10);
                p->b = 255;
            } else {
                // Light cyan/blue vapor
                p->r = 180 + (rand() % 50);
                p->g = 220 + (rand() % 35);
                p->b = 255;
            }
            p->size = 1.5f + (float)(rand() % 10) / 10.0f;  // Small puffs
            spawned++;
        }
    }
}

// Update all particles
static inline void update_particles(float deltaTime) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &g_particles[i];
        if (!p->active) continue;

        // Physics
        p->velY -= 0.4f;
        p->x += p->velX;
        p->y += p->velY;
        p->z += p->velZ;

        // Lifetime
        p->life -= deltaTime;

        // Deactivate dead particles
        if (p->life <= 0.0f) {
            p->active = false;
        }
    }
}

// ============================================================
// SLOPE-BASED PHYSICS SYSTEM
// ============================================================
// Slope-based ground physics system for natural movement on inclines
// Key features:
// - Forward velocity + facing angle (not separate X/Z velocity)
// - Slope steepness affects movement speed
// - Surface friction classes (normal, slippery, very slippery)
// - Acceleration/deceleration curves
// ============================================================

// Ground physics constants (tuned for this game's scale)
#define GROUND_WALK_ACCEL 0.8f           // Acceleration per frame
#define GROUND_WALK_SPEED_CAP 6.0f       // Normal max walk speed
#define GROUND_RUN_SPEED_CAP 10.0f       // Absolute max speed
#define GROUND_FRICTION_NORMAL 0.50f     // Ground friction (lower = stops faster, tighter)
#define GROUND_FRICTION_SLIPPERY 0.85f   // Slippery surface friction
#define GROUND_FRICTION_VERY_SLIPPERY 0.92f  // Oil/ice friction

// Slope-based sliding - uses constants from constants.h
// The key insight: acceleration is CONTINUOUS and scales with steepness
// No discrete buckets - just physics responding to the actual slope angle

// Calculate slope steepness from normal
static inline float slope_get_steepness(float nx, float ny, float nz) {
    (void)ny;
    return sqrtf(nx * nx + nz * nz);
}

// Slope-based sliding physics - acceleration and friction
// steepness = sqrt(nx^2 + nz^2) - the horizontal component of the normal
// downhillX/Z = normalized direction gravity would push you
static void slope_apply_slide_physics(PlayerState* state, float downhillX, float downhillZ, float steepness) {
    // Choose acceleration and friction based on surface
    float accel = state->onOilPuddle ? SLIDE_ACCEL_SLIPPERY : SLIDE_ACCEL;
    float friction = state->onOilPuddle ? SLIDE_FRICTION_SLIPPERY : SLIDE_FRICTION;

    // Gentle slope threshold - below this we can stop sliding
    const float GENTLE_THRESHOLD = 0.4f;

    // Apply gravity acceleration in downhill direction
    // Acceleration scales with steepness (steeper = faster acceleration)
    // On gentle slopes, reduce gravity so friction can win
    if (steepness > 0.01f) {
        float effectiveAccel = accel;
        if (steepness < GENTLE_THRESHOLD) {
            // Reduce gravity on gentle slopes - lerp from 0 to full accel
            float t = steepness / GENTLE_THRESHOLD;
            effectiveAccel = accel * t * t;  // Quadratic falloff for smoother stop
        }
        state->slideVelX += effectiveAccel * steepness * downhillX;
        state->slideVelZ += effectiveAccel * steepness * downhillZ;
    }

    // Apply friction - stronger on flat/gentle ground to stop sliding
    float effectiveFriction = friction;
    if (steepness < GENTLE_THRESHOLD) {
        // Lerp to flat-ground friction as slope becomes gentle
        float t = steepness / GENTLE_THRESHOLD;
        effectiveFriction = SLIDE_FRICTION_FLAT + t * (friction - SLIDE_FRICTION_FLAT);
    }
    state->slideVelX *= effectiveFriction;
    state->slideVelZ *= effectiveFriction;

    // Update facing direction based on velocity
    float speed = sqrtf(state->slideVelX * state->slideVelX + state->slideVelZ * state->slideVelZ);
    if (speed > 1.0f) {
        state->slideYaw = atan2f(state->slideVelZ, state->slideVelX);
    }
}

// Slope-based sliding update - returns true if sliding should stop
// This handles steering, physics, speed cap, and stop detection
static bool slope_update_sliding(PlayerState* state, float inputX, float inputZ, float downhillX, float downhillZ, float steepness) {
    float inputMag = sqrtf(inputX * inputX + inputZ * inputZ);
    float speed = sqrtf(state->slideVelX * state->slideVelX + state->slideVelZ * state->slideVelZ);

    // Steering: rotate velocity based on input
    if (inputMag > 0.1f && speed > 1.0f) {
        float steerFactor = inputMag * 0.05f;
        float oldVelX = state->slideVelX;
        float oldVelZ = state->slideVelZ;

        // Rotate velocity (asymmetric steering)
        state->slideVelX += oldVelZ * inputX * steerFactor;
        state->slideVelZ -= oldVelX * inputZ * steerFactor;

        // Preserve speed after steering
        float newSpeed = sqrtf(state->slideVelX * state->slideVelX + state->slideVelZ * state->slideVelZ);
        if (newSpeed > 0.1f) {
            state->slideVelX = state->slideVelX * speed / newSpeed;
            state->slideVelZ = state->slideVelZ * speed / newSpeed;
        }
    }

    // Apply gravity acceleration and friction
    slope_apply_slide_physics(state, downhillX, downhillZ, steepness);

    // Cap speed
    speed = sqrtf(state->slideVelX * state->slideVelX + state->slideVelZ * state->slideVelZ);
    if (speed > SLIDE_MAX_SPEED) {
        float scale = SLIDE_MAX_SPEED / speed;
        state->slideVelX *= scale;
        state->slideVelZ *= scale;
        speed = SLIDE_MAX_SPEED;
    }

    // Stop sliding on gentle slopes when speed is low
    // On steep slopes, keep sliding (gravity will accelerate)
    // 0.4 steepness ≈ 22 degrees - matches GENTLE_THRESHOLD in physics
    bool isGentleSlope = steepness < 0.4f;
    if (isGentleSlope && speed < SLIDE_STOP_THRESHOLD) {
        state->slideVelX = 0.0f;
        state->slideVelZ = 0.0f;
        return true;  // Stopped
    }

    return false;  // Still sliding
}

// Update walking speed with slope influence
// Returns the target forward speed based on input, with slope influence
__attribute__((unused))
static float slope_update_walking_speed(PlayerState* state, float inputMagnitude, float nx, float ny, float nz) {
    // Calculate current forward speed
    float forwardSpeed = sqrtf(state->velX * state->velX + state->velZ * state->velZ);

    // Target speed based on input (0 to GROUND_WALK_SPEED_CAP)
    float targetSpeed = inputMagnitude * GROUND_WALK_SPEED_CAP;

    // Accelerate toward target speed (speed-dependent acceleration)
    float accel = GROUND_WALK_ACCEL - forwardSpeed / 43.0f;
    if (accel < 0.1f) accel = 0.1f;

    if (forwardSpeed < targetSpeed) {
        forwardSpeed += accel;
        if (forwardSpeed > targetSpeed) forwardSpeed = targetSpeed;
    } else if (forwardSpeed > targetSpeed) {
        // Decelerate
        forwardSpeed -= accel * 2.0f;  // Decel faster than accel
        if (forwardSpeed < targetSpeed) forwardSpeed = targetSpeed;
    }

    // Apply slope influence on steep slopes
    float steepness = slope_get_steepness(nx, ny, nz);
    if (steepness > 0.1f && forwardSpeed > 1.0f) {
        // Get movement direction
        float moveDirX = sinf(state->playerAngle);
        float moveDirZ = cosf(state->playerAngle);

        // Dot product of movement direction and downhill direction
        // Positive = going uphill (against normal), Negative = going downhill
        float uphillDot = (moveDirX * nx + moveDirZ * nz) / steepness;

        if (uphillDot > 0.0f) {
            // Going uphill - reduce speed based on steepness and how directly uphill
            float uphillPenalty = 1.0f - steepness * uphillDot * 0.7f;
            if (uphillPenalty < 0.3f) uphillPenalty = 0.3f;
            forwardSpeed *= uphillPenalty;
        } else {
            // Going downhill - slight speed boost
            float downhillBoost = 1.0f - uphillDot * steepness * 0.3f;  // uphillDot is negative
            if (downhillBoost > 1.5f) downhillBoost = 1.5f;
            forwardSpeed *= downhillBoost;
        }
    }

    // Cap at absolute maximum
    if (forwardSpeed > GROUND_RUN_SPEED_CAP) forwardSpeed = GROUND_RUN_SPEED_CAP;

    return forwardSpeed;
}

// Apply ground friction when stopping
__attribute__((unused))
static void ground_apply_friction(PlayerState* state) {
    float friction;
    if (state->onOilPuddle) {
        friction = GROUND_FRICTION_VERY_SLIPPERY;
    } else {
        friction = GROUND_FRICTION_NORMAL;
    }

    state->velX *= friction;
    state->velZ *= friction;

    // Zero out tiny velocities
    if (state->velX * state->velX + state->velZ * state->velZ < 0.5f) {
        state->velX = 0.0f;
        state->velZ = 0.0f;
    }
}

T3DViewport viewport;
T3DMat4FP* playerMatFP;
T3DMat4FP* roboMatFP;
T3DMat4FP* arcMatFP;  // Matrices for jump arc dots

int frameIdx = 0;
rspq_block_t *dplDraw = NULL;

T3DVec3 camPos = {{0, -71.0f, -120.0f}};

// Player position - NOW IN PLAYER STRUCT VIA MACRO BRIDGE (player.x/y/z)

// Frame pacing: store previous frame positions for render interpolation
static float prevCubeX = 0.0f;
static float prevCubeY = 100.0f;
static float prevCubeZ = 0.0f;
static float renderCubeX = 0.0f;  // Interpolated position for rendering
static float renderCubeY = 100.0f;
static float renderCubeZ = 0.0f;
static float frameLerpFactor = 0.5f;  // How much to blend (0.5 = halfway between frames)
#define FRAME_PACING_ENABLED 1  // Set to 0 to disable interpolation

// Ground level (fallback) - set below death threshold (-500) so player can die from falling
float groundLevel = -600.0f;
float bestGroundY = -9999.0f;

// Camera smoothing - NOW IN PLAYER STRUCT VIA MACRO BRIDGE

// Camera collision smoothing - NOW IN PLAYER STRUCT VIA MACRO BRIDGE

// Debug fly camera
static float debugCamX = 0.0f;
static float debugCamY = 100.0f;
static float debugCamZ = -120.0f;
static float debugCamYaw = 0.0f;
static float debugCamPitch = 0.0f;

// Saved camera state when entering debug mode
static float savedCamZ = -120.0f;

// Debug placement mode
static bool debugPlacementMode = false;
static DecoType debugDecoType = DECO_BARREL;
static float debugDecoX = 0.0f;
static float debugDecoY = 0.0f;
static float debugDecoZ = 0.0f;
static float debugDecoRotY = 0.0f;
static float debugDecoScaleX = 1.0f;
static float debugDecoScaleY = 1.0f;
static float debugDecoScaleZ = 1.0f;
static int debugTriggerCooldown = 0;

// Patrol route placement mode
static bool patrolPlacementMode = false; // true while placing patrol route for a rat (or other future decorations)
static DecoType patrolDecoHolder; // holds whichever decoration is being placed while patrol route is being defined
static int patrolPointCount = 0; // number of patrol points placed for current decoration being placed
static T3DVec3* patrolPoints = NULL; // dynamic array of patrol points for current decoration being placed

// Debug delete mode (raycast selection in camera mode)
static int debugHighlightedDecoIndex = -1;  // Index of decoration currently highlighted for deletion
static float debugDeleteCooldown = 0.0f;    // Cooldown to prevent accidental rapid deletion

// Debug collision visualization
static bool debugShowCollision = false;  // Toggle with D-Left in debug mode

// Reverse gravity
bool reverseGravity = false;

// Player scale cheat (1.0 = normal, 2.0 = giant, 0.5 = tiny)
float g_playerScaleCheat = 1.0f;

// Tweakable gameplay parameters (charge jump) - scaled for 30 FPS
static float chargeJumpMaxBase = 4.0f * FPS_SCALE_SQRT;
static float chargeJumpMaxMultiplier = 1.5f * FPS_SCALE_SQRT;
static float chargeJumpEarlyBase = 3.0f * FPS_SCALE_SQRT;
static float chargeJumpEarlyMultiplier = 2.0f * FPS_SCALE_SQRT;
static float maxChargeTime = 1.5f;

// Small hop parameters (quick A tap)
static float hopThreshold = 0.15f;                      // Time threshold for hop vs charge (seconds)
static float hopVelocityY = 3.0f * FPS_SCALE_SQRT;      // Small hop vertical velocity
float holdX = 0.0f;
float holdZ = 0.0f;
static float hopForwardSpeed = 1.0f * FPS_SCALE;        // Small hop forward speed (gentle)

// sync_player_state() REMOVED - macros now redirect static globals to Player struct directly
// The macro bridge (defined after `static Player player;`) makes this function unnecessary.

// Check if game scene is initialized (for demo_scene.c to know if it's safe to update/draw)
bool is_game_scene_initialized(void) {
    return gameSceneInitialized;
}

// Initialize lighting state from level data
static void init_lighting_state(LevelID level) {
    // Get initial colors from level
    level_get_ambient_color(level, &lightingState.ambientR, &lightingState.ambientG, &lightingState.ambientB);
    level_get_directional_color(level, &lightingState.directionalR, &lightingState.directionalG, &lightingState.directionalB);

    // Entity light starts same as map directional light
    lightingState.entityDirectR = lightingState.directionalR;
    lightingState.entityDirectG = lightingState.directionalG;
    lightingState.entityDirectB = lightingState.directionalB;

    // Entity light direction starts same as map light direction
    level_get_light_direction(level, &lightingState.entityLightDirX,
                              &lightingState.entityLightDirY, &lightingState.entityLightDirZ);

    // Set targets to current (no lerping initially)
    lightingState.targetAmbientR = lightingState.ambientR;
    lightingState.targetAmbientG = lightingState.ambientG;
    lightingState.targetAmbientB = lightingState.ambientB;
    lightingState.targetDirectionalR = lightingState.directionalR;
    lightingState.targetDirectionalG = lightingState.directionalG;
    lightingState.targetDirectionalB = lightingState.directionalB;
    lightingState.targetEntityDirectR = lightingState.entityDirectR;
    lightingState.targetEntityDirectG = lightingState.entityDirectG;
    lightingState.targetEntityDirectB = lightingState.entityDirectB;
    lightingState.targetEntityDirX = lightingState.entityLightDirX;
    lightingState.targetEntityDirY = lightingState.entityLightDirY;
    lightingState.targetEntityDirZ = lightingState.entityLightDirZ;

    // No checkpoint lighting set yet
    lightingState.hasCheckpointLighting = false;
    lightingState.isLerping = false;
    lightingState.lerpProgress = 1.0f;
}

// Trigger a lighting change (called from checkpoint or DECO_LIGHT_TRIGGER)
// Pass 0 for any color to not change that component
// Pass 999.0f for direction components to not change direction
// Note: "directional" colors now refer to ENTITY light (affects player/decorations only)
// Map light is static from level data and cannot be changed at runtime
void game_trigger_lighting_change_ex(uint8_t ambientR, uint8_t ambientG, uint8_t ambientB,
                                      uint8_t entityDirectR, uint8_t entityDirectG, uint8_t entityDirectB,
                                      float entityDirX, float entityDirY, float entityDirZ) {
    // Check if any lighting change is specified
    bool hasAmbient = (ambientR != 0 || ambientG != 0 || ambientB != 0);
    bool hasEntityColor = (entityDirectR != 0 || entityDirectG != 0 || entityDirectB != 0);
    bool hasEntityDir = (entityDirX < 900.0f && entityDirY < 900.0f && entityDirZ < 900.0f);  // 999 means "no change"

    if (!hasAmbient && !hasEntityColor && !hasEntityDir) {
        return;  // No lighting change specified
    }

    // Capture current values as "from" for lerp
    lightingState.fromAmbientR = lightingState.ambientR;
    lightingState.fromAmbientG = lightingState.ambientG;
    lightingState.fromAmbientB = lightingState.ambientB;
    lightingState.fromEntityDirectR = lightingState.entityDirectR;
    lightingState.fromEntityDirectG = lightingState.entityDirectG;
    lightingState.fromEntityDirectB = lightingState.entityDirectB;
    lightingState.fromEntityDirX = lightingState.entityLightDirX;
    lightingState.fromEntityDirY = lightingState.entityLightDirY;
    lightingState.fromEntityDirZ = lightingState.entityLightDirZ;

    // Set target colors (keep current if not specified)
    if (hasAmbient) {
        lightingState.targetAmbientR = ambientR;
        lightingState.targetAmbientG = ambientG;
        lightingState.targetAmbientB = ambientB;
    } else {
        lightingState.targetAmbientR = lightingState.ambientR;
        lightingState.targetAmbientG = lightingState.ambientG;
        lightingState.targetAmbientB = lightingState.ambientB;
    }
    if (hasEntityColor) {
        lightingState.targetEntityDirectR = entityDirectR;
        lightingState.targetEntityDirectG = entityDirectG;
        lightingState.targetEntityDirectB = entityDirectB;
    } else {
        lightingState.targetEntityDirectR = lightingState.entityDirectR;
        lightingState.targetEntityDirectG = lightingState.entityDirectG;
        lightingState.targetEntityDirectB = lightingState.entityDirectB;
    }
    if (hasEntityDir) {
        lightingState.targetEntityDirX = entityDirX;
        lightingState.targetEntityDirY = entityDirY;
        lightingState.targetEntityDirZ = entityDirZ;
    } else {
        lightingState.targetEntityDirX = lightingState.entityLightDirX;
        lightingState.targetEntityDirY = lightingState.entityLightDirY;
        lightingState.targetEntityDirZ = lightingState.entityLightDirZ;
    }

    // Save checkpoint lighting for respawn
    lightingState.checkpointAmbientR = lightingState.targetAmbientR;
    lightingState.checkpointAmbientG = lightingState.targetAmbientG;
    lightingState.checkpointAmbientB = lightingState.targetAmbientB;
    lightingState.checkpointEntityDirectR = lightingState.targetEntityDirectR;
    lightingState.checkpointEntityDirectG = lightingState.targetEntityDirectG;
    lightingState.checkpointEntityDirectB = lightingState.targetEntityDirectB;
    lightingState.checkpointEntityDirX = lightingState.targetEntityDirX;
    lightingState.checkpointEntityDirY = lightingState.targetEntityDirY;
    lightingState.checkpointEntityDirZ = lightingState.targetEntityDirZ;
    lightingState.hasCheckpointLighting = true;

    // Start lerping
    lightingState.isLerping = true;
    lightingState.lerpProgress = 0.0f;

    debugf("Entity lighting change: color(%d,%d,%d)->(%d,%d,%d) dir(%.1f,%.1f,%.1f)->(%.1f,%.1f,%.1f)\n",
           lightingState.fromEntityDirectR, lightingState.fromEntityDirectG, lightingState.fromEntityDirectB,
           lightingState.targetEntityDirectR, lightingState.targetEntityDirectG, lightingState.targetEntityDirectB,
           lightingState.fromEntityDirX, lightingState.fromEntityDirY, lightingState.fromEntityDirZ,
           lightingState.targetEntityDirX, lightingState.targetEntityDirY, lightingState.targetEntityDirZ);
}

// Legacy wrapper for checkpoint compatibility (no direction change)
void game_trigger_lighting_change(uint8_t ambientR, uint8_t ambientG, uint8_t ambientB,
                                   uint8_t directionalR, uint8_t directionalG, uint8_t directionalB) {
    game_trigger_lighting_change_ex(ambientR, ambientG, ambientB,
                                     directionalR, directionalG, directionalB,
                                     999.0f, 999.0f, 999.0f);  // 999 = no direction change
}

// Get current lighting state (for light triggers to save/restore)
void game_get_current_lighting(uint8_t* ambientR, uint8_t* ambientG, uint8_t* ambientB,
                                uint8_t* entityDirectR, uint8_t* entityDirectG, uint8_t* entityDirectB,
                                float* entityDirX, float* entityDirY, float* entityDirZ) {
    *ambientR = lightingState.ambientR;
    *ambientG = lightingState.ambientG;
    *ambientB = lightingState.ambientB;
    *entityDirectR = lightingState.entityDirectR;
    *entityDirectG = lightingState.entityDirectG;
    *entityDirectB = lightingState.entityDirectB;
    *entityDirX = lightingState.entityLightDirX;
    *entityDirY = lightingState.entityLightDirY;
    *entityDirZ = lightingState.entityLightDirZ;
}

// Directly set lighting values (for distance-based blending, bypasses lerp system)
void game_set_lighting_direct(uint8_t ambientR, uint8_t ambientG, uint8_t ambientB,
                               uint8_t entityDirectR, uint8_t entityDirectG, uint8_t entityDirectB,
                               float entityDirX, float entityDirY, float entityDirZ) {
    lightingState.ambientR = ambientR;
    lightingState.ambientG = ambientG;
    lightingState.ambientB = ambientB;
    lightingState.entityDirectR = entityDirectR;
    lightingState.entityDirectG = entityDirectG;
    lightingState.entityDirectB = entityDirectB;
    // Only update direction if valid (< 900 means valid, 999 means no change)
    if (entityDirX < 900.0f) lightingState.entityLightDirX = entityDirX;
    if (entityDirY < 900.0f) lightingState.entityLightDirY = entityDirY;
    if (entityDirZ < 900.0f) lightingState.entityLightDirZ = entityDirZ;
    // Also update targets to match (prevents lerp system from fighting)
    lightingState.targetAmbientR = ambientR;
    lightingState.targetAmbientG = ambientG;
    lightingState.targetAmbientB = ambientB;
    lightingState.targetEntityDirectR = entityDirectR;
    lightingState.targetEntityDirectG = entityDirectG;
    lightingState.targetEntityDirectB = entityDirectB;
    if (entityDirX < 900.0f) lightingState.targetEntityDirX = entityDirX;
    if (entityDirY < 900.0f) lightingState.targetEntityDirY = entityDirY;
    if (entityDirZ < 900.0f) lightingState.targetEntityDirZ = entityDirZ;
    // Stop any ongoing lerp
    lightingState.isLerping = false;
    lightingState.lerpProgress = 1.0f;
}

// Restore checkpoint lighting (called on respawn)
static void restore_checkpoint_lighting(void) {
    if (lightingState.hasCheckpointLighting) {
        // Set current to checkpoint values (instant, no lerp on respawn)
        lightingState.ambientR = lightingState.checkpointAmbientR;
        lightingState.ambientG = lightingState.checkpointAmbientG;
        lightingState.ambientB = lightingState.checkpointAmbientB;
        // Entity light color and direction
        lightingState.entityDirectR = lightingState.checkpointEntityDirectR;
        lightingState.entityDirectG = lightingState.checkpointEntityDirectG;
        lightingState.entityDirectB = lightingState.checkpointEntityDirectB;
        lightingState.entityLightDirX = lightingState.checkpointEntityDirX;
        lightingState.entityLightDirY = lightingState.checkpointEntityDirY;
        lightingState.entityLightDirZ = lightingState.checkpointEntityDirZ;
        // Targets match current
        lightingState.targetAmbientR = lightingState.ambientR;
        lightingState.targetAmbientG = lightingState.ambientG;
        lightingState.targetAmbientB = lightingState.ambientB;
        lightingState.targetEntityDirectR = lightingState.entityDirectR;
        lightingState.targetEntityDirectG = lightingState.entityDirectG;
        lightingState.targetEntityDirectB = lightingState.entityDirectB;
        lightingState.targetEntityDirX = lightingState.entityLightDirX;
        lightingState.targetEntityDirY = lightingState.entityLightDirY;
        lightingState.targetEntityDirZ = lightingState.entityLightDirZ;
        lightingState.isLerping = false;
        lightingState.lerpProgress = 1.0f;
    }
}

// Update lighting lerp (call each frame)
static void update_lighting(float deltaTime) {
    if (!lightingState.isLerping) return;

    // Lerp speed (1.0 = complete in 1 second, 2.0 = 0.5 seconds)
    const float LIGHTING_LERP_SPEED = 2.0f;

    lightingState.lerpProgress += deltaTime * LIGHTING_LERP_SPEED;
    if (lightingState.lerpProgress >= 1.0f) {
        lightingState.lerpProgress = 1.0f;
        lightingState.isLerping = false;
        // Snap to target
        lightingState.ambientR = lightingState.targetAmbientR;
        lightingState.ambientG = lightingState.targetAmbientG;
        lightingState.ambientB = lightingState.targetAmbientB;
        lightingState.entityDirectR = lightingState.targetEntityDirectR;
        lightingState.entityDirectG = lightingState.targetEntityDirectG;
        lightingState.entityDirectB = lightingState.targetEntityDirectB;
        lightingState.entityLightDirX = lightingState.targetEntityDirX;
        lightingState.entityLightDirY = lightingState.targetEntityDirY;
        lightingState.entityLightDirZ = lightingState.targetEntityDirZ;
    } else {
        // Smooth lerp using smoothstep for nicer transition
        float t = lightingState.lerpProgress;
        float smooth = t * t * (3.0f - 2.0f * t);  // smoothstep

        // Lerp ambient color
        lightingState.ambientR = (uint8_t)(lightingState.fromAmbientR +
            (lightingState.targetAmbientR - lightingState.fromAmbientR) * smooth);
        lightingState.ambientG = (uint8_t)(lightingState.fromAmbientG +
            (lightingState.targetAmbientG - lightingState.fromAmbientG) * smooth);
        lightingState.ambientB = (uint8_t)(lightingState.fromAmbientB +
            (lightingState.targetAmbientB - lightingState.fromAmbientB) * smooth);
        // Lerp entity light color
        lightingState.entityDirectR = (uint8_t)(lightingState.fromEntityDirectR +
            (lightingState.targetEntityDirectR - lightingState.fromEntityDirectR) * smooth);
        lightingState.entityDirectG = (uint8_t)(lightingState.fromEntityDirectG +
            (lightingState.targetEntityDirectG - lightingState.fromEntityDirectG) * smooth);
        lightingState.entityDirectB = (uint8_t)(lightingState.fromEntityDirectB +
            (lightingState.targetEntityDirectB - lightingState.fromEntityDirectB) * smooth);
        // Lerp entity light direction
        lightingState.entityLightDirX = lightingState.fromEntityDirX +
            (lightingState.targetEntityDirX - lightingState.fromEntityDirX) * smooth;
        lightingState.entityLightDirY = lightingState.fromEntityDirY +
            (lightingState.targetEntityDirY - lightingState.fromEntityDirY) * smooth;
        lightingState.entityLightDirZ = lightingState.fromEntityDirZ +
            (lightingState.targetEntityDirZ - lightingState.fromEntityDirZ) * smooth;
    }
}

void init_game_scene(void) {
    // Enable RDP validator to debug hardware crashes
    // rdpq_debug_start();  // DISABLED - validator itself may be causing freezes

    // Seed random number generator for pain animation variety
    srand(TICKS_READ());

    // Initialize particle system
    init_particles();

    // Initialize replay system
    replay_init();

    // UI sprites and sounds are loaded globally in main.c

    // Set current level from level select (or default to LEVEL_1 if coming from non-debug start)
    currentLevel = (LevelID)selectedLevelID;
    if (currentLevel >= LEVEL_COUNT) {
        currentLevel = LEVEL_1;
    }

    // Reset level-local stats for level complete screen
    levelDeaths = 0;
    levelTime = 0.0f;
    levelBoltsCollected = 0;

    // Reset checkpoint (new level = fresh start)
    g_checkpointActive = false;
    g_checkpointX = 0.0f;
    g_checkpointY = 0.0f;
    g_checkpointZ = 0.0f;

    // Reset transition and celebration state (critical for scene re-entry)
    isTransitioning = false;
    transitionTimer = 0.0f;
    targetTransitionLevel = 0;
    targetTransitionSpawn = 0;
    celebrateReturnToMenu = false;
    isPreTransitioning = false;
    preTransitionTimer = 0.0f;
    celebratePhase = CELEBRATE_INACTIVE;

    // Reset cutscene states
    g_cs2Triggered = false;
    g_cs3Triggered = false;
    cs3Playing = false;
    cs3CurrentSegment = 0;
    g_cs4Triggered = false;
    cs4Playing = false;
    cs4CurrentSegment = 0;

    // Reset party fog state
    partyFogIntensity = 0.0f;
    partyFogHue = 0.0f;

    // Reset all activation states (doors, electric walls, etc.)
    activation_reset_all();

    // Reset HUD animation state
    healthHudY = HEALTH_HUD_HIDE_Y;
    healthHudTargetY = HEALTH_HUD_HIDE_Y;
    healthFlashTimer = 0.0f;
    healthHudVisible = false;
    healthHudHideTimer = 0.0f;
    screwHudX = SCREW_HUD_HIDE_X;
    screwHudTargetX = SCREW_HUD_HIDE_X;
    screwAnimFrame = 0;
    screwAnimTimer = 0.0f;
    screwHudVisible = false;
    screwHudHideTimer = 0.0f;

    // Reset chargepad buff state (prevents buffs persisting across scene transitions)
    buffJumpActive = false;
    buffGlideActive = false;
    buffSpeedTimer = 0.0f;
    buffInvincible = false;

    // Reset visual effect timers
    shakeIntensity = 0.0f;
    shakeOffsetX = 0.0f;
    shakeOffsetY = 0.0f;
    hitFlashTimer = 0.0f;
    boltFlashTimer = 0.0f;
    hitstopTimer = 0.0f;

    // Reset reward popup state
    rewardPopupActive = false;
    rewardPopupTimer = 0.0f;

    // Reset player damage state (via macro bridge to player struct)
    playerHealth = 3;
    maxPlayerHealth = 3;
    playerIsDead = false;
    playerIsHurt = false;
    playerIsRespawning = false; g_playerIsRespawning = false;
    playerInvincibilityTimer = 0.0f;
    playerHurtAnimTime = 0.0f;
    playerDeathTimer = 0.0f;
    playerRespawnDelayTimer = 0.0f;
    irisActive = false;
    irisRadius = 320.0f;
    fadeAlpha = 0;

    // Reset all state
    frameIdx = 0;
    debugFlyMode = false;

    // Reset player position and velocity
    cubeX = 0.0f;
    cubeY = 100.0f;
    cubeZ = 0.0f;
    // Initialize frame pacing positions to match
    prevCubeX = cubeX;
    prevCubeY = cubeY;
    prevCubeZ = cubeZ;
    renderCubeX = cubeX;
    renderCubeY = cubeY;
    renderCubeZ = cubeZ;
    bestGroundY = -9999.0f;

    // Reset camera
    camPos = (T3DVec3){{0, -71.0f, -120.0f}};
    camTarget = (T3DVec3){{0, -100.0f, 0}};
    smoothCamX = 0.0f;
    smoothCamY = 49.0f;
    smoothCollisionCamX = 0.0f;
    smoothCollisionCamY = 49.0f;
    smoothCollisionCamZ = -120.0f;

    // Reset debug fly camera
    debugCamX = 0.0f;
    debugCamY = 100.0f;
    debugCamZ = -120.0f;
    debugCamYaw = 0.0f;
    debugCamPitch = 0.0f;

    // Reset debug placement mode
    debugPlacementMode = false;
    debugDecoType = DECO_BARREL;
    debugDecoX = 0.0f;
    debugDecoY = 0.0f;
    debugDecoZ = 0.0f;
    debugDecoRotY = 0.0f;
    debugDecoScaleX = 1.0f;
    debugDecoScaleY = 1.0f;
    debugDecoScaleZ = 1.0f;
    debugTriggerCooldown = 0;
    debugHighlightedDecoIndex = -1;
    debugDeleteCooldown = 0.0f;

    // Initialize debug menu with physics items
    debug_menu_init("PHYSICS DEBUG");
    debug_menu_add_float("Move Speed", &controlConfig.moveSpeed, 0.1f, 20.0f, 0.1f);
    debug_menu_add_float("Jump Force", &controlConfig.jumpForce, 0.1f, 50.0f, 0.1f);
    debug_menu_add_float("Gravity", &controlConfig.gravity, 0.01f, 2.0f, 0.01f);
    debug_menu_add_float("Player Radius", &playerRadius, 1.0f, 50.0f, 1.0f);
    debug_menu_add_float("Player Height", &playerHeight, 1.0f, 100.0f, 1.0f);
    debug_menu_add_float("ChgJmp MaxBase", &chargeJumpMaxBase, 0.1f, 50.0f, 0.1f);
    debug_menu_add_float("ChgJmp MaxMult", &chargeJumpMaxMultiplier, 0.1f, 10.0f, 0.1f);
    debug_menu_add_float("ChgJmp ErlBase", &chargeJumpEarlyBase, 0.1f, 50.0f, 0.1f);
    debug_menu_add_float("ChgJmp ErlMult", &chargeJumpEarlyMultiplier, 0.1f, 10.0f, 0.1f);
    debug_menu_add_float("Max Charge", &maxChargeTime, 0.1f, 5.0f, 0.1f);

    // Initialize controls
    controls_init(&controlConfig);
    playerState.velX = 0.0f;
    playerState.velY = 0.0f;
    playerState.velZ = 0.0f;
    playerState.playerAngle = T3D_DEG_TO_RAD(-90.0f);
    playerState.isGrounded = false;
    playerState.isOnSlope = false;
    playerState.isSliding = false;
    playerState.slopeType = SLOPE_FLAT;
    playerState.slopeNormalX = 0.0f;
    playerState.slopeNormalY = 1.0f;
    playerState.slopeNormalZ = 0.0f;
    playerState.currentJumps = 0;
    playerState.groundedFrames = 0;
    playerState.canMove = true;
    playerState.canRotate = true;
    playerState.canJump = true;
    playerState.slideVelX = 0.0f;
    playerState.slideVelZ = 0.0f;
    playerState.slideYaw = 0.0f;
    playerState.hitWall = false;
    playerState.wallNormalX = 0.0f;
    playerState.wallNormalZ = 0.0f;
    playerState.wallHitTimer = 0;

    // Load sound effects (audio already initialized in main.c)
    wav64_open(&sfxBoltCollect, "rom:/BoltCollected.wav64");
    wav64_open(&sfxTurretFire, "rom:/Turret_Fire.wav64");
    wav64_open(&sfxTurretZap, "rom:/Misc_Zap_1.wav64");
    wav64_open(&sfxJumpSound, "rom:/JumpSound.wav64");

    // Load pre-transition sounds (lazy load sprites later)
    if (!preTransitionSoundsLoaded) {
        wav64_open(&sfxSlideWhistle, "rom:/SlideWhistle.wav64");
        wav64_open(&sfxMetallicThud, "rom:/MetallicThud2.wav64");
        preTransitionSoundsLoaded = true;
    }

    // Initialize map loader and decoration runtime
    maploader_init(&mapLoader, FB_COUNT, VISIBILITY_RANGE);
    map_runtime_init(&mapRuntime, FB_COUNT, VISIBILITY_RANGE);

    // Reset activation system for fresh level start
    activation_reset_all();

    // Reset animation tracking (prevents stale pointer to freed animations)
    currentlyAttachedAnim = NULL;

    // Load current level (map segments + decorations)
    level_load(currentLevel, &mapLoader, &mapRuntime);
    mapRuntime.mapLoader = &mapLoader;  // Set collision reference for turret raycasts

    // Initialize lighting state from level data
    init_lighting_state(currentLevel);

    // Load level 3 special decorations (stream + water level at origin)
    // level3_special_load removed

    // Set body part for this level
    currentPart = (RobotParts)level_get_body_part(currentLevel);
    debugf("Level body part: %d\n", currentPart);

    // Find first PlayerSpawn decoration and use it as spawn point
    bool foundSpawn = false;
    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (deco->type == DECO_PLAYERSPAWN && deco->active) {
            cubeX = deco->posX;
            cubeY = deco->posY;
            cubeZ = deco->posZ;
            foundSpawn = true;
            debugf("Player spawn found at: (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);

            // IMMEDIATELY snap to ground right here
            float groundY = maploader_get_ground_height(&mapLoader, cubeX, cubeY + 100.0f, cubeZ);
            debugf("  Ground check at spawn: groundY=%.1f (loader segments=%d)\n", groundY, mapLoader.count);
            if (groundY > -9000.0f) {
                cubeY = groundY + 2.0f;
                debugf("  SNAPPED spawn to Y=%.1f\n", cubeY);
            }
            break;
        }
    }
    if (!foundSpawn) {
        debugf("No PlayerSpawn found, using default position\n");
    }

    // In demo mode, check if demo has a custom start position (non-level-origin spawn)
    if (g_demoMode) {
        float demoStartX, demoStartY, demoStartZ;
        if (demo_get_start_position(&demoStartX, &demoStartY, &demoStartZ)) {
            cubeX = demoStartX;
            cubeY = demoStartY;
            cubeZ = demoStartZ;
            debugf("Demo: Using custom start position (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);
        }
    }

    // Handle return from body part tutorial - restore player position and upgrade body part
    // Only do this when NOT in demo mode (i.e., returning from demo back to normal gameplay)
    if (!g_demoMode && g_tutorialType != TUTORIAL_NONE && g_tutorialReturnLevel >= 0) {
        debugf("Returning from tutorial type %d to (%.1f, %.1f, %.1f)\n",
               g_tutorialType, g_tutorialReturnX, g_tutorialReturnY, g_tutorialReturnZ);

        // Restore player position
        cubeX = g_tutorialReturnX;
        cubeY = g_tutorialReturnY;
        cubeZ = g_tutorialReturnZ;

        // Upgrade body part based on tutorial type
        if (g_tutorialType == TUTORIAL_TORSO) {
            currentPart = PART_TORSO;
            if (partsObtained < 1) partsObtained = 1;  // Unlock torso
            debugf("Tutorial: Torso unlocked!\n");
        } else if (g_tutorialType == TUTORIAL_ARMS) {
            currentPart = PART_ARMS;
            if (partsObtained < 2) partsObtained = 2;  // Unlock arms
            debugf("Tutorial: Arms unlocked!\n");
        } else if (g_tutorialType == TUTORIAL_HEAD) {
            currentPart = PART_HEAD;
            if (partsObtained < 3) partsObtained = 3;  // Unlock head
            debugf("Tutorial: Head unlocked!\n");
        } else if (g_tutorialType == TUTORIAL_LEGS) {
            currentPart = PART_LEGS;
            if (partsObtained < 4) partsObtained = 4;  // Unlock legs (all parts)
            debugf("Tutorial: Legs unlocked!\n");
        }

        // Clear tutorial state
        g_tutorialType = TUTORIAL_NONE;
        g_tutorialReturnLevel = -1;
    }

    // Start playback if in replay mode, otherwise start automatic recording
    if (g_replayMode) {
        // In replay mode, start playback
        if (!replay_start_playback(currentLevel)) {
            debugf("Warning: No replay data for level %d\n", currentLevel);
            g_replayMode = false;  // Fall back to normal mode
        }
    } else if (!g_demoMode) {
        // Start automatic recording from level start (for ghost system)
        // This records from level start; D-pad right can override with mid-level recording
        replay_start_recording(currentLevel, cubeX, cubeY, cubeZ);
        debugf("Auto-recording started at (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);
    }

    // Allocate matrices for decorations (80 decorations + 60 extra slots for projectiles/multi-part objects per frame)
    decoMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT * (MAX_DECORATIONS + 60));

    // Allocate player matrices
    playerMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
    roboMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
    arcMatFP = malloc_uncached(sizeof(T3DMat4FP) * 15);  // Max arc dots

    // Load shadow sprite and create shadow quad vertices
    shadowSprite = sprite_load("rom:/shadow.sprite");
    shadowVerts = malloc_uncached(sizeof(T3DVertPacked) * 2);  // 4 verts = 2 packed

    // Load buff FX texture for player glow effect
    buffFxSprite = sprite_load("rom:/robot_player_fx.sprite");
    buffFlashTimer = 0.0f;
    buffFlashState = false;

    // Health HUD - lazy loaded on first damage
    healthSpritesLoaded = false;
    healthHudY = HEALTH_HUD_HIDE_Y;
    healthHudTargetY = HEALTH_HUD_HIDE_Y;
    healthHudVisible = false;
    healthFlashTimer = 0.0f;
    healthHudHideTimer = 0.0f;

    // Screw HUD - lazy loaded on first bolt collection
    screwSpritesLoaded = false;
    screwHudX = SCREW_HUD_HIDE_X;
    screwHudTargetX = SCREW_HUD_HIDE_X;
    screwHudVisible = false;
    screwAnimFrame = 0;
    screwAnimTimer = 0.0f;
    screwHudHideTimer = 0.0f;

    // Golden Screw HUD - lazy loaded on first golden screw collection
    goldenSpritesLoaded = false;
    goldenHudX = GOLDEN_HUD_HIDE_X;
    goldenHudTargetX = GOLDEN_HUD_HIDE_X;
    goldenHudVisible = false;
    goldenAnimFrame = 0;
    goldenAnimTimer = 0.0f;
    goldenHudHideTimer = 0.0f;

    // Reward popup - reset on scene init
    rewardPopupActive = false;
    rewardPopupTimer = 0.0f;

    // Allocate decal vertices and matrix for slime oil trails
    decalVerts = malloc_uncached(sizeof(T3DVertPacked) * 2);  // 4 verts = 2 packed
    decalMatFP = malloc_uncached(sizeof(T3DMat4FP));

    // Load player model based on current body part
    const char* modelPath = get_body_part_model_path(currentPart);
    debugf("Loading player model for part %d: %s\n", currentPart, modelPath);
    torsoModel = t3d_model_load(modelPath);
    if (torsoModel) {
        torsoSkel = t3d_skeleton_create(torsoModel);
        torsoSkelBlend = t3d_skeleton_clone(&torsoSkel, false);

        // Load animations based on current body part
        if (currentPart == PART_TORSO || currentPart == PART_HEAD) {
            // Load torso animations (also used for head mode)
            // Idle is the default standing pose - loops continuously
            torsoAnimIdle = t3d_anim_create(torsoModel, "torso_idle");
            t3d_anim_set_looping(&torsoAnimIdle, true);
            t3d_anim_attach(&torsoAnimIdle, &torsoSkel);  // Attach idle to main skeleton
            t3d_anim_set_playing(&torsoAnimIdle, true);

            torsoAnimWalk = t3d_anim_create(torsoModel, "torso_walk_fast");
            t3d_anim_attach(&torsoAnimWalk, &torsoSkelBlend);
            t3d_anim_set_looping(&torsoAnimWalk, true);
            t3d_anim_set_playing(&torsoAnimWalk, true);

            torsoAnimWalkSlow = t3d_anim_create(torsoModel, "torso_walk_slow");
            t3d_anim_attach(&torsoAnimWalkSlow, &torsoSkelBlend);
            t3d_anim_set_looping(&torsoAnimWalkSlow, true);
            t3d_anim_set_playing(&torsoAnimWalkSlow, true);

            torsoAnimJumpCharge = t3d_anim_create(torsoModel, "torso_jump_charge");
            t3d_anim_set_looping(&torsoAnimJumpCharge, false);

            torsoAnimJumpLaunch = t3d_anim_create(torsoModel, "torso_jump_launch");
            t3d_anim_set_looping(&torsoAnimJumpLaunch, false);
            debugf("torso_jump_launch: %s dur=%.2f\n", torsoAnimJumpLaunch.animRef ? "OK" : "MISSING",
                   torsoAnimJumpLaunch.animRef ? torsoAnimJumpLaunch.animRef->duration : 0.0f);

            torsoAnimJumpDouble = t3d_anim_create(torsoModel, "torso_double");
            t3d_anim_set_looping(&torsoAnimJumpDouble, false);
            debugf("torso_double: %s dur=%.2f\n", torsoAnimJumpDouble.animRef ? "OK" : "MISSING",
                   torsoAnimJumpDouble.animRef ? torsoAnimJumpDouble.animRef->duration : 0.0f);

            torsoAnimJumpTriple = t3d_anim_create(torsoModel, "torso_tripple");  // Note: typo in model
            t3d_anim_set_looping(&torsoAnimJumpTriple, false);
            debugf("torso_tripple: %s dur=%.2f\n", torsoAnimJumpTriple.animRef ? "OK" : "MISSING",
                   torsoAnimJumpTriple.animRef ? torsoAnimJumpTriple.animRef->duration : 0.0f);

            // Initialize currentLaunchAnim to default
            currentLaunchAnim = &torsoAnimJumpLaunch;

            torsoAnimJumpLand = t3d_anim_create(torsoModel, "torso_jump_land");
            t3d_anim_set_looping(&torsoAnimJumpLand, false);
            debugf("torso_jump_land: %s\n", torsoAnimJumpLand.animRef ? "OK" : "MISSING");

            // Wait is the fidget animation - plays once every 7 seconds
            torsoAnimWait = t3d_anim_create(torsoModel, "torso_wait");
            t3d_anim_set_looping(&torsoAnimWait, false);

            // Pain animations - play when taking damage
            torsoAnimPain1 = t3d_anim_create(torsoModel, "torso_pain_1");
            t3d_anim_set_looping(&torsoAnimPain1, false);
            debugf("torso_pain_1: %s\n", torsoAnimPain1.animRef ? "OK" : "MISSING");

            torsoAnimPain2 = t3d_anim_create(torsoModel, "torso_pain_2");
            t3d_anim_set_looping(&torsoAnimPain2, false);
            debugf("torso_pain_2: %s\n", torsoAnimPain2.animRef ? "OK" : "MISSING");

            // Death animation - play when health reaches 0
            torsoAnimDeath = t3d_anim_create(torsoModel, "torso_death");
            t3d_anim_set_looping(&torsoAnimDeath, false);
            debugf("torso_death: %s\n", torsoAnimDeath.animRef ? "OK" : "MISSING");

            // Slide animations - play once and hold last frame
            torsoAnimSlideFront = t3d_anim_create(torsoModel, "torso_slide_front");
            t3d_anim_set_looping(&torsoAnimSlideFront, false);
            debugf("torso_slide_front: %s\n", torsoAnimSlideFront.animRef ? "OK" : "MISSING");

            torsoAnimSlideFrontRecover = t3d_anim_create(torsoModel, "torso_slide_front_recover");
            t3d_anim_set_looping(&torsoAnimSlideFrontRecover, false);
            debugf("torso_slide_front_recover: %s\n", torsoAnimSlideFrontRecover.animRef ? "OK" : "MISSING");

            torsoAnimSlideBack = t3d_anim_create(torsoModel, "torso_slide_back");
            t3d_anim_set_looping(&torsoAnimSlideBack, false);
            debugf("torso_slide_back: %s\n", torsoAnimSlideBack.animRef ? "OK" : "MISSING");

            torsoAnimSlideBackRecover = t3d_anim_create(torsoModel, "torso_slide_back_recover");
            t3d_anim_set_looping(&torsoAnimSlideBackRecover, false);
            debugf("torso_slide_back_recover: %s\n", torsoAnimSlideBackRecover.animRef ? "OK" : "MISSING");

            torsoHasAnims = true;
            debugf("Loaded Torso model with animations\n");
        } else if (currentPart == PART_ARMS) {
            // Load arms mode animations from Robo_arms model
            armsAnimIdle = t3d_anim_create(torsoModel, "arms_idle");
            t3d_anim_set_looping(&armsAnimIdle, true);
            t3d_anim_attach(&armsAnimIdle, &torsoSkel);  // Attach idle to main skeleton
            t3d_anim_set_playing(&armsAnimIdle, true);
            debugf("arms_idle: %s\n", armsAnimIdle.animRef ? "OK" : "MISSING");

            armsAnimWalk1 = t3d_anim_create(torsoModel, "arms_walk_1");
            t3d_anim_attach(&armsAnimWalk1, &torsoSkelBlend);
            t3d_anim_set_looping(&armsAnimWalk1, true);
            t3d_anim_set_playing(&armsAnimWalk1, true);
            debugf("arms_walk_1: %s\n", armsAnimWalk1.animRef ? "OK" : "MISSING");

            armsAnimWalk2 = t3d_anim_create(torsoModel, "arms_walk_2");
            t3d_anim_set_looping(&armsAnimWalk2, true);
            debugf("arms_walk_2: %s\n", armsAnimWalk2.animRef ? "OK" : "MISSING");

            armsAnimJump = t3d_anim_create(torsoModel, "arms_jump");
            t3d_anim_set_looping(&armsAnimJump, false);
            debugf("arms_jump: %s\n", armsAnimJump.animRef ? "OK" : "MISSING");

            armsAnimJumpLand = t3d_anim_create(torsoModel, "arms_jump_land");
            t3d_anim_set_looping(&armsAnimJumpLand, false);
            debugf("arms_jump_land: %s\n", armsAnimJumpLand.animRef ? "OK" : "MISSING");

            armsAnimAtkSpin = t3d_anim_create(torsoModel, "arms_atk_spin");
            t3d_anim_set_looping(&armsAnimAtkSpin, false);
            debugf("arms_atk_spin: %s\n", armsAnimAtkSpin.animRef ? "OK" : "MISSING");

            armsAnimAtkWhip = t3d_anim_create(torsoModel, "arms_atk_whip");
            t3d_anim_set_looping(&armsAnimAtkWhip, false);
            debugf("arms_atk_whip: %s\n", armsAnimAtkWhip.animRef ? "OK" : "MISSING");

            armsAnimDeath = t3d_anim_create(torsoModel, "arms_death");
            t3d_anim_set_looping(&armsAnimDeath, false);
            debugf("arms_death: %s\n", armsAnimDeath.animRef ? "OK" : "MISSING");

            armsAnimPain1 = t3d_anim_create(torsoModel, "arms_pain_1");
            t3d_anim_set_looping(&armsAnimPain1, false);
            debugf("arms_pain_1: %s\n", armsAnimPain1.animRef ? "OK" : "MISSING");

            armsAnimPain2 = t3d_anim_create(torsoModel, "arms_pain_2");
            t3d_anim_set_looping(&armsAnimPain2, false);
            debugf("arms_pain_2: %s\n", armsAnimPain2.animRef ? "OK" : "MISSING");

            armsAnimSlide = t3d_anim_create(torsoModel, "arms_slide");
            t3d_anim_set_looping(&armsAnimSlide, true);
            debugf("arms_slide: %s\n", armsAnimSlide.animRef ? "OK" : "MISSING");

            armsHasAnims = armsAnimIdle.animRef && armsAnimWalk1.animRef && armsAnimAtkSpin.animRef;
            debugf("Arms mode animations: %s\n", armsHasAnims ? "OK" : "MISSING");
        } else if (currentPart == PART_LEGS) {
            // Load fullbody (fb_*) animations from Robo_fb model
            debugf("Loading fullbody animations...\n");

            // Idle - default standing pose, loops
            fbAnimIdle = t3d_anim_create(torsoModel, "fb_idle");
            t3d_anim_set_looping(&fbAnimIdle, true);
            t3d_anim_attach(&fbAnimIdle, &torsoSkel);
            t3d_anim_set_playing(&fbAnimIdle, true);
            debugf("fb_idle: %s\n", fbAnimIdle.animRef ? "OK" : "MISSING");

            // Walk - loops while moving
            fbAnimWalk = t3d_anim_create(torsoModel, "fb_walk");
            t3d_anim_attach(&fbAnimWalk, &torsoSkelBlend);
            t3d_anim_set_looping(&fbAnimWalk, true);
            t3d_anim_set_playing(&fbAnimWalk, true);
            debugf("fb_walk: %s\n", fbAnimWalk.animRef ? "OK" : "MISSING");

            // Run - faster movement
            fbAnimRun = t3d_anim_create(torsoModel, "fb_run");
            t3d_anim_set_looping(&fbAnimRun, true);
            debugf("fb_run: %s\n", fbAnimRun.animRef ? "OK" : "MISSING");

            // Jump - standard jump
            fbAnimJump = t3d_anim_create(torsoModel, "fb_jump");
            t3d_anim_set_looping(&fbAnimJump, false);
            debugf("fb_jump: %s\n", fbAnimJump.animRef ? "OK" : "MISSING");

            // Wait - fidget animation
            fbAnimWait = t3d_anim_create(torsoModel, "fb_wait");
            t3d_anim_set_looping(&fbAnimWait, false);
            debugf("fb_wait: %s\n", fbAnimWait.animRef ? "OK" : "MISSING");

            // Crouch - crouching idle (play once and hold last frame)
            fbAnimCrouch = t3d_anim_create(torsoModel, "fb_crouch");
            t3d_anim_set_looping(&fbAnimCrouch, false);
            debugf("fb_crouch: %s\n", fbAnimCrouch.animRef ? "OK" : "MISSING");

            // Crouch jump - jump from crouch
            fbAnimCrouchJump = t3d_anim_create(torsoModel, "fb_crouch_jump");
            t3d_anim_set_looping(&fbAnimCrouchJump, false);
            debugf("fb_crouch_jump: %s\n", fbAnimCrouchJump.animRef ? "OK" : "MISSING");

            // Crouch jump hover - holding in air after crouch jump
            fbAnimCrouchJumpHover = t3d_anim_create(torsoModel, "fb_crouch_jump_hover");
            t3d_anim_set_looping(&fbAnimCrouchJumpHover, true);
            debugf("fb_crouch_jump_hover: %s\n", fbAnimCrouchJumpHover.animRef ? "OK" : "MISSING");

            // Long jump animation (plays once and holds last frame)
            fbAnimLongJump = t3d_anim_create(torsoModel, "fb_long_jump");
            t3d_anim_set_looping(&fbAnimLongJump, false);
            debugf("fb_long_jump: %s\n", fbAnimLongJump.animRef ? "OK" : "MISSING");

            // Death animation
            fbAnimDeath = t3d_anim_create(torsoModel, "fb_death");
            t3d_anim_set_looping(&fbAnimDeath, false);
            debugf("fb_death: %s\n", fbAnimDeath.animRef ? "OK" : "MISSING");

            // Pain animations
            fbAnimPain1 = t3d_anim_create(torsoModel, "fb_pain_1");
            t3d_anim_set_looping(&fbAnimPain1, false);
            debugf("fb_pain_1: %s\n", fbAnimPain1.animRef ? "OK" : "MISSING");

            fbAnimPain2 = t3d_anim_create(torsoModel, "fb_pain_2");
            t3d_anim_set_looping(&fbAnimPain2, false);
            debugf("fb_pain_2: %s\n", fbAnimPain2.animRef ? "OK" : "MISSING");

            // Slide animation
            fbAnimSlide = t3d_anim_create(torsoModel, "fb_slide");
            t3d_anim_set_looping(&fbAnimSlide, true);
            debugf("fb_slide: %s\n", fbAnimSlide.animRef ? "OK" : "MISSING");

            // Spin attack (ground)
            fbAnimSpinAtk = t3d_anim_create(torsoModel, "fb_spin_atk");
            // Validate animation has valid bone targets (targetIdx 255/0xFFFF = invalid)
            if (fbAnimSpinAtk.animRef) {
                uint16_t totalChannels = fbAnimSpinAtk.animRef->channelsQuat + fbAnimSpinAtk.animRef->channelsScalar;
                bool hasInvalidTarget = false;
                // Also check if target exceeds skeleton bone count
                const T3DChunkSkeleton* skelChunk = t3d_model_get_skeleton(torsoModel);
                uint16_t maxBones = skelChunk ? skelChunk->boneCount : 0;
                for (uint16_t i = 0; i < totalChannels && !hasInvalidTarget; i++) {
                    uint16_t targetIdx = fbAnimSpinAtk.animRef->channelMappings[i].targetIdx;
                    if (targetIdx >= 255 || targetIdx >= maxBones) {
                        hasInvalidTarget = true;
                        debugf("fb_spin_atk: Invalid target %d (max bones: %d)\n", targetIdx, maxBones);
                    }
                }
                if (hasInvalidTarget) {
                    debugf("fb_spin_atk: INVALID BONE TARGETS - disabling\n");
                    fbAnimSpinAtk.animRef = NULL;  // Disable broken animation
                } else {
                    t3d_anim_set_looping(&fbAnimSpinAtk, false);
                    debugf("fb_spin_atk: OK (%d channels, %d bones)\n", totalChannels, maxBones);
                }
            } else {
                debugf("fb_spin_atk: MISSING\n");
            }

            // Spin attack (air) - NOTE: This anim may not exist in all fullbody models
            fbAnimSpinAir = t3d_anim_create(torsoModel, "fb_spin_air");
            // Validate animation has valid bone targets (targetIdx 255/0xFFFF = invalid)
            if (fbAnimSpinAir.animRef) {
                uint16_t totalChannels = fbAnimSpinAir.animRef->channelsQuat + fbAnimSpinAir.animRef->channelsScalar;
                bool hasInvalidTarget = false;
                for (uint16_t i = 0; i < totalChannels && !hasInvalidTarget; i++) {
                    if (fbAnimSpinAir.animRef->channelMappings[i].targetIdx >= 255) {
                        hasInvalidTarget = true;
                    }
                }
                if (hasInvalidTarget) {
                    debugf("fb_spin_air: INVALID BONE TARGETS - disabling\n");
                    fbAnimSpinAir.animRef = NULL;  // Disable broken animation
                } else {
                    t3d_anim_set_looping(&fbAnimSpinAir, false);
                    debugf("fb_spin_air: OK (%d channels)\n", totalChannels);
                }
            } else {
                debugf("fb_spin_air: MISSING\n");
            }

            // Spin charge (play once and hold last frame)
            fbAnimSpinCharge = t3d_anim_create(torsoModel, "fb_spin_charge");
            t3d_anim_set_looping(&fbAnimSpinCharge, false);
            debugf("fb_spin_charge: %s\n", fbAnimSpinCharge.animRef ? "OK" : "MISSING");

            // Ninja run
            fbAnimRunNinja = t3d_anim_create(torsoModel, "fb_run_ninja");
            t3d_anim_set_looping(&fbAnimRunNinja, true);
            debugf("fb_run_ninja: %s\n", fbAnimRunNinja.animRef ? "OK" : "MISSING");

            // Crouch attack
            fbAnimCrouchAttack = t3d_anim_create(torsoModel, "fb_crouch_attack");
            t3d_anim_set_looping(&fbAnimCrouchAttack, false);
            debugf("fb_crouch_attack: %s\n", fbAnimCrouchAttack.animRef ? "OK" : "MISSING");

            fbHasAnims = fbAnimIdle.animRef && fbAnimWalk.animRef && fbAnimJump.animRef;
            debugf("Fullbody mode animations: %s\n", fbHasAnims ? "OK" : "MISSING");
        }
    }

    // Load simple cube for jump arc visualization (BlueCube is simpler/safer than TransitionCollision)
    arcDotModel = t3d_model_load("rom:/BlueCube.t3dm");

    // Allocate arc dot vertices for cube rendering (8 vertices for a cube)
    arcDotVerts = malloc_uncached(sizeof(T3DVertPacked) * 4);  // 8 verts = 4 packed
    // Cube vertices: 8 corners of a unit cube centered at origin
    // Bottom face (y = -1): 0-3, Top face (y = 1): 4-7
    int16_t *adPos0 = t3d_vertbuffer_get_pos(arcDotVerts, 0);
    int16_t *adPos1 = t3d_vertbuffer_get_pos(arcDotVerts, 1);
    int16_t *adPos2 = t3d_vertbuffer_get_pos(arcDotVerts, 2);
    int16_t *adPos3 = t3d_vertbuffer_get_pos(arcDotVerts, 3);
    int16_t *adPos4 = t3d_vertbuffer_get_pos(arcDotVerts, 4);
    int16_t *adPos5 = t3d_vertbuffer_get_pos(arcDotVerts, 5);
    int16_t *adPos6 = t3d_vertbuffer_get_pos(arcDotVerts, 6);
    int16_t *adPos7 = t3d_vertbuffer_get_pos(arcDotVerts, 7);
    // Bottom face vertices
    adPos0[0] = -1; adPos0[1] = -1; adPos0[2] = -1;  // 0: left-bottom-back
    adPos1[0] =  1; adPos1[1] = -1; adPos1[2] = -1;  // 1: right-bottom-back
    adPos2[0] =  1; adPos2[1] = -1; adPos2[2] =  1;  // 2: right-bottom-front
    adPos3[0] = -1; adPos3[1] = -1; adPos3[2] =  1;  // 3: left-bottom-front
    // Top face vertices
    adPos4[0] = -1; adPos4[1] =  1; adPos4[2] = -1;  // 4: left-top-back
    adPos5[0] =  1; adPos5[1] =  1; adPos5[2] = -1;  // 5: right-top-back
    adPos6[0] =  1; adPos6[1] =  1; adPos6[2] =  1;  // 6: right-top-front
    adPos7[0] = -1; adPos7[1] =  1; adPos7[2] =  1;  // 7: left-top-front
    // Set all vertex colors to white
    for (int i = 0; i < 8; i++) {
        *t3d_vertbuffer_get_color(arcDotVerts, i) = 0xFFFFFFFF;
    }
    data_cache_hit_writeback(arcDotVerts, 4 * sizeof(T3DVertPacked));

    // Reset torso state
    isCharging = false;
    isJumping = false;
    jumpAnimPaused = false;
    jumpChargeTime = 0.0f;
    jumpComboCount = 0;
    jumpComboTimer = 0.0f;
    jumpAimX = 0.0f;
    jumpAimY = 0.0f;
    lastValidAimX = 0.0f;
    wallJumpInputBuffer = 0;
    lastValidAimY = 0.0f;
    aimGraceTimer = 0.0f;
    idleFrames = 0;
    playingFidget = false;
    fidgetPlayTime = 0.0f;
    wasSliding = false;
    isSlidingFront = true;
    isSlideRecovering = false;
    slideRecoverTime = 0.0f;

    // Reset arms mode state
    isArmsMode = false;
    armsIsSpinning = false;
    armsIsWhipping = false;
    armsSpinTime = 0.0f;
    armsWhipTime = 0.0f;

    // Reset fullbody mode state
    fbIsCrouching = false;
    fbIsLongJumping = false;
    fbIsBackflipping = false;
    fbLongJumpSpeed = 0.0f;
    fbIsHovering = false;
    fbHoverTime = 0.0f;
    fbHoverTiltX = 0.0f;
    fbHoverTiltZ = 0.0f;
    fbHoverTiltVelX = 0.0f;
    fbHoverTiltVelZ = 0.0f;
    fbCrouchJumpWindup = false;
    fbCrouchJumpWindupTime = 0.0f;
    fbCrouchJumpRising = false;

    // Reset health state
    playerHealth = maxPlayerHealth;
    playerIsDead = false;
    playerIsHurt = false;
    playerHurtAnimTime = 0.0f;
    playerCurrentPainAnim = NULL;
    playerInvincibilityTimer = 0.0f;
    playerInvincibilityFlashFrame = 0;

    // Reset death transition
    playerDeathTimer = 0.0f;
    fadeAlpha = 0.0f;
    playerIsRespawning = false; g_playerIsRespawning = false;
    playerRespawnDelayTimer = 0.0f;

    // Reset level transition
    isTransitioning = false;
    transitionTimer = 0.0f;
    targetTransitionLevel = 0;
    targetTransitionSpawn = 0;
    celebrateReturnToMenu = false;
    isPreTransitioning = false;
    preTransitionTimer = 0.0f;

    // SNAP TO GROUND IMMEDIATELY - before iris opens, so player is never seen hovering
    {
        float oldY = cubeY;
        float groundY = maploader_get_ground_height(&mapLoader, cubeX, cubeY + 50.0f, cubeZ);
        debugf("GROUND SNAP: pos=(%.1f,%.1f,%.1f) groundY=%.1f\n", cubeX, oldY, cubeZ, groundY);
        if (groundY > -9000.0f) {
            // Add 2.0f offset (same as normal collision) so player feet touch ground
            cubeY = groundY + 2.0f;
            debugf("  -> Snapped to Y=%.1f (ground+2)\n", cubeY);
        } else {
            debugf("  -> NO GROUND FOUND, keeping Y=%.1f\n", cubeY);
        }
        // Sync all position variables
        prevCubeX = cubeX; prevCubeY = cubeY; prevCubeZ = cubeZ;
        renderCubeX = cubeX; renderCubeY = cubeY; renderCubeZ = cubeZ;
        // Force grounded state
        playerState.isGrounded = true;
        playerState.velY = 0.0f;
        isJumping = false;
        isLanding = false;
    }

    // Check if we should start with iris opening effect (from menu transition)
    if (startWithIrisOpen) {
        irisActive = true;
        irisRadius = 0.0f;  // Start fully closed
        irisCenterX = 160.0f;
        irisCenterY = 120.0f;
        playerIsRespawning = true; g_playerIsRespawning = true;  // Use respawn logic to open iris
        playerRespawnDelayTimer = 0.5f;  // Skip most of the delay, go straight to opening
        startWithIrisOpen = false;  // Clear the flag
    } else {
        irisActive = false;
        irisRadius = 400.0f;
    }

    // currentPart is already set by level_get_body_part() earlier
    partSwitchCooldown = 0;

    // Reset pause state
    isPaused = false;
    pauseMenuInitialized = false;
    isQuittingToMenu = false;

    // Initialize dialogue system for interact triggers
    dialogue_init(&dialogueBox);
    scriptRunning = false;
    activeInteractTrigger = NULL;

    // Initialize level banner and show current level name
    level_banner_init(&levelBanner);

    viewport = t3d_viewport_create_buffered(FB_COUNT);

    gameSceneInitialized = true;

    // Initialize player struct spawn point
    player.spawnX = cubeX;
    player.spawnY = cubeY;
    player.spawnZ = cubeZ;
    player.playerIndex = 0;
    player.port = JOYPAD_PORT_1;

    // Macro bridge makes sync_player_state() unnecessary - state is updated directly

    // Show level banner after everything is initialized (skip in demo mode)
    if (!g_demoMode) {
        level_banner_show(&levelBanner, get_level_name_with_number(currentLevel));

        // Check if controls tutorial should show for this level/body part
        // Skip if CS2 cutscene will play on Level 2 (tutorial triggers after cutscene ends)
        bool cs2WillPlay = (currentLevel == LEVEL_2 && !save_has_watched_cs2());
        debugf("Tutorial init: currentLevel=%d, currentPart=%d, cs2WillPlay=%d\n",
               currentLevel, (int)currentPart, cs2WillPlay);
        if (!cs2WillPlay) {
            tutorial_check_and_show(currentLevel, (int)currentPart);

            // Disable player controls during tutorial
            if (tutorialActive) {
                debugf("Tutorial: Disabling player controls\n");
                playerState.canMove = false;
                playerState.canJump = false;
                playerState.canRotate = false;
            }
        } else {
            debugf("Tutorial: Skipping init check, CS2 cutscene will play\n");
        }

        // Countdown disabled for single player - player can move immediately
    }

    // Play CS2 cutscene intro when starting level 2 (only once per save file, skip in demo mode)
    if (currentLevel == LEVEL_2 && !g_demoMode && !cs2Playing && !save_has_watched_cs2()) {
        debugf("Starting Level 2, playing CS2 cutscene intro\n");
        save_mark_cs2_watched();  // Mark as watched so it only plays once
        cs2Playing = true;
        cs2CurrentFrame = 0;
        cs2FrameTimer = 0.0f;
        cs2_load_frame(0);

        // Disable player controls during cutscene
        playerState.canMove = false;
        playerState.canJump = false;
        playerState.canRotate = false;
    }

    // Play CS4 cutscene intro when starting level 4 (arms level, only once per save file, skip in demo mode)
    if (currentLevel == LEVEL_4 && !g_demoMode && !cs4Playing && !save_has_watched_cs4()) {
        debugf("Starting Level 4, playing CS4 cutscene intro (arms explanation)\n");
        save_mark_cs4_watched();  // Mark as watched so it only plays once
        cs4Playing = true;
        cs4CurrentSegment = 0;
        cs4FrameTimer = 0.0f;
        cs4_load_frame(0);

        // Disable player controls during cutscene
        playerState.canMove = false;
        playerState.canJump = false;
        playerState.canRotate = false;
    }

    // Mark scene as fully initialized
    gameSceneInitialized = true;
}

void deinit_game_scene(void) {
    if (!gameSceneInitialized) return;
    gameSceneInitialized = false;  // Clear flag immediately

    // CRITICAL: Wait for BOTH RSP and RDP to finish ALL operations before freeing resources.
    // rdpq_fence() does NOT block the CPU - it only schedules a fence command in the queue.
    // rspq_wait() actually blocks and waits for both RSP and RDP to complete all queued work.
    rspq_wait();

    if (test_sprite) {
        sprite_free(test_sprite);
        test_sprite = NULL;
    }
    // Free health HUD sprites
    for (int i = 0; i < 4; i++) {
        if (healthSprites[i]) {
            sprite_free(healthSprites[i]);
            healthSprites[i] = NULL;
        }
    }
    healthSpritesLoaded = false;
    // Free screw HUD sprites
    for (int i = 0; i < SCREW_HUD_FRAME_COUNT; i++) {
        if (screwSprites[i]) {
            sprite_free(screwSprites[i]);
            screwSprites[i] = NULL;
        }
    }
    screwSpritesLoaded = false;
    // Free golden screw HUD sprites
    for (int i = 0; i < GOLDEN_HUD_FRAME_COUNT; i++) {
        if (goldenSprites[i]) {
            sprite_free(goldenSprites[i]);
            goldenSprites[i] = NULL;
        }
    }
    goldenSpritesLoaded = false;
    // Free pre-transition logo sprite
    if (preTransitionLogo) {
        sprite_free(preTransitionLogo);
        preTransitionLogo = NULL;
    }
    if (torsoModel) {
        // SAFETY: Reset animation tracking BEFORE destroying animations to prevent use-after-free
        currentlyAttachedAnim = NULL;
        if (torsoHasAnims) {
            t3d_anim_destroy(&torsoAnimIdle); torsoAnimIdle.animRef = NULL;
            t3d_anim_destroy(&torsoAnimWalk); torsoAnimWalk.animRef = NULL;
            t3d_anim_destroy(&torsoAnimWalkSlow); torsoAnimWalkSlow.animRef = NULL;
            t3d_anim_destroy(&torsoAnimJumpCharge); torsoAnimJumpCharge.animRef = NULL;
            t3d_anim_destroy(&torsoAnimJumpLaunch); torsoAnimJumpLaunch.animRef = NULL;
            t3d_anim_destroy(&torsoAnimJumpDouble); torsoAnimJumpDouble.animRef = NULL;
            t3d_anim_destroy(&torsoAnimJumpTriple); torsoAnimJumpTriple.animRef = NULL;
            t3d_anim_destroy(&torsoAnimJumpLand); torsoAnimJumpLand.animRef = NULL;
            t3d_anim_destroy(&torsoAnimWait); torsoAnimWait.animRef = NULL;
            t3d_anim_destroy(&torsoAnimPain1); torsoAnimPain1.animRef = NULL;
            t3d_anim_destroy(&torsoAnimPain2); torsoAnimPain2.animRef = NULL;
            t3d_anim_destroy(&torsoAnimDeath); torsoAnimDeath.animRef = NULL;
            t3d_anim_destroy(&torsoAnimSlideFront); torsoAnimSlideFront.animRef = NULL;
            t3d_anim_destroy(&torsoAnimSlideFrontRecover); torsoAnimSlideFrontRecover.animRef = NULL;
            t3d_anim_destroy(&torsoAnimSlideBack); torsoAnimSlideBack.animRef = NULL;
            t3d_anim_destroy(&torsoAnimSlideBackRecover); torsoAnimSlideBackRecover.animRef = NULL;
            torsoHasAnims = false;
        }
        if (armsHasAnims) {
            t3d_anim_destroy(&armsAnimIdle); armsAnimIdle.animRef = NULL;
            t3d_anim_destroy(&armsAnimWalk1); armsAnimWalk1.animRef = NULL;
            t3d_anim_destroy(&armsAnimWalk2); armsAnimWalk2.animRef = NULL;
            t3d_anim_destroy(&armsAnimJump); armsAnimJump.animRef = NULL;
            t3d_anim_destroy(&armsAnimJumpLand); armsAnimJumpLand.animRef = NULL;
            t3d_anim_destroy(&armsAnimAtkSpin); armsAnimAtkSpin.animRef = NULL;
            t3d_anim_destroy(&armsAnimAtkWhip); armsAnimAtkWhip.animRef = NULL;
            t3d_anim_destroy(&armsAnimDeath); armsAnimDeath.animRef = NULL;
            t3d_anim_destroy(&armsAnimPain1); armsAnimPain1.animRef = NULL;
            t3d_anim_destroy(&armsAnimPain2); armsAnimPain2.animRef = NULL;
            t3d_anim_destroy(&armsAnimSlide); armsAnimSlide.animRef = NULL;
            armsHasAnims = false;
        }
        if (fbHasAnims) {
            t3d_anim_destroy(&fbAnimIdle); fbAnimIdle.animRef = NULL;
            t3d_anim_destroy(&fbAnimWalk); fbAnimWalk.animRef = NULL;
            t3d_anim_destroy(&fbAnimRun); fbAnimRun.animRef = NULL;
            t3d_anim_destroy(&fbAnimJump); fbAnimJump.animRef = NULL;
            t3d_anim_destroy(&fbAnimWait); fbAnimWait.animRef = NULL;
            t3d_anim_destroy(&fbAnimCrouch); fbAnimCrouch.animRef = NULL;
            t3d_anim_destroy(&fbAnimCrouchJump); fbAnimCrouchJump.animRef = NULL;
            t3d_anim_destroy(&fbAnimCrouchJumpHover); fbAnimCrouchJumpHover.animRef = NULL;
            t3d_anim_destroy(&fbAnimLongJump); fbAnimLongJump.animRef = NULL;
            t3d_anim_destroy(&fbAnimDeath); fbAnimDeath.animRef = NULL;
            t3d_anim_destroy(&fbAnimPain1); fbAnimPain1.animRef = NULL;
            t3d_anim_destroy(&fbAnimPain2); fbAnimPain2.animRef = NULL;
            t3d_anim_destroy(&fbAnimSlide); fbAnimSlide.animRef = NULL;
            t3d_anim_destroy(&fbAnimSpinAir); fbAnimSpinAir.animRef = NULL;
            t3d_anim_destroy(&fbAnimSpinAtk); fbAnimSpinAtk.animRef = NULL;
            t3d_anim_destroy(&fbAnimSpinCharge); fbAnimSpinCharge.animRef = NULL;
            t3d_anim_destroy(&fbAnimRunNinja); fbAnimRunNinja.animRef = NULL;
            t3d_anim_destroy(&fbAnimCrouchAttack); fbAnimCrouchAttack.animRef = NULL;
            fbHasAnims = false;
        }
        t3d_skeleton_destroy(&torsoSkelBlend);
        t3d_skeleton_destroy(&torsoSkel);
        t3d_model_free(torsoModel);
        torsoModel = NULL;
    }
    // Free cutscene model if loaded
    if (cutsceneModelLoaded && cutsceneModel) {
        t3d_anim_destroy(&cutsceneAnim);
        t3d_skeleton_destroy(&cutsceneSkel);
        t3d_model_free(cutsceneModel);
        cutsceneModel = NULL;
        cutsceneModelLoaded = false;
        cutsceneFalloffPlaying = false;
        cutscenePlayerHidden = false;
    }
    if (arcDotModel) {
        t3d_model_free(arcDotModel);
        arcDotModel = NULL;
    }
    if (arcDotVerts) {
        free_uncached(arcDotVerts);
        arcDotVerts = NULL;
    }
    maploader_free(&mapLoader);
    map_runtime_free(&mapRuntime);
    collision_loader_free_all();  // Free collision meshes to prevent stale pointer reuse
    t3d_viewport_destroy(&viewport);
    if (decoMatFP) {
        free_uncached(decoMatFP);
        decoMatFP = NULL;
    }
    if (playerMatFP) {
        free_uncached(playerMatFP);
        playerMatFP = NULL;
    }
    if (roboMatFP) {
        free_uncached(roboMatFP);
        roboMatFP = NULL;
    }
    if (arcMatFP) {
        free_uncached(arcMatFP);
        arcMatFP = NULL;
    }

    // Cleanup UV scrolling data (from deco_render.h)
    deco_render_free_uv_data();

    // Cleanup level 3 special decorations
    // level3_special_free removed

    // Cleanup CS2 slideshow sprites (double-buffered)
    if (cs2CurrentSprite) {
        sprite_free(cs2CurrentSprite);
        cs2CurrentSprite = NULL;
    }
    if (cs2NextSprite) {
        sprite_free(cs2NextSprite);
        cs2NextSprite = NULL;
    }
    cs2Playing = false;
    cs2CurrentFrame = 0;

    // Cleanup CS3 slideshow sprites (double-buffered)
    if (cs3CurrentSprite) {
        sprite_free(cs3CurrentSprite);
        cs3CurrentSprite = NULL;
    }
    if (cs3NextSprite) {
        sprite_free(cs3NextSprite);
        cs3NextSprite = NULL;
    }
    cs3Playing = false;
    cs3CurrentSegment = 0;

    // Cleanup CS4 sprites
    if (cs4CurrentSprite) {
        sprite_free(cs4CurrentSprite);
        cs4CurrentSprite = NULL;
    }
    if (cs4NextSprite) {
        sprite_free(cs4NextSprite);
        cs4NextSprite = NULL;
    }
    cs4Playing = false;
    cs4CurrentSegment = 0;

    // Cleanup shadow sprite and verts
    if (shadowSprite) {
        sprite_free(shadowSprite);
        shadowSprite = NULL;
    }
    if (shadowVerts) {
        free_uncached(shadowVerts);
        shadowVerts = NULL;
    }

    // Cleanup decal verts and matrix
    if (decalVerts) {
        free_uncached(decalVerts);
        decalVerts = NULL;
    }
    if (decalMatFP) {
        free_uncached(decalMatFP);
        decalMatFP = NULL;
    }

    // Cleanup patrol points (debug feature)
    if (patrolPoints) {
        free(patrolPoints);
        patrolPoints = NULL;
    }

    // Cleanup audio (don't close mixer/audio - they're managed globally in main.c)
    level_stop_music();  // Stop level music first
    // Stop any SFX channels that might be playing the bolt collect sound
    // SFX uses channels 2-7 (SFX_CHANNEL_START to SFX_CHANNEL_START + SFX_CHANNEL_COUNT - 1)
    for (int ch = 2; ch < 8; ch++) {
        mixer_ch_stop(ch);
    }
    rspq_highpri_sync();  // Sync high-priority RSP queue (audio) before closing wav64
    rspq_wait();  // Wait for RSP to finish before closing wav64
    wav64_close(&sfxBoltCollect);
    wav64_close(&sfxTurretFire);
    wav64_close(&sfxTurretZap);
    wav64_close(&sfxJumpSound);

    // Free buff FX sprite (was missing - memory leak)
    if (buffFxSprite) {
        sprite_free(buffFxSprite);
        buffFxSprite = NULL;
    }

    // Free countdown sprites if still loaded (scene exit mid-countdown)
    for (int i = 0; i < 4; i++) {
        if (countdownSprites[i]) {
            sprite_free(countdownSprites[i]);
            countdownSprites[i] = NULL;
        }
    }

    // Free rank sprite if still loaded (scene exit during celebration)
    if (rankSprite) {
        sprite_free(rankSprite);
        rankSprite = NULL;
    }

    // UI sprites and sounds are managed globally in main.c

    gameSceneInitialized = false;
}

// Check if player is dead (can be called from decoration callbacks)
// Weak symbol - multiplayer.c overrides this when multiplayer scene is active
__attribute__((weak)) bool player_is_dead(void) {
    return playerIsDead;
}

// Spawn splash particles (can be called from decoration callbacks)
void game_spawn_splash_particles(float x, float y, float z, int count, uint8_t r, uint8_t g, uint8_t b) {
    spawn_splash_particles(x, y, z, count, r, g, b);
}

// Spawn death decal (can be called from decoration callbacks)
void game_spawn_death_decal(float x, float y, float z, float scale, bool isLava) {
    spawn_death_decal(x, y, z, scale, isLava);
}

// Spawn spark particles (can be called from decoration callbacks)
void game_spawn_spark_particles(float x, float y, float z, int count) {
    spawn_spark_particles(x, y, z, count);
}

// Spawn trail particles (can be called from decoration callbacks)
void game_spawn_trail_particles(float x, float y, float z, int count) {
    spawn_trail_particles(x, y, z, count);
}

// Trigger health HUD display with optional flash effect
static void trigger_health_display(bool withFlash) {
    // Lazy load health sprites on first use
    if (!healthSpritesLoaded) {
        debugf("Lazy loading health sprites\n");
        // CRITICAL: Wait for RSP queue to flush before loading from ROM
        // ROM DMA conflicts with RDP DMA can cause RSP queue timeout
        rspq_wait();
        healthSprites[0] = sprite_load("rom:/health1.sprite");
        healthSprites[1] = sprite_load("rom:/health2.sprite");
        healthSprites[2] = sprite_load("rom:/health3.sprite");
        healthSprites[3] = sprite_load("rom:/health4.sprite");
        healthSpritesLoaded = true;
    }

    healthHudVisible = true;
    healthHudTargetY = HEALTH_HUD_SHOW_Y;
    if (withFlash) {
        healthFlashTimer = HEALTH_FLASH_DURATION;
        healthHudHideTimer = HEALTH_DISPLAY_DURATION;  // Auto-hide after duration
    }
}

// Hide health HUD
static void hide_health_display(void) {
    healthHudVisible = false;
    healthHudTargetY = HEALTH_HUD_HIDE_Y;
    healthHudHideTimer = 0.0f;
}

// Trigger screw/bolt HUD display (slides in from right with spinning animation)
static void trigger_screw_display(bool autoHide) {
    // Lazy load screw sprites on first use (6 frames: 1,3,5,7,9,11)
    if (!screwSpritesLoaded) {
        debugf("Lazy loading screw sprites\n");
        // CRITICAL: Wait for RSP queue to flush before loading from ROM
        // ROM DMA conflicts with RDP DMA can cause RSP queue timeout
        rspq_wait();
        const int frameNumbers[SCREW_HUD_FRAME_COUNT] = {1, 3, 5, 7, 9, 11};
        for (int i = 0; i < SCREW_HUD_FRAME_COUNT; i++) {
            char path[32];
            sprintf(path, "rom:/ScrewUI%d.sprite", frameNumbers[i]);
            screwSprites[i] = sprite_load(path);
        }
        screwSpritesLoaded = true;
    }

    screwHudVisible = true;
    screwHudTargetX = SCREW_HUD_SHOW_X;
    if (autoHide) {
        screwHudHideTimer = SCREW_DISPLAY_DURATION;
    }
}

// Hide screw/bolt HUD
static void hide_screw_display(void) {
    screwHudVisible = false;
    screwHudTargetX = SCREW_HUD_HIDE_X;
    screwHudHideTimer = 0.0f;
}

// Trigger golden screw HUD display (slides in from right with spinning animation)
static void trigger_golden_display(bool autoHide) {
    // Lazy load golden screw sprites on first use (8 frames: gbolt1-8)
    if (!goldenSpritesLoaded) {
        debugf("Lazy loading golden screw sprites\n");
        // CRITICAL: Wait for RSP queue to flush before loading from ROM
        rspq_wait();
        for (int i = 0; i < GOLDEN_HUD_FRAME_COUNT; i++) {
            char path[32];
            sprintf(path, "rom:/GBOLT%d.sprite", i + 1);
            goldenSprites[i] = sprite_load(path);
        }
        goldenSpritesLoaded = true;
    }

    goldenHudVisible = true;
    goldenHudTargetX = GOLDEN_HUD_SHOW_X;
    if (autoHide) {
        goldenHudHideTimer = GOLDEN_DISPLAY_DURATION;
    }
}

// Hide golden screw HUD
static void hide_golden_display(void) {
    goldenHudVisible = false;
    goldenHudTargetX = GOLDEN_HUD_HIDE_X;
    goldenHudHideTimer = 0.0f;
}

// Single-player damage implementation (called by multiplayer.c when not in MP mode)
void singleplayer_take_damage(int damage) {
    // Ignore damage while invincible, dead, or in hurt animation
    // Also ignore if chargepad invincibility buff is active
    if (playerIsDead || playerInvincibilityTimer > 0.0f || buffInvincible) return;

    // Cancel any charged jump
    if (isCharging) {
        isCharging = false;
    }

    playerHealth -= damage;
    debugf("Player took %d damage! Health: %d/%d\n", damage, playerHealth, maxPlayerHealth);

    // Screen shake, hit flash, hitstop, and impact stars on any damage
    trigger_screen_shake(8.0f);
    trigger_hit_flash(0.15f);
    trigger_hitstop(0.08f);  // Brief freeze frame on damage
    trigger_health_display(true);  // Show health HUD with flash
    spawn_impact_stars();  // Cartoon stars around head

    if (playerHealth <= 0) {
        playerHealth = 0;
        playerIsDead = true;
        playerIsHurt = false;
        playerInvincibilityTimer = 0.0f;  // No invincibility when dead
        playerDeathTimer = 0.0f;  // Start death transition
        trigger_screen_shake(15.0f);  // Extra shake on death

        // Close any active dialogue/script on death
        if (scriptRunning) {
            dialogue_close(&dialogueBox);
            scriptRunning = false;
            if (activeInteractTrigger) {
                activeInteractTrigger->state.interactTrigger.interacting = false;
                activeInteractTrigger = NULL;
            }
        }

        // Track death in save system
        save_increment_deaths();
        save_increment_level_deaths(currentLevel);
        save_auto_save();
        levelDeaths++;  // Track for level complete screen

        // Stop recording without saving (death = failed run)
        if (replay_is_recording()) {
            replay_stop_recording(false);
        }

        // Start death animation (if available)
        if (currentPart == PART_TORSO && torsoHasAnims && torsoAnimDeath.animRef != NULL) {
            attach_anim_if_different(&torsoAnimDeath, &torsoSkel);
            t3d_anim_set_time(&torsoAnimDeath, 0.0f);
            t3d_anim_set_playing(&torsoAnimDeath, true);
        } else if (currentPart == PART_ARMS && armsHasAnims && armsAnimDeath.animRef != NULL) {
            attach_anim_if_different(&armsAnimDeath, &torsoSkel);
            t3d_anim_set_time(&armsAnimDeath, 0.0f);
            t3d_anim_set_playing(&armsAnimDeath, true);
        } else if (currentPart == PART_LEGS && fbHasAnims && fbAnimDeath.animRef != NULL) {
            attach_anim_if_different(&fbAnimDeath, &torsoSkel);
            t3d_anim_set_time(&fbAnimDeath, 0.0f);
            t3d_anim_set_playing(&fbAnimDeath, true);
        }
        debugf("Player died!\n");
    } else {
        // Start invincibility frames
        playerInvincibilityTimer = INVINCIBILITY_DURATION;
        playerInvincibilityFlashFrame = 0;

        // Start hurt animation (if available)
        playerIsHurt = true;
        playerHurtAnimTime = 0.0f;
        playerCurrentPainAnim = NULL;
        if (currentPart == PART_TORSO && torsoHasAnims) {
            // Randomly choose pain_1 or pain_2
            int painChoice = rand() % 2;
            T3DAnim* painAnim = (painChoice == 0) ? &torsoAnimPain1 : &torsoAnimPain2;
            if (painAnim->animRef != NULL) {
                playerCurrentPainAnim = painAnim;
                attach_anim_if_different(painAnim, &torsoSkel);
                t3d_anim_set_time(painAnim, 0.0f);
                t3d_anim_set_playing(painAnim, true);
                debugf("Playing torso pain animation %d\n", painChoice + 1);
            } else {
                // Animation not loaded - skip hurt state
                playerIsHurt = false;
                debugf("Warning: Torso pain animation not loaded!\n");
            }
        } else if (currentPart == PART_ARMS && armsHasAnims) {
            // Randomly choose arms pain_1 or pain_2
            int painChoice = rand() % 2;
            T3DAnim* painAnim = (painChoice == 0) ? &armsAnimPain1 : &armsAnimPain2;
            if (painAnim->animRef != NULL) {
                playerCurrentPainAnim = painAnim;
                attach_anim_if_different(painAnim, &torsoSkel);
                t3d_anim_set_time(painAnim, 0.0f);
                t3d_anim_set_playing(painAnim, true);
                debugf("Playing arms pain animation %d\n", painChoice + 1);
            } else {
                // Animation not loaded - skip hurt state
                playerIsHurt = false;
                debugf("Warning: Arms pain animation not loaded!\n");
            }
        } else if (currentPart == PART_LEGS && fbHasAnims) {
            // Randomly choose fullbody pain_1 or pain_2
            int painChoice = rand() % 2;
            T3DAnim* painAnim = (painChoice == 0) ? &fbAnimPain1 : &fbAnimPain2;
            if (painAnim->animRef != NULL) {
                playerCurrentPainAnim = painAnim;
                attach_anim_if_different(painAnim, &torsoSkel);
                t3d_anim_set_time(painAnim, 0.0f);
                t3d_anim_set_playing(painAnim, true);
                debugf("Playing fullbody pain animation %d\n", painChoice + 1);
            } else {
                // Animation not loaded - skip hurt state
                playerIsHurt = false;
                debugf("Warning: Fullbody pain animation not loaded!\n");
            }
        }
    }
}

// Global function to damage the player (can be called from decoration callbacks)
// Weak symbol - multiplayer.c overrides this when multiplayer scene is active
__attribute__((weak)) void player_take_damage(int damage) {
    singleplayer_take_damage(damage);
}

// Global function to squash the player model (can be called from decoration callbacks)
// Weak symbol - multiplayer.c overrides this when multiplayer scene is active
__attribute__((weak)) void player_squash(float amount) {
    squashScale = amount;
    squashVelocity = 0.0f;
}

// Global function to knock back the player (can be called from decoration callbacks)
// Weak symbol - multiplayer.c overrides this when multiplayer scene is active
__attribute__((weak)) void player_knockback(float fromX, float fromZ, float strength) {
    // Calculate direction from source to player (away from attacker)
    float dx = cubeX - fromX;
    float dz = cubeZ - fromZ;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > 0.01f) {
        dx /= dist;
        dz /= dist;
    } else {
        // Default knockback direction if on top of source
        dx = 0.0f;
        dz = 1.0f;
    }

    // Calculate total knockback distance
    float knockbackDist = strength * 4.0f;

    // Use substeps to prevent tunneling through walls (same approach as normal movement)
    const int KB_SUBSTEPS = 4;
    const float kbRadius = 8.0f;
    const float kbHeight = 30.0f;
    float stepDist = knockbackDist / KB_SUBSTEPS;

    for (int step = 0; step < KB_SUBSTEPS; step++) {
        // Calculate next position for this substep
        float nextX = cubeX + dx * stepDist;
        float nextZ = cubeZ + dz * stepDist;

        // Check map walls
        float pushX = 0.0f, pushZ = 0.0f;
        bool hitWall = maploader_check_walls(&mapLoader, nextX, cubeY, nextZ,
                                              kbRadius, kbHeight, &pushX, &pushZ);
        if (hitWall) {
            nextX += pushX;
            nextZ += pushZ;
        }

        // Check decoration walls
        float decoPushX = 0.0f, decoPushZ = 0.0f;
        bool hitDecoWall = map_check_deco_walls(&mapRuntime, nextX, cubeY, nextZ,
                                                 kbRadius, kbHeight, &decoPushX, &decoPushZ);
        if (hitDecoWall) {
            nextX += decoPushX;
            nextZ += decoPushZ;
        }

        // Update position
        cubeX = nextX;
        cubeZ = nextZ;

        // If we hit a wall, stop knockback early (don't keep pushing into wall)
        if (hitWall || hitDecoWall) {
            break;
        }
    }

    // Small upward pop
    playerState.velY = 2.0f;
    playerState.isGrounded = false;
}

// Global function to bounce the player upward (stomp reward)
void player_bounce(float strength) {
    playerState.velY = strength;
    playerState.isGrounded = false;
}

// Global function to push the player smoothly (adds velocity, not teleport)
void player_push(float fromX, float fromZ, float strength) {
    // Calculate direction from source to player (away from pusher)
    float dx = cubeX - fromX;
    float dz = cubeZ - fromZ;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > 0.01f) {
        dx /= dist;
        dz /= dist;
    } else {
        dx = 0.0f;
        dz = 1.0f;
    }
    // Add velocity in that direction
    playerState.velX += dx * strength;
    playerState.velZ += dz * strength;
}

// Called when player collects a bolt - saves to save file
void game_on_bolt_collected(int levelId, int boltIndex) {
    save_collect_bolt(levelId, boltIndex);
    levelBoltsCollected++;  // Track for level complete screen
    // Auto-save after collecting a bolt
    save_auto_save();
    // Show screw HUD with spinning animation
    trigger_screw_display(true);
    // Trigger white screen flash
    trigger_bolt_flash();
    // Juicy screen shake on bolt collect
    trigger_screen_shake(3.0f);

    // Check if this was the final collectible - show reward popup
    int totalBolts = save_get_total_bolts_collected();
    int totalScrewg = save_get_total_screwg_collected();
    bool allBoltsCollected = (totalBolts >= TOTAL_BOLTS_IN_GAME);
    bool allScrewgCollected = (TOTAL_SCREWG_IN_GAME == 0 || totalScrewg >= TOTAL_SCREWG_IN_GAME);
    if (allBoltsCollected && allScrewgCollected && !rewardPopupActive) {
        rewardPopupActive = true;
        rewardPopupTimer = REWARD_POPUP_DURATION;
        debugf("*** ALL COLLECTIBLES FOUND! Showing reward popup ***\n");
    }
}

// Called when player collects a golden screw - saves to save file
void game_on_screwg_collected(int levelId, int screwgIndex) {
    save_collect_screwg(levelId, screwgIndex);
    // Auto-save after collecting a golden screw
    save_auto_save();
    // Show golden screw HUD with spinning animation (separate from regular bolts)
    trigger_golden_display(true);
    // Trigger white screen flash
    trigger_bolt_flash();
    // Extra juicy screen shake for golden screw
    trigger_screen_shake(5.0f);

    // Check if this was the final collectible - show reward popup
    int totalBolts = save_get_total_bolts_collected();
    int totalScrewg = save_get_total_screwg_collected();
    bool allBoltsCollected = (totalBolts >= TOTAL_BOLTS_IN_GAME);
    bool allScrewgCollected = (TOTAL_SCREWG_IN_GAME == 0 || totalScrewg >= TOTAL_SCREWG_IN_GAME);
    if (allBoltsCollected && allScrewgCollected && !rewardPopupActive) {
        rewardPopupActive = true;
        rewardPopupTimer = REWARD_POPUP_DURATION;
        debugf("*** ALL COLLECTIBLES FOUND! Showing reward popup ***\n");
    }
}

// Cutscene state for body part tutorials
static bool g_cutsceneActive = false;
static bool g_cutsceneShowingDialogue = false;

// Start the torso tutorial cutscene
// This freezes the game and shows dialogue explaining torso controls
void game_start_torso_cutscene(void) {
    debugf("Starting torso tutorial cutscene\n");

    // Set cutscene active (freezes gameplay)
    g_cutsceneActive = true;
    g_cutsceneShowingDialogue = true;

    // Queue up the tutorial dialogue
    dialogue_queue_clear(&dialogueBox);
    dialogue_queue_add(&dialogueBox, "You found your TORSO!", "SYSTEM");
    dialogue_queue_add(&dialogueBox, "Hold A to CHARGE your jump. The longer you charge, the farther you'll go!", "TORSO");
    dialogue_queue_add(&dialogueBox, "Use the analog stick while charging to AIM your jump direction.", "TORSO");
    dialogue_queue_add(&dialogueBox, "Release A to LAUNCH! You can jump over large gaps this way.", "TORSO");
    dialogue_queue_add(&dialogueBox, "Quick tap A for a small HOP. Good for precise platforming!", "TORSO");

    // Start showing the queued dialogue
    dialogue_queue_start(&dialogueBox);

    // Give player the torso
    currentPart = PART_TORSO;

    // Flash and shake
    trigger_bolt_flash();
    trigger_screen_shake(5.0f);
}

// Check if cutscene just ended and clean up
static void game_check_cutscene_end(void) {
    if (g_cutsceneActive && g_cutsceneShowingDialogue) {
        // Check if dialogue is done
        if (!dialogue_is_active(&dialogueBox) && !dialogue_has_queued(&dialogueBox)) {
            debugf("Torso cutscene complete, resuming gameplay\n");
            g_cutsceneActive = false;
            g_cutsceneShowingDialogue = false;
        }
    }
}

// Check if we're in a cutscene (freezes gameplay)
bool game_is_cutscene_active(void) {
    return g_cutsceneActive;
}

// Restart the current level (called from pause menu)
void game_restart_level(void) {
    debugf("Restarting level %d\n", currentLevel);

    // Reset checkpoint (full restart = fresh start from level beginning)
    g_checkpointActive = false;
    g_checkpointX = 0.0f;
    g_checkpointY = 0.0f;
    g_checkpointZ = 0.0f;

    // Reset player state
    playerHealth = maxPlayerHealth;
    playerIsDead = false;
    playerIsHurt = false;
    playerHurtAnimTime = 0.0f;
    playerCurrentPainAnim = NULL;
    playerInvincibilityTimer = 0.0f;
    playerInvincibilityFlashFrame = 0;

    // Reset activation system (buttons, lasers, moving platforms, etc.)
    activation_reset_all();

    // Reset death/transition state
    playerDeathTimer = 0.0f;
    fadeAlpha = 0.0f;
    playerIsRespawning = false; g_playerIsRespawning = false;
    playerRespawnDelayTimer = 0.0f;
    isTransitioning = false;
    transitionTimer = 0.0f;
    irisActive = false;
    irisRadius = 400.0f;

    // Reset player velocity
    playerState.velX = 0.0f;
    playerState.velY = 0.0f;
    playerState.velZ = 0.0f;
    playerState.isGrounded = false;
    playerState.isOnSlope = false;
    playerState.isSliding = false;
    playerState.currentJumps = 0;
    playerState.canMove = true;
    playerState.canRotate = true;
    playerState.canJump = true;

    // Reset animation states
    isCharging = false;
    isJumping = false;
    isLanding = false;
    jumpAnimPaused = false;
    jumpChargeTime = 0.0f;
    jumpComboCount = 0;
    jumpComboTimer = 0.0f;
    idleFrames = 0;

    // Clear decorations and reload level
    map_clear_decorations(&mapRuntime);
    level_load(currentLevel, &mapLoader, &mapRuntime);
    mapRuntime.mapLoader = &mapLoader;  // Set collision reference for turret raycasts

    // Reinitialize lighting from level data (full restart = no checkpoint)
    init_lighting_state(currentLevel);

    // Set body part for this level
    currentPart = (RobotParts)level_get_body_part(currentLevel);

    // Find first PlayerSpawn and use it
    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (deco->type == DECO_PLAYERSPAWN && deco->active) {
            cubeX = deco->posX;
            cubeY = deco->posY;
            cubeZ = deco->posZ;
            // Sync frame pacing positions to avoid jitter on respawn
            prevCubeX = cubeX;
            prevCubeY = cubeY;
            prevCubeZ = cubeZ;
            renderCubeX = cubeX;
            renderCubeY = cubeY;
            renderCubeZ = cubeZ;
            debugf("Player respawned at: (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);
            break;
        }
    }

    // Stop any existing recording and restart from level start
    if (replay_is_recording()) {
        replay_stop_recording(false);  // Discard old recording
    }
    replay_start_recording(currentLevel, cubeX, cubeY, cubeZ);
    debugf("Auto-recording restarted at (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);

    // Countdown disabled for single player - player can move immediately

    debugf("Level %d restarted\n", currentLevel);
}

// Forward declaration
static void show_pause_menu(void);
static void show_options_menu(void);

// Helper to update volume option labels in-place (like menu_scene.c)
static void update_options_volume_labels(void) {
    // Update option labels directly (option 0 = music, option 1 = sfx)
    snprintf(optionsMenu.options[0], UI_MAX_OPTION_LENGTH, "Music Volume: %d", save_get_music_volume());
    snprintf(optionsMenu.options[1], UI_MAX_OPTION_LENGTH, "SFX Volume: %d", save_get_sfx_volume());
}

// Options menu - adjust volumes with left/right
static void on_options_leftright(int value) {
    int direction;
    int index = option_decode_leftright(value, &direction);

    if (index == 0) {
        // Music volume
        int vol = save_get_music_volume() + direction;
        if (vol < 0) vol = 0;
        if (vol > 10) vol = 10;
        save_set_music_volume(vol);
        update_options_volume_labels();
    } else if (index == 1) {
        // SFX volume
        int vol = save_get_sfx_volume() + direction;
        if (vol < 0) vol = 0;
        if (vol > 10) vol = 10;
        save_set_sfx_volume(vol);
        update_options_volume_labels();
        // Play test sound so user can hear the new volume
        ui_play_hover_sound();
    }
    // Don't call show_options_menu() - just update labels in place
}

// Options menu callback
static void on_options_select(int choice) {
    if (choice == 2) {  // Back
        save_write_settings();  // Save volume settings
        // Note: isInOptionsMenu will be set to false when menu closes
        // and the update loop will call show_pause_menu()
    }
    // Ignore select on volume items (use left/right instead)
}

// Options menu cancel callback (B button)
static void on_options_cancel(int choice) {
    (void)choice;
    save_write_settings();
    // Note: isInOptionsMenu will be set to false when menu closes
    // and the update loop will call show_pause_menu()
}

// Pause menu callback
static void on_pause_menu_select(int choice) {
    switch (choice) {
        case 0:  // Resume
            isPaused = false;
            hide_health_display();
            hide_screw_display();
            hide_golden_display();
            level_banner_unpause(&levelBanner);
            break;
        case 1:  // Options
            show_options_menu();
            break;
        case 2:  // Restart
            isPaused = false;
            level_banner_unpause(&levelBanner);
            game_restart_level();
            break;
        case 3:  // Save & Return to Menu
            // Cancel any active replay recording without saving
            if (replay_is_recording()) {
                replay_stop_recording(false);
            }
            save_auto_save();
            level_stop_music();
            isPaused = false;
            level_banner_unpause(&levelBanner);
            // Start iris close effect, will change scene when fully closed
            isQuittingToMenu = true;
            irisActive = true;
            irisRadius = 400.0f;
            irisCenterX = 160.0f;
            irisCenterY = 120.0f;
            // Tell menu scene to open with iris effect
            menuStartWithIrisOpen = true;
            break;
    }
}

// Show options submenu
static void show_options_menu(void) {
    if (!optionsMenuInitialized) {
        option_init(&optionsMenu);
        optionsMenuInitialized = true;
    }

    // Build volume labels with current values
    char musicLabel[32], sfxLabel[32];
    snprintf(musicLabel, sizeof(musicLabel), "Music Volume: %d", save_get_music_volume());
    snprintf(sfxLabel, sizeof(sfxLabel), "SFX Volume: %d", save_get_sfx_volume());

    option_set_title(&optionsMenu, "Options");
    option_add(&optionsMenu, musicLabel);
    option_add(&optionsMenu, sfxLabel);
    option_add(&optionsMenu, "Back");

    // Set up left/right callback for volume adjustment
    option_set_leftright(&optionsMenu, on_options_leftright);
    option_show(&optionsMenu, on_options_select, on_options_cancel);
    isInOptionsMenu = true;
}

// Open pause menu
static void show_pause_menu(void) {
    if (!pauseMenuInitialized) {
        option_init(&pauseMenu);
        pauseMenuInitialized = true;
    }

    option_set_title(&pauseMenu, "PAUSED");
    option_add(&pauseMenu, "Resume");
    option_add(&pauseMenu, "Options");
    option_add(&pauseMenu, "Restart Level");
    option_add(&pauseMenu, "Save & Quit");
    option_show(&pauseMenu, on_pause_menu_select, NULL);
    isPaused = true;
    isInOptionsMenu = false;
    trigger_health_display(false);  // Show health without flash when paused
    trigger_screw_display(false);   // Show bolt counter without auto-hide when paused
    trigger_golden_display(false);  // Show golden screw counter without auto-hide when paused
    level_banner_pause(&levelBanner);  // Show level name banner when paused

    // Clear input buffers to prevent stale inputs on unpause
    jumpBufferTimer = 0.0f;
    wallJumpInputBuffer = 0;
    coyoteTimer = 0.0f;
}

// Show level complete screen with current stats
void game_show_level_complete(void) {
    int totalBoltsInLevel = get_level_bolt_count(currentLevel);
    level_complete_set_data(currentLevel, levelBoltsCollected, totalBoltsInLevel, levelDeaths, levelTime);
    change_scene(LEVEL_COMPLETE);
}

// ============================================================
// CELEBRATION SYSTEM FUNCTIONS
// ============================================================

// Spawn a celebration firework at the celebration location
static void celebrate_spawn_firework(void) {
    for (int i = 0; i < MAX_CELEBRATE_FIREWORKS; i++) {
        if (!celebrateFireworks[i].active) {
            celebrateFireworks[i].active = true;
            celebrateFireworks[i].exploded = false;
            // Spawn across the screen width (X maps to screen X, range -140 to +140 from center)
            celebrateFireworks[i].x = celebrateWorldX + (celebrate_randf() - 0.5f) * 280.0f;
            celebrateFireworks[i].y = celebrateWorldY;
            celebrateFireworks[i].z = celebrateWorldZ + (celebrate_randf() - 0.5f) * 60.0f;
            celebrateFireworks[i].velY = 150.0f + celebrate_randf() * 80.0f;  // Upward velocity
            celebrateFireworks[i].colorIdx = celebrate_rand() % NUM_CELEBRATE_COLORS;
            break;
        }
    }
}

// Explode a celebration firework into sparks
static void celebrate_explode_firework(CelebrateFirework* fw) {
    fw->exploded = true;
    fw->active = false;

    // Spawn sparks in a sphere
    int sparksToSpawn = 8 + (celebrate_rand() % 6);  // 8-14 sparks
    for (int i = 0; i < sparksToSpawn; i++) {
        for (int j = 0; j < MAX_CELEBRATE_SPARKS; j++) {
            if (!celebrateSparks[j].active) {
                float theta = celebrate_randf() * 6.283f;  // Angle around Y
                float phi = celebrate_randf() * 3.14159f;  // Angle from vertical
                float speed = 40.0f + celebrate_randf() * 40.0f;
                celebrateSparks[j].active = true;
                celebrateSparks[j].x = fw->x;
                celebrateSparks[j].y = fw->y;
                celebrateSparks[j].z = fw->z;
                celebrateSparks[j].velX = sinf(phi) * cosf(theta) * speed;
                celebrateSparks[j].velY = cosf(phi) * speed;
                celebrateSparks[j].velZ = sinf(phi) * sinf(theta) * speed;
                celebrateSparks[j].maxLife = 0.5f + celebrate_randf() * 0.5f;
                celebrateSparks[j].life = celebrateSparks[j].maxLife;
                celebrateSparks[j].colorIdx = fw->colorIdx;
                break;
            }
        }
    }
}

// Start the celebration at given world position
static void start_celebration(float worldX, float worldY, float worldZ, int targetLevel, int targetSpawn) {
    celebratePhase = CELEBRATE_FIREWORKS;
    celebrateTimer = 0.0f;
    celebrateFireworkSpawnTimer = 0.0f;
    celebrateWorldX = worldX;
    celebrateWorldY = worldY;
    celebrateWorldZ = worldZ;
    celebrateBlinkTimer = 0;
    celebrateRng = 54321 + (uint32_t)(levelTime * 1000);

    // Store target for later transition
    targetTransitionLevel = targetLevel;
    targetTransitionSpawn = targetSpawn;

    // Freeze the player in place
    playerState.velX = 0.0f;
    playerState.velY = 0.0f;
    playerState.velZ = 0.0f;

    // Clear all fireworks and sparks
    for (int i = 0; i < MAX_CELEBRATE_FIREWORKS; i++) {
        celebrateFireworks[i].active = false;
        celebrateFireworks[i].exploded = false;
    }
    for (int i = 0; i < MAX_CELEBRATE_SPARKS; i++) {
        celebrateSparks[i].active = false;
    }

    // Mark level as completed
    save_complete_level(currentLevel);

    // Update best time if this was a faster run
    uint16_t completionTime = (uint16_t)levelTime;
    uint16_t currentBest = save_get_best_time(currentLevel);
    bool isNewBestTime = (currentBest == 0 || completionTime < currentBest);

    if (isNewBestTime) {
        save_update_best_time(currentLevel, completionTime);
    }

    // Calculate and save rank
    // Use total bolts collected for this level (already saved + collected this run)
    // This allows replaying levels to still get S rank even if bolts were already collected
    int totalBolts = get_level_bolt_count(currentLevel);
    int savedLevelBolts = save_get_level_bolts_collected(currentLevel);
    int effectiveBoltsCollected = savedLevelBolts;  // Already includes bolts from this run
    celebrateRank = calculate_level_rank(levelDeaths, effectiveBoltsCollected, totalBolts, levelTime);
    save_update_best_rank(currentLevel, celebrateRank);

    // Stop recording - only save replay if it's a new best time
    if (replay_is_recording()) {
        bool shouldSaveGhost = isNewBestTime;
        // Always dump replay data as C code for demo system (for development)
        replay_dump_as_code(currentLevel);
        replay_stop_recording(shouldSaveGhost);
    }

    save_auto_save();

    debugf("Started celebration at (%.1f, %.1f, %.1f) for transition to Level %d\n",
           worldX, worldY, worldZ, targetLevel);
}

// Update celebration state (call from update_game_scene)
static void update_celebration(float deltaTime) {
    if (celebratePhase == CELEBRATE_INACTIVE) return;

    celebrateTimer += deltaTime;
    celebrateBlinkTimer++;
    if (celebrateBlinkTimer >= UI_BLINK_RATE * 2) {
        celebrateBlinkTimer = 0;
    }

    if (celebratePhase == CELEBRATE_FIREWORKS) {
        // Spawn fireworks periodically
        celebrateFireworkSpawnTimer += deltaTime;
        if (celebrateFireworkSpawnTimer >= CELEBRATE_FIREWORK_INTERVAL) {
            celebrateFireworkSpawnTimer = 0.0f;
            celebrate_spawn_firework();
        }

        // Update firework rockets
        for (int i = 0; i < MAX_CELEBRATE_FIREWORKS; i++) {
            if (celebrateFireworks[i].active && !celebrateFireworks[i].exploded) {
                celebrateFireworks[i].y += celebrateFireworks[i].velY * deltaTime;
                celebrateFireworks[i].velY -= 120.0f * deltaTime;  // Gravity slowing it

                // Explode when velocity gets low or reaches peak height
                if (celebrateFireworks[i].velY <= 20.0f ||
                    celebrateFireworks[i].y > celebrateWorldY + 150.0f) {
                    celebrate_explode_firework(&celebrateFireworks[i]);
                }
            }
        }

        // Update sparks
        for (int i = 0; i < MAX_CELEBRATE_SPARKS; i++) {
            if (celebrateSparks[i].active) {
                celebrateSparks[i].x += celebrateSparks[i].velX * deltaTime;
                celebrateSparks[i].y += celebrateSparks[i].velY * deltaTime;
                celebrateSparks[i].z += celebrateSparks[i].velZ * deltaTime;
                celebrateSparks[i].velY -= 80.0f * deltaTime;  // Gravity
                celebrateSparks[i].life -= deltaTime;

                if (celebrateSparks[i].life <= 0.0f) {
                    celebrateSparks[i].active = false;
                }
            }
        }

        // After firework duration, show the UI
        if (celebrateTimer >= CELEBRATE_FIREWORK_DURATION) {
            celebratePhase = CELEBRATE_UI_SHOWING;
            debugf("Fireworks done, showing level complete UI\n");
        }
    }
    else if (celebratePhase == CELEBRATE_UI_SHOWING) {
        // Continue updating sparks so they don't freeze
        for (int i = 0; i < MAX_CELEBRATE_SPARKS; i++) {
            if (celebrateSparks[i].active) {
                celebrateSparks[i].x += celebrateSparks[i].velX * deltaTime;
                celebrateSparks[i].y += celebrateSparks[i].velY * deltaTime;
                celebrateSparks[i].z += celebrateSparks[i].velZ * deltaTime;
                celebrateSparks[i].velY -= 80.0f * deltaTime;  // Gravity
                celebrateSparks[i].life -= deltaTime;

                if (celebrateSparks[i].life <= 0.0f) {
                    celebrateSparks[i].active = false;
                }
            }
        }

        // Also spawn new fireworks to keep it festive
        celebrateFireworkSpawnTimer += deltaTime;
        if (celebrateFireworkSpawnTimer >= CELEBRATE_FIREWORK_INTERVAL) {
            celebrateFireworkSpawnTimer = 0.0f;
            celebrate_spawn_firework();
        }

        // Update rising fireworks
        for (int i = 0; i < MAX_CELEBRATE_FIREWORKS; i++) {
            if (celebrateFireworks[i].active && !celebrateFireworks[i].exploded) {
                celebrateFireworks[i].y += celebrateFireworks[i].velY * deltaTime;
                celebrateFireworks[i].velY -= 120.0f * deltaTime;

                if (celebrateFireworks[i].velY <= 20.0f ||
                    celebrateFireworks[i].y > celebrateWorldY + 150.0f) {
                    celebrate_explode_firework(&celebrateFireworks[i]);
                }
            }
        }

        // Wait for A button (continue) or B button (return to menu)
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (pressed.a || pressed.b) {
            celebrateReturnToMenu = pressed.b;  // Track if user chose to return to menu
            celebratePhase = CELEBRATE_DONE;
            // Free rank sprite when celebration ends
            if (rankSprite != NULL) {
                sprite_free(rankSprite);
                rankSprite = NULL;
                rankSpriteChar = 0;
            }
            // Start the iris close effect
            irisActive = true;
            irisRadius = 400.0f;  // Start large
            irisCenterX = 160.0f;
            irisCenterY = 120.0f;
            irisPaused = false;
            irisPauseTimer = 0.0f;
            ui_play_press_a_sound();  // Play confirmation sound
            if (celebrateReturnToMenu) {
                debugf("B pressed, returning to menu\n");
            } else {
                debugf("A pressed, starting iris transition to Level %d\n", targetTransitionLevel);
            }
        }
    }
    else if (celebratePhase == CELEBRATE_DONE) {
        // Handle iris closing transition
        if (irisActive && irisRadius > 0.0f) {
            // Shrink iris with dramatic pause when small
            if (irisPaused) {
                // Hold at small circle for dramatic effect
                irisPauseTimer += deltaTime;
                if (irisPauseTimer >= IRIS_PAUSE_DURATION) {
                    irisPaused = false;
                }
            } else if (irisRadius > IRIS_PAUSE_RADIUS && !irisPaused && irisPauseTimer == 0.0f) {
                // Initial shrinking phase
                float shrinkSpeed = irisRadius * 0.06f;
                if (shrinkSpeed < 3.0f) shrinkSpeed = 3.0f;
                irisRadius -= shrinkSpeed;

                // Snap to pause radius when we reach it
                if (irisRadius <= IRIS_PAUSE_RADIUS) {
                    irisRadius = IRIS_PAUSE_RADIUS;
                    irisPaused = true;
                    irisPauseTimer = 0.0f;
                }
            } else if (irisRadius > 0.0f) {
                // Final closing phase
                float shrinkSpeed = irisRadius * 0.12f;
                if (shrinkSpeed < 2.0f) shrinkSpeed = 2.0f;
                irisRadius -= shrinkSpeed;
                if (irisRadius < 0.0f) irisRadius = 0.0f;
            }
        }

        // When iris fully closed, load the new level (or return to menu)
        if (irisRadius <= 0.0f) {
            // In replay mode, return to menu instead of loading next level
            if (g_replayMode) {
                debugf("Replay complete, returning to menu\n");
                replay_stop_playback();
                g_replayMode = false;
                celebratePhase = CELEBRATE_INACTIVE;
                isTransitioning = false;
                irisActive = false;
                change_scene(MENU_SCENE);
                return;
            }

            // User chose "Return to Menu" with B button
            if (celebrateReturnToMenu) {
                debugf("Returning to menu (user choice)\n");
                celebratePhase = CELEBRATE_INACTIVE;
                isTransitioning = false;
                irisActive = false;
                celebrateReturnToMenu = false;
                change_scene(MENU_SCENE);
                return;
            }

            // Check if we're entering level 2 (play CS2 cutscene intro, only once per save)
            bool isEnteringLevel2 = (targetTransitionLevel == LEVEL_2);

            if (isEnteringLevel2 && !cs2Playing && !save_has_watched_cs2()) {
                debugf("Entering Level 2, playing CS2 cutscene intro\n");
                save_mark_cs2_watched();  // Mark as watched so it only plays once
                celebratePhase = CELEBRATE_CS2_SLIDESHOW;
                cs2Playing = true;
                cs2CurrentFrame = 0;
                cs2FrameTimer = 0.0f;
                cs2_load_frame(0);
                return;  // Wait for CS2 to finish
            }

            debugf("Iris closed, loading level %d...\n", targetTransitionLevel);

            // Update current level and selectedLevelID (so pickups know the correct level)
            currentLevel = (LevelID)targetTransitionLevel;
            selectedLevelID = targetTransitionLevel;

            // Reset player velocity and state
            playerState.velX = 0.0f;
            playerState.velY = 0.0f;
            playerState.velZ = 0.0f;
            playerState.isGrounded = false;
            playerState.canMove = true;
            playerState.canRotate = true;
            playerState.canJump = true;

            // Reset player health and activation system on level transition
            playerHealth = maxPlayerHealth;
            activation_reset_all();

            // Reset animation states
            isCharging = false;
            isJumping = false;
            isLanding = false;
            jumpAnimPaused = false;
            jumpChargeTime = 0.0f;
            idleFrames = 0;
            playingFidget = false;
            fidgetPlayTime = 0.0f;

            // Free old level data
            map_runtime_free(&mapRuntime);
            maploader_free(&mapLoader);
            collision_loader_free_all();  // Free collision meshes to prevent stale pointer reuse
            // (level3_special removed - using DECO_LEVEL3_STREAM instead)

            // Clear fog override so new level starts with its default fog
            game_clear_fog_override();

            // Reload for new level
            maploader_init(&mapLoader, FB_COUNT, VISIBILITY_RANGE);
            map_runtime_init(&mapRuntime, FB_COUNT, VISIBILITY_RANGE);
            currentlyAttachedAnim = NULL;
            level_load(currentLevel, &mapLoader, &mapRuntime);
            mapRuntime.mapLoader = &mapLoader;  // Set collision reference for turret raycasts
            // level3_special_load removed  // Load level 3 special decos if entering level 3

            // Initialize lighting for new level
            init_lighting_state(currentLevel);

            // Set body part for this level (reload model if changed)
            RobotParts oldPart = currentPart;
            RobotParts newPart = (RobotParts)level_get_body_part(currentLevel);

            if (oldPart != newPart || torsoModel == NULL) {
                debugf("Body part changed from %d to %d, reloading model\n", oldPart, newPart);

                // Free old model and animations
                if (torsoModel) {
                    currentlyAttachedAnim = NULL;
                    if (torsoHasAnims) {
                        t3d_anim_destroy(&torsoAnimIdle); torsoAnimIdle.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimWalk); torsoAnimWalk.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimWalkSlow); torsoAnimWalkSlow.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimJumpCharge); torsoAnimJumpCharge.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimJumpLaunch); torsoAnimJumpLaunch.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimJumpDouble); torsoAnimJumpDouble.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimJumpTriple); torsoAnimJumpTriple.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimJumpLand); torsoAnimJumpLand.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimWait); torsoAnimWait.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimPain1); torsoAnimPain1.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimPain2); torsoAnimPain2.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimDeath); torsoAnimDeath.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimSlideFront); torsoAnimSlideFront.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimSlideFrontRecover); torsoAnimSlideFrontRecover.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimSlideBack); torsoAnimSlideBack.animRef = NULL;
                        t3d_anim_destroy(&torsoAnimSlideBackRecover); torsoAnimSlideBackRecover.animRef = NULL;
                        torsoHasAnims = false;
                    }
                    if (armsHasAnims) {
                        t3d_anim_destroy(&armsAnimIdle); armsAnimIdle.animRef = NULL;
                        t3d_anim_destroy(&armsAnimWalk1); armsAnimWalk1.animRef = NULL;
                        t3d_anim_destroy(&armsAnimWalk2); armsAnimWalk2.animRef = NULL;
                        t3d_anim_destroy(&armsAnimJump); armsAnimJump.animRef = NULL;
                        t3d_anim_destroy(&armsAnimJumpLand); armsAnimJumpLand.animRef = NULL;
                        t3d_anim_destroy(&armsAnimAtkSpin); armsAnimAtkSpin.animRef = NULL;
                        t3d_anim_destroy(&armsAnimAtkWhip); armsAnimAtkWhip.animRef = NULL;
                        t3d_anim_destroy(&armsAnimDeath); armsAnimDeath.animRef = NULL;
                        t3d_anim_destroy(&armsAnimPain1); armsAnimPain1.animRef = NULL;
                        t3d_anim_destroy(&armsAnimPain2); armsAnimPain2.animRef = NULL;
                        t3d_anim_destroy(&armsAnimSlide); armsAnimSlide.animRef = NULL;
                        armsHasAnims = false;
                    }
                    if (fbHasAnims) {
                        t3d_anim_destroy(&fbAnimIdle); fbAnimIdle.animRef = NULL;
                        t3d_anim_destroy(&fbAnimWalk); fbAnimWalk.animRef = NULL;
                        t3d_anim_destroy(&fbAnimRun); fbAnimRun.animRef = NULL;
                        t3d_anim_destroy(&fbAnimJump); fbAnimJump.animRef = NULL;
                        t3d_anim_destroy(&fbAnimWait); fbAnimWait.animRef = NULL;
                        t3d_anim_destroy(&fbAnimCrouch); fbAnimCrouch.animRef = NULL;
                        t3d_anim_destroy(&fbAnimCrouchJump); fbAnimCrouchJump.animRef = NULL;
                        t3d_anim_destroy(&fbAnimCrouchJumpHover); fbAnimCrouchJumpHover.animRef = NULL;
                        t3d_anim_destroy(&fbAnimLongJump); fbAnimLongJump.animRef = NULL;
                        t3d_anim_destroy(&fbAnimDeath); fbAnimDeath.animRef = NULL;
                        t3d_anim_destroy(&fbAnimPain1); fbAnimPain1.animRef = NULL;
                        t3d_anim_destroy(&fbAnimPain2); fbAnimPain2.animRef = NULL;
                        t3d_anim_destroy(&fbAnimSlide); fbAnimSlide.animRef = NULL;
                        t3d_anim_destroy(&fbAnimSpinAir); fbAnimSpinAir.animRef = NULL;
                        t3d_anim_destroy(&fbAnimSpinAtk); fbAnimSpinAtk.animRef = NULL;
                        t3d_anim_destroy(&fbAnimSpinCharge); fbAnimSpinCharge.animRef = NULL;
                        t3d_anim_destroy(&fbAnimRunNinja); fbAnimRunNinja.animRef = NULL;
                        t3d_anim_destroy(&fbAnimCrouchAttack); fbAnimCrouchAttack.animRef = NULL;
                        fbHasAnims = false;
                    }
                    t3d_skeleton_destroy(&torsoSkelBlend);
                    t3d_skeleton_destroy(&torsoSkel);
                    t3d_model_free(torsoModel);
                    torsoModel = NULL;
                }

                // Update current part and load new model
                currentPart = newPart;
                const char* modelPath = get_body_part_model_path(currentPart);
                debugf("Loading player model for part %d: %s\n", currentPart, modelPath);
                torsoModel = t3d_model_load(modelPath);

                if (torsoModel) {
                    torsoSkel = t3d_skeleton_create(torsoModel);
                    torsoSkelBlend = t3d_skeleton_clone(&torsoSkel, false);

                    if (currentPart == PART_TORSO || currentPart == PART_HEAD) {
                        torsoAnimIdle = t3d_anim_create(torsoModel, "torso_idle");
                        t3d_anim_set_looping(&torsoAnimIdle, true);
                        t3d_anim_attach(&torsoAnimIdle, &torsoSkel);
                        t3d_anim_set_playing(&torsoAnimIdle, true);

                        torsoAnimWalk = t3d_anim_create(torsoModel, "torso_walk_fast");
                        t3d_anim_attach(&torsoAnimWalk, &torsoSkelBlend);
                        t3d_anim_set_looping(&torsoAnimWalk, true);
                        t3d_anim_set_playing(&torsoAnimWalk, true);

                        torsoAnimWalkSlow = t3d_anim_create(torsoModel, "torso_walk_slow");
                        t3d_anim_attach(&torsoAnimWalkSlow, &torsoSkelBlend);
                        t3d_anim_set_looping(&torsoAnimWalkSlow, true);
                        t3d_anim_set_playing(&torsoAnimWalkSlow, true);

                        torsoAnimJumpCharge = t3d_anim_create(torsoModel, "torso_jump_charge");
                        t3d_anim_set_looping(&torsoAnimJumpCharge, false);

                        torsoAnimJumpLaunch = t3d_anim_create(torsoModel, "torso_jump_launch");
                        t3d_anim_set_looping(&torsoAnimJumpLaunch, false);

                        torsoAnimJumpDouble = t3d_anim_create(torsoModel, "torso_double");
                        t3d_anim_set_looping(&torsoAnimJumpDouble, false);

                        torsoAnimJumpTriple = t3d_anim_create(torsoModel, "torso_tripple");
                        t3d_anim_set_looping(&torsoAnimJumpTriple, false);

                        // Initialize currentLaunchAnim to default
                        currentLaunchAnim = &torsoAnimJumpLaunch;

                        torsoAnimJumpLand = t3d_anim_create(torsoModel, "torso_jump_land");
                        t3d_anim_set_looping(&torsoAnimJumpLand, false);

                        torsoAnimWait = t3d_anim_create(torsoModel, "torso_wait");
                        t3d_anim_set_looping(&torsoAnimWait, false);

                        torsoAnimPain1 = t3d_anim_create(torsoModel, "torso_pain_1");
                        t3d_anim_set_looping(&torsoAnimPain1, false);

                        torsoAnimPain2 = t3d_anim_create(torsoModel, "torso_pain_2");
                        t3d_anim_set_looping(&torsoAnimPain2, false);

                        torsoAnimDeath = t3d_anim_create(torsoModel, "torso_death");
                        t3d_anim_set_looping(&torsoAnimDeath, false);

                        torsoAnimSlideFront = t3d_anim_create(torsoModel, "torso_slide_front");
                        t3d_anim_set_looping(&torsoAnimSlideFront, false);

                        torsoAnimSlideFrontRecover = t3d_anim_create(torsoModel, "torso_slide_front_recover");
                        t3d_anim_set_looping(&torsoAnimSlideFrontRecover, false);

                        torsoAnimSlideBack = t3d_anim_create(torsoModel, "torso_slide_back");
                        t3d_anim_set_looping(&torsoAnimSlideBack, false);

                        torsoAnimSlideBackRecover = t3d_anim_create(torsoModel, "torso_slide_back_recover");
                        t3d_anim_set_looping(&torsoAnimSlideBackRecover, false);

                        torsoHasAnims = true;
                        debugf("Loaded Torso model with animations for level transition\n");
                    } else if (currentPart == PART_ARMS) {
                        armsAnimIdle = t3d_anim_create(torsoModel, "arms_idle");
                        t3d_anim_set_looping(&armsAnimIdle, true);
                        t3d_anim_attach(&armsAnimIdle, &torsoSkel);
                        t3d_anim_set_playing(&armsAnimIdle, true);

                        armsAnimWalk1 = t3d_anim_create(torsoModel, "arms_walk_1");
                        t3d_anim_attach(&armsAnimWalk1, &torsoSkelBlend);
                        t3d_anim_set_looping(&armsAnimWalk1, true);
                        t3d_anim_set_playing(&armsAnimWalk1, true);

                        armsAnimWalk2 = t3d_anim_create(torsoModel, "arms_walk_2");
                        t3d_anim_set_looping(&armsAnimWalk2, true);

                        armsAnimJump = t3d_anim_create(torsoModel, "arms_jump");
                        t3d_anim_set_looping(&armsAnimJump, false);

                        armsAnimJumpLand = t3d_anim_create(torsoModel, "arms_jump_land");
                        t3d_anim_set_looping(&armsAnimJumpLand, false);

                        armsAnimAtkSpin = t3d_anim_create(torsoModel, "arms_atk_spin");
                        t3d_anim_set_looping(&armsAnimAtkSpin, false);

                        armsAnimAtkWhip = t3d_anim_create(torsoModel, "arms_atk_whip");
                        t3d_anim_set_looping(&armsAnimAtkWhip, false);

                        armsAnimDeath = t3d_anim_create(torsoModel, "arms_death");
                        t3d_anim_set_looping(&armsAnimDeath, false);

                        armsAnimPain1 = t3d_anim_create(torsoModel, "arms_pain_1");
                        t3d_anim_set_looping(&armsAnimPain1, false);

                        armsAnimPain2 = t3d_anim_create(torsoModel, "arms_pain_2");
                        t3d_anim_set_looping(&armsAnimPain2, false);

                        armsAnimSlide = t3d_anim_create(torsoModel, "arms_slide");
                        t3d_anim_set_looping(&armsAnimSlide, true);

                        armsHasAnims = armsAnimIdle.animRef && armsAnimWalk1.animRef && armsAnimAtkSpin.animRef;
                        debugf("Arms mode animations loaded: %s\n", armsHasAnims ? "OK" : "MISSING");
                    } else if (currentPart == PART_LEGS) {
                        // Load fullbody animations
                        debugf("Loading fullbody animations (runtime)...\n");

                        fbAnimIdle = t3d_anim_create(torsoModel, "fb_idle");
                        t3d_anim_set_looping(&fbAnimIdle, true);
                        t3d_anim_attach(&fbAnimIdle, &torsoSkel);
                        t3d_anim_set_playing(&fbAnimIdle, true);

                        fbAnimWalk = t3d_anim_create(torsoModel, "fb_walk");
                        t3d_anim_attach(&fbAnimWalk, &torsoSkelBlend);
                        t3d_anim_set_looping(&fbAnimWalk, true);
                        t3d_anim_set_playing(&fbAnimWalk, true);

                        fbAnimRun = t3d_anim_create(torsoModel, "fb_run");
                        t3d_anim_set_looping(&fbAnimRun, true);

                        fbAnimJump = t3d_anim_create(torsoModel, "fb_jump");
                        t3d_anim_set_looping(&fbAnimJump, false);

                        fbAnimWait = t3d_anim_create(torsoModel, "fb_wait");
                        t3d_anim_set_looping(&fbAnimWait, false);

                        fbAnimCrouch = t3d_anim_create(torsoModel, "fb_crouch");
                        t3d_anim_set_looping(&fbAnimCrouch, true);

                        fbAnimCrouchJump = t3d_anim_create(torsoModel, "fb_crouch_jump");
                        t3d_anim_set_looping(&fbAnimCrouchJump, false);

                        fbAnimCrouchJumpHover = t3d_anim_create(torsoModel, "fb_crouch_jump_hover");
                        t3d_anim_set_looping(&fbAnimCrouchJumpHover, true);

                        fbAnimLongJump = t3d_anim_create(torsoModel, "fb_long_jump");
                        t3d_anim_set_looping(&fbAnimLongJump, false);

                        fbAnimDeath = t3d_anim_create(torsoModel, "fb_death");
                        t3d_anim_set_looping(&fbAnimDeath, false);

                        fbAnimPain1 = t3d_anim_create(torsoModel, "fb_pain_1");
                        t3d_anim_set_looping(&fbAnimPain1, false);

                        fbAnimPain2 = t3d_anim_create(torsoModel, "fb_pain_2");
                        t3d_anim_set_looping(&fbAnimPain2, false);

                        fbAnimSlide = t3d_anim_create(torsoModel, "fb_slide");
                        t3d_anim_set_looping(&fbAnimSlide, true);

                        fbAnimSpinAir = t3d_anim_create(torsoModel, "fb_spin_air");
                        t3d_anim_set_looping(&fbAnimSpinAir, false);

                        fbAnimSpinAtk = t3d_anim_create(torsoModel, "fb_spin_atk");
                        t3d_anim_set_looping(&fbAnimSpinAtk, false);

                        fbAnimSpinCharge = t3d_anim_create(torsoModel, "fb_spin_charge");
                        t3d_anim_set_looping(&fbAnimSpinCharge, true);

                        fbAnimRunNinja = t3d_anim_create(torsoModel, "fb_run_ninja");
                        t3d_anim_set_looping(&fbAnimRunNinja, true);

                        fbAnimCrouchAttack = t3d_anim_create(torsoModel, "fb_crouch_attack");
                        t3d_anim_set_looping(&fbAnimCrouchAttack, false);

                        fbHasAnims = fbAnimIdle.animRef && fbAnimWalk.animRef && fbAnimJump.animRef;
                        debugf("Fullbody mode animations loaded: %s\n", fbHasAnims ? "OK" : "MISSING");
                    }
                }

                // Reset animation states for new body part
                isCharging = false;
                isJumping = false;
                isLanding = false;
                jumpAnimPaused = false;
                jumpChargeTime = 0.0f;
                armsIsSpinning = false;
                armsIsWhipping = false;
                armsSpinTime = 0.0f;
                armsWhipTime = 0.0f;
            } else {
                // Same body part, just reset to idle animation
                currentPart = newPart;
                if (currentPart == PART_TORSO && torsoHasAnims && torsoAnimIdle.animRef != NULL) {
                    t3d_anim_attach(&torsoAnimIdle, &torsoSkel);
                    t3d_anim_set_time(&torsoAnimIdle, 0.0f);
                    t3d_anim_set_playing(&torsoAnimIdle, true);
                    t3d_anim_update(&torsoAnimIdle, 0.0f);
                    if (skeleton_is_valid(&torsoSkel)) {
                        t3d_skeleton_update(&torsoSkel);
                    }
                } else if (currentPart == PART_ARMS && armsHasAnims && armsAnimIdle.animRef != NULL) {
                    t3d_anim_attach(&armsAnimIdle, &torsoSkel);
                    t3d_anim_set_time(&armsAnimIdle, 0.0f);
                    t3d_anim_set_playing(&armsAnimIdle, true);
                    t3d_anim_update(&armsAnimIdle, 0.0f);
                    if (skeleton_is_valid(&torsoSkel)) {
                        t3d_skeleton_update(&torsoSkel);
                    }
                }
            }

            // Find spawn point
            int spawnIndex = 0;
            bool foundSpawn = false;
            for (int i = 0; i < mapRuntime.decoCount; i++) {
                DecoInstance* deco = &mapRuntime.decorations[i];
                if (deco->type == DECO_PLAYERSPAWN && deco->active) {
                    if (spawnIndex == targetTransitionSpawn) {
                        cubeX = deco->posX;
                        cubeY = deco->posY;
                        cubeZ = deco->posZ;
                        foundSpawn = true;
                        debugf("Found spawn %d at (%.1f, %.1f, %.1f)\n",
                               targetTransitionSpawn, cubeX, cubeY, cubeZ);
                        break;
                    }
                    spawnIndex++;
                }
            }
            if (!foundSpawn) {
                // Fallback to level default
                const LevelData* level = ALL_LEVELS[currentLevel];
                cubeX = level->playerStartX;
                cubeY = level->playerStartY;
                cubeZ = level->playerStartZ;
            }

            // Snap player to ground immediately so they don't hover
            float groundY = maploader_get_ground_height(&mapLoader, cubeX, cubeY + 50.0f, cubeZ);
            if (groundY > -9000.0f) {
                cubeY = groundY;
                prevCubeX = cubeX; prevCubeY = groundY; prevCubeZ = cubeZ;
                renderCubeX = cubeX; renderCubeY = groundY; renderCubeZ = cubeZ;
                playerState.isGrounded = true;
                playerState.velY = 0.0f;
                isJumping = false;
                isLanding = false;
            }

            // Reset level tracking
            levelTime = 0.0f;
            levelDeaths = 0;
            levelBoltsCollected = 0;

            // Reset celebration state
            celebratePhase = CELEBRATE_INACTIVE;
            isTransitioning = false;

            // Recording is now manual (D-pad right toggle) - don't auto-start

            // Now start iris opening with respawn logic
            playerIsRespawning = true; g_playerIsRespawning = true;
            playerRespawnDelayTimer = 0.5f;

            // Show level banner
            level_banner_show(&levelBanner, get_level_name_with_number(currentLevel));

            // Check for controls tutorial after level loads
            debugf("Tutorial check after transition: level=%d, part=%d\n", currentLevel, (int)currentPart);
            tutorial_check_and_show(currentLevel, (int)currentPart);
            if (tutorialActive) {
                playerState.canMove = false;
                playerState.canJump = false;
                playerState.canRotate = false;
            }

            // Countdown disabled for single player - player can move immediately

            debugf("Level %d loaded, celebration complete\n", currentLevel);
        }
    }
    else if (celebratePhase == CELEBRATE_CS2_SLIDESHOW) {
        // CS2 slideshow is playing - update it here
        cs2FrameTimer += 1.0f;  // Increment by 1 frame per update (30fps)

        // Check if current frame duration expired
        float frameDuration = (float)CS2_FRAME_DURATIONS[cs2CurrentFrame];
        if (cs2FrameTimer >= frameDuration) {
            cs2FrameTimer = 0.0f;
            cs2CurrentFrame++;

            if (cs2CurrentFrame >= CS2_FRAME_COUNT) {
                // Slideshow complete - continue with level loading
                debugf("=== CS2 CUTSCENE COMPLETE, loading level ===\n");
                cs2Playing = false;

                // Unload both sprites (double-buffered)
                if (cs2CurrentSprite) {
                    sprite_free(cs2CurrentSprite);
                    cs2CurrentSprite = NULL;
                }
                if (cs2NextSprite) {
                    sprite_free(cs2NextSprite);
                    cs2NextSprite = NULL;
                }

                // Go back to CELEBRATE_DONE to continue the level load
                celebratePhase = CELEBRATE_DONE;
            } else {
                // Advance to next frame (uses pre-loaded sprite)
                cs2_advance_frame();
            }
        }
    }
}

// Global function to trigger level transition (can be called from decoration callbacks)
void trigger_level_transition(int targetLevel, int targetSpawn) {
    // Validate target level
    if (targetLevel < 0 || targetLevel >= LEVEL_COUNT) {
        debugf("ERROR: Invalid target level %d\n", targetLevel);
        return;
    }

    // Don't trigger if already transitioning or celebrating
    if (isTransitioning || celebratePhase != CELEBRATE_INACTIVE) {
        return;
    }

    // In demo mode, just set the goal reached flag and skip the celebration
    // demo_scene.c will handle starting the next demo
    if (g_demoMode) {
        debugf("Demo: Goal reached, signaling demo_scene\n");
        g_demoGoalReached = true;
        return;
    }

    debugf("Triggering level celebration -> Level %d, Spawn %d\n", targetLevel, targetSpawn);

    // Start celebration at player's current position
    start_celebration(cubeX, cubeY, cubeZ, targetLevel, targetSpawn);
}

// Immediate level transition (now with pre-transition: wait, slide whistle, Psyops logo)
void trigger_immediate_level_transition(int targetLevel, int targetSpawn) {
    // Validate target level
    if (targetLevel < 0 || targetLevel >= LEVEL_COUNT) {
        debugf("ERROR: Invalid target level %d\n", targetLevel);
        return;
    }

    // Don't trigger if already transitioning or pre-transitioning
    if (isTransitioning || isPreTransitioning) {
        return;
    }

    debugf("Starting pre-transition -> Level %d, Spawn %d\n", targetLevel, targetSpawn);

    // Start pre-transition phase (wait, slide whistle, Psyops logo)
    isPreTransitioning = true;
    preTransitionTimer = 0.0f;
    preTransitionTargetLevel = targetLevel;
    preTransitionTargetSpawn = targetSpawn;

    // Disable player control during pre-transition
    playerState.canMove = false;
    playerState.canRotate = false;
    playerState.canJump = false;
}

// Start the actual level transition (called after pre-transition completes)
static void start_actual_level_transition(void) {
    isPreTransitioning = false;
    isTransitioning = true;
    transitionTimer = 0.0f;
    targetTransitionLevel = preTransitionTargetLevel;
    targetTransitionSpawn = preTransitionTargetSpawn;
}

// Start a script by ID (for interact triggers)
static void start_interact_script(int scriptId) {
    script_init(&activeScript);

    switch (scriptId) {
        case 1: {
            // Script 1: Generic NPC greeting
            script_add_dialogue(&activeScript, "Hello there!", "NPC", -1);
            break;
        }
        case 2: {
            // Script 2: Sign/info
            script_add_dialogue(&activeScript, "Welcome to this area!", NULL, -1);
            break;
        }
        default: {
            // Unknown script - show placeholder
            char msg[64];
            snprintf(msg, sizeof(msg), "Interact script %d not defined.", scriptId);
            script_add_dialogue(&activeScript, msg, "System", -1);
            break;
        }
    }

    script_start(&activeScript, &dialogueBox, NULL);
    scriptRunning = true;
}

void update_game_scene(void) {
    HEAP_CHECK("update_start");

    // Clear tutorial dismissal flag from previous frame
    tutorialJustDismissed = false;

    frameIdx = (frameIdx + 1) % FB_COUNT;
    float deltaTime = DELTA_TIME;  // 1/30 second at 30 FPS

    // Update audio mixer (always, even when paused)
    uint32_t audioStart = get_ticks();
    if (audio_can_write()) {
        short *buf = audio_write_begin();
        rspq_wait();  // Flush RSPQ to exit highpri mode before mixer_poll (needed when rdpq_debug is active)
        save_apply_volume_settings_safe();  // Apply pending volume changes in safe window
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }
    g_audioTicks += get_ticks() - audioStart;

    HEAP_CHECK("post_audio");

    // Update lighting lerp (always runs, even during hitstop/cutscenes)
    update_lighting(deltaTime);

    // Update party fog (based on jukebox playing state or debug toggle)
    static bool debugPartyMode = false;
    bool anyJukeboxPlaying = false;
    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* inst = &mapRuntime.decorations[i];
        if (inst->active && inst->type == DECO_JUKEBOX && inst->state.jukebox.isPlaying) {
            anyJukeboxPlaying = true;
            break;
        }
    }
    // Debug: Toggle party mode with C-down + C-up together (for testing fog without jukebox)
    joypad_buttons_t partyBtns = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t partyHeld = joypad_get_buttons_held(JOYPAD_PORT_1);
    if (partyBtns.c_down && partyHeld.c_up) {
        debugPartyMode = !debugPartyMode;
        debugf("Debug party mode: %s\n", debugPartyMode ? "ON" : "OFF");
    }
    if (anyJukeboxPlaying || debugPartyMode) {
        // Ramp up fog intensity gradually
        if (partyFogIntensity < 1.0f) {
            partyFogIntensity += deltaTime * 0.15f;
            if (partyFogIntensity > 1.0f) partyFogIntensity = 1.0f;
        }
        // Cycle fog color through hues
        partyFogHue += deltaTime * 30.0f;
        if (partyFogHue >= 360.0f) partyFogHue -= 360.0f;
    } else {
        // Fade out fog when party mode ends
        if (partyFogIntensity > 0.0f) {
            partyFogIntensity -= deltaTime * 0.5f;
            if (partyFogIntensity < 0.0f) partyFogIntensity = 0.0f;
        }
    }

    // Hitstop freeze frame - pause game logic briefly on damage
    if (hitstopTimer > 0.0f) {
        hitstopTimer -= deltaTime;
        // During hitstop, only update screen shake (to show the impact)
        update_screen_shake(deltaTime);
        return;  // Skip all other game logic
    }

    // Check for cutscene falloff trigger from decoration system
    extern bool g_cutsceneFalloffTriggered;
    extern float g_cutsceneFalloffX, g_cutsceneFalloffY, g_cutsceneFalloffZ, g_cutsceneFalloffRotY;
    if (g_cutsceneFalloffTriggered && !cutsceneFalloffPlaying) {
        debugf("=== CUTSCENE FALLOFF STARTING ===\n");
        cutsceneFalloffPlaying = true;
        cutsceneFalloffTimer = 0.0f;
        cutsceneFalloffX = g_cutsceneFalloffX;
        cutsceneFalloffY = g_cutsceneFalloffY;
        cutsceneFalloffZ = g_cutsceneFalloffZ;
        cutsceneFalloffRotY = g_cutsceneFalloffRotY;
        cutscenePlayerHidden = true;

        // Disable player controls
        playerState.canMove = false;
        playerState.canJump = false;
        playerState.canRotate = false;

        // Load cutscene model if not already loaded
        if (!cutsceneModelLoaded) {
            debugf("Loading cutscene model: rom:/Robo_cs.t3dm\n");
            rspq_wait();  // Ensure RSP is idle before loading
            cutsceneModel = t3d_model_load("rom:/Robo_cs.t3dm");
            if (cutsceneModel) {
                cutsceneSkel = t3d_skeleton_create(cutsceneModel);
                cutsceneAnim = t3d_anim_create(cutsceneModel, "cs_1");
                t3d_anim_set_looping(&cutsceneAnim, false);
                t3d_anim_attach(&cutsceneAnim, &cutsceneSkel);
                t3d_anim_set_time(&cutsceneAnim, 0.0f);
                t3d_anim_set_playing(&cutsceneAnim, true);
                cutsceneModelLoaded = true;
                debugf("Cutscene model loaded, animation cs_1: %s\n",
                       cutsceneAnim.animRef ? "OK" : "MISSING");
            } else {
                debugf("ERROR: Failed to load cutscene model!\n");
                cutsceneFalloffPlaying = false;
                cutscenePlayerHidden = false;
            }
        } else {
            // Model already loaded, just reset animation
            t3d_anim_set_time(&cutsceneAnim, 0.0f);
            t3d_anim_set_playing(&cutsceneAnim, true);
        }
        g_cutsceneFalloffTriggered = false;  // Clear the trigger
    }

    // Update cutscene falloff animation if playing
    if (cutsceneFalloffPlaying && cutsceneModelLoaded) {
        cutsceneFalloffTimer += deltaTime;
        t3d_anim_update(&cutsceneAnim, deltaTime);
        t3d_skeleton_update(&cutsceneSkel);

        // Check if animation finished
        if (!cutsceneAnim.isPlaying) {
            debugf("=== CUTSCENE FALLOFF COMPLETE ===\n");
            cutsceneFalloffPlaying = false;
            cutscenePlayerHidden = false;

            // Re-enable player controls
            playerState.canMove = true;
            playerState.canJump = true;
            playerState.canRotate = true;

            // TODO: Trigger level transition or game over here
        }

        // Skip normal player update during cutscene
        // (but still process some things like pause menu)
    }

    // Check for cutscene 2 (slideshow) trigger from decoration system
    extern bool g_cs2Triggered;
    if (g_cs2Triggered && !cs2Playing) {
        debugf("=== CUTSCENE 2 (SLIDESHOW) STARTING ===\n");
        cs2Playing = true;
        cs2CurrentFrame = 0;
        cs2FrameTimer = 0.0f;

        // Disable player controls
        playerState.canMove = false;
        playerState.canJump = false;
        playerState.canRotate = false;

        // Load first frame
        cs2_load_frame(0);

        g_cs2Triggered = false;  // Clear trigger
    }

    // Update cutscene 2 slideshow if playing (but not if in celebration CS2 phase - that's handled separately)
    if (cs2Playing && celebratePhase != CELEBRATE_CS2_SLIDESHOW) {
        cs2FrameTimer += 1.0f;  // Increment by 1 frame per update (30fps)

        // Check if current frame duration expired
        float frameDuration = (float)CS2_FRAME_DURATIONS[cs2CurrentFrame];
        if (cs2FrameTimer >= frameDuration) {
            cs2FrameTimer = 0.0f;
            cs2CurrentFrame++;

            if (cs2CurrentFrame >= CS2_FRAME_COUNT) {
                // Slideshow complete
                debugf("=== CUTSCENE 2 COMPLETE ===\n");
                cs2Playing = false;

                // Unload both sprites (double-buffered)
                if (cs2CurrentSprite) {
                    sprite_free(cs2CurrentSprite);
                    cs2CurrentSprite = NULL;
                }
                if (cs2NextSprite) {
                    sprite_free(cs2NextSprite);
                    cs2NextSprite = NULL;
                }

                // Check for controls tutorial after cutscene ends
                tutorial_check_and_show(currentLevel, (int)currentPart);

                // Re-enable player controls (unless tutorial is now showing)
                if (!tutorialActive) {
                    playerState.canMove = true;
                    playerState.canJump = true;
                    playerState.canRotate = true;
                }
            } else {
                // Advance to next frame (uses pre-loaded sprite)
                cs2_advance_frame();
            }
        }
    }

    // Check for cutscene 3 (ending slideshow) trigger from decoration system
    extern bool g_cs3Triggered;
    if (g_cs3Triggered && !cs3Playing) {
        debugf("=== CUTSCENE 3 (ENDING SLIDESHOW) STARTING ===\n");
        cs3Playing = true;
        cs3CurrentSegment = 0;
        cs3FrameTimer = 0.0f;

        // Disable player controls
        playerState.canMove = false;
        playerState.canJump = false;
        playerState.canRotate = false;

        // Load first segment
        cs3_load_frame(0);

        g_cs3Triggered = false;  // Clear trigger
    }

    // Update cutscene 3 slideshow if playing
    if (cs3Playing) {
        cs3FrameTimer += 1.0f;  // Increment by 1 frame per update (30fps)

        // Check if current segment duration expired
        float segmentDuration = (float)CS3_FRAME_DURATIONS[cs3CurrentSegment];
        if (cs3FrameTimer >= segmentDuration) {
            cs3FrameTimer = 0.0f;
            cs3CurrentSegment++;

            if (cs3CurrentSegment >= CS3_SEGMENT_COUNT) {
                // Slideshow complete - return to main menu
                debugf("=== CUTSCENE 3 COMPLETE - RETURNING TO MENU ===\n");
                cs3Playing = false;

                // Unload both sprites (double-buffered)
                if (cs3CurrentSprite) {
                    sprite_free(cs3CurrentSprite);
                    cs3CurrentSprite = NULL;
                }
                if (cs3NextSprite) {
                    sprite_free(cs3NextSprite);
                    cs3NextSprite = NULL;
                }

                // Mark level 7 as completed (same as normal level transitions)
                save_complete_level(LEVEL_7);

                // Return to main menu
                change_scene(MENU_SCENE);
            } else {
                // Advance to next segment (uses pre-loaded sprite)
                cs3_advance_frame();
            }
        }
    }

    // Check for cutscene 4 (arms explanation) trigger
    extern bool g_cs4Triggered;
    if (g_cs4Triggered && !cs4Playing) {
        debugf("=== CUTSCENE 4 (ARMS EXPLANATION) STARTING ===\n");
        cs4Playing = true;
        cs4CurrentSegment = 0;
        cs4FrameTimer = 0.0f;

        // Disable player controls
        playerState.canMove = false;
        playerState.canJump = false;
        playerState.canRotate = false;

        // Load first segment
        cs4_load_frame(0);

        g_cs4Triggered = false;  // Clear trigger
    }

    // Update cutscene 4 slideshow if playing
    if (cs4Playing) {
        cs4FrameTimer += 1.0f;  // Increment by 1 frame per update (30fps)

        float segmentDuration = (float)CS4_FRAME_DURATIONS[cs4CurrentSegment];
        if (cs4FrameTimer >= segmentDuration) {
            cs4FrameTimer = 0.0f;
            cs4CurrentSegment++;

            if (cs4CurrentSegment >= CS4_SEGMENT_COUNT) {
                // Slideshow complete
                debugf("=== CUTSCENE 4 COMPLETE ===\n");
                cs4Playing = false;

                if (cs4CurrentSprite) { sprite_free(cs4CurrentSprite); cs4CurrentSprite = NULL; }
                if (cs4NextSprite) { sprite_free(cs4NextSprite); cs4NextSprite = NULL; }

                // Re-enable player controls
                playerState.canMove = true;
                playerState.canJump = true;
                playerState.canRotate = true;

                // Check for controls tutorial after cutscene ends
                tutorial_check_and_show(currentLevel, (int)currentPart);
            } else {
                cs4_advance_frame();
            }
        }
    }

    // Update chargepad buff timers (legs mode speed/invincibility)
    if (buffSpeedTimer > 0.0f) {
        buffSpeedTimer -= deltaTime;
        if (buffSpeedTimer <= 0.0f) {
            buffSpeedTimer = 0.0f;
            buffInvincible = false;
            debugf("Speed/invincibility buff expired!\n");
        }
    }

    // Update buff flash timer for texture swap effect (~0.25 seconds per state)
    bool hasBuff = buffJumpActive || buffGlideActive || buffSpeedTimer > 0.0f;
    if (hasBuff) {
        buffFlashTimer += deltaTime;
        if (buffFlashTimer >= 0.25f) {
            buffFlashTimer = 0.0f;
            buffFlashState = !buffFlashState;
        }
    } else {
        buffFlashTimer = 0.0f;
        buffFlashState = false;
    }

    // Update celebration system (fireworks + UI overlay)
    // Need to poll joypad here for A button input during celebration
    if (celebratePhase != CELEBRATE_INACTIVE) {
        joypad_poll();
    }
    update_celebration(deltaTime);

    // Update countdown timer (pauses during pause menu, waits for iris to finish)
    if (countdown_is_active()) {
        countdown_update(deltaTime, isPaused, playerIsRespawning, irisActive);
        // Re-enable movement when countdown finishes
        if (!countdown_is_active()) {
            playerState.canMove = true;
            playerState.canJump = true;
        }
    }

    // Update health HUD animation (runs even when paused)
    if (healthHudY != healthHudTargetY) {
        float diff = healthHudTargetY - healthHudY;
        healthHudY += diff * HEALTH_HUD_SPEED * deltaTime;
        // Snap when close enough
        if (fabsf(diff) < 0.5f) {
            healthHudY = healthHudTargetY;
        }
    }
    if (healthFlashTimer > 0.0f) {
        healthFlashTimer -= deltaTime;
    }
    if (healthHudHideTimer > 0.0f) {
        healthHudHideTimer -= deltaTime;
        if (healthHudHideTimer <= 0.0f && !isPaused) {
            hide_health_display();
        }
    }

    // Update screw HUD position (slides in/out from right)
    if (screwHudX != screwHudTargetX) {
        float diff = screwHudTargetX - screwHudX;
        screwHudX += diff * SCREW_HUD_SPEED * deltaTime;
        if (fabsf(diff) < 0.5f) {
            screwHudX = screwHudTargetX;
        }
    }
    // Update screw animation (spinning effect)
    if (screwHudVisible || screwHudX < SCREW_HUD_HIDE_X - 1.0f) {
        screwAnimTimer += deltaTime;
        if (screwAnimTimer >= 1.0f / SCREW_ANIM_FPS) {
            screwAnimTimer -= 1.0f / SCREW_ANIM_FPS;
            screwAnimFrame = (screwAnimFrame + 1) % SCREW_HUD_FRAME_COUNT;
        }
    }
    // Auto-hide screw HUD after timer
    if (screwHudHideTimer > 0.0f) {
        screwHudHideTimer -= deltaTime;
        if (screwHudHideTimer <= 0.0f && !isPaused) {
            hide_screw_display();
        }
    }

    // Update golden screw HUD position (slides in/out from left)
    if (goldenHudX != goldenHudTargetX) {
        float diff = goldenHudTargetX - goldenHudX;
        goldenHudX += diff * GOLDEN_HUD_SPEED * deltaTime;
        if (fabsf(diff) < 0.5f) {
            goldenHudX = goldenHudTargetX;
        }
    }
    // Update golden screw animation (spinning effect)
    if (goldenHudVisible || goldenHudX > GOLDEN_HUD_HIDE_X + 1.0f) {
        goldenAnimTimer += deltaTime;
        if (goldenAnimTimer >= 1.0f / GOLDEN_ANIM_FPS) {
            goldenAnimTimer -= 1.0f / GOLDEN_ANIM_FPS;
            goldenAnimFrame = (goldenAnimFrame + 1) % GOLDEN_HUD_FRAME_COUNT;
        }
    }
    // Auto-hide golden screw HUD after timer
    if (goldenHudHideTimer > 0.0f) {
        goldenHudHideTimer -= deltaTime;
        if (goldenHudHideTimer <= 0.0f && !isPaused) {
            hide_golden_display();
        }
    }

    // Update reward popup timer
    if (rewardPopupActive && rewardPopupTimer > 0.0f) {
        rewardPopupTimer -= deltaTime;
        if (rewardPopupTimer <= 0.0f) {
            rewardPopupActive = false;
        }
    }

    // Handle pause menu
    if (isPaused) {
        joypad_poll();  // Need to poll since controls_update is skipped

        // Update the appropriate menu based on current state
        if (isInOptionsMenu) {
            option_update(&optionsMenu, JOYPAD_PORT_1);
            // Check if options menu was closed
            if (!option_is_active(&optionsMenu)) {
                isInOptionsMenu = false;
                show_pause_menu();  // Return to pause menu
            }
        } else {
            option_update(&pauseMenu, JOYPAD_PORT_1);
            // Check if menu was closed by callback or B button
            if (!option_is_active(&pauseMenu) && !isInOptionsMenu) {
                isPaused = false;
                hide_health_display();
                hide_screw_display();
                hide_golden_display();
                level_banner_unpause(&levelBanner);
            }
        }

        // Update level banner animation while paused
        level_banner_update(&levelBanner, DELTA_TIME);
        return;  // Skip all game updates while paused
    }

    // Handle quit-to-menu iris transition
    if (isQuittingToMenu && irisActive) {
        // Shrink iris toward center
        float shrinkSpeed = irisRadius * 0.08f;
        if (shrinkSpeed < 3.0f) shrinkSpeed = 3.0f;
        irisRadius -= shrinkSpeed;

        // Keep updating the level banner so it slides out properly
        level_banner_update(&levelBanner, DELTA_TIME);

        if (irisRadius <= 0.0f) {
            irisRadius = 0.0f;
            // Iris fully closed - transition to menu
            isQuittingToMenu = false;
            irisActive = false;
            change_scene(MENU_SCENE);
        }
        return;  // Skip game updates during quit transition
    }

    // Handle cutscene dialogue (body part tutorials)
    if (g_cutsceneActive) {
        joypad_poll();
        if (dialogue_update(&dialogueBox, JOYPAD_PORT_1)) {
            // Check if cutscene dialogue is complete
            game_check_cutscene_end();
        }
        // Keep updating particles and decorations for visual polish
        update_particles(deltaTime);
        return;  // Skip game updates while cutscene active
    }

    // Handle dialogue for interact triggers
    if (scriptRunning) {
        joypad_poll();
        if (dialogue_update(&dialogueBox, JOYPAD_PORT_1)) {
            // Dialogue consumed input
            if (!dialogue_is_active(&dialogueBox)) {
                // Dialogue just ended
                scriptRunning = false;
                if (activeInteractTrigger) {
                    // Restore player angle
                    playerState.playerAngle = activeInteractTrigger->state.interactTrigger.savedPlayerAngle;
                    // Clear interacting flag
                    activeInteractTrigger->state.interactTrigger.interacting = false;
                    // Mark as triggered if once-only
                    if (activeInteractTrigger->state.interactTrigger.onceOnly) {
                        activeInteractTrigger->state.interactTrigger.hasTriggered = true;
                    }
                    activeInteractTrigger = NULL;
                }
            }
        }
        return;  // Skip game updates while dialogue active
    }

    // Check for interact trigger A button press (before regular input)
    if (!playerIsDead && !playerIsHurt && !playerIsRespawning && !isTransitioning && !debugFlyMode) {
        for (int i = 0; i < mapRuntime.decoCount; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            if (deco->active && deco->type == DECO_INTERACTTRIGGER &&
                deco->state.interactTrigger.playerInRange && !deco->state.interactTrigger.interacting) {
                // Check if A button was pressed
                joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
                if (pressed.a) {
                    // Save player angle
                    deco->state.interactTrigger.savedPlayerAngle = playerState.playerAngle;
                    // Rotate player to look at specified angle
                    playerState.playerAngle = deco->state.interactTrigger.lookAtAngle;
                    // Start dialogue
                    deco->state.interactTrigger.interacting = true;
                    activeInteractTrigger = deco;
                    start_interact_script(deco->state.interactTrigger.scriptId);
                    ui_play_ui_open_sound();  // Play UI open sound
                    break;
                }
            }
        }
    }

    // Track play time (only when not dead/respawning and not celebrating)
    bool isCelebratingNow = (celebratePhase != CELEBRATE_INACTIVE);
    if (!playerIsDead && !playerIsRespawning && !isCelebratingNow) {
        save_add_play_time(deltaTime);
        levelTime += deltaTime;  // Track for level complete screen
    }

    // Process controls (disabled when dead, hurt, respawning, transitioning, celebrating, or countdown active)
    uint32_t inputStart = get_ticks();
    bool isCelebrating = (celebratePhase != CELEBRATE_INACTIVE);
    if (!playerIsDead && !playerIsHurt && !playerIsRespawning && !isTransitioning && !isCelebrating && !countdown_is_active()) {
        // In demo mode, use ROM-stored demo data
        if (g_demoMode && demo_is_playing()) {
            int8_t demoStickX, demoStickY;
            uint16_t demoButtons;
            if (demo_get_frame(&demoStickX, &demoStickY, &demoButtons)) {
                // Store demo inputs globally for use by rest of game logic
                g_replayButtonsPrev = g_replayButtonsHeld;
                g_replayButtonsHeld = demoButtons;
                g_replayStickX = demoStickX;
                g_replayStickY = demoStickY;

                controls_update_replay(&playerState, &controlConfig, demoStickX, demoStickY, demoButtons);
            }
            // Demo finished is handled by demo_scene.c which checks demo_is_playing()
        } else if (g_replayMode && replay_is_playing()) {
            // In replay mode, use RAM-stored recorded inputs
            int8_t replayStickX, replayStickY;
            uint16_t replayButtons;
            if (replay_get_frame(&replayStickX, &replayStickY, &replayButtons)) {
                // Store replay inputs globally for use by rest of game logic
                g_replayButtonsPrev = g_replayButtonsHeld;  // Save previous for pressed detection
                g_replayButtonsHeld = replayButtons;
                g_replayStickX = replayStickX;
                g_replayStickY = replayStickY;

                controls_update_replay(&playerState, &controlConfig, replayStickX, replayStickY, replayButtons);
            } else {
                // Replay finished - go back to menu
                replay_stop_playback();
                g_replayMode = false;
                isQuittingToMenu = true;
            }
        } else if (!tutorialActive) {
            // Normal gameplay - use real controls (skip if tutorial is showing)
            // Pre-check: disable normal jump if crouching in fullbody mode (hover jump will handle it)
            if (currentPart == PART_LEGS && fbIsCrouching) {
                disableNormalJump = true;
            }
            controls_update(&playerState, &controlConfig, JOYPAD_PORT_1);
            // Post-check: reset disableNormalJump if not in hover/wind-up state
            if (!fbCrouchJumpWindup && !fbIsHovering) {
                disableNormalJump = (currentPart == PART_TORSO || currentPart == PART_ARMS);
            }

            // Record input for replay (only in normal mode, not replay mode)
            if (replay_is_recording()) {
                joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
                joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
                // Convert joypad_buttons_t to uint16_t bitmask
                uint16_t buttonMask = 0;
                if (held.a) buttonMask |= 0x0001;
                if (held.b) buttonMask |= 0x0002;
                if (held.z) buttonMask |= 0x0004;
                if (held.start) buttonMask |= 0x0008;
                if (held.d_up) buttonMask |= 0x0010;
                if (held.d_down) buttonMask |= 0x0020;
                if (held.d_left) buttonMask |= 0x0040;
                if (held.d_right) buttonMask |= 0x0080;
                if (held.l) buttonMask |= 0x0100;
                if (held.r) buttonMask |= 0x0200;
                if (held.c_up) buttonMask |= 0x0400;
                if (held.c_down) buttonMask |= 0x0800;
                if (held.c_left) buttonMask |= 0x1000;
                if (held.c_right) buttonMask |= 0x2000;
                replay_record_frame(inputs.stick_x, inputs.stick_y, buttonMask);
            }
        }
    }
    g_inputTicks += get_ticks() - inputStart;

    // Check for debug toggle (cheat code is entered on title screen)
    // Skip toggle if menu is open to prevent conflicts
    if (!debug_menu_is_open() && psyops_check_debug_toggle(JOYPAD_PORT_1)) {
        debugFlyMode = psyops_is_debug_active();
        if (debugFlyMode) {
            // Save camera state and initialize debug camera at current view
            savedCamZ = camPos.v[2];
            debugCamX = camPos.v[0];
            debugCamY = camPos.v[1];
            debugCamZ = camPos.v[2];
            debugCamYaw = 3.14159f;  // Start facing opposite direction
            debugCamPitch = 0.0f;
            debugPlacementMode = false;
        } else {
            // Restore camera Z position when leaving debug mode
            camPos.v[2] = savedCamZ;
            debugPlacementMode = false;
        }
    }

    // Handle debug menu (shared module)
    if (debug_menu_update(JOYPAD_PORT_1, deltaTime)) {
        return;  // Debug menu consumed input
    }

    // Apply gravity when dead (controls_update is skipped, but we want player to keep falling)
    if (playerIsDead && !playerState.isGrounded) {
        playerState.velY -= GRAVITY;
        if (playerState.velY < TERMINAL_VELOCITY) {
            playerState.velY = TERMINAL_VELOCITY;
        }
    }

    // Apply gravity during respawn so player falls to ground before iris opens
    if (playerIsRespawning && !playerState.isGrounded) {
        playerState.velY -= GRAVITY;
        if (playerState.velY < TERMINAL_VELOCITY) {
            playerState.velY = TERMINAL_VELOCITY;
        }
    }

    // Toggle performance graph with C-Left + C-Right (works always, not just in debug mode)
    {
        joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
        static bool perfGraphToggleCooldown = false;
        if (held.c_left && held.c_right) {
            if (!perfGraphToggleCooldown) {
                perfGraphEnabled = !perfGraphEnabled;
                perfGraphToggleCooldown = true;
            }
        } else {
            perfGraphToggleCooldown = false;
        }
    }

    if (debugFlyMode) {
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

        float stickX = apply_deadzone(inputs.stick_x / 128.0f);
        float stickY = apply_deadzone(inputs.stick_y / 128.0f);
        float cosYaw = cosf(debugCamYaw);
        float sinYaw = sinf(debugCamYaw);

        // L/R triggers cycle decoration type
        if (debugTriggerCooldown > 0) debugTriggerCooldown--;
        if (debugTriggerCooldown == 0) {
            if (held.l && !patrolPlacementMode) {
                debugDecoType = (debugDecoType + DECO_TYPE_COUNT - 1) % DECO_TYPE_COUNT;
                map_get_deco_model(&mapRuntime, debugDecoType);
                debugTriggerCooldown = 15;
            }
            if (held.r && !patrolPlacementMode) {
                debugDecoType = (debugDecoType + 1) % DECO_TYPE_COUNT;
                map_get_deco_model(&mapRuntime, debugDecoType);
                debugTriggerCooldown = 15;
            }
        }

        if (!debugPlacementMode) {
            // === CAMERA MODE ===
            float flySpeed = 5.0f;
            float rotSpeed = 0.05f;

            if (held.c_left) debugCamYaw -= rotSpeed;
            if (held.c_right) debugCamYaw += rotSpeed;
            if (held.c_up) debugCamPitch += rotSpeed;
            if (held.c_down) debugCamPitch -= rotSpeed;

            if (debugCamPitch > 1.5f) debugCamPitch = 1.5f;
            if (debugCamPitch < -1.5f) debugCamPitch = -1.5f;

            debugCamX += (stickX * cosYaw + stickY * sinYaw) * flySpeed;
            debugCamZ += (stickX * sinYaw - stickY * cosYaw) * flySpeed;

            if (held.a) debugCamY += flySpeed;
            if (held.z) debugCamY -= flySpeed;

            // === RAYCAST SELECTION FOR DELETION ===
            // Calculate camera look direction from yaw and pitch
            float cosPitch = cosf(debugCamPitch);
            float sinPitch = sinf(debugCamPitch);
            float lookDirX = sinYaw * cosPitch;
            float lookDirY = sinPitch;
            float lookDirZ = -cosYaw * cosPitch;

            // Find decoration closest to camera look ray
            float bestScore = 999999.0f;
            int bestIndex = -1;
            float maxSelectDist = 500.0f;  // Max distance to select
            float selectRadius = 30.0f;     // Lenient selection radius

            for (int i = 0; i < mapRuntime.decoCount; i++) {
                DecoInstance* deco = &mapRuntime.decorations[i];
                if (!deco->active || deco->type == DECO_NONE) continue;

                // Vector from camera to decoration
                float toCamX = deco->posX - debugCamX;
                float toCamY = deco->posY - debugCamY;
                float toCamZ = deco->posZ - debugCamZ;

                // Distance along look direction (dot product)
                float alongRay = toCamX * lookDirX + toCamY * lookDirY + toCamZ * lookDirZ;

                // Skip if behind camera or too far
                if (alongRay < 10.0f || alongRay > maxSelectDist) continue;

                // Perpendicular distance from ray (cross product magnitude)
                float perpX = toCamY * lookDirZ - toCamZ * lookDirY;
                float perpY = toCamZ * lookDirX - toCamX * lookDirZ;
                float perpZ = toCamX * lookDirY - toCamY * lookDirX;
                float perpDist = sqrtf(perpX * perpX + perpY * perpY + perpZ * perpZ);

                // Score based on perpendicular distance (lower is better)
                // Also factor in distance to prefer closer objects
                if (perpDist < selectRadius) {
                    float score = perpDist + alongRay * 0.1f;  // Slight preference for closer
                    if (score < bestScore) {
                        bestScore = score;
                        bestIndex = i;
                    }
                }
            }

            debugHighlightedDecoIndex = bestIndex;

            // Update delete cooldown
            if (debugDeleteCooldown > 0.0f) {
                debugDeleteCooldown -= DELTA_TIME;
            }

            // D-pad right deletes highlighted decoration
            if (pressed.d_right && debugHighlightedDecoIndex >= 0 && debugDeleteCooldown <= 0.0f) {
                DecoInstance* deco = &mapRuntime.decorations[debugHighlightedDecoIndex];
                debugf("Deleted decoration: type=%d at (%.1f, %.1f, %.1f)\n",
                    deco->type, deco->posX, deco->posY, deco->posZ);
                map_remove_decoration(&mapRuntime, debugHighlightedDecoIndex);
                debugHighlightedDecoIndex = -1;
                debugDeleteCooldown = 0.3f;  // 300ms cooldown
            }

            // D-pad left toggles collision debug visualization
            if (pressed.d_left) {
                debugShowCollision = !debugShowCollision;
                debugf("Collision debug: %s\n", debugShowCollision ? "ON" : "OFF");
            }

            // B enters placement mode
            if (pressed.b) {
                debugPlacementMode = true;
                debugDecoX = debugCamX + sinYaw * 50.0f;
                debugDecoY = debugCamY - 20.0f;
                debugDecoZ = debugCamZ - cosYaw * 50.0f;
                debugDecoRotY = 0.0f;
                debugDecoScaleX = 1.0f;
                debugDecoScaleY = 1.0f;
                debugDecoScaleZ = 1.0f;
            }
        } else if (debugPlacementMode) {
            // === PLACEMENT MODE ===
            float moveSpeed = 2.0f;
            float rotSpeed = 0.05f;
            float scaleSpeed = 0.02f;

            if (held.z) {
                // Z held: scale mode
                debugDecoScaleX += stickX * scaleSpeed;
                debugDecoScaleZ += stickY * scaleSpeed;
                if (held.c_up) debugDecoScaleY += scaleSpeed;
                if (held.c_down) debugDecoScaleY -= scaleSpeed;

                if (debugDecoScaleX < 0.1f) debugDecoScaleX = 0.1f;
                if (debugDecoScaleY < 0.1f) debugDecoScaleY = 0.1f;
                if (debugDecoScaleZ < 0.1f) debugDecoScaleZ = 0.1f;
            } else {
                // Normal: move and rotate
                debugDecoX += sinYaw * stickY * moveSpeed;
                debugDecoZ -= cosYaw * stickY * moveSpeed;
                debugDecoX += cosYaw * stickX * moveSpeed;
                debugDecoZ += sinYaw * stickX * moveSpeed;

                if (held.c_up) debugDecoY += moveSpeed;
                if (held.c_down) debugDecoY -= moveSpeed;
                if (held.c_left) debugDecoRotY -= rotSpeed;
                if (held.c_right) debugDecoRotY += rotSpeed;
            }

            // A places the decoration
            if (pressed.a) {
                if(debugDecoType == DECO_RAT){
                    // Enter patrol placement mode for rats
                    patrolPlacementMode = true;
                    patrolDecoHolder = debugDecoType;
                    debugDecoType = DECO_PATROLPOINT;

                    // Initialize patrol points array with the rat's initial position
                    patrolPointCount = 1;
                    patrolPoints = malloc(sizeof(T3DVec3) * patrolPointCount);
                    patrolPoints[0].v[0] = debugDecoX;
                    patrolPoints[0].v[1] = debugDecoY;
                    patrolPoints[0].v[2] = debugDecoZ;

                    // Place visual marker for first patrol point
                    map_add_decoration(&mapRuntime, DECO_PATROLPOINT,
                        debugDecoX, debugDecoY, debugDecoZ, debugDecoRotY,
                        debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ);

                    debugf("Entering patrol placement mode\n");
                    debugf("Press A to place patrol points, Z to undo, B to finish\n");
                    return;
                }
                int idx = map_add_decoration(&mapRuntime, debugDecoType,
                    debugDecoX, debugDecoY, debugDecoZ, debugDecoRotY,
                    debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ);
                if (idx >= 0) {
                    map_print_all_decorations(&mapRuntime);
                }
            }

            // B returns to camera mode
            if (pressed.b) {
                debugPlacementMode = false;
            }
        } else if(patrolPlacementMode){ //=== PATROL POINT PLACEMENT MODE ===
            // Move patrol point preview
            float moveSpeed = 2.0f;
            if (held.c_up) debugDecoY += moveSpeed;
            if (held.c_down) debugDecoY -= moveSpeed;
            // Note: Use stick for XZ movement (handled in debugFlyMode camera section)

            // B finalizes and exits patrol placement mode
            if (pressed.b) {
                debugf("Finalizing patrol route - %d patrol points\n", patrolPointCount);

                // Place decoration with patrol route using map_add_decoration_patrol
                int idx = map_add_decoration_patrol(&mapRuntime, patrolDecoHolder,
                    patrolPoints[0].v[0], patrolPoints[0].v[1], patrolPoints[0].v[2],
                    debugDecoRotY, debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ,
                    patrolPoints, patrolPointCount);

                if (idx >= 0) {
                    debugf("Placed %s with %d patrol points\n", DECO_TYPES[patrolDecoHolder].name, patrolPointCount);
                    map_print_all_decorations(&mapRuntime);
                }

                // Clean up temporary patrol data
                free(patrolPoints);
                patrolPoints = NULL;
                patrolPointCount = 0;

                // Restore decoration type and exit patrol mode
                debugDecoType = patrolDecoHolder;
                patrolPlacementMode = false;
                debugPlacementMode = true;
            }

            // Z removes last patrol point (or cancels if removing first point)
            if (pressed.z) {
                if (patrolPointCount > 1) {
                    // Remove last patrol point
                    patrolPointCount--;
                    T3DVec3* newPoints = realloc(patrolPoints, sizeof(T3DVec3) * patrolPointCount);
                    if (newPoints) patrolPoints = newPoints;  // Safe realloc - keep old pointer on failure

                    // Remove last patrol point marker from map
                    for (int i = mapRuntime.decoCount - 1; i >= 0; i--) {
                        if (mapRuntime.decorations[i].type == DECO_PATROLPOINT && mapRuntime.decorations[i].active) {
                            map_remove_decoration(&mapRuntime, i);
                            debugf("Removed patrol point %d\n", patrolPointCount + 1);
                            break;
                        }
                    }
                } else {
                    debugf("Cancelling patrol placement (no points)\n");

                    // Clean up patrol data
                    free(patrolPoints);
                    patrolPoints = NULL;
                    patrolPointCount = 0;

                    // Remove first patrol point marker from map
                    for (int i = mapRuntime.decoCount - 1; i >= 0; i--) {
                        if (mapRuntime.decorations[i].type == DECO_PATROLPOINT && mapRuntime.decorations[i].active) {
                            map_remove_decoration(&mapRuntime, i);
                            break;
                        }
                    }

                    // Restore decoration type and exit patrol mode
                    debugDecoType = patrolDecoHolder;
                    patrolPlacementMode = false;
                    debugPlacementMode = true;
                }
            }

            // A places a patrol point
            if (pressed.a) {
                T3DVec3* newPoints = realloc(patrolPoints, sizeof(T3DVec3) * (patrolPointCount + 1));
                if (!newPoints) {
                    debugf("ERROR: Failed to allocate patrol point memory\n");
                } else {
                    patrolPoints = newPoints;
                }
                patrolPoints[patrolPointCount].v[0] = debugDecoX;
                patrolPoints[patrolPointCount].v[1] = debugDecoY;
                patrolPoints[patrolPointCount].v[2] = debugDecoZ;
                patrolPointCount++;

                // Add visual marker for patrol point
                map_add_decoration(&mapRuntime, DECO_PATROLPOINT,
                    debugDecoX, debugDecoY, debugDecoZ, 0.0f,
                    0.1f, 0.1f, 0.1f);
                debugf("Placed patrol point %d at (%.1f, %.1f, %.1f)\n", patrolPointCount,
                    debugDecoX, debugDecoY, debugDecoZ);
            }
        }
    } else {
        // =================================================================
        // FRAME PACING: Store previous frame position for render interpolation
        // =================================================================
        prevCubeX = cubeX;
        prevCubeY = cubeY;
        prevCubeZ = cubeZ;

        // =================================================================
        // QUARTER STEPS (prevents tunneling through walls)
        // =================================================================
        // Instead of moving full velocity then checking collision,
        // we move in 4 substeps, checking walls after each one.
        // This prevents high-speed charge jumps from going through walls.
        // =================================================================
        #define NUM_SUBSTEPS 4

        // Reset wall hit flag (but preserve normal if timer is active!)
        playerState.hitWall = false;

        // Decrement wall hit timer - only reset normal when timer expires
        if (playerState.wallHitTimer > 0) {
            playerState.wallHitTimer--;
            // Keep wallNormalX/Z for wall kick!
        } else {
            // Timer expired, clear the normal
            playerState.wallNormalX = 0.0f;
            playerState.wallNormalZ = 0.0f;
        }

        bool isCelebratingPhysics = (celebratePhase != CELEBRATE_INACTIVE);
        if (!isCharging && !playerIsDead && !playerIsHurt && !playerIsRespawning && !isTransitioning && !isCelebratingPhysics) {
            // Only do substep collision if map is loaded (check segment count)
            bool mapReady = (mapLoader.count > 0);

            if (mapReady) {
                float stepVelX = playerState.velX / NUM_SUBSTEPS;
                float stepVelZ = playerState.velZ / NUM_SUBSTEPS;

                for (int step = 0; step < NUM_SUBSTEPS; step++) {
                    // Move one substep
                    float nextX = cubeX + stepVelX;
                    float nextZ = cubeZ + stepVelZ;

                    // Check wall collision at new position
                    float pushX = 0.0f, pushZ = 0.0f;
                    bool hitWall = maploader_check_walls_ex(&mapLoader, nextX, cubeY, nextZ,
                        playerRadius, playerHeight, &pushX, &pushZ, bestGroundY);

                    // Also check decoration walls
                    float decoPushX = 0.0f, decoPushZ = 0.0f;
                    bool hitDecoWall = map_check_deco_walls(&mapRuntime, nextX, cubeY, nextZ,
                        playerRadius, playerHeight, &decoPushX, &decoPushZ);

                    if (hitWall || hitDecoWall) {
                        // We hit a wall - apply push and record wall hit
                        nextX += pushX + decoPushX;
                        nextZ += pushZ + decoPushZ;

                        // Calculate wall normal from push direction (normalized)
                        float totalPushX = pushX + decoPushX;
                        float totalPushZ = pushZ + decoPushZ;
                        float pushLen = sqrtf(totalPushX * totalPushX + totalPushZ * totalPushZ);
                        if (pushLen > 0.01f) {
                            playerState.hitWall = true;
                            playerState.wallNormalX = totalPushX / pushLen;
                            playerState.wallNormalZ = totalPushZ / pushLen;
                            playerState.wallHitTimer = 8;  // 8 frame window for wall kick (increased from 5 for leniency)

                            // Kill velocity into wall (but keep tangent velocity)
                            // dot = how much velocity goes into wall
                            float dot = playerState.velX * playerState.wallNormalX +
                                       playerState.velZ * playerState.wallNormalZ;
                            if (dot < 0) {  // Only if moving into wall
                                playerState.velX -= dot * playerState.wallNormalX;
                                playerState.velZ -= dot * playerState.wallNormalZ;
                                // Update substep velocity for remaining steps
                                stepVelX = playerState.velX / NUM_SUBSTEPS;
                                stepVelZ = playerState.velZ / NUM_SUBSTEPS;
                            }
                        }
                    }

                    cubeX = nextX;
                    cubeZ = nextZ;
                }
            } else {
                // Map not ready - just move directly without collision
                cubeX += playerState.velX;
                cubeZ += playerState.velZ;
            }
        }

        // Y movement (no substeps needed - walls are vertical)
        // Allow dead players to keep falling (skip ceiling collision but update position)
        // Allow respawning players to fall so they land before iris opens
        if (!isTransitioning && !isCelebratingPhysics) {
            if (!playerIsDead) {
                // Ceiling collision - check BEFORE moving to prevent tunneling
                if (playerState.velY > 0) {
                    // Cast ray from player's head position upward
                    float headY = cubeY + PLAYER_HEIGHT;
                    float ceilingY = maploader_get_ceiling_height(&mapLoader, cubeX, headY, cubeZ);

                    if (ceilingY < INVALID_CEILING_Y - 10.0f) {
                        // Found a ceiling - check if moving would hit it
                        float newHeadY = headY + playerState.velY;
                        if (newHeadY >= ceilingY) {
                            // Bonk! Clamp position to just below ceiling
                            cubeY = ceilingY - PLAYER_HEIGHT - 0.5f;  // Small buffer
                            playerState.velY = 0;
                        } else {
                            cubeY += playerState.velY;
                        }
                    } else {
                        cubeY += playerState.velY;
                    }
                } else {
                    cubeY += playerState.velY;
                }
            } else {
                // Dead - just update Y position (no ceiling check, keep falling)
                cubeY += playerState.velY;
            }
        }
    }

    // =================================================================
    // PLATFORM DISPLACEMENT
    // =================================================================
    // Check ALL platforms in one call, apply displacement BEFORE collision
    // This is the clean way - platforms provide velocity, physics runs after
    // Skip during transitions to avoid accessing freed/invalid map data
    // =================================================================
    PlatformResult platformResult = {0};
    if (!isTransitioning) {
        platformResult = platform_get_displacement(&mapRuntime, cubeX, cubeY, cubeZ);
    }

    bool isCelebratingPlatform = (celebratePhase != CELEBRATE_INACTIVE);
    if (platformResult.onPlatform && !playerIsDead && !playerIsRespawning && !isTransitioning && !isCelebratingPlatform) {
        cubeX += platformResult.deltaX;
        cubeY += platformResult.deltaY;
        cubeZ += platformResult.deltaZ;
    }

    // Apply cog wall collision push (always, regardless of standing on it)
    if (platformResult.hitWall && !playerIsDead && !playerIsRespawning && !isTransitioning && !isCelebratingPlatform) {
        cubeX += platformResult.wallPushX;
        cubeZ += platformResult.wallPushZ;
    }

    // Update map visibility based on player XZ position
    float checkX = debugFlyMode ? debugCamX : cubeX;
    float checkZ = debugFlyMode ? debugCamZ : cubeZ;
    maploader_update_visibility(&mapLoader, checkX, checkZ);

    // Collision (skip in fly mode) - wall collision now handled in substeps above
    uint32_t perfCollisionStart = get_ticks();
    uint32_t perfWallTime = 0, perfDecoWallTime = 0, perfGroundTime = 0, perfDecoGroundTime = 0;

    if (!debugFlyMode) {
        // Wall collision already handled in substeps above for normal movement
        // This section only needed for sliding or other special movement

        // =================================================================
        // PLATFORM GROUND OVERRIDE
        // =================================================================
        // If player is on a platform that overrides ground physics (like cog),
        // set grounded state and skip normal ground collision
        // =================================================================
        if (platformResult.overrideGroundPhysics) {
            playerState.isGrounded = true;
            playerState.velY = 0.0f;
            playerState.isSliding = false;
            playerState.isOnSlope = false;
            playerState.groundedFrames++;
        }

        // =================================================================
        // GROUND COLLISION
        // =================================================================
        // "Step up" logic like classic 3D platformers:
        // 1. Search for ground from above (finds slopes above us)
        // 2. If ground is within step-up range, snap to it
        // 3. If ground is below, apply gravity
        // =================================================================

        uint32_t groundStart = get_ticks();

        // Skip normal ground collision if platform handles it
        if (platformResult.overrideGroundPhysics) goto skip_ground_collision;

        // Skip ground collision when dead - player should keep falling
        if (playerIsDead) goto skip_ground_collision;

        // Skip ground collision during wind-up only - hover needs ground detection for landing
        if (fbCrouchJumpWindup) goto skip_ground_collision;

        // How high the player can "step up" onto surfaces (like walking up stairs)
        const float MAX_STEP_UP = 15.0f;
        // How far down to check for ground when airborne
        (void)MAX_STEP_UP; // May be unused in some code paths

        float groundNX = 0.0f, groundNY = 1.0f, groundNZ = 0.0f;

        // Search for ground from well above the player
        // This finds slopes/surfaces that are above current position
        float searchFromY = cubeY + MAX_STEP_UP;
        bestGroundY = maploader_get_ground_height_normal(&mapLoader, cubeX, searchFromY, cubeZ,
            &groundNX, &groundNY, &groundNZ);

        // Also check decoration ground height (search from same height)
        float decoGroundY = map_get_deco_ground_height(&mapRuntime, cubeX, searchFromY, cubeZ);
        if (decoGroundY > bestGroundY) {
            bestGroundY = decoGroundY;
            groundNX = 0.0f; groundNY = 1.0f; groundNZ = 0.0f;
        }

        // NOTE: level3 water level collision disabled - player should fall through poison
        // if (level3WaterLevelCollision) { ... }

        perfGroundTime = get_ticks() - groundStart;
        perfDecoGroundTime = 0;

        // Store slope info for other systems (shadow, facing direction, etc)
        // Normals are already in world space from collision system
        playerState.slopeNormalX = groundNX;
        playerState.slopeNormalY = groundNY;
        playerState.slopeNormalZ = groundNZ;

        // Slope classification (continuous physics, simple categories)
        // SLOPE_FLAT: Can walk freely (normal.y >= 0.866, i.e. < 30°)
        // SLOPE_STEEP: Too steep to walk up, forces sliding (normal.y < 0.866)
        // SLOPE_WALL: Too steep to stand on at all (normal.y < 0.5)
        SlopeType slopeType;
        if (groundNY < SLOPE_WALL_THRESHOLD) {
            slopeType = SLOPE_WALL;
        } else if (groundNY < SLOPE_STEEP_THRESHOLD) {
            slopeType = SLOPE_STEEP;
        } else {
            slopeType = SLOPE_FLAT;
        }
        playerState.slopeType = slopeType;

        // Track if we were grounded last frame for peak height tracking
        bool wasGroundedLastFrame = playerState.isGrounded;

        // Reset grounded state each frame (will be set below if on ground)
        // NOTE: isSliding is NOT reset here - it persists until friction stops us
        // This allows sliding to continue across different slope types
        playerState.isGrounded = false;

        // Track peak height while airborne (for squash effect on landing)
        if (wasGroundedLastFrame) {
            // Just left the ground - reset peak to current position
            jumpPeakY = cubeY;
            // Start coyote timer (only if we walked off, not if we jumped)
            if (!isJumping && !isCharging) {
                coyoteTimer = COYOTE_TIME;
            }
        } else {
            // In the air - update peak Y if we're higher
            if (cubeY > jumpPeakY) {
                jumpPeakY = cubeY;
            }
            // Count down coyote timer
            if (coyoteTimer > 0.0f) {
                coyoteTimer -= deltaTime;
            }
        }

        // Check if we found valid ground
        bool hasGround = bestGroundY > INVALID_GROUND_Y + 10.0f;


        if (hasGround) {
            // Ground surface (where player's feet would be)
            float groundSurface = bestGroundY + 2.0f;
            // How far is ground from player?
            float groundDist = cubeY - groundSurface;

            // Can we step onto this surface?
            // groundDist < 0 means ground is above us (stepping up onto platform)
            // groundDist > 0 means ground is below us (falling toward ground)
            bool canStepUp = groundDist >= -MAX_STEP_UP;  // Not too high above
            // Only snap to ground if:
            // 1. Ground is above us (stepping up) - always allow
            // 2. OR we're falling/stationary and within snap distance (not being pushed up by fan/etc)
            bool isSteppingUp = groundDist < 0.0f;
            bool isFallingNearGround = groundDist <= 5.0f && playerState.velY <= 1.0f;
            bool isNearGround = isSteppingUp || isFallingNearGround;

            // Calculate downhill direction from surface normal.
            // NOTE: groundNX/NZ are already in world space (collision calculates normals
            // from rotated vertices), so no additional rotation is needed.
            // Downhill = negative XZ of the normal (points where gravity would pull)
            float steepness = sqrtf(groundNX * groundNX + groundNZ * groundNZ);
            float downhillX = 0.0f, downhillZ = 0.0f;
            if (steepness > 0.001f) {
                // Downhill is the SAME direction as where the normal points in XZ
                // (because the normal points outward from the slope surface,
                // which is the direction gravity would push you)
                downhillX = groundNX / steepness;
                downhillZ = groundNZ / steepness;
            }

            // Can we stand on this surface at all?
            // Skip ground snap during hover ascent - let the player rise
            bool skipGroundSnap = (fbIsHovering && fbCrouchJumpRising);
            if (canStepUp && isNearGround && !skipGroundSnap) {
                // Snap to ground
                cubeY = groundSurface;
                playerState.velY = 0.0f;
                playerState.isGrounded = true;
                playerState.groundedFrames++;
                coyoteTimer = 0.0f;  // Reset coyote timer on landing

                // =========================================================
                // LEDGE MAGNETISM - nudge player onto platform if near edge
                // Check 4 cardinal directions for ground; nudge away from edges
                // =========================================================
                #define LEDGE_CHECK_DIST 8.0f    // Distance to check for edge
                #define LEDGE_NUDGE_STRENGTH 1.5f  // How much to nudge per frame
                float nudgeX = 0.0f, nudgeZ = 0.0f;
                float checkY = cubeY + 10.0f;  // Search from slightly above ground

                // Check +X direction
                float groundPX = maploader_get_ground_height(&mapLoader, cubeX + LEDGE_CHECK_DIST, checkY, cubeZ);
                if (groundPX < cubeY - 50.0f) nudgeX -= LEDGE_NUDGE_STRENGTH;  // No ground to +X, nudge -X

                // Check -X direction
                float groundNX2 = maploader_get_ground_height(&mapLoader, cubeX - LEDGE_CHECK_DIST, checkY, cubeZ);
                if (groundNX2 < cubeY - 50.0f) nudgeX += LEDGE_NUDGE_STRENGTH;  // No ground to -X, nudge +X

                // Check +Z direction
                float groundPZ = maploader_get_ground_height(&mapLoader, cubeX, checkY, cubeZ + LEDGE_CHECK_DIST);
                if (groundPZ < cubeY - 50.0f) nudgeZ -= LEDGE_NUDGE_STRENGTH;  // No ground to +Z, nudge -Z

                // Check -Z direction
                float groundNZ2 = maploader_get_ground_height(&mapLoader, cubeX, checkY, cubeZ - LEDGE_CHECK_DIST);
                if (groundNZ2 < cubeY - 50.0f) nudgeZ += LEDGE_NUDGE_STRENGTH;  // No ground to -Z, nudge +Z

                // Apply nudge if any edge detected
                if (nudgeX != 0.0f || nudgeZ != 0.0f) {
                    cubeX += nudgeX;
                    cubeZ += nudgeZ;
                }

                // Reset jumps after being grounded for 5 frames (failsafe)
                if (playerState.groundedFrames >= 5) {
                    playerState.currentJumps = 0;
                }

                // =========================================================
                // SLOPE PHYSICS
                // Simple rules:
                // - On steep slopes (normal.y < 0.866): immediately slide
                // - Sliding continues until friction stops you on flat ground
                // - No complex timers or phases - just physics
                // =========================================================

                // Track slope state
                playerState.isOnSlope = (slopeType != SLOPE_FLAT);

                // Determine if we should be sliding
                bool shouldSlide = false;

                if (playerState.isSliding) {
                    // Already sliding - continue until stopped by friction
                    shouldSlide = true;
                    playerState.steepSlopeTimer = 0.0f;
                } else if (isSlideRecovering) {
                    // Don't start sliding while recovering from a slide
                    playerState.steepSlopeTimer = 0.0f;
                } else if (slopeType == SLOPE_STEEP) {
                    // Steep slope - struggle-then-slide behavior
                    // Player can briefly try to walk up, but speed lerps to zero
                    playerState.steepSlopeTimer += DELTA_TIME;

                    // Struggle phase: lerp walking speed toward zero
                    // Takes about 0.3 seconds to fully stop
                    float struggleTime = 0.3f;
                    float t = playerState.steepSlopeTimer / struggleTime;
                    if (t > 1.0f) t = 1.0f;

                    // Reduce walking speed based on time on slope
                    float speedMult = 1.0f - t;  // 1.0 -> 0.0 over struggleTime
                    playerState.velX *= speedMult;
                    playerState.velZ *= speedMult;

                    // After struggle time, start sliding
                    if (playerState.steepSlopeTimer >= struggleTime) {
                        shouldSlide = true;

                        // Cancel any active charge jump
                        if (isCharging) {
                            isCharging = false;
                            jumpChargeTime = 0.0f;
                            landingSquash = 0.0f;
                            chargeSquash = 0.0f;
                            squashScale = 1.0f;
                            squashVelocity = 0.0f;
                        }

                        // Initialize slide velocity in downhill direction
                        float initSpeed = 15.0f;
                        playerState.slideVelX = downhillX * initSpeed;
                        playerState.slideVelZ = downhillZ * initSpeed;
                    }
                } else {
                    // Not on steep slope - reset timer
                    playerState.steepSlopeTimer = 0.0f;
                }

                if (shouldSlide) {
                    playerState.isSliding = true;

                    // Prevent uphill sliding - if moving uphill on a slope, redirect downhill
                    float slideSpeed = sqrtf(playerState.slideVelX * playerState.slideVelX +
                                             playerState.slideVelZ * playerState.slideVelZ);
                    float downhillDot = playerState.slideVelX * downhillX + playerState.slideVelZ * downhillZ;

                    if (steepness > 0.05f && downhillDot < 0 && slideSpeed < 20.0f) {
                        // Moving uphill with low speed - redirect downhill
                        float minSpeed = 10.0f + steepness * 30.0f;
                        playerState.slideVelX = downhillX * minSpeed;
                        playerState.slideVelZ = downhillZ * minSpeed;
                    }

                    // Get player input for slide steering
                    joypad_inputs_t joypad = joypad_get_inputs(JOYPAD_PORT_1);
                    float inputX = apply_deadzone(joypad.stick_x / 128.0f);
                    float inputZ = -apply_deadzone(joypad.stick_y / 128.0f);

                    // Slope-based sliding physics
                    bool stopped = slope_update_sliding(&playerState, inputX, inputZ, downhillX, downhillZ, steepness);

                    if (stopped) {
                        // Friction brought us to a stop on flat ground
                        playerState.isSliding = false;
                        playerState.isOnSlope = false;
                    } else {
                        // Spawn dust particles while sliding fast
                        slideSpeed = sqrtf(playerState.slideVelX * playerState.slideVelX +
                                           playerState.slideVelZ * playerState.slideVelZ);
                        if (slideSpeed > 5.0f && (rand() % 3) == 0) {
                            float behindX = cubeX - (playerState.slideVelX / slideSpeed) * 8.0f;
                            float behindZ = cubeZ - (playerState.slideVelZ / slideSpeed) * 8.0f;
                            spawn_dust_particles(behindX, cubeY, behindZ, 1);
                        }

                        // Apply slide velocity
                        float newX = cubeX + playerState.slideVelX * DELTA_TIME;
                        float newZ = cubeZ + playerState.slideVelZ * DELTA_TIME;

                        // Stick to ground at new position (but only if within step range)
                        float newGroundNX, newGroundNY, newGroundNZ;
                        float newGroundY = maploader_get_ground_height_normal(&mapLoader,
                            newX, cubeY + 50.0f, newZ, &newGroundNX, &newGroundNY, &newGroundNZ);

                        if (newGroundY > INVALID_GROUND_Y + 10.0f) {
                            float groundDrop = cubeY - (newGroundY + 2.0f);
                            // Only snap to ground if within reasonable step-down distance
                            // If ground is too far below, player should fall naturally
                            if (groundDrop < 10.0f) {
                                cubeX = newX;
                                cubeZ = newZ;
                                cubeY = newGroundY + 2.0f;
                                playerState.velY = 0.0f;
                            } else {
                                // Ground too far below - go airborne
                                cubeX = newX;
                                cubeZ = newZ;
                                // Don't snap Y - let gravity handle it
                                // isSliding stays true so horizontal momentum continues
                            }
                        }
                    }

                    // Clear walking velocity while sliding
                    playerState.velX = 0.0f;
                    playerState.velZ = 0.0f;
                }
            }
            // Ground exists but too far to step - check if falling onto it
            else if (groundDist > 0 && playerState.velY < 0) {
                // We're above ground and falling - will we hit it this frame?
                float nextY = cubeY + playerState.velY;
                if (nextY <= groundSurface) {
                    // Capture falling velocity BEFORE zeroing it (for momentum transfer)
                    float landingVelY = playerState.velY;  // Negative value (falling down)

                    // Land on it
                    cubeY = groundSurface;
                    playerState.velY = 0.0f;
                    playerState.isGrounded = true;
                    playerState.groundedFrames++;

                    // Transfer landing momentum into slide on steep slopes
                    // Don't start sliding if we're recovering from a previous slide
                    if (slopeType == SLOPE_STEEP && !isSlideRecovering) {
                        float fallSpeed = -landingVelY;  // Make positive
                        float slopeFactor = 1.0f - groundNY;

                        // Transfer fall momentum to slide
                        float momentumSpeed = fallSpeed * (0.5f + slopeFactor) * 2.0f;
                        if (momentumSpeed < 15.0f) momentumSpeed = 15.0f;

                        // Add horizontal momentum if moving downhill
                        float horizDot = playerState.velX * downhillX + playerState.velZ * downhillZ;
                        if (horizDot > 0) {
                            momentumSpeed += horizDot * 0.5f;
                        }

                        playerState.slideVelX = downhillX * momentumSpeed;
                        playerState.slideVelZ = downhillZ * momentumSpeed;
                        playerState.isSliding = true;
                        playerState.velX = 0.0f;
                        playerState.velZ = 0.0f;
                    }
                }
            }
            // Already sliding in air - maintain momentum
            else if (playerState.isSliding) {
                cubeX += playerState.slideVelX * DELTA_TIME;
                cubeZ += playerState.slideVelZ * DELTA_TIME;
            }
        }

        // Fallback ground (safety net) - skip when dead so player keeps falling
        if (!playerIsDead && !playerState.isGrounded && cubeY <= groundLevel + 2.0f) {
            cubeY = groundLevel + 2.0f;
            playerState.velY = 0.0f;
            playerState.isGrounded = true;
            // Reset slide on fallback floor
            if (playerState.isSliding) {
                playerState.isSliding = false;
            }
            playerState.groundedFrames++;
        }

        // Reset grounded frame counter when airborne
        if (!playerState.isGrounded) {
            playerState.groundedFrames = 0;
        }

        skip_ground_collision:;  // Label for cog physics to skip ground handling
    }
    uint32_t perfCollisionTime = get_ticks() - perfCollisionStart;

    // Fall death - player dies if they fall below Y=-500 (same as enemy despawn)
    if (!playerIsDead && !playerIsRespawning && cubeY < -500.0f) {
        singleplayer_take_damage(999);  // Instant death
    }

    // Camera
    uint32_t cameraStart = get_ticks();
    if (debugFlyMode) {
        camPos.v[0] = debugCamX;
        camPos.v[1] = debugCamY;
        camPos.v[2] = debugCamZ;
        float cosPitch = cosf(debugCamPitch);
        camTarget.v[0] = debugCamX + sinf(debugCamYaw) * cosPitch * 100.0f;
        camTarget.v[1] = debugCamY + sinf(debugCamPitch) * 100.0f;
        camTarget.v[2] = debugCamZ - cosf(debugCamYaw) * cosPitch * 100.0f;
    } else {
        // Camera follows player with fixed offsets
        // Y offset: camera above player, Z offset: camera behind player (based on zoom level)
        const float CAM_Y_OFFSET = 49.0f;

        // Determine target zoom based on current zoom level
        float targetZoom;
        switch (cameraZoomLevel) {
            case 0:  targetZoom = CAM_ZOOM_CLOSE;  break;
            case 2:  targetZoom = CAM_ZOOM_FAR;    break;
            default: targetZoom = CAM_ZOOM_NORMAL; break;
        }
        // Smoothly lerp toward target zoom
        smoothCamZoom += (targetZoom - smoothCamZoom) * CAM_ZOOM_LERP_SPEED;

        const float CAM_CHARGE_PULLBACK = -30.0f;  // Extra distance when fully charged

        // Death camera zoom - slowly zoom in on player during death animation
        static float deathCameraZoom = 0.0f;
        const float DEATH_ZOOM_AMOUNT = 60.0f;  // How much closer camera gets at full zoom
        const float DEATH_ZOOM_SPEED = 0.8f;    // Speed of zoom in (0-1 per second)
        if (playerIsDead && !playerIsRespawning) {
            // Gradually zoom in during death
            if (deathCameraZoom < 1.0f) {
                deathCameraZoom += DEATH_ZOOM_SPEED * deltaTime;
                if (deathCameraZoom > 1.0f) deathCameraZoom = 1.0f;
            }
        } else {
            // Reset zoom when not dead
            deathCameraZoom = 0.0f;
        }

        // Camera lead - offset target based on velocity to see ahead of movement
        const float CAM_LEAD_AMOUNT = 3.0f;  // Multiplier for velocity lead
        float leadX = playerState.velX * CAM_LEAD_AMOUNT;
        float leadZ = playerState.velZ * CAM_LEAD_AMOUNT;

        float targetX = cubeX + leadX;
        float targetY = cubeY + CAM_Y_OFFSET;

        // Pull camera back when charging jump (only for torso)
        float chargePullback = 0.0f;
        static float smoothChargePullback = 0.0f;
        if (isCharging && jumpChargeTime >= hopThreshold && currentPart == PART_TORSO) {
            float chargeRatio = (jumpChargeTime - hopThreshold) / (maxChargeTime - hopThreshold);
            if (chargeRatio > 1.0f) chargeRatio = 1.0f;
            chargePullback = chargeRatio * CAM_CHARGE_PULLBACK;
        }
        smoothChargePullback += (chargePullback - smoothChargePullback) * 0.1f;

        // Smooth camera follow - lag lets you see ahead
        float smoothFactorX = 0.08f;  // Horizontal lag (lower = more lag)
        float smoothFactorY = 0.06f;  // Vertical lag (slower = more lookahead when jumping)
        smoothCamX += (targetX - smoothCamX) * smoothFactorX;
        smoothCamY += (targetY - smoothCamY) * smoothFactorY;

        camPos.v[0] = smoothCamX;
        camPos.v[1] = smoothCamY;
        // Z-axis camera lead: push camera forward when moving into screen, back when moving toward camera
        // Apply death camera zoom (brings camera closer during death animation)
        float deathZoomOffset = deathCameraZoom * DEATH_ZOOM_AMOUNT;
        camPos.v[2] = cubeZ + smoothCamZoom + smoothChargePullback + leadZ + deathZoomOffset;

        // === CAMERA COLLISION ===
        // If camera position clips into a wall, push it back away from walls
        // The camera should back up to clear space, not zoom toward player

        float playerCenterY = cubeY + 10.0f;
        float desiredCamX = camPos.v[0];
        float desiredCamY = camPos.v[1];
        float desiredCamZ = camPos.v[2];

        // Check if camera's current position is inside geometry
        // If so, try pushing it further back (more negative Z) to clear the wall
        if (maploader_raycast_blocked(&mapLoader, cubeX, playerCenterY, cubeZ,
                                      desiredCamX, desiredCamY, desiredCamZ)) {
            // Camera clips into wall - push back in the camera's "back" direction
            // Try progressively pushing the camera further back (away from player)
            float pushBackAmount = 20.0f;  // How far to try pushing back
            float bestCamZ = desiredCamZ;
            bool foundClear = false;

            for (int i = 1; i <= 4; i++) {
                float testZ = desiredCamZ - (pushBackAmount * i / 4.0f);  // Push back (more negative Z)

                if (!maploader_raycast_blocked(&mapLoader, cubeX, playerCenterY, cubeZ,
                                               desiredCamX, desiredCamY, testZ)) {
                    // Found a clear position
                    bestCamZ = testZ;
                    foundClear = true;
                    break;
                }
            }

            if (foundClear) {
                // Add small buffer to stay clear of wall
                desiredCamZ = bestCamZ - 5.0f;
            }
            // If no clear position found, leave camera where it is (will clip, but better than nothing)
        }

        // Smooth interpolation to prevent camera jumping
        float camSmoothFactor = 0.06f;  // Gentle smoothing

        smoothCollisionCamX += (desiredCamX - smoothCollisionCamX) * camSmoothFactor;
        smoothCollisionCamY += (desiredCamY - smoothCollisionCamY) * camSmoothFactor;
        smoothCollisionCamZ += (desiredCamZ - smoothCollisionCamZ) * camSmoothFactor;

        camPos.v[0] = smoothCollisionCamX;
        camPos.v[1] = smoothCollisionCamY;
        camPos.v[2] = smoothCollisionCamZ;

        // When charging, look toward the arc direction (smoothly lerped)
        float desiredTargetX = cubeX;
        if (isCharging && jumpChargeTime >= hopThreshold) {
            float arcOffsetX = jumpArcEndX - cubeX;
            desiredTargetX = cubeX + arcOffsetX * 0.3f;  // Look 30% toward arc end
        }
        smoothCamTargetX += (desiredTargetX - smoothCamTargetX) * 0.15f;
        camTarget.v[0] = smoothCamTargetX;
        camTarget.v[1] = cubeY;
        camTarget.v[2] = cubeZ;
    }
    g_cameraTicks += get_ticks() - cameraStart;

    // Body part is set by level data, no runtime switching
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
    joypad_buttons_t pressed_test = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    // Test damage system disabled - was C-Down takes 1 damage
    // if (pressed_test.c_down && currentPart == PART_TORSO && !debugFlyMode && !playerIsDead && !playerIsHurt && !playerIsRespawning) {
    //     player_take_damage(1);
    // }

    // Camera zoom controls (C-up = zoom in, C-down = zoom out) - works for all body types
    // Only when not in debug fly mode, not dead, not in cutscene/dialogue
    if (!debugFlyMode && !playerIsDead && !scriptRunning && !cs2Playing) {
        if (pressed_test.c_up && cameraZoomLevel > 0) {
            cameraZoomLevel--;
            debugf("Camera zoom: %d (closer)\n", cameraZoomLevel);
        } else if (pressed_test.c_down && cameraZoomLevel < 2) {
            cameraZoomLevel++;
            debugf("Camera zoom: %d (farther)\n", cameraZoomLevel);
        }
    }

    // Disable normal jump for torso (charge jump only) and arms (handles its own jump)
    disableNormalJump = (currentPart == PART_TORSO || currentPart == PART_ARMS);

    // Air control: only torso lacks air control (arms and legs can steer in air)
    playerState.hasAirControl = (currentPart != PART_TORSO);

    // Update player skeleton/animation
    uint32_t perfAnimStart = get_ticks();
    bool isMoving = (fabsf(playerState.velX) > 0.1f || fabsf(playerState.velZ) > 0.1f);

    if (currentPart == PART_TORSO && torsoHasAnims) {
        joypad_buttons_t pressed = get_game_buttons_pressed();
        joypad_buttons_t held = get_game_buttons_held();
        // For released: held last frame but NOT held this frame
        joypad_buttons_t released = {0};

        // Wall jump input buffer - if A pressed, set buffer; otherwise decrement
        #define WALL_JUMP_INPUT_BUFFER_FRAMES 6  // 6 frames (~200ms) buffer for wall jump input
        if (pressed.a) {
            wallJumpInputBuffer = WALL_JUMP_INPUT_BUFFER_FRAMES;
        } else if (wallJumpInputBuffer > 0) {
            wallJumpInputBuffer--;
        }

        // Wall jump grace period - wait a few frames after leaving ground before wall jump works
        // This prevents accidental wall jumps off switches/edges immediately after leaving ground
        #define WALL_JUMP_GRACE_FRAMES 5  // 5 frames (~166ms) grace period
        static int framesSinceGrounded = 0;
        if (playerState.isGrounded) {
            framesSinceGrounded = 0;
        } else if (framesSinceGrounded < 100) {  // Cap to prevent overflow
            framesSinceGrounded++;
        }
        if ((g_replayMode && replay_is_playing()) || (g_demoMode && demo_is_playing())) {
            uint16_t releasedMask = g_replayButtonsPrev & ~g_replayButtonsHeld;
            released.a = (releasedMask & REPLAY_BTN_A) != 0;
            released.b = (releasedMask & REPLAY_BTN_B) != 0;
        } else {
            released = joypad_get_buttons_released(JOYPAD_PORT_1);
        }

        // === DEATH STATE (highest priority) ===
        if (playerIsDead) {
            // Play death animation and hold final frame
            if (torsoAnimDeath.animRef != NULL) {
                if (!torsoAnimDeath.isPlaying) {
                    // Animation finished - hold final frame
                    float duration = torsoAnimDeath.animRef->duration;
                    t3d_anim_set_time(&torsoAnimDeath, duration);
                } else {
                    t3d_anim_update(&torsoAnimDeath, deltaTime);
                }
            }
        }
        // === HURT STATE ===
        else if (playerIsHurt) {
            playerHurtAnimTime += deltaTime;

            // Check if pain animation finished
            if (playerCurrentPainAnim != NULL && playerCurrentPainAnim->animRef != NULL) {
                if (!playerCurrentPainAnim->isPlaying || playerHurtAnimTime > playerCurrentPainAnim->animRef->duration) {
                    playerIsHurt = false;
                    playerHurtAnimTime = 0.0f;
                    playerCurrentPainAnim = NULL;
                } else {
                    t3d_anim_update(playerCurrentPainAnim, deltaTime);
                }
            } else {
                // No valid animation - end hurt state
                playerIsHurt = false;
                playerHurtAnimTime = 0.0f;
                playerCurrentPainAnim = NULL;
            }
        }
        // === NORMAL STATES ===
        else {
        // Determine current state
        bool canIdle = playerState.isGrounded && !isMoving && !isCharging && !isJumping;

        // === JUMP BUFFER ===
        // Track A press in the air for jump buffering
        if (pressed.a && !playerState.isGrounded && !isCharging) {
            jumpBufferTimer = JUMP_BUFFER_TIME;
        }
        // Count down buffer timer
        if (jumpBufferTimer > 0.0f) {
            jumpBufferTimer -= deltaTime;
        }

        // === JUMP CHARGE ===
        // Allow starting a charge immediately upon landing (even if still in jumping state)
        // Player stops moving immediately when A is pressed
        // Coyote time: can jump for a brief window after leaving ground
        // Jump buffer: if A was pressed recently in air, trigger jump on land
        bool canStartJump = playerState.isGrounded || coyoteTimer > 0.0f;
        bool wantsJump = pressed.a || jumpBufferTimer > 0.0f;
        // Disable jump while sliding
        // Allow canceling landing animation with jump (3)
        // ALSO allow jump even if canJump is false during landing (to fix 1-frame delay)
        if (wantsJump && canStartJump && !isCharging && (playerState.canJump || isLanding) && !playerState.isSliding) {
            isCharging = true;
            isBufferedCharge = false;  // This is a normal charge, not buffered
            coyoteTimer = 0.0f;      // Consume coyote time
            jumpBufferTimer = 0.0f;  // Consume buffer

            // Triple jump combo: if within combo window, increment
            // After triple jump (combo=2), reset to start fresh combo
            if (jumpComboTimer > 0.0f && jumpComboCount < 2) {
                jumpComboCount++;
                debugf("TRIPLE JUMP: combo %d\n", jumpComboCount);
            } else {
                // Reset combo if: timer expired or already did triple
                if (jumpComboCount > 0) {
                    debugf("TRIPLE JUMP: reset (timer=%.2f, wasCombo=%d)\n",
                           jumpComboTimer, jumpComboCount);
                }
                jumpComboCount = 0;
            }
            jumpComboTimer = 0.0f;  // Consume timer

            // If we were considered 'jumping' from the previous air state, clear it to enter charging state
            isJumping = false;
            isLanding = false;  // Cancel landing animation (3)
            isMoving = false; // STOP movement immediately when A pressed
            jumpChargeTime = 0.0f;
            // Reset last valid aim for fresh charge
            lastValidAimX = 0.0f;
            lastValidAimY = 0.0f;
            aimGraceTimer = 0.0f;
            // Stop player movement immediately
            holdX = playerState.velX;
            holdZ = playerState.velZ;
            playerState.velX = 0.0f;
            playerState.velZ = 0.0f;
            playerState.canMove = false;
            playerState.canRotate = true; // allow aiming while charging
            playerState.canJump = true;
            // TODO(2): jumpChargeTime += BONUS_CHARGE_RATE * deltaTime; // Faster charge if buffered from air
            idleFrames = 0;
            playingFidget = false;
            fidgetPlayTime = 0.0f;
            // Stay in idle animation initially - will switch to charge anim after hopThreshold
            attach_anim_if_different(&torsoAnimIdle, &torsoSkel);
            t3d_anim_set_time(&torsoAnimIdle, 0.0f);
            t3d_anim_set_playing(&torsoAnimIdle, true);
        }

        if (isCharging) {
            // Reset idle state during charge
            idleFrames = 0;
            playingFidget = false;
            fidgetPlayTime = 0.0f;

            // Read stick input for jump aiming (X inverted to match world coords, with deadzone)
            joypad_inputs_t aimInputs = get_game_inputs();
            float rawAimX = -apply_deadzone(aimInputs.stick_x / 128.0f);
            float rawAimY = apply_deadzone(aimInputs.stick_y / 128.0f);
            float rawMag = sqrtf(rawAimX * rawAimX + rawAimY * rawAimY);

            // Grace period: store last valid direction and reset timer when stick is pushed
            // When stick returns to neutral, timer counts down and clears stored direction
            if (rawMag > AIM_GRACE_THRESHOLD) {
                lastValidAimX = rawAimX;
                lastValidAimY = rawAimY;
                aimGraceTimer = AIM_GRACE_DURATION;  // Reset grace timer
            } else {
                // Stick is neutral - count down grace timer
                aimGraceTimer -= deltaTime;
                if (aimGraceTimer <= 0.0f) {
                    // Grace period expired - clear stored direction
                    lastValidAimX = 0.0f;
                    lastValidAimY = 0.0f;
                    aimGraceTimer = 0.0f;
                }
            }

            // Arc display always uses current raw stick input (retracts when stick released)
            jumpAimX = rawAimX;
            jumpAimY = rawAimY;

            // Calculate aim magnitude (0 = straight up, 1 = full directional)
            float aimMag = sqrtf(jumpAimX * jumpAimX + jumpAimY * jumpAimY);
            if (aimMag > 1.0f) aimMag = 1.0f;

            // Update player facing to match stick direction (if stick is pushed)
            if (aimMag > 0.3f) {
                playerState.playerAngle = atan2f(-jumpAimX, jumpAimY);
            }

            float prevChargeTime = jumpChargeTime;
            // Triple jump combo: faster charge rate on subsequent jumps
            float comboChargeMult = (jumpComboCount == 0) ? JUMP_COMBO_CHARGE_MULT_1 :
                                    (jumpComboCount == 1) ? JUMP_COMBO_CHARGE_MULT_2 :
                                                            JUMP_COMBO_CHARGE_MULT_3;
            float chargeRate = deltaTime * comboChargeMult;
            jumpChargeTime += chargeRate;

            // Squash down while charging (anticipation before stretch on release)
            // Additive with landing squash - charge adds more squash on top
            float chargeRatio = jumpChargeTime / maxChargeTime;
            if (chargeRatio > 1.0f) chargeRatio = 1.0f;

            // Charge squash is separate and additive
            chargeSquash = chargeRatio * 0.25f;  // 0 to 0.25

            // Total squash = 1.0 - (landingSquash + chargeSquash)
            // Spring physics only affects landingSquash, chargeSquash is held constant
            squashScale = 1.0f - landingSquash - chargeSquash;
            squashVelocity = 0.0f;  // Hold the squash, don't spring back yet

            // Transition to charge animation after hop threshold is passed
            if (prevChargeTime < hopThreshold && jumpChargeTime >= hopThreshold) {
                attach_anim_if_different(&torsoAnimJumpCharge, &torsoSkel);
                t3d_anim_set_time(&torsoAnimJumpCharge, 0.0f);
                t3d_anim_set_playing(&torsoAnimJumpCharge, true);
            }

            // Update the appropriate animation
            if (jumpChargeTime >= hopThreshold) {
                if (torsoAnimJumpCharge.animRef != NULL && torsoAnimJumpCharge.isPlaying) {
                    t3d_anim_update(&torsoAnimJumpCharge, deltaTime);
                }
            } else {
                if (torsoAnimIdle.animRef != NULL && torsoAnimIdle.isPlaying) {
                    t3d_anim_update(&torsoAnimIdle, deltaTime);
                }
            }

            // Horizontal range scale (reduce sideways movement)
            const float HORIZONTAL_SCALE = 0.4f;

            // Clamp charge time at max - hold squish until player releases
            if (jumpChargeTime > maxChargeTime) {
                jumpChargeTime = maxChargeTime;
                // Hold at max charge squash
                chargeSquash = 0.25f;  // Max charge squash
                squashScale = 1.0f - landingSquash - chargeSquash;
                squashVelocity = 0.0f;
            }

            // Manual release - check if it's a quick tap (hop) or longer hold (charge jump)
            if (released.a) {
                isCharging = false;
                isJumping = true;
                isLanding = false;
                isMoving = false;
                playerState.canJump = false;
                playerState.canMove = false;
                playerState.canRotate = false;
                jumpAnimPaused = false;

                // Grace period: if stick is neutral now but was pushed recently, use last valid direction
                float execAimX = jumpAimX;
                float execAimY = jumpAimY;
                float execMag = aimMag;
                if (execMag < AIM_GRACE_THRESHOLD) {
                    // Stick is neutral - use the stored last valid direction for jump execution
                    execAimX = lastValidAimX;
                    execAimY = lastValidAimY;
                    execMag = sqrtf(execAimX * execAimX + execAimY * execAimY);
                    if (execMag > 1.0f) execMag = 1.0f;
                }

                if (jumpChargeTime < hopThreshold) {
                    // Quick tap = small hop - use stick direction, or facing direction if no stick
                    playerState.velY = hopVelocityY;
                    // Small stretch for hop (no landing squash component)
                    landingSquash = 0.0f;  // Remove landing squash
                    chargeSquash = 0.0f;   // No charge either
                    squashScale = 1.1f;
                    squashVelocity = 1.0f;
                    if (execMag > 0.1f) {
                        // Apply horizontal scale uniformly to both axes
                        playerState.velX = holdX;
                        playerState.velZ = holdZ;
                    } else {
                        // No stick = hop forward in facing direction
                        playerState.velX = holdX;
                        playerState.velZ = holdZ;
                    }
                } else {
                    // Longer hold = charge jump - use stick direction
                    // Triple jump combo: higher velocity on subsequent jumps
                    float comboPowerMult = (jumpComboCount == 0) ? JUMP_COMBO_POWER_MULT_1 :
                                           (jumpComboCount == 1) ? JUMP_COMBO_POWER_MULT_2 :
                                                                   JUMP_COMBO_POWER_MULT_3;
                    float jumpVelY = (chargeJumpEarlyBase + jumpChargeTime * chargeJumpEarlyMultiplier) * comboPowerMult;
                    // Apply 2x jump buff from chargepad (torso mode)
                    if (buffJumpActive) {
                        jumpVelY *= 2.0f;
                        buffJumpActive = false;  // Consume the buff
                        debugf("Jump buff consumed! 2x height!\n");
                    }
                    playerState.velY = jumpVelY;
                    wav64_play(&sfxJumpSound, 2);  // Play jump sound on channel 2
                    float forward = (3.0f + 2.0f * jumpChargeTime) * FPS_SCALE * execMag * comboPowerMult;
                    if (execMag > 0.1f) {
                        // Apply horizontal scale uniformly to both axes to normalize speed
                        playerState.velX = (execAimX / execMag) * forward * HORIZONTAL_SCALE;
                        playerState.velZ = (execAimY / execMag) * forward * HORIZONTAL_SCALE;
                    } else {
                        playerState.velX = 0.0f;
                        playerState.velZ = 0.0f;
                    }
                    // Stretch based on charge amount
                    landingSquash = 0.0f;  // Remove landing squash component
                    chargeSquash = 0.0f;   // Clear charge squash too
                    float chargeRatio = jumpChargeTime / maxChargeTime;
                    squashScale = 1.1f + chargeRatio * 0.15f;  // 1.1 to 1.25
                    squashVelocity = 1.0f + chargeRatio * 1.0f;
                }
                // Track if this jump was aimed (stick moved) - required for combo continuation
                lastJumpWasAimed = (execMag > 0.1f);

                // Select animation based on combo count (single, double, triple)
                currentLaunchAnim = &torsoAnimJumpLaunch;  // Default: first jump
                const char* animName = "launch";
                if (jumpComboCount == 1 && torsoAnimJumpDouble.animRef != NULL) {
                    currentLaunchAnim = &torsoAnimJumpDouble;  // Double jump
                    animName = "DOUBLE";
                } else if (jumpComboCount == 2 && torsoAnimJumpTriple.animRef != NULL) {
                    currentLaunchAnim = &torsoAnimJumpTriple;  // Triple jump
                    animName = "TRIPLE";
                }
                debugf("JUMP LAUNCH: combo=%d anim=%s duration=%.2f\n", jumpComboCount, animName,
                       currentLaunchAnim->animRef ? currentLaunchAnim->animRef->duration : -1.0f);
                attach_anim_if_different(currentLaunchAnim, &torsoSkel);
                t3d_anim_set_time(currentLaunchAnim, 0.0f);
                t3d_anim_set_playing(currentLaunchAnim, true);
            }

            if (isCharging && pressed.b) {
                // Cancel jump charge
                isCharging = false;
                isMoving = false;
                jumpChargeTime = 0.0f;
                landingSquash = 0.0f;
                chargeSquash = 0.0f;
                squashScale = 1.0f;
                squashVelocity = 0.0f;
                jumpAimX = 0.0f;
                jumpAimY = 0.0f;
                attach_anim_if_different(&torsoAnimIdle, &torsoSkel);
                t3d_anim_set_time(&torsoAnimIdle, 0.0f);
                t3d_anim_set_playing(&torsoAnimIdle, true);
                playerState.canMove = true;
                playerState.canRotate = true;
                playerState.canJump = true;
            }
        }
        // === JUMP RELEASE ===
        else if (isJumping) {
            // If we landed on a slope and started sliding, exit jump state immediately
            // Let the sliding animation block handle it
            if (playerState.isSliding && playerState.isGrounded) {
                isJumping = false;
                isLanding = false;
                jumpAnimPaused = false;
                playerState.currentJumps = 0;
                // canJump stays false - will be reset when slide ends
                // Fall through to sliding block on next frame
            }

            // Reset idle state during jump
            idleFrames = 0;
            playingFidget = false;
            fidgetPlayTime = 0.0f;

            // === LANDING PHASE ===
            if (isLanding) {
                // Update landing animation
                if (torsoAnimJumpLand.animRef != NULL) {
                    if (torsoAnimJumpLand.isPlaying) {
                        playerState.canJump = true;  // Allow jump to cancel landing
                        t3d_anim_update(&torsoAnimJumpLand, deltaTime);
                        joypad_inputs_t aimInputs = get_game_inputs();
                        float rawAimX = -apply_deadzone(aimInputs.stick_x / 128.0f);
                        float rawAimY = apply_deadzone(aimInputs.stick_y / 128.0f);
                        if(rawAimX != 0.0f || rawAimY != 0.0f) // if the player is trying to move, cancel the landing animation.
                        {
                            // Cancel landing animation if player moves
                            isLanding = false;
                            t3d_anim_set_playing(&torsoAnimJumpLand, false);
                            isMoving = true;
                            playerState.canMove = true;
                            playerState.canRotate = true;
                        }
                        // Slow down movement during landing (but don't cancel animation with stick)
                        playerState.velX *= 0.8f;
                        playerState.velZ *= 0.8f;
                    }
                    // Check if landing animation finished - hold last frame briefly then exit
                    if (!torsoAnimJumpLand.isPlaying) {
                        // Hold last frame - animation already stopped at end
                        t3d_anim_set_time(&torsoAnimJumpLand, torsoAnimJumpLand.animRef->duration);
                        // Jump fully finished
                        isJumping = false;
                        isLanding = false;
                        isMoving = true;
                        playerState.canMove = true;
                        playerState.canRotate = true;
                    }
                } else {
                    // No land animation - just finish
                    isJumping = false;
                    isLanding = false;
                    isMoving = true;
                    playerState.canMove = true;
                    playerState.canRotate = true;
                }
            }
            // === LAUNCH PHASE (ascending / mid-air) ===
            else if (!jumpAnimPaused) {
                // === WALL KICK CHECK ===
                // If we hit a wall recently and press A (or have buffered A), kick off it
                // IMPORTANT: Fixed velocity, not based on incoming speed!
                // Input buffer: pressing A before hitting wall also triggers wall kick
                // Grace period: wait a few frames after leaving ground before wall jump works
                bool wallKickInput = pressed.a || wallJumpInputBuffer > 0;
                bool pastGracePeriod = framesSinceGrounded >= WALL_JUMP_GRACE_FRAMES;
                if (playerState.wallHitTimer > 0 && wallKickInput && !playerState.isGrounded && pastGracePeriod) {
                    // Consume the input buffer
                    wallJumpInputBuffer = 0;
                    // Wall kick! Fixed, consistent jump - same every time
                    #define WALL_KICK_VEL_Y 8.0f     // Fixed upward velocity
                    #define WALL_KICK_VEL_XZ 4.0f    // Fixed horizontal push away from wall

                    // Wall normal points AWAY from wall (it's the push direction)
                    float awayX = playerState.wallNormalX;
                    float awayZ = playerState.wallNormalZ;

                    // Reflect player's FACING ANGLE across the wall normal
                    // This preserves the player's intended direction and feels intuitive
                    // It's also skill-based: players can aim wall kicks by turning mid-air!

                    // Convert player angle to direction vector
                    float facingX = -sinf(playerState.playerAngle);
                    float facingZ = cosf(playerState.playerAngle);

                    // Reflect facing direction across wall normal
                    // Formula: reflected = incident - 2 * (incident · normal) * normal
                    float dot = facingX * awayX + facingZ * awayZ;
                    float reflectedX = facingX - 2.0f * dot * awayX;
                    float reflectedZ = facingZ - 2.0f * dot * awayZ;

                    // Normalize and apply fixed speed
                    float reflectedLen = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
                    if (reflectedLen > 0.01f) {
                        playerState.velX = (reflectedX / reflectedLen) * WALL_KICK_VEL_XZ;
                        playerState.velZ = (reflectedZ / reflectedLen) * WALL_KICK_VEL_XZ;
                        torsoWallJumpAngle = atan2f(-(reflectedX / reflectedLen), (reflectedZ / reflectedLen));
                    } else {
                        // Fallback: kick straight away from wall
                        playerState.velX = awayX * WALL_KICK_VEL_XZ;
                        playerState.velZ = awayZ * WALL_KICK_VEL_XZ;
                        torsoWallJumpAngle = atan2f(-awayX, awayZ);
                    }

                    playerState.velY = WALL_KICK_VEL_Y;
                    playerState.playerAngle = torsoWallJumpAngle;
                    torsoIsWallJumping = true;

                    // Lock movement and rotation during wall jump arc
                    playerState.canMove = false;
                    playerState.canRotate = false;

                    // Reset wall hit timer so we can't chain infinitely fast
                    playerState.wallHitTimer = 0;

                    // Reset jump peak for landing squash calculation
                    jumpPeakY = cubeY;

                    // Restart jump animation (wall kicks use default launch anim, reset combo)
                    jumpComboCount = 0;
                    currentLaunchAnim = &torsoAnimJumpLaunch;
                    if (currentLaunchAnim->animRef != NULL) {
                        t3d_anim_set_time(currentLaunchAnim, 0.0f);
                        t3d_anim_set_playing(currentLaunchAnim, true);
                    }

                    // Spawn dust particles at wall contact point
                    // Offset slightly toward the wall (opposite of away direction)
                    spawn_dust_particles(cubeX - awayX * 5.0f, cubeY, cubeZ - awayZ * 5.0f, 3);

                    debugf("Wall kick! Away: %.2f, %.2f\n", awayX, awayZ);
                }
                if (currentLaunchAnim != NULL && currentLaunchAnim->animRef != NULL &&
                    currentLaunchAnim->targetsQuat != NULL) {
                    t3d_anim_update(currentLaunchAnim, deltaTime);
                    // Debug: which animation is being updated?
                    static int jumpFrameCount = 0;
                    if (jumpFrameCount < 10) {
                        const char* animName = (currentLaunchAnim == &torsoAnimJumpLaunch) ? "launch" :
                                               (currentLaunchAnim == &torsoAnimJumpDouble) ? "DOUBLE" :
                                               (currentLaunchAnim == &torsoAnimJumpTriple) ? "TRIPLE" : "other";
                        debugf("UPDATE: %s time=%.3f dur=%.3f\n", animName, currentLaunchAnim->time, currentLaunchAnim->animRef->duration);
                        jumpFrameCount++;
                    }
                    // Guard against missing animation data before dereferencing animRef
                    if (currentLaunchAnim->animRef->duration > 0.0f) {
                        float animProgress = currentLaunchAnim->time / currentLaunchAnim->animRef->duration;
                        // Use different thresholds for each jump type to allow full rotations
                        float pauseThreshold = (currentLaunchAnim == &torsoAnimJumpTriple) ? JUMP_ANIM_PROGRESS_3 :
                                               (currentLaunchAnim == &torsoAnimJumpDouble) ? JUMP_ANIM_PROGRESS_2 :
                                                                                            JUMP_ANIM_PROGRESS_1;
                        if (animProgress > pauseThreshold && !playerState.isGrounded) {
                            jumpAnimPaused = true;
                            t3d_anim_set_playing(currentLaunchAnim, false);
                            jumpFrameCount = 0;  // Reset for next jump
                        }
                    }
                } else {
                    // No jumpLaunch animation available - fall back to a simple heuristic
                    if (playerState.velY < 0.0f && !playerState.isGrounded) {
                        jumpAnimPaused = true;
                    }
                }

                // If we land while still in launch animation, transition to landing
                if (playerState.isGrounded && !playerState.isSliding) {
                    playerState.velX *= 0.8f;
                    playerState.velZ *= 0.8f;
                    // Force transition to landing state
                    jumpAnimPaused = true;
                }
            }
            // === WALL KICK CHECK (while falling/paused) ===
            // Also check during the falling phase when animation is paused
            // Use input buffer for more forgiving wall kick timing
            // Grace period: wait a few frames after leaving ground before wall jump works
            bool wallKickInputFalling = pressed.a || wallJumpInputBuffer > 0;
            bool pastGracePeriodFalling = framesSinceGrounded >= WALL_JUMP_GRACE_FRAMES;
            if (jumpAnimPaused && !playerState.isGrounded && playerState.wallHitTimer > 0 && wallKickInputFalling && pastGracePeriodFalling) {
                // Consume the input buffer
                wallJumpInputBuffer = 0;
                // Wall kick! Fixed, consistent jump
                float awayX = playerState.wallNormalX;
                float awayZ = playerState.wallNormalZ;

                // Reflect player's facing angle across wall normal
                float facingX = -sinf(playerState.playerAngle);
                float facingZ = cosf(playerState.playerAngle);
                float dot = facingX * awayX + facingZ * awayZ;
                float reflectedX = facingX - 2.0f * dot * awayX;
                float reflectedZ = facingZ - 2.0f * dot * awayZ;

                float reflectedLen = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
                if (reflectedLen > 0.01f) {
                    playerState.velX = (reflectedX / reflectedLen) * WALL_KICK_VEL_XZ;
                    playerState.velZ = (reflectedZ / reflectedLen) * WALL_KICK_VEL_XZ;
                    torsoWallJumpAngle = atan2f(-(reflectedX / reflectedLen), (reflectedZ / reflectedLen));
                } else {
                    playerState.velX = awayX * WALL_KICK_VEL_XZ;
                    playerState.velZ = awayZ * WALL_KICK_VEL_XZ;
                    torsoWallJumpAngle = atan2f(-awayX, awayZ);
                }

                playerState.velY = WALL_KICK_VEL_Y;
                playerState.playerAngle = torsoWallJumpAngle;
                torsoIsWallJumping = true;

                // Lock movement and rotation during wall jump arc
                playerState.canMove = false;
                playerState.canRotate = false;

                playerState.wallHitTimer = 0;
                jumpPeakY = cubeY;
                jumpAnimPaused = false;  // Unpause to restart jump
                // Wall kicks use default launch anim, reset combo
                jumpComboCount = 0;
                currentLaunchAnim = &torsoAnimJumpLaunch;
                if (currentLaunchAnim->animRef != NULL) {
                    t3d_anim_set_time(currentLaunchAnim, 0.0f);
                    t3d_anim_set_playing(currentLaunchAnim, true);
                }

                // Spawn dust particles at wall contact point
                spawn_dust_particles(cubeX - awayX * 5.0f, cubeY, cubeZ - awayZ * 5.0f, 3);

                debugf("Wall kick (falling)! Away: %.2f, %.2f\n", awayX, awayZ);
            }
            // === TRANSITION TO LANDING ===
            // Landed while in air (paused or just now) - start landing animation
            if (jumpAnimPaused && playerState.isGrounded && !playerState.isSliding) {
                jumpAnimPaused = false;
                playerState.canJump = true; // can chain jumps
                torsoIsWallJumping = false; // Reset wall jump on landing
                playerState.canMove = true;   // Restore movement on landing
                playerState.canRotate = true; // Restore rotation on landing

                // Calculate fall distance for landing effects (always calculate, used for both paths)
                float fallDistance = jumpPeakY - cubeY;
                float squashAmount = (fallDistance - 30.0f) / 170.0f;
                if (squashAmount < 0.0f) squashAmount = 0.0f;
                if (squashAmount > 1.0f) squashAmount = 1.0f;

                // Check for buffered jump - if A is held/pressed during landing, immediately start charging
                joypad_buttons_t heldNow = get_game_buttons_held();
                bool wantsBufferedJump = (jumpBufferTimer > 0.0f || heldNow.a || pressed.a);

                if (wantsBufferedJump) {
                    // BUFFERED JUMP: Skip landing animation, immediate charge with visual feedback
                    isLanding = false;
                    isJumping = false;
                    isCharging = true;
                    isBufferedCharge = true;  // Mark this as a buffered charge for bonus rate
                    jumpChargeTime = 0.01f; // Start just above 0 to show we're charging
                    jumpBufferTimer = 0.0f;  // Consume buffer

                    // Triple jump combo: increment if continuing chain
                    // After triple jump (combo=2), reset to start fresh combo
                    if (jumpComboCount < 2) {
                        jumpComboCount++;
                        debugf("BUFFERED JUMP: combo now %d\n", jumpComboCount);
                    } else {
                        // Already did triple jump - reset
                        jumpComboCount = 0;
                        debugf("BUFFERED JUMP: reset combo after triple\n");
                    }
                    jumpComboTimer = 0.0f;  // Consume timer
                    playerState.velX = 0.0f;
                    playerState.velZ = 0.0f;
                    playerState.canMove = false;
                    playerState.canRotate = true;

                    // Apply landing squash component (will spring back)
                    landingSquash = squashAmount * 0.75f;  // 0.0 to 0.75
                    chargeSquash = 0.0f;  // Start with no charge squash
                    squashScale = 1.0f - landingSquash - chargeSquash;
                    squashVelocity = 0.0f;

                    // Spawn dust particles on landing (even for buffered jumps)
                    if (fallDistance > 20.0f) {
                        int dustCount = 2 + (int)(squashAmount * 2);
                        spawn_dust_particles(cubeX, bestGroundY, cubeZ, dustCount);
                    }

                    // Initialize jump aim with current stick input for immediate arc display
                    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
                    jumpAimX = inputs.stick_x / 128.0f;
                    jumpAimY = inputs.stick_y / 128.0f;
                    // Reset last valid aim for fresh charge
                    lastValidAimX = 0.0f;
                    lastValidAimY = 0.0f;
                    aimGraceTimer = 0.0f;

                    // Start with charge animation if we have enough charge time
                    // (arc will appear once jumpChargeTime >= hopThreshold in draw code)
                    if (torsoAnimJumpCharge.animRef != NULL) {
                        attach_anim_if_different(&torsoAnimJumpCharge, &torsoSkel);
                        t3d_anim_set_time(&torsoAnimJumpCharge, 0.0f);
                        t3d_anim_set_playing(&torsoAnimJumpCharge, true);
                    }
                } else {
                    // NORMAL LANDING: Play landing animation with squash
                    isLanding = true;

                    // Start triple jump combo timer - player has JUMP_COMBO_WINDOW seconds to start next jump
                    jumpComboTimer = JUMP_COMBO_WINDOW;
                    debugf("LANDING: combo timer set to %.2f, current combo=%d\n", jumpComboTimer, jumpComboCount);

                    // Apply squash as landing squash component
                    landingSquash = squashAmount * 0.75f;
                    chargeSquash = 0.0f;
                    squashScale = 1.0f - landingSquash;
                    squashVelocity = 0.0f;

                    // Spawn dust particles on significant landings
                    if (fallDistance > 40.0f) {
                        int dustCount = 2 + (int)(squashAmount * 2);  // 2-4 dust puffs
                        spawn_dust_particles(cubeX, bestGroundY, cubeZ, dustCount);
                    }

                    // Screen shake and impact stars on hard landings (200+ height)
                    if (fallDistance > 200.0f) {
                        float shakeStrength = 6.0f + (fallDistance - 200.0f) / 50.0f;
                        if (shakeStrength > 15.0f) shakeStrength = 15.0f;
                        trigger_screen_shake(shakeStrength);
                        spawn_impact_stars();  // Stars around head on hard landing
                    }

                    // Switch to landing animation
                    if (torsoAnimJumpLand.animRef != NULL) {
                        attach_anim_if_different(&torsoAnimJumpLand, &torsoSkel);
                        t3d_anim_set_time(&torsoAnimJumpLand, 0.0f);
                        t3d_anim_set_playing(&torsoAnimJumpLand, true);
                    } else {
                        // No land animation - just finish
                        isJumping = false;
                        isLanding = false;
                        isMoving = true;
                        playerState.canMove = true;
                        playerState.canRotate = true;
                    }
                }
            }
        }
        // === FALLING THROUGH AIR (not grounded, not jumping state) ===
        // Don't enter falling state if sliding - player is still "grounded" on slope
        else if (!playerState.isGrounded && !isJumping && !playerState.isSliding) {
            idleFrames = 0;
            playingFidget = false;
            fidgetPlayTime = 0.0f;
            isMoving = false; // prevent playerAngle changes while falling
            isJumping = true; // Enter jumping state when falling
            isLanding = false;
            jumpAnimPaused = true;
            // If we have a jump land animation, use its start frame as the falling pose
            if (torsoAnimJumpLand.animRef != NULL) {
                attach_anim_if_different(&torsoAnimJumpLand, &torsoSkel);
                t3d_anim_set_time(&torsoAnimJumpLand, 0.0f);
                t3d_anim_set_playing(&torsoAnimJumpLand, false);
            } else if (torsoAnimJumpLaunch.animRef != NULL) {
                // Fallback: use end of launch animation
                attach_anim_if_different(&torsoAnimJumpLaunch, &torsoSkel);
                t3d_anim_set_time(&torsoAnimJumpLaunch, torsoAnimJumpLaunch.animRef->duration);
                t3d_anim_set_playing(&torsoAnimJumpLaunch, false);
            }
            // While falling, lock rotation and movement
            playerState.canMove = false;
            playerState.canRotate = false;
        }
        // === SLIDING ON SLOPE ===
        else if (playerState.isSliding) {
            // Player is sliding down a slope - play slide anim and turn to face downhill
            idleFrames = 0;
            playingFidget = false;
            isJumping = false;
            jumpAnimPaused = false;
            isSlideRecovering = false;
            playerState.canMove = true;
            playerState.canRotate = false;  // We handle rotation manually

            // Slope downhill direction is (slopeNormalX, slopeNormalZ) projected to XZ plane
            float slopeDirX = playerState.slopeNormalX;
            float slopeDirZ = playerState.slopeNormalZ;

            // Calculate target angle to face downhill direction
            // atan2(-x, z) gives angle in our coordinate system
            float targetAngle = atan2f(-slopeDirX, slopeDirZ);

            // Lerp player angle toward target (smooth turn)
            float angleDiff = targetAngle - playerState.playerAngle;
            // Normalize angle difference to [-PI, PI]
            while (angleDiff > 3.14159265f) angleDiff -= 6.28318530f;
            while (angleDiff < -3.14159265f) angleDiff += 6.28318530f;

            // Lerp speed (radians per second)
            const float SLIDE_TURN_SPEED = 8.0f;
            float maxTurn = SLIDE_TURN_SPEED * deltaTime;
            if (angleDiff > maxTurn) angleDiff = maxTurn;
            else if (angleDiff < -maxTurn) angleDiff = -maxTurn;

            playerState.playerAngle += angleDiff;
            // Normalize final angle
            while (playerState.playerAngle > 3.14159265f) playerState.playerAngle -= 6.28318530f;
            while (playerState.playerAngle < -3.14159265f) playerState.playerAngle += 6.28318530f;

            // Always use front slide animation since we're turning to face downhill
            isSlidingFront = true;
            if (torsoHasAnims && torsoAnimSlideFront.animRef != NULL) {
                // First frame of sliding - reset animation to start
                if (!wasSliding) {
                    t3d_anim_set_time(&torsoAnimSlideFront, 0.0f);
                    t3d_anim_set_playing(&torsoAnimSlideFront, true);
                }
                attach_anim_if_different(&torsoAnimSlideFront, &torsoSkel);

                // Play once and hold last frame
                float animDuration = torsoAnimSlideFront.animRef->duration;
                if (torsoAnimSlideFront.time < animDuration && skeleton_is_valid(&torsoSkel)) {
                    t3d_anim_update(&torsoAnimSlideFront, deltaTime);
                }
                // else: animation finished, hold last frame (don't update)
            }
            wasSliding = true;
        }
        // === SLIDE RECOVERY (just stopped sliding) ===
        else if (wasSliding && !isSlideRecovering) {
            // Start recovery animation
            isSlideRecovering = true;
            slideRecoverTime = 0.0f;
            playerState.canMove = false;    // Lock movement during recovery
            playerState.canRotate = false;  // Lock rotation during recovery
            T3DAnim* recoverAnim = isSlidingFront ? &torsoAnimSlideFrontRecover : &torsoAnimSlideBackRecover;
            if (torsoHasAnims && recoverAnim->animRef != NULL) {
                attach_anim_if_different(recoverAnim, &torsoSkel);
                t3d_anim_set_time(recoverAnim, 0.0f);
                t3d_anim_set_playing(recoverAnim, true);
            } else {
                // No recovery anim, skip recovery state
                isSlideRecovering = false;
                playerState.canMove = true;
                playerState.canRotate = true;
                playerState.canJump = true;
            }
            wasSliding = false;
        }
        else if (isSlideRecovering) {
            // Continue recovery animation - keep movement locked
            playerState.canMove = false;
            playerState.canRotate = false;
            T3DAnim* recoverAnim = isSlidingFront ? &torsoAnimSlideFrontRecover : &torsoAnimSlideBackRecover;
            if (torsoHasAnims && recoverAnim->animRef != NULL) {
                slideRecoverTime += deltaTime;
                if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(recoverAnim, deltaTime);

                // Check if recovery animation finished
                if (slideRecoverTime >= recoverAnim->animRef->duration) {
                    isSlideRecovering = false;
                    slideRecoverTime = 0.0f;
                    playerState.canMove = true;
                    playerState.canRotate = true;
                    playerState.canJump = true;
                }
            } else {
                // No recovery anim, just end immediately
                isSlideRecovering = false;
                playerState.canMove = true;
                playerState.canRotate = true;
                playerState.canJump = true;
            }
        }
        // === TRIPLE JUMP COMBO TIMER ===
        // Count down while grounded and not charging - reset combo when timer expires
        if (playerState.isGrounded && !isCharging && !isJumping && jumpComboTimer > 0.0f) {
            jumpComboTimer -= deltaTime;
            if (jumpComboTimer <= 0.0f) {
                jumpComboCount = 0;
                jumpComboTimer = 0.0f;
            }
        }
        // === WALKING ===
        if (isMoving && !isCharging && !isJumping && !isLanding && !isSlideRecovering) {
            idleFrames = 0;
            playingFidget = false;
            fidgetPlayTime = 0.0f;

            // Choose walk animation based on stick magnitude
            // Slow walk when stick is below 50% in any direction, fast walk above
            joypad_inputs_t walkInputs = get_game_inputs();
            float stickX = walkInputs.stick_x / 128.0f;
            float stickY = walkInputs.stick_y / 128.0f;
            float stickMag = sqrtf(stickX * stickX + stickY * stickY);

            if (stickMag < 0.5f && torsoAnimWalkSlow.animRef) {
                // Slow walk animation
                attach_anim_if_different(&torsoAnimWalkSlow, &torsoSkel);
                if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&torsoAnimWalkSlow, deltaTime);
            } else {
                // Fast walk animation
                attach_anim_if_different(&torsoAnimWalk, &torsoSkel);
                if (torsoAnimWalk.animRef && skeleton_is_valid(&torsoSkel)) t3d_anim_update(&torsoAnimWalk, deltaTime);
            }
        }
        // === IDLE STATE (grounded, not moving, not jumping) ===
        else if (canIdle && torsoAnimWait.animRef && torsoAnimIdle.animRef) {
            float fidgetDuration = torsoAnimWait.animRef->duration;

            // Currently playing fidget (wait animation)?
            if (playingFidget) {
                fidgetPlayTime += deltaTime;
                if (fidgetPlayTime < fidgetDuration) {
                    if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&torsoAnimWait, deltaTime);
                } else {
                    playingFidget = false;
                    fidgetPlayTime = 0.0f;
                    idleFrames = 0;
                }
            }
            // Time to start fidget?
            else if (idleFrames >= IDLE_WAIT_FRAMES) {
                playingFidget = true;
                fidgetPlayTime = 0.0f;
                idleFrames = 0;
                attach_anim_if_different(&torsoAnimWait, &torsoSkel);
                t3d_anim_set_time(&torsoAnimWait, 0.0f);
                t3d_anim_set_playing(&torsoAnimWait, true);
            }
            // Default: idle animation (normal standing pose)
            else {
                idleFrames++;
                attach_anim_if_different(&torsoAnimIdle, &torsoSkel);
                if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&torsoAnimIdle, deltaTime);
            }
        }
        // === IN AIR (not grounded, not jumping state) ===
        else if (!isJumping && torsoAnimIdle.animRef) {
            idleFrames = 0;
            playingFidget = false;
            fidgetPlayTime = 0.0f;
            // Keep idle pose while in air
            attach_anim_if_different(&torsoAnimIdle, &torsoSkel);
            if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&torsoAnimIdle, deltaTime);
        }
        }  // End of normal states

        // Keep facing away from wall during wall jump
        if (torsoIsWallJumping && !playerState.isGrounded) {
            playerState.playerAngle = torsoWallJumpAngle;
        }

        // Debug: ALWAYS print to verify this code runs
        static int skelDebugCount = 0;
        if (skelDebugCount < 3) {
            debugf("TORSO_SKEL_UPDATE: frame %d\n", skelDebugCount);
            skelDebugCount++;
        }
        if (skeleton_is_valid(&torsoSkel)) {
            t3d_skeleton_update(&torsoSkel);
        }
    }
    // === ARMS MODE ===
    else if (currentPart == PART_ARMS && armsHasAnims) {
        static bool armsDebugPrinted = false;
        if (!armsDebugPrinted) {
            debugf("ARMS MODE ACTIVE: armsHasAnims=%d\n", armsHasAnims);
            armsDebugPrinted = true;
        }
        joypad_buttons_t pressed = get_game_buttons_pressed();
        joypad_buttons_t held = get_game_buttons_held();

        // === JUMP BUFFER (Arms Mode) ===
        // Track A press in the air for jump buffering
        if (pressed.a && !playerState.isGrounded && !isJumping) {
            jumpBufferTimer = JUMP_BUFFER_TIME;
        }
        // Count down buffer timer
        if (jumpBufferTimer > 0.0f) {
            jumpBufferTimer -= deltaTime;
        }

        // Wall jump grace period - wait a few frames after leaving ground
        #define ARMS_WALL_JUMP_GRACE_FRAMES 5
        static int armsFramesSinceGrounded = 0;
        if (playerState.isGrounded) {
            armsFramesSinceGrounded = 0;
        } else if (armsFramesSinceGrounded < 100) {
            armsFramesSinceGrounded++;
        }

        // === DEATH STATE (highest priority) ===
        if (playerIsDead) {
            if (armsAnimDeath.animRef != NULL) {
                attach_anim_if_different(&armsAnimDeath, &torsoSkel);
                t3d_anim_update(&armsAnimDeath, deltaTime);
            }
        }
        // === HURT STATE ===
        else if (playerIsHurt) {
            playerHurtAnimTime += deltaTime;
            if (playerCurrentPainAnim != NULL && playerCurrentPainAnim->animRef != NULL) {
                if (!playerCurrentPainAnim->isPlaying || playerHurtAnimTime > playerCurrentPainAnim->animRef->duration) {
                    playerIsHurt = false;
                    playerHurtAnimTime = 0.0f;
                    playerCurrentPainAnim = NULL;
                } else {
                    t3d_anim_update(playerCurrentPainAnim, deltaTime);
                }
            } else {
                playerIsHurt = false;
                playerHurtAnimTime = 0.0f;
                playerCurrentPainAnim = NULL;
            }
        }
        // === SPIN ATTACK (B button) ===
        else if (armsIsSpinning) {
            armsSpinTime += deltaTime;
            if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&armsAnimAtkSpin, deltaTime);

            // Spawn electricity particles during spin (every 3-4 frames)
            static int spinElectricFrame = 0;
            spinElectricFrame++;
            if (spinElectricFrame >= 3) {
                spawn_electric_particles(cubeX, cubeY + 10.0f, cubeZ, 2);
                spinElectricFrame = 0;
            }

            if (armsAnimAtkSpin.animRef) {
                float spinProgress = armsSpinTime / armsAnimAtkSpin.animRef->duration;

                // Allow movement during first 55% of spin
                if (spinProgress < 0.55f) {
                    playerState.canMove = true;
                    playerState.canRotate = true;
                } else {
                    // Stop movement at the end (last 45%)
                    playerState.canMove = false;
                    playerState.canRotate = false;
                    // Quickly decelerate
                    playerState.velX *= 0.8f;
                    playerState.velZ *= 0.8f;
                }

                // Check if spin animation finished
                if (armsSpinTime >= armsAnimAtkSpin.animRef->duration) {
                    armsIsSpinning = false;
                    armsSpinTime = 0.0f;
                    playerState.canMove = true;
                    playerState.canRotate = true;
                    // Stop completely
                    playerState.velX = 0.0f;
                    playerState.velZ = 0.0f;
                }
            }
        }
        // === GLIDING (double jump in air) ===
        else if (armsIsGliding) {
            armsSpinTime += deltaTime;
            if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&armsAnimAtkSpin, deltaTime);

            // Spawn electricity particles during glide spin (every 3-4 frames)
            static int glideElectricFrame = 0;
            glideElectricFrame++;
            if (glideElectricFrame >= 3) {
                spawn_electric_particles(cubeX, cubeY + 10.0f, cubeZ, 2);
                glideElectricFrame = 0;
            }

            // End glide when animation finishes or we land
            if (playerState.isGrounded) {
                armsIsGliding = false;
                armsHasDoubleJumped = false;
                playerState.isGliding = false;  // Disable reduced gravity
                // Consume glide buff when glide ends
                if (buffGlideActive) {
                    buffGlideActive = false;
                    debugf("Glide buff consumed!\n");
                }
                armsSpinTime = 0.0f;
            } else if (armsAnimAtkSpin.animRef && armsSpinTime >= armsAnimAtkSpin.animRef->duration) {
                // Animation finished but still in air - stop gliding state but keep hasDoubleJumped
                armsIsGliding = false;
                playerState.isGliding = false;  // Disable reduced gravity
                // Consume glide buff when glide ends
                if (buffGlideActive) {
                    buffGlideActive = false;
                    debugf("Glide buff consumed!\n");
                }
                armsSpinTime = 0.0f;
            }
        }
        // === WHIP ATTACK (C-up) ===
        else if (armsIsWhipping) {
            armsWhipTime += deltaTime;
            if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&armsAnimAtkWhip, deltaTime);

            // Check if whip animation finished
            if (armsAnimAtkWhip.animRef && armsWhipTime >= armsAnimAtkWhip.animRef->duration) {
                armsIsWhipping = false;
                armsWhipTime = 0.0f;
                playerState.canMove = true;
                playerState.canRotate = true;
            }
        }
        // === JUMPING/LANDING ===
        else if (isJumping) {
            // === WALL KICK CHECK (A button near wall) ===
            // Grace period: wait a few frames after leaving ground before wall jump works
            bool armsPastGracePeriod = armsFramesSinceGrounded >= ARMS_WALL_JUMP_GRACE_FRAMES;
            if (playerState.wallHitTimer > 0 && pressed.a && !playerState.isGrounded && armsPastGracePeriod) {
                // Wall kick! Fixed, consistent jump
                #define ARMS_WALL_KICK_VEL_Y 8.0f
                #define ARMS_WALL_KICK_VEL_XZ 4.0f

                float awayX = playerState.wallNormalX;
                float awayZ = playerState.wallNormalZ;

                // Reflect player's facing angle across wall normal
                float facingX = -sinf(playerState.playerAngle);
                float facingZ = cosf(playerState.playerAngle);
                float dot = facingX * awayX + facingZ * awayZ;
                float reflectedX = facingX - 2.0f * dot * awayX;
                float reflectedZ = facingZ - 2.0f * dot * awayZ;

                float reflectedLen = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
                if (reflectedLen > 0.01f) {
                    playerState.velX = (reflectedX / reflectedLen) * ARMS_WALL_KICK_VEL_XZ;
                    playerState.velZ = (reflectedZ / reflectedLen) * ARMS_WALL_KICK_VEL_XZ;
                    armsWallJumpAngle = atan2f(-(reflectedX / reflectedLen), (reflectedZ / reflectedLen));
                } else {
                    playerState.velX = awayX * ARMS_WALL_KICK_VEL_XZ;
                    playerState.velZ = awayZ * ARMS_WALL_KICK_VEL_XZ;
                    armsWallJumpAngle = atan2f(-awayX, awayZ);
                }

                playerState.velY = ARMS_WALL_KICK_VEL_Y;
                playerState.playerAngle = armsWallJumpAngle;
                armsIsWallJumping = true;

                // Lock movement and rotation during wall jump arc
                playerState.canMove = false;
                playerState.canRotate = false;

                playerState.wallHitTimer = 0;

                // Reset double jump so we can glide after wall kick
                armsHasDoubleJumped = false;
                armsIsGliding = false;
                playerState.isGliding = false;

                // Restart jump animation
                attach_anim_if_different(&armsAnimJump, &torsoSkel);
                t3d_anim_set_time(&armsAnimJump, 0.0f);
                t3d_anim_set_playing(&armsAnimJump, true);

                // Spawn dust particles at wall
                spawn_dust_particles(cubeX - awayX * 5.0f, cubeY, cubeZ - awayZ * 5.0f, 3);

                debugf("Arms wall kick! Away: %.2f, %.2f\n", awayX, awayZ);
            }
            // Check for double jump / glide (B button in air) - spin attack animation
            // Can cancel wall jump into spin attack
            else if (pressed.b && !armsHasDoubleJumped && !armsIsGliding) {
                armsHasDoubleJumped = true;
                armsIsGliding = true;
                playerState.isGliding = true;  // Enable reduced gravity
                armsSpinTime = 0.0f;
                // Cancel wall jump state - regain control
                armsIsWallJumping = false;
                playerState.canMove = true;
                playerState.canRotate = true;
                // Give a small upward boost and forward momentum
                playerState.velY = 2.0f;
                // Add forward momentum in facing direction
                float forwardBoost = 4.0f;
                playerState.velX += sinf(playerState.playerAngle) * forwardBoost;
                playerState.velZ += cosf(playerState.playerAngle) * forwardBoost;
                // Play spin animation
                attach_anim_if_different(&armsAnimAtkSpin, &torsoSkel);
                t3d_anim_set_time(&armsAnimAtkSpin, 0.0f);
                t3d_anim_set_playing(&armsAnimAtkSpin, true);
            }
            // Arms jump animation (when not gliding)
            else if (!armsIsGliding && armsAnimJump.animRef) {
                if (skeleton_is_valid(&torsoSkel) && anim_is_safe_to_update(&armsAnimJump)) t3d_anim_update(&armsAnimJump, deltaTime);
            }
        }
        else if (isLanding) {
            // Arms landing animation
            // Allow canceling landing with jump (check A button during landing)
            bool wantsJump = (pressed.a || jumpBufferTimer > 0.0f || held.a);
            bool canJump = (playerState.canJump || isLanding); // Allow jump even during landing

            if (wantsJump && canJump) {
                // Cancel landing and jump again!
                isLanding = false;
                isJumping = true;
                playerState.velY = 8.0f;
                playerState.isGrounded = false;
                playerState.canJump = false;
                armsHasDoubleJumped = false;
                jumpBufferTimer = 0.0f;
                coyoteTimer = 0.0f;

                // Spawn dust particles on jump-cancel
                spawn_dust_particles(cubeX, cubeY, cubeZ, 2);

                attach_anim_if_different(&armsAnimJump, &torsoSkel);
                t3d_anim_set_time(&armsAnimJump, 0.0f);
                t3d_anim_set_playing(&armsAnimJump, true);
            } else if (armsAnimJumpLand.animRef) {
                // Continue playing landing animation
                playerState.canJump = true;  // Keep canJump true so we can cancel
                if (skeleton_is_valid(&torsoSkel) && anim_is_safe_to_update(&armsAnimJumpLand)) t3d_anim_update(&armsAnimJumpLand, deltaTime);
                // Check if player wants to move - cancel landing animation (same as torso mode)
                joypad_inputs_t aimInputs = get_game_inputs();
                float rawAimX = -apply_deadzone(aimInputs.stick_x / 128.0f);
                float rawAimY = apply_deadzone(aimInputs.stick_y / 128.0f);
                if (rawAimX != 0.0f || rawAimY != 0.0f) {
                    // Cancel landing animation if player moves
                    isLanding = false;
                    t3d_anim_set_playing(&armsAnimJumpLand, false);
                    playerState.canMove = true;
                    playerState.canRotate = true;
                    playerState.canJump = true;
                }
                else if (!armsAnimJumpLand.isPlaying) {
                    isLanding = false;
                    playerState.canMove = true;
                    playerState.canRotate = true;
                    playerState.canJump = true;
                }
            } else {
                isLanding = false;
            }
        }
        // === NORMAL STATES ===
        else {
            // Start spin attack (B button)
            if (pressed.b && playerState.isGrounded && !armsIsSpinning && !armsIsWhipping) {
                armsIsSpinning = true;
                armsSpinTime = 0.0f;
                playerState.canMove = true;   // Allow movement during spin
                playerState.canRotate = true; // Allow rotation during spin
                attach_anim_if_different(&armsAnimAtkSpin, &torsoSkel);
                t3d_anim_set_time(&armsAnimAtkSpin, 0.0f);
                t3d_anim_set_playing(&armsAnimAtkSpin, true);
            }
            // Whip attack removed
            // Jump (A button) - simple jump, not charge-based
            // Support coyote time and jump buffer
            else if ((pressed.a || jumpBufferTimer > 0.0f) && (playerState.isGrounded || coyoteTimer > 0.0f) && (playerState.canJump || isLanding)) {
                // Jump initiated
                isLanding = false;  // Cancel landing animation if active
                isJumping = true;
                isLanding = false;  // Cancel landing animation if active (3)
                playerState.velY = 8.0f;  // Arms mode jump (lower than other modes)
                wav64_play(&sfxJumpSound, 2);  // Play jump sound on channel 2
                playerState.isGrounded = false;
                playerState.canJump = false;
                armsHasDoubleJumped = false;  // Reset double jump when starting new jump
                armsIsGliding = false;
                coyoteTimer = 0.0f;      // Consume coyote time
                jumpBufferTimer = 0.0f;  // Consume buffer
                jumpPeakY = cubeY;  // Track peak for landing squash
                attach_anim_if_different(&armsAnimJump, &torsoSkel);
                t3d_anim_set_time(&armsAnimJump, 0.0f);
                t3d_anim_set_playing(&armsAnimJump, true);
            }
            // Sliding on steep slope
            else if (playerState.isSliding && playerState.isGrounded) {
                if (armsAnimSlide.animRef) {
                    attach_anim_if_different(&armsAnimSlide, &torsoSkel);
                    if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&armsAnimSlide, deltaTime);
                }
                // Turn to face downhill direction
                float slopeDirX = playerState.slopeNormalX;
                float slopeDirZ = playerState.slopeNormalZ;
                float targetAngle = atan2f(-slopeDirX, slopeDirZ);
                float angleDiff = targetAngle - playerState.playerAngle;
                while (angleDiff > 3.14159265f) angleDiff -= 6.28318530f;
                while (angleDiff < -3.14159265f) angleDiff += 6.28318530f;
                const float SLIDE_TURN_SPEED = 8.0f;
                float maxTurn = SLIDE_TURN_SPEED * deltaTime;
                if (angleDiff > maxTurn) angleDiff = maxTurn;
                else if (angleDiff < -maxTurn) angleDiff = -maxTurn;
                playerState.playerAngle += angleDiff;
            }
            // Walking - 2 speeds based on stick magnitude
            else if (isMoving) {
                // Calculate stick magnitude for walk speed
                float stickMag = sqrtf(playerState.velX * playerState.velX + playerState.velZ * playerState.velZ);
                float walkThreshold = 3.0f;  // Threshold for fast walk

                if (stickMag > walkThreshold && armsAnimWalk2.animRef) {
                    // Fast walk
                    attach_anim_if_different(&armsAnimWalk2, &torsoSkel);
                    if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&armsAnimWalk2, deltaTime);
                } else if (armsAnimWalk1.animRef) {
                    // Slow walk
                    attach_anim_if_different(&armsAnimWalk1, &torsoSkel);
                    if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&armsAnimWalk1, deltaTime);
                }
            }
            // Idle
            else if (playerState.isGrounded) {
                attach_anim_if_different(&armsAnimIdle, &torsoSkel);
                if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&armsAnimIdle, deltaTime);
            }
            // In air (not jumping state)
            else {
                attach_anim_if_different(&armsAnimIdle, &torsoSkel);
                if (skeleton_is_valid(&torsoSkel)) t3d_anim_update(&armsAnimIdle, deltaTime);
            }
        }

        // Handle landing detection for arms mode
        if (isJumping && playerState.isGrounded) {
            armsHasDoubleJumped = false;  // Reset double jump on landing
            armsIsGliding = false;
            armsIsWallJumping = false;    // Reset wall jump on landing
            playerState.isGliding = false;  // Disable reduced gravity
            playerState.canMove = true;   // Restore movement on landing
            playerState.canRotate = true; // Restore rotation on landing

            // Check for buffered jump input - if player pressed/held A during landing window
            // Check BEFORE clearing isJumping to maintain state continuity
            if (jumpBufferTimer > 0.0f || held.a || pressed.a) {
                // Immediately start another jump (cancel landing animation)
                // Keep isJumping true
                isLanding = false;
                playerState.velY = 8.0f;
                playerState.isGrounded = false;
                playerState.canJump = false;
                armsHasDoubleJumped = false;
                jumpBufferTimer = 0.0f;  // Consume buffer
                coyoteTimer = 0.0f;      // Reset coyote timer

                // Spawn dust particles on buffered jump
                spawn_dust_particles(cubeX, cubeY, cubeZ, 2);

                attach_anim_if_different(&armsAnimJump, &torsoSkel);
                t3d_anim_set_time(&armsAnimJump, 0.0f);
                t3d_anim_set_playing(&armsAnimJump, true);
            } else {
                // Normal landing - enter landing state
                isJumping = false;
                isLanding = true;

                // Calculate fall distance and apply landing squash (same as torso)
                float fallDistance = jumpPeakY - cubeY;
                if (fallDistance < 0) fallDistance = 0;
                float squashAmount = fallDistance / 300.0f;  // 0.0 to 1.0 over 300 units
                if (squashAmount > 1.0f) squashAmount = 1.0f;
                landingSquash = squashAmount * 0.75f;  // 0.0 to 0.75
                chargeSquash = 0.0f;
                squashScale = 1.0f - landingSquash;
                squashVelocity = 0.0f;

                // Screen shake and impact stars on hard landings (200+ height)
                if (fallDistance > 200.0f) {
                    float shakeStrength = 6.0f + (fallDistance - 200.0f) / 50.0f;
                    if (shakeStrength > 15.0f) shakeStrength = 15.0f;
                    trigger_screen_shake(shakeStrength);
                    spawn_impact_stars();
                }

                // Spawn dust particles on landing
                int dustCount = 2 + (int)(squashAmount * 2);
                spawn_dust_particles(cubeX, cubeY, cubeZ, dustCount);

                if (armsAnimJumpLand.animRef) {
                    attach_anim_if_different(&armsAnimJumpLand, &torsoSkel);
                    t3d_anim_set_time(&armsAnimJumpLand, 0.0f);
                    t3d_anim_set_playing(&armsAnimJumpLand, true);
                } else {
                    isLanding = false;
                    playerState.canJump = true;
                }
            }
        }

        // Keep facing wall during wall jump
        if (armsIsWallJumping && !playerState.isGrounded) {
            playerState.playerAngle = armsWallJumpAngle;
        }
        // Also reset double jump if we land without being in isJumping state (e.g., walked off edge)
        else if (playerState.isGrounded && armsHasDoubleJumped) {
            armsHasDoubleJumped = false;
            armsIsGliding = false;
            playerState.isGliding = false;  // Disable reduced gravity
        }

        if (skeleton_is_valid(&torsoSkel)) {
            t3d_skeleton_update(&torsoSkel);
        }
    }
    // === FULLBODY MODE (PART_LEGS) - crouch mechanics ===
    else if (currentPart == PART_LEGS && fbHasAnims) {
        joypad_buttons_t pressed = get_game_buttons_pressed();
        joypad_buttons_t held = get_game_buttons_held();

        // Crouch constants
        const float LONG_JUMP_SPEED = 8.0f;      // Forward speed during long jump
        const float LONG_JUMP_HEIGHT = 9.0f;    // Vertical velocity for long jump
        const float BACKFLIP_HEIGHT = 14.0f;     // Higher vertical for backflip
        const float BACKFLIP_BACK_SPEED = 2.0f;  // Slight backward movement
        const float CROUCH_MOVE_MULT = 0.4f;     // Movement speed multiplier when crouching

        // Hover constants (A while crouching stationary - propeller jump)
        const float HOVER_WINDUP_TIME = 0.3f;           // Wind-up delay before launch
        const float HOVER_ASCENT_DURATION = 1.0f;       // Duration of ascent phase (seconds)
        const float HOVER_PROPULSION = 7.0f;            // Constant upward propulsion during ascent
        const float HOVER_HORIZONTAL_FACTOR = 1.0f;     // How much tilt converts to horizontal speed - 2x buff
        const float HOVER_FALL_SPEED = -2.5f;           // Slow spin descent speed
        const float HOVER_AIR_CONTROL = 0.5f;           // Horizontal control during descent
        const float HOVER_MAX_HORIZONTAL_SPEED = 6.0f;  // Max horizontal speed during descent
        const float HOVER_TILT_ANGLE_MAX = 45.0f;       // Max tilt from vertical (degrees) - increased from 35
        const float HOVER_TILT_LERP_SPEED = 8.0f;       // How fast tilt responds to stick
        const float HOVER_TILT_DECAY = 120.0f;          // Tilt decay rate when stick neutral (deg/s)
        const float HOVER_FACING_LERP_SPEED = 10.0f;    // How fast facing rotates toward tilt direction

        // === JUMP BUFFER (Fullbody Mode) ===
        // Don't trigger jump buffer during special jump states (long jump/hover already use the jump)
        if (pressed.a && !playerState.isGrounded && !isJumping && !fbIsLongJumping && !fbIsBackflipping && !fbIsCrouching && !fbIsHovering) {
            jumpBufferTimer = JUMP_BUFFER_TIME;
            debugf("DEBUG: JUMP BUFFER triggered (airborne A press)\n");
        }
        if (jumpBufferTimer > 0.0f) {
            jumpBufferTimer -= deltaTime;
        }

        // Wall jump grace period - wait a few frames after leaving ground
        #define FB_WALL_JUMP_GRACE_FRAMES 5
        static int fbFramesSinceGrounded = 0;
        if (playerState.isGrounded) {
            fbFramesSinceGrounded = 0;
        } else if (fbFramesSinceGrounded < 100) {
            fbFramesSinceGrounded++;
        }

        // Check crouch input (Z button)
        bool wantsCrouch = held.z;
        float currentSpeed = sqrtf(playerState.velX * playerState.velX + playerState.velZ * playerState.velZ);

        // DEBUG: Log all A presses in fullbody mode
        if (pressed.a) {
            debugf("DEBUG: A pressed - grounded=%d crouch=%d wantsCrouch=%d windUp=%d hover=%d speed=%.2f\n",
                   playerState.isGrounded, fbIsCrouching, wantsCrouch, fbCrouchJumpWindup, fbIsHovering, currentSpeed);
        }

        // Track crouch state for animation reset
        static bool fbWasCrouchingPrev = false;
        // Track slide state for rotation reset
        static bool fbWasSlidingPrev = false;

        // === DEATH STATE (highest priority) ===
        if (playerIsDead) {
            fbIsCrouching = false;
            fbIsLongJumping = false;
            fbIsBackflipping = false;
            fbIsHovering = false;
            fbHoverTime = 0.0f;
            fbHoverTiltX = 0.0f;
            fbHoverTiltZ = 0.0f;
            fbHoverTiltVelX = 0.0f;
            fbHoverTiltVelZ = 0.0f;
            fbIsSpinning = false;
            fbIsSpinningAir = false;
            fbSpinTime = 0.0f;
            fbIsCharging = false;
            fbChargeTime = 0.0f;
            fbIsCrouchAttacking = false;
            fbCrouchAttackTime = 0.0f;
            fbIsWallJumping = false;
            fbCrouchJumpWindup = false;
            fbCrouchJumpWindupTime = 0.0f;
            // Re-enable normal jump in controls.c
            disableNormalJump = (currentPart == PART_TORSO || currentPart == PART_ARMS);
            if (fbAnimDeath.animRef != NULL) {
                attach_anim_if_different(&fbAnimDeath, &torsoSkel);
                t3d_anim_update(&fbAnimDeath, deltaTime);
            }
        }
        // === HURT STATE ===
        else if (playerIsHurt) {
            fbIsCrouching = false;
            playerHurtAnimTime += deltaTime;

            // Check if pain animation finished
            if (playerCurrentPainAnim != NULL && playerCurrentPainAnim->animRef != NULL) {
                if (!playerCurrentPainAnim->isPlaying || playerHurtAnimTime > playerCurrentPainAnim->animRef->duration) {
                    playerIsHurt = false;
                    playerHurtAnimTime = 0.0f;
                    playerCurrentPainAnim = NULL;
                } else {
                    attach_anim_if_different(playerCurrentPainAnim, &torsoSkel);
                    t3d_anim_update(playerCurrentPainAnim, deltaTime);
                }
            } else {
                // No valid animation - end hurt state immediately
                playerIsHurt = false;
                playerHurtAnimTime = 0.0f;
                playerCurrentPainAnim = NULL;
            }
        }
        // === GROUND SPIN ATTACK (B while grounded, not crouching) ===
        else if (fbIsSpinning && playerState.isGrounded) {
            fbSpinTime += deltaTime;
            // Use animation duration or default 0.5s if no animation
            float spinDuration = fbAnimSpinAtk.animRef ? fbAnimSpinAtk.animRef->duration : 0.5f;

            if (fbAnimSpinAtk.animRef != NULL) {
                attach_anim_if_different(&fbAnimSpinAtk, &torsoSkel);
                t3d_anim_update(&fbAnimSpinAtk, deltaTime);
            }

            // Allow movement during first 55% of spin (like arms)
            float spinProgress = fbSpinTime / spinDuration;
            if (spinProgress < 0.55f) {
                playerState.canMove = true;
                playerState.canRotate = true;
            } else {
                playerState.canMove = false;
                playerState.canRotate = false;
                playerState.velX *= 0.8f;
                playerState.velZ *= 0.8f;
            }

            // End spin when animation/duration finishes
            if (fbSpinTime >= spinDuration) {
                fbIsSpinning = false;
                fbSpinTime = 0.0f;
                playerState.canMove = true;
                playerState.canRotate = true;
                playerState.velX = 0.0f;
                playerState.velZ = 0.0f;
            }

            // Spawn electricity particles during spin
            static int fbSpinElectricFrame = 0;
            fbSpinElectricFrame++;
            if (fbSpinElectricFrame >= 3) {
                spawn_electric_particles(cubeX, cubeY + 10.0f, cubeZ, 2);
                fbSpinElectricFrame = 0;
            }
        }
        // === AIR SPIN ATTACK (B while airborne) ===
        else if (fbIsSpinningAir && !playerState.isGrounded) {
            fbSpinTime += deltaTime;
            // Use animation duration or default 0.6s if no animation
            float airSpinDuration = fbAnimSpinAir.animRef ? fbAnimSpinAir.animRef->duration : 0.6f;

            // Cap fall speed for slow descent (don't reduce gravity - just limit fall speed)
            const float FB_AIR_SPIN_MAX_FALL = -3.0f;
            if (playerState.velY < FB_AIR_SPIN_MAX_FALL) {
                playerState.velY = FB_AIR_SPIN_MAX_FALL;
            }

            if (fbAnimSpinAir.animRef != NULL) {
                attach_anim_if_different(&fbAnimSpinAir, &torsoSkel);
                t3d_anim_update(&fbAnimSpinAir, deltaTime);
            }
            // Electricity particles
            static int fbAirSpinElectricFrame = 0;
            fbAirSpinElectricFrame++;
            if (fbAirSpinElectricFrame >= 3) {
                spawn_electric_particles(cubeX, cubeY + 10.0f, cubeZ, 2);
                fbAirSpinElectricFrame = 0;
            }
            // End on landing or animation/duration finish
            if (playerState.isGrounded) {
                fbIsSpinningAir = false;
                fbSpinTime = 0.0f;
            } else if (fbSpinTime >= airSpinDuration) {
                fbIsSpinningAir = false;
                fbSpinTime = 0.0f;
            }
        }
        // === SPIN CHARGE (Z + holding B) - grounded only ===
        // Cancel charge if player leaves the ground
        else if (fbIsCharging && !playerState.isGrounded) {
            fbIsCharging = false;
            fbChargeTime = 0.0f;
            playerState.canMove = true;
            playerState.canRotate = true;
            debugf("FB: Spin charge canceled (left ground)\n");
        }
        else if (fbIsCharging && playerState.isGrounded) {
            fbChargeTime += deltaTime;
            if (fbAnimSpinCharge.animRef != NULL) {
                attach_anim_if_different(&fbAnimSpinCharge, &torsoSkel);
                // Only update if animation hasn't finished (hold last frame)
                float animTime = t3d_anim_get_time(&fbAnimSpinCharge);
                if (animTime < fbAnimSpinCharge.animRef->duration) {
                    t3d_anim_update(&fbAnimSpinCharge, deltaTime);
                }
            }
            // Electricity particles build up (more intense the longer you charge)
            static int fbChargeElectricFrame = 0;
            fbChargeElectricFrame++;
            int particleCount = 3 + (int)(fbChargeTime * 2.0f);  // More particles over time
            if (particleCount > 8) particleCount = 8;
            if (fbChargeElectricFrame >= 2) {
                spawn_electric_particles(cubeX, cubeY + 10.0f, cubeZ, particleCount);
                fbChargeElectricFrame = 0;
            }
            // Stop movement while charging
            playerState.velX = 0.0f;
            playerState.velZ = 0.0f;
            // Release B = release charged spin attack!
            if (!held.b) {
                fbIsCharging = false;
                float chargedTime = fbChargeTime;  // Save before reset
                fbChargeTime = 0.0f;
                // Trigger spin attack on release
                fbIsSpinning = true;
                fbSpinTime = 0.0f;
                fbIsCrouching = false;
                playerState.canMove = true;
                playerState.canRotate = true;
                if (fbAnimSpinAtk.animRef != NULL) {
                    attach_anim_if_different(&fbAnimSpinAtk, &torsoSkel);
                    t3d_anim_set_time(&fbAnimSpinAtk, 0.0f);
                    t3d_anim_set_playing(&fbAnimSpinAtk, true);
                }
                // Big burst of electricity on release
                spawn_electric_particles(cubeX, cubeY + 10.0f, cubeZ, 10 + (int)(chargedTime * 5.0f));
                debugf("FB: Spin charge released! (charged %.2fs)\n", chargedTime);
            }
        }
        // === CROUCH ATTACK (crouch + C-down) ===
        else if (fbIsCrouchAttacking) {
            fbCrouchAttackTime += deltaTime;
            if (fbAnimCrouchAttack.animRef != NULL) {
                attach_anim_if_different(&fbAnimCrouchAttack, &torsoSkel);
                t3d_anim_update(&fbAnimCrouchAttack, deltaTime);
                // End when animation finishes
                if (fbCrouchAttackTime >= fbAnimCrouchAttack.animRef->duration) {
                    fbIsCrouchAttacking = false;
                    fbCrouchAttackTime = 0.0f;
                    fbIsCrouching = false;
                    playerState.canMove = true;
                    playerState.canRotate = true;
                }
            } else {
                // No animation, end attack immediately
                fbIsCrouchAttacking = false;
                fbCrouchAttackTime = 0.0f;
                fbIsCrouching = false;
                playerState.canMove = true;
                playerState.canRotate = true;
            }
            // Slow movement during attack
            playerState.velX *= 0.5f;
            playerState.velZ *= 0.5f;
        }
        // === SLIDING ON SLOPE ===
        else if (playerState.isSliding && playerState.isGrounded) {
            // Play slide animation while sliding down slopes
            idleFrames = 0;
            playingFidget = false;
            fbIsCrouching = false;
            playerState.canRotate = false;  // We handle rotation manually

            // Turn to face the sliding direction (same as torso mode)
            float slopeDirX = playerState.slopeNormalX;
            float slopeDirZ = playerState.slopeNormalZ;

            // Calculate target angle to face downhill direction
            float targetAngle = atan2f(-slopeDirX, slopeDirZ);

            // Lerp player angle toward target (smooth turn)
            float angleDiff = targetAngle - playerState.playerAngle;
            // Normalize angle difference to [-PI, PI]
            while (angleDiff > 3.14159265f) angleDiff -= 6.28318530f;
            while (angleDiff < -3.14159265f) angleDiff += 6.28318530f;

            // Lerp speed (radians per second)
            const float SLIDE_TURN_SPEED = 8.0f;
            float maxTurn = SLIDE_TURN_SPEED * deltaTime;
            if (angleDiff > maxTurn) angleDiff = maxTurn;
            else if (angleDiff < -maxTurn) angleDiff = -maxTurn;

            playerState.playerAngle += angleDiff;
            // Normalize final angle
            while (playerState.playerAngle > 3.14159265f) playerState.playerAngle -= 6.28318530f;
            while (playerState.playerAngle < -3.14159265f) playerState.playerAngle += 6.28318530f;

            if (fbAnimSlide.animRef != NULL) {
                attach_anim_if_different(&fbAnimSlide, &torsoSkel);
                t3d_anim_update(&fbAnimSlide, deltaTime);
            } else if (fbAnimCrouch.animRef != NULL) {
                // Fallback to crouch animation if no slide anim
                attach_anim_if_different(&fbAnimCrouch, &torsoSkel);
                t3d_anim_update(&fbAnimCrouch, deltaTime);
            }
        }
        // === LONG JUMP / BACKFLIP IN AIR ===
        else if ((fbIsLongJumping || fbIsBackflipping) && !playerState.isGrounded) {
            // B button during long jump = cancel into spin/glide!
            if (pressed.b && fbIsLongJumping && !fbIsSpinningAir) {
                fbIsLongJumping = false;
                fbLongJumpSpeed = 0.0f;
                fbIsSpinningAir = true;
                fbSpinTime = 0.0f;
                playerState.canMove = true;
                playerState.canRotate = true;
                if (fbAnimSpinAir.animRef != NULL) {
                    attach_anim_if_different(&fbAnimSpinAir, &torsoSkel);
                    t3d_anim_set_time(&fbAnimSpinAir, 0.0f);
                    t3d_anim_set_playing(&fbAnimSpinAir, true);
                }
                debugf("FB: Long jump canceled into air spin!\n");
            }
            // Use dedicated long jump animation for long jumps
            else if (fbIsLongJumping && fbAnimLongJump.animRef != NULL) {
                attach_anim_if_different(&fbAnimLongJump, &torsoSkel);
                t3d_anim_update(&fbAnimLongJump, deltaTime);
            }
            // Use crouch jump hover for backflips
            else if (fbAnimCrouchJumpHover.animRef != NULL) {
                attach_anim_if_different(&fbAnimCrouchJumpHover, &torsoSkel);
                t3d_anim_update(&fbAnimCrouchJumpHover, deltaTime);
            } else if (fbAnimJump.animRef != NULL) {
                // Fallback to regular jump anim
                attach_anim_if_different(&fbAnimJump, &torsoSkel);
                t3d_anim_update(&fbAnimJump, deltaTime);
            }

            // Long jump maintains forward momentum but decays from air friction
            // No wall jump during long jump - must commit to the arc or cancel with B
            if (fbIsLongJumping) {
                // Apply air friction to slow down the long jump speed
                const float LONG_JUMP_AIR_FRICTION = 0.97f;  // Per-frame decay (0.97 = ~3% per frame)
                fbLongJumpSpeed *= LONG_JUMP_AIR_FRICTION;

                // Below a threshold, end long jump and let normal air control take over
                if (fbLongJumpSpeed < 1.0f) {
                    fbIsLongJumping = false;
                    fbLongJumpSpeed = 0.0f;
                    playerState.canMove = true;
                    playerState.canRotate = true;
                } else {
                    // Still in long jump - apply decaying velocity in facing direction
                    float angle = playerState.playerAngle;
                    playerState.velX = -sinf(angle) * fbLongJumpSpeed;
                    playerState.velZ = cosf(angle) * fbLongJumpSpeed;
                }
            }
        }
        // === CROUCH JUMP WIND-UP (delay before launch) ===
        else if (fbCrouchJumpWindup) {
            fbCrouchJumpWindupTime += deltaTime;

            // Keep player firmly grounded during wind-up (no accidental velocity)
            playerState.velY = 0.0f;
            playerState.isGrounded = true;
            playerState.canMove = false;
            playerState.canRotate = false;

            // Z button cancels wind-up
            if (pressed.z) {
                fbCrouchJumpWindup = false;
                fbCrouchJumpWindupTime = 0.0f;
                playerState.canMove = true;
                playerState.canRotate = true;
                // Re-enable normal jump in controls.c
                disableNormalJump = (currentPart == PART_TORSO || currentPart == PART_ARMS);
                debugf("FB: Crouch jump wind-up CANCELED!\n");
            }
            // Update wind-up animation
            else if (fbAnimCrouchJump.animRef != NULL) {
                attach_anim_if_different(&fbAnimCrouchJump, &torsoSkel);
                t3d_anim_update(&fbAnimCrouchJump, deltaTime);
            }

            // After wind-up delay, actually launch!
            if (fbCrouchJumpWindupTime >= HOVER_WINDUP_TIME) {
                fbCrouchJumpWindup = false;
                fbCrouchJumpWindupTime = 0.0f;
                fbCrouchJumpRising = true;  // Now in ascent phase
                fbIsHovering = true;
                fbHoverTime = 0.0f;
                fbIsCrouching = false;
                isJumping = true;
                playerState.canMove = false;   // Control is via tilt, not normal movement
                playerState.canRotate = false;
                // Launch with initial upward velocity
                playerState.velX = 0.0f;
                playerState.velZ = 0.0f;
                playerState.velY = HOVER_PROPULSION;
                playerState.isGrounded = false;
                playerState.isGliding = true;  // Flag to disable gravity in controls_update
                // Reset tilt state
                fbHoverTiltX = 0.0f;
                fbHoverTiltZ = 0.0f;
                fbHoverTiltVelX = 0.0f;
                fbHoverTiltVelZ = 0.0f;
                debugf("FB: Hover ascent started!\n");
            }
        }
        // === HOVER STATE (Propeller jump with world-frame directional control) ===
        // Control scheme: matches normal movement (stick right = screen right)
        //   Stick Up    → World +Z (screen forward)
        //   Stick Down  → World -Z (screen backward)
        //   Stick Right → World -X (screen right)
        //   Stick Left  → World +X (screen left)
        // Player model faces, tilts, and travels in stick direction
        else if (fbIsHovering) {
            // Check for landing during descent first
            bool hoverEnded = false;
            if (!fbCrouchJumpRising && playerState.isGrounded) {
                // Landed during descent - end hover
                fbIsHovering = false;
                fbHoverTime = 0.0f;
                fbHoverTiltX = 0.0f;
                fbHoverTiltZ = 0.0f;
                playerState.canMove = true;
                playerState.canRotate = true;
                playerState.isGliding = false;
                disableNormalJump = (currentPart == PART_TORSO || currentPart == PART_ARMS);
                debugf("FB: Hover ended - landed\n");
                hoverEnded = true;
            }

            if (!hoverEnded) {
            fbHoverTime += deltaTime;

            // During ascent, override ground collision to prevent snapping back
            if (fbCrouchJumpRising) {
                playerState.isGrounded = false;  // Force airborne during ascent
            }

            // Read stick input
            joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
            float stickX = inputs.stick_x / 128.0f;
            float stickY = inputs.stick_y / 128.0f;

            // Apply deadzone
            if (fabsf(stickX) < 0.15f) stickX = 0.0f;
            if (fabsf(stickY) < 0.15f) stickY = 0.0f;

            // Convert stick to world-frame target direction
            // Match normal movement convention: -stickX for X, +stickY for Z
            // Stick right = screen right = -X in world coords
            float targetWorldX = -stickX;  // Right on stick = -X in world (matches normal movement)
            float targetWorldZ = stickY;   // Up on stick = +Z in world

            float stickMag = sqrtf(targetWorldX * targetWorldX + targetWorldZ * targetWorldZ);
            if (stickMag > 1.0f) {
                targetWorldX /= stickMag;
                targetWorldZ /= stickMag;
                stickMag = 1.0f;
            }

            // === ASCENT PHASE (fbCrouchJumpRising = true, duration: HOVER_ASCENT_DURATION) ===
            if (fbCrouchJumpRising) {
                // Check if ascent phase should end
                if (fbHoverTime >= HOVER_ASCENT_DURATION) {
                    fbCrouchJumpRising = false;  // Transition to descent
                    playerState.isGliding = false;  // Re-enable gravity
                    debugf("FB: Hover ascent ended, entering descent\n");
                } else {
                    // Keep gravity disabled during ascent
                    playerState.isGliding = true;

                    // Constant upward propulsion - compute total velocity magnitude
                    // Split between vertical and horizontal based on tilt
                    float tiltMag = sqrtf(fbHoverTiltX * fbHoverTiltX + fbHoverTiltZ * fbHoverTiltZ);
                    float tiltRatio = tiltMag / HOVER_TILT_ANGLE_MAX;  // 0 to 1
                    if (tiltRatio > 1.0f) tiltRatio = 1.0f;

                    // More tilt = more horizontal, less vertical
                    // But favor vertical: at max tilt, still 60% vertical
                    float horizontalFactor = tiltRatio * HOVER_HORIZONTAL_FACTOR;
                    float verticalFactor = 1.0f - (horizontalFactor * 0.6f);  // At max tilt: 0.76 vertical

                    // Apply propulsion split
                    playerState.velY = HOVER_PROPULSION * verticalFactor;

                    // Apply horizontal velocity in tilt direction (world-frame)
                    if (tiltMag > 0.5f) {
                        float tiltDirX = fbHoverTiltX / tiltMag;
                        float tiltDirZ = fbHoverTiltZ / tiltMag;
                        float horizSpeed = HOVER_PROPULSION * horizontalFactor;
                        playerState.velX = tiltDirX * horizSpeed;
                        playerState.velZ = tiltDirZ * horizSpeed;
                    } else {
                        // Minimal tilt - mostly vertical, little horizontal drift
                        playerState.velX *= 0.9f;  // Dampen any existing horizontal
                        playerState.velZ *= 0.9f;
                    }

                    // Update tilt based on stick input
                    if (stickMag > 0.01f) {
                        // Target tilt in world-frame
                        float targetTiltX = targetWorldX * HOVER_TILT_ANGLE_MAX * stickMag;
                        float targetTiltZ = targetWorldZ * HOVER_TILT_ANGLE_MAX * stickMag;

                        // Smooth tilt transition
                        float tiltLerpSpeed = HOVER_TILT_LERP_SPEED * deltaTime;
                        fbHoverTiltX += (targetTiltX - fbHoverTiltX) * tiltLerpSpeed;
                        fbHoverTiltZ += (targetTiltZ - fbHoverTiltZ) * tiltLerpSpeed;

                        // Face the direction of tilt/velocity (which uses targetWorldX directly)
                        // Since targetWorldX = -stickX, we use it directly (no extra negation)
                        float targetAngle = atan2f(targetWorldX, targetWorldZ);
                        float angleDiff = targetAngle - playerState.playerAngle;
                        // Normalize to [-PI, PI]
                        while (angleDiff > M_PI) angleDiff -= 2.0f * M_PI;
                        while (angleDiff < -M_PI) angleDiff += 2.0f * M_PI;

                        float rotSpeed = HOVER_FACING_LERP_SPEED * deltaTime;
                        if (fabsf(angleDiff) < rotSpeed) {
                            playerState.playerAngle = targetAngle;
                        } else {
                            playerState.playerAngle += (angleDiff > 0 ? rotSpeed : -rotSpeed);
                        }
                    } else {
                        // No stick input - decay tilt toward neutral
                        float decayRate = HOVER_TILT_DECAY * deltaTime;
                        if (fabsf(fbHoverTiltX) < decayRate) fbHoverTiltX = 0.0f;
                        else fbHoverTiltX -= (fbHoverTiltX > 0 ? decayRate : -decayRate);
                        if (fabsf(fbHoverTiltZ) < decayRate) fbHoverTiltZ = 0.0f;
                        else fbHoverTiltZ -= (fbHoverTiltZ > 0 ? decayRate : -decayRate);
                    }

                    // Play ascent animation (looping hover animation)
                    if (fbAnimCrouchJumpHover.animRef != NULL) {
                        attach_anim_if_different(&fbAnimCrouchJumpHover, &torsoSkel);
                        t3d_anim_update(&fbAnimCrouchJumpHover, deltaTime);
                    } else if (fbAnimJump.animRef != NULL) {
                        attach_anim_if_different(&fbAnimJump, &torsoSkel);
                        t3d_anim_update(&fbAnimJump, deltaTime);
                    }
                }
            }

            // === DESCENT PHASE (fbCrouchJumpRising = false) ===
            if (!fbCrouchJumpRising) {
                // Gravity is now active (isGliding = false)
                playerState.isGliding = false;

                // Z button cancels hover - fall normally
                if (pressed.z) {
                    fbIsHovering = false;
                    fbHoverTime = 0.0f;
                    fbHoverTiltX = 0.0f;
                    fbHoverTiltZ = 0.0f;
                    playerState.canMove = true;
                    playerState.canRotate = true;
                    // Re-enable normal jump in controls.c
                    disableNormalJump = (currentPart == PART_TORSO || currentPart == PART_ARMS);
                    debugf("FB: Hover canceled with Z!\n");
                }
                // No wall jump during hover - must commit to the descent
                {
                    // Normal descent - air control
                    playerState.velX += targetWorldX * HOVER_AIR_CONTROL * stickMag;
                    playerState.velZ += targetWorldZ * HOVER_AIR_CONTROL * stickMag;

                    // Cap horizontal speed during descent
                    float hSpeed = sqrtf(playerState.velX * playerState.velX + playerState.velZ * playerState.velZ);
                    if (hSpeed > HOVER_MAX_HORIZONTAL_SPEED) {
                        float scale = HOVER_MAX_HORIZONTAL_SPEED / hSpeed;
                        playerState.velX *= scale;
                        playerState.velZ *= scale;
                    }

                    // Cap fall speed for slow spin descent
                    if (playerState.velY < HOVER_FALL_SPEED) {
                        playerState.velY = HOVER_FALL_SPEED;
                    }

                    // Update facing if stick is pushed
                    if (stickMag > 0.3f) {
                        // Descent uses opposite convention for some reason
                        float targetAngle = atan2f(-targetWorldX, targetWorldZ);
                        float angleDiff = targetAngle - playerState.playerAngle;
                        while (angleDiff > M_PI) angleDiff -= 2.0f * M_PI;
                        while (angleDiff < -M_PI) angleDiff += 2.0f * M_PI;
                        float rotSpeed = HOVER_FACING_LERP_SPEED * 0.5f * deltaTime;
                        if (fabsf(angleDiff) < rotSpeed) {
                            playerState.playerAngle = targetAngle;
                        } else {
                            playerState.playerAngle += (angleDiff > 0 ? rotSpeed : -rotSpeed);
                        }
                    }

                    // Decay tilt during descent
                    float decayRate = HOVER_TILT_DECAY * deltaTime;
                    if (fabsf(fbHoverTiltX) < decayRate) fbHoverTiltX = 0.0f;
                    else fbHoverTiltX -= (fbHoverTiltX > 0 ? decayRate : -decayRate);
                    if (fabsf(fbHoverTiltZ) < decayRate) fbHoverTiltZ = 0.0f;
                    else fbHoverTiltZ -= (fbHoverTiltZ > 0 ? decayRate : -decayRate);

                    // Play descent animation (looping hover animation)
                    if (fbAnimCrouchJumpHover.animRef != NULL) {
                        attach_anim_if_different(&fbAnimCrouchJumpHover, &torsoSkel);
                        t3d_anim_update(&fbAnimCrouchJumpHover, deltaTime);
                    } else if (fbAnimJump.animRef != NULL) {
                        attach_anim_if_different(&fbAnimJump, &torsoSkel);
                        t3d_anim_update(&fbAnimJump, deltaTime);
                    }
                }
            }
            }  // end if (!hoverEnded)
        }
        // === NORMAL JUMPING (not long jump/backflip) ===
        // Only enter jumping state when actually airborne AND not trying to crouch
        else if (!playerState.isGrounded && !wantsCrouch) {
            // Reset crouch state when airborne from normal jump
            if (!fbIsLongJumping && !fbIsBackflipping) {
                fbIsCrouching = false;
            }

            // === WALL KICK CHECK (A button near wall) ===
            // Grace period: wait a few frames after leaving ground before wall jump works
            bool fbPastGracePeriod = fbFramesSinceGrounded >= FB_WALL_JUMP_GRACE_FRAMES;
            if (playerState.wallHitTimer > 0 && pressed.a && !playerState.isGrounded && fbPastGracePeriod) {
                debugf("DEBUG: WALL KICK triggered (airborne)\n");
                // Wall kick! Fixed, consistent jump
                #define FB_WALL_KICK_VEL_Y 8.0f
                #define FB_WALL_KICK_VEL_XZ 4.0f

                float awayX = playerState.wallNormalX;
                float awayZ = playerState.wallNormalZ;

                // Reflect player's facing angle across wall normal (same as arms mode)
                float facingX = -sinf(playerState.playerAngle);
                float facingZ = cosf(playerState.playerAngle);
                float dot = facingX * awayX + facingZ * awayZ;
                float reflectedX = facingX - 2.0f * dot * awayX;
                float reflectedZ = facingZ - 2.0f * dot * awayZ;

                // Normalize and blend with pure away direction for more consistent bouncing
                float len = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
                if (len > 0.01f) {
                    reflectedX /= len;
                    reflectedZ /= len;
                    // Blend 50% reflected + 50% away
                    reflectedX = reflectedX * 0.5f + awayX * 0.5f;
                    reflectedZ = reflectedZ * 0.5f + awayZ * 0.5f;
                    // Re-normalize
                    len = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
                    if (len > 0.01f) {
                        reflectedX /= len;
                        reflectedZ /= len;
                    }
                    fbWallJumpAngle = atan2f(-reflectedX, reflectedZ);
                } else {
                    fbWallJumpAngle = atan2f(-awayX, awayZ);
                }

                playerState.velY = FB_WALL_KICK_VEL_Y;
                playerState.velX = reflectedX * FB_WALL_KICK_VEL_XZ;
                playerState.velZ = reflectedZ * FB_WALL_KICK_VEL_XZ;
                playerState.playerAngle = fbWallJumpAngle;
                fbIsWallJumping = true;

                // Lock movement and rotation during wall jump arc
                playerState.canMove = false;
                playerState.canRotate = false;

                // Clear wall hit timer so we don't trigger multiple kicks
                playerState.wallHitTimer = 0;

                debugf("FB: Wall kick! angle=%.2f\n", fbWallJumpAngle);
            }

            // B while airborne = air spin attack!
            if (pressed.b && !fbIsSpinningAir && !fbIsHovering) {
                fbIsSpinningAir = true;
                fbSpinTime = 0.0f;
                if (fbAnimSpinAir.animRef != NULL) {
                    attach_anim_if_different(&fbAnimSpinAir, &torsoSkel);
                    t3d_anim_set_time(&fbAnimSpinAir, 0.0f);
                    t3d_anim_set_playing(&fbAnimSpinAir, true);
                }
                debugf("FB: Air spin attack!\n");
            }
            // Normal jump animation (when not air spinning)
            else if (!fbIsSpinningAir && fbAnimJump.animRef != NULL) {
                attach_anim_if_different(&fbAnimJump, &torsoSkel);
                if (!fbAnimJump.isPlaying) {
                    t3d_anim_set_time(&fbAnimJump, 0.0f);
                    t3d_anim_set_playing(&fbAnimJump, true);
                }
                float animTime = fbAnimJump.animRef ? t3d_anim_get_time(&fbAnimJump) : 0.0f;
                float animDuration = fbAnimJump.animRef ? fbAnimJump.animRef->duration : 1.0f;
                if (animTime < animDuration) {
                    t3d_anim_update(&fbAnimJump, deltaTime);
                }
            }
        }
        // === CROUCHING (grounded, holding Z) ===
        // Also stay in crouch if already crouching (handles grounded flickering)
        else if (wantsCrouch && (playerState.isGrounded || fbIsCrouching)) {
            // Reset crouch animation when first entering crouch
            if (!fbWasCrouchingPrev && fbAnimCrouch.animRef != NULL) {
                t3d_anim_set_time(&fbAnimCrouch, 0.0f);
                t3d_anim_set_playing(&fbAnimCrouch, true);
            }
            fbIsCrouching = true;
            idleFrames = 0;
            playingFidget = false;

            // A while crouching = crouch jump (long jump if moving, hover if still)
            if (pressed.a && !fbCrouchJumpWindup && !fbIsLongJumping) {
                debugf("DEBUG: CROUCH BLOCK A - speed=%.2f\n", currentSpeed);
                if (currentSpeed > 0.1f) {
                    // === LONG JUMP (crouch + A while moving) - handle immediately! ===
                    fbIsLongJumping = true;
                    fbIsCrouching = false;
                    isJumping = true;
                    playerState.isGrounded = false;
                    playerState.velY = LONG_JUMP_HEIGHT;

                    // Reset long jump animation to start and ensure it's playing
                    if (fbAnimLongJump.animRef != NULL) {
                        t3d_anim_set_time(&fbAnimLongJump, 0.0f);
                        t3d_anim_set_playing(&fbAnimLongJump, true);
                    }

                    // Launch in facing direction (facing uses -sinf for X, +cosf for Z)
                    const float LONG_JUMP_LAUNCH_SPEED = 6.0f;
                    fbLongJumpSpeed = LONG_JUMP_LAUNCH_SPEED;
                    float angle = playerState.playerAngle;
                    playerState.velX = -sinf(angle) * LONG_JUMP_LAUNCH_SPEED;
                    playerState.velZ = cosf(angle) * LONG_JUMP_LAUNCH_SPEED;

                    // Dust particles on launch
                    spawn_dust_particles(cubeX, cubeY, cubeZ, 4);

                    // Play crouch jump animation
                    if (fbAnimCrouchJump.animRef != NULL) {
                        attach_anim_if_different(&fbAnimCrouchJump, &torsoSkel);
                        t3d_anim_set_time(&fbAnimCrouchJump, 0.0f);
                        t3d_anim_set_playing(&fbAnimCrouchJump, true);
                    }
                    debugf("FB: LONG JUMP from crouch! speed=%.2f\n", currentSpeed);
                } else {
                    // === HOVER (crouch + A while still) - start wind-up ===
                    fbCrouchJumpWindup = true;
                    fbCrouchJumpWindupTime = 0.0f;
                    playerState.canMove = false;
                    playerState.canRotate = false;
                    // Keep player grounded immediately (don't wait for wind-up handler next frame)
                    playerState.velY = 0.0f;
                    playerState.isGrounded = true;
                    // Disable normal jump in controls.c (legs mode check)
                    disableNormalJump = true;
                    // Start playing fb_crouch_jump animation during wind-up
                    if (fbAnimCrouchJump.animRef != NULL) {
                        attach_anim_if_different(&fbAnimCrouchJump, &torsoSkel);
                        t3d_anim_set_time(&fbAnimCrouchJump, 0.0f);
                        t3d_anim_set_playing(&fbAnimCrouchJump, true);
                    }
                    debugf("FB: Crouch jump wind-up started!\n");
                }
            }
            // Z+C-left while crouching = crouch attack!
            else if (held.z && pressed.c_left && !fbIsCrouchAttacking) {
                fbIsCrouchAttacking = true;
                fbCrouchAttackTime = 0.0f;
                playerState.canMove = false;
                playerState.canRotate = false;
                if (fbAnimCrouchAttack.animRef != NULL) {
                    attach_anim_if_different(&fbAnimCrouchAttack, &torsoSkel);
                    t3d_anim_set_time(&fbAnimCrouchAttack, 0.0f);
                    t3d_anim_set_playing(&fbAnimCrouchAttack, true);
                }
                debugf("FB: Crouch attack!\n");
            }
            // Z + holding B = start spin charge
            else if (held.b && !fbIsCharging && !fbIsCrouchAttacking) {
                fbIsCharging = true;
                fbChargeTime = 0.0f;
                playerState.canMove = false;
                playerState.canRotate = false;
                // Attach and reset the spin charge animation
                if (fbAnimSpinCharge.animRef != NULL) {
                    attach_anim_if_different(&fbAnimSpinCharge, &torsoSkel);
                    t3d_anim_set_time(&fbAnimSpinCharge, 0.0f);
                    t3d_anim_set_playing(&fbAnimSpinCharge, true);
                }
                debugf("FB: Spin charge started!\n");
            }
            else {
                // Disable movement input while crouching (but don't zero velocity - let physics settle)
                playerState.canMove = false;
                // Gentle deceleration - need to maintain speed for long jump
                playerState.velX *= 0.95f;
                playerState.velZ *= 0.95f;

                // Play crouch animation (play once and hold last frame)
                if (fbAnimCrouch.animRef != NULL) {
                    attach_anim_if_different(&fbAnimCrouch, &torsoSkel);
                    // Only update if animation hasn't finished
                    float animTime = t3d_anim_get_time(&fbAnimCrouch);
                    float animDuration = fbAnimCrouch.animRef->duration;
                    if (animTime < animDuration) {
                        t3d_anim_update(&fbAnimCrouch, deltaTime);
                    }
                    // else: hold last frame (don't update)
                }
            }
        }
        // === WALKING/RUNNING ===
        else if (isMoving) {
            fbIsCrouching = false;
            idleFrames = 0;
            playingFidget = false;

            // Ground spin attack (B while running)
            if (pressed.b && !fbIsSpinning) {
                fbIsSpinning = true;
                fbSpinTime = 0.0f;
                if (fbAnimSpinAtk.animRef != NULL) {
                    attach_anim_if_different(&fbAnimSpinAtk, &torsoSkel);
                    t3d_anim_set_time(&fbAnimSpinAtk, 0.0f);
                    t3d_anim_set_playing(&fbAnimSpinAtk, true);
                }
                debugf("FB: Ground spin attack!\n");
            }
            // Ninja run when speed buff active
            else if (buffSpeedTimer > 0.0f && currentSpeed > 3.0f && fbAnimRunNinja.animRef != NULL) {
                attach_anim_if_different(&fbAnimRunNinja, &torsoSkel);
                t3d_anim_update(&fbAnimRunNinja, deltaTime);
            }
            // Normal run/walk animations
            else if (currentSpeed > 3.0f && fbAnimRun.animRef != NULL) {
                attach_anim_if_different(&fbAnimRun, &torsoSkel);
                t3d_anim_update(&fbAnimRun, deltaTime);
            } else if (fbAnimWalk.animRef != NULL) {
                attach_anim_if_different(&fbAnimWalk, &torsoSkel);
                t3d_anim_update(&fbAnimWalk, deltaTime);
            }
        }
        // === IDLE STATE ===
        else {
            fbIsCrouching = false;
            // Fidget timer
            if (playingFidget && fbAnimWait.animRef != NULL) {
                fidgetPlayTime += deltaTime;
                float fidgetDuration = fbAnimWait.animRef->duration;
                if (fidgetPlayTime < fidgetDuration) {
                    t3d_anim_update(&fbAnimWait, deltaTime);
                } else {
                    playingFidget = false;
                    fidgetPlayTime = 0.0f;
                    idleFrames = 0;
                }
            }
            else if (idleFrames >= IDLE_WAIT_FRAMES && fbAnimWait.animRef != NULL) {
                playingFidget = true;
                fidgetPlayTime = 0.0f;
                idleFrames = 0;
                attach_anim_if_different(&fbAnimWait, &torsoSkel);
                t3d_anim_set_time(&fbAnimWait, 0.0f);
                t3d_anim_set_playing(&fbAnimWait, true);
            }
            else if (fbAnimIdle.animRef != NULL) {
                idleFrames++;
                attach_anim_if_different(&fbAnimIdle, &torsoSkel);
                t3d_anim_update(&fbAnimIdle, deltaTime);
            }
        }

        // Track jump peak for squash
        if (!playerState.isGrounded && playerState.velY > 0) {
            if (cubeY > jumpPeakY) jumpPeakY = cubeY;
        }

        // Detect landing for squash effect
        static bool fbWasAirborne = false;
        static bool fbJustLandedFromSpecial = false;  // Prevents immediate jump after landing from special jumps
        if (!playerState.isGrounded) {
            fbWasAirborne = true;
            fbJustLandedFromSpecial = false;  // Clear when airborne
        } else if (fbWasAirborne) {
            fbWasAirborne = false;
            isJumping = false;
            // Track if landing from special jump state (before clearing them)
            fbJustLandedFromSpecial = fbIsLongJumping || fbIsBackflipping || fbIsHovering || fbIsSpinningAir;
            if (fbJustLandedFromSpecial) {
                jumpBufferTimer = 0.0f;  // Clear jump buffer to prevent buffered jumps
            }
            // Reset long jump/backflip/hover state on landing
            fbIsLongJumping = false;
            fbIsBackflipping = false;
            fbLongJumpSpeed = 0.0f;
            fbIsHovering = false;
            fbHoverTime = 0.0f;
            fbHoverTiltX = 0.0f;
            fbHoverTiltZ = 0.0f;
            fbHoverTiltVelX = 0.0f;
            fbHoverTiltVelZ = 0.0f;
            fbCrouchJumpWindup = false;
            fbCrouchJumpWindupTime = 0.0f;
            fbCrouchJumpRising = false;
            fbIsSpinningAir = false;
            fbSpinTime = 0.0f;
            fbIsWallJumping = false;  // Reset wall jump on landing
            playerState.canMove = true;   // Restore movement on landing
            playerState.canRotate = true; // Restore rotation on landing
            playerState.isGliding = false;  // Ensure reduced gravity is off
            // Re-enable normal jump in controls.c (based on current body part)
            disableNormalJump = (currentPart == PART_TORSO || currentPart == PART_ARMS);

            float fallDistance = jumpPeakY - cubeY;
            if (fallDistance < 0) fallDistance = 0;
            float squashAmount = fallDistance / 300.0f;
            if (squashAmount > 1.0f) squashAmount = 1.0f;
            landingSquash = squashAmount * 0.75f;
            chargeSquash = 0.0f;
            squashScale = 1.0f - landingSquash;
            squashVelocity = 0.0f;

            if (fallDistance > 200.0f) {
                float shakeStrength = 6.0f + (fallDistance - 200.0f) / 50.0f;
                if (shakeStrength > 15.0f) shakeStrength = 15.0f;
                trigger_screen_shake(shakeStrength);
                spawn_impact_stars();
            }
            spawn_dust_particles(cubeX, cubeY, cubeZ, 2 + (int)(squashAmount * 2));
            jumpPeakY = cubeY;
        }

        // === JUMP INPUT ===
        // Skip entirely if crouching - crouch jumps are handled in the crouch block above
        // Also skip on the frame we land from special jumps (prevents "double jump" from held A)
        bool wantsJump = pressed.a || jumpBufferTimer > 0.0f;
        if (wantsJump && playerState.isGrounded && !isJumping && !fbIsLongJumping && !fbIsBackflipping && !fbCrouchJumpWindup && !fbIsHovering && !fbIsCrouching && !wantsCrouch && !fbJustLandedFromSpecial) {
            debugf("DEBUG: JUMP INPUT BLOCK entered! wantsJump=%d buffer=%.2f\n", wantsJump, jumpBufferTimer);
            jumpBufferTimer = 0.0f;
            jumpPeakY = cubeY;

            // Check if crouching for special jumps (but not if hover just launched)
            if (fbIsCrouching && !fbIsHovering) {
                debugf("DEBUG: JUMP INPUT - crouch special jump path\n");
                if (currentSpeed > 0.1f) {
                    // === LONG JUMP (crouch + A while moving at all) ===
                    fbIsLongJumping = true;
                    fbIsCrouching = false;
                    isJumping = true;
                    playerState.isGrounded = false;
                    playerState.velY = LONG_JUMP_HEIGHT;

                    // Reset long jump animation to start and ensure it's playing
                    if (fbAnimLongJump.animRef != NULL) {
                        t3d_anim_set_time(&fbAnimLongJump, 0.0f);
                        t3d_anim_set_playing(&fbAnimLongJump, true);
                    }

                    // Store forward speed and set direction (facing uses -sinf for X, +cosf for Z)
                    fbLongJumpSpeed = LONG_JUMP_SPEED;
                    float angle = playerState.playerAngle;
                    playerState.velX = -sinf(angle) * LONG_JUMP_SPEED;
                    playerState.velZ = cosf(angle) * LONG_JUMP_SPEED;

                    // Dust particles on launch
                    spawn_dust_particles(cubeX, cubeY, cubeZ, 4);

                    // Play crouch jump animation
                    if (fbAnimCrouchJump.animRef != NULL) {
                        attach_anim_if_different(&fbAnimCrouchJump, &torsoSkel);
                        t3d_anim_set_time(&fbAnimCrouchJump, 0.0f);
                        t3d_anim_set_playing(&fbAnimCrouchJump, true);
                    }
                    debugf("LONG JUMP! speed=%.2f\n", LONG_JUMP_SPEED);
                } else {
                    // === BACKFLIP (crouch + A while still) - DISABLED, hover jump handles this ===
                    // fbIsBackflipping = true;
                    // fbIsCrouching = false;
                    // isJumping = true;
                    // playerState.isGrounded = false;
                    // playerState.velY = BACKFLIP_HEIGHT;

                    // // Move slightly backward
                    // float angle = playerState.playerAngle;
                    // playerState.velX = -sinf(angle) * BACKFLIP_BACK_SPEED;
                    // playerState.velZ = cosf(angle) * BACKFLIP_BACK_SPEED;

                    // // Play crouch jump animation
                    // if (fbAnimCrouchJump.animRef != NULL) {
                    //     attach_anim_if_different(&fbAnimCrouchJump, &torsoSkel);
                    //     t3d_anim_set_time(&fbAnimCrouchJump, 0.0f);
                    //     t3d_anim_set_playing(&fbAnimCrouchJump, true);
                    // }
                    // debugf("BACKFLIP!\n");
                }
            } else {
                // === NORMAL JUMP ===
                debugf("DEBUG: NORMAL JUMP triggered! velY=%.2f\n", controlConfig.jumpForce);
                isJumping = true;
                playerState.isGrounded = false;
                playerState.velY = controlConfig.jumpForce;
                wav64_play(&sfxJumpSound, 2);  // Play jump sound on channel 2
            }
        }

        // Update crouch tracking for animation reset
        bool isCurrentlyCrouching = wantsCrouch && playerState.isGrounded;
        if (fbWasCrouchingPrev && !isCurrentlyCrouching) {
            // Restore movement when exiting crouch
            playerState.canMove = true;
        }
        fbWasCrouchingPrev = isCurrentlyCrouching;

        // Reset rotation when exiting slide
        bool isCurrentlySliding = playerState.isSliding && playerState.isGrounded;
        if (fbWasSlidingPrev && !isCurrentlySliding) {
            playerState.canRotate = true;  // Restore rotation control
        }
        fbWasSlidingPrev = isCurrentlySliding;

        // Keep facing wall during wall jump (same as arms mode)
        if (fbIsWallJumping && !playerState.isGrounded) {
            playerState.playerAngle = fbWallJumpAngle;
        }

        if (skeleton_is_valid(&torsoSkel)) {
            t3d_skeleton_update(&torsoSkel);
        }
    }
    // === HEAD FALLBACK (basic squash support) ===
    else if (currentPart == PART_HEAD) {
        // HEAD mode uses torso model with head_ animations (minimal movement)
        if (!playerState.isGrounded && playerState.velY > 0) {
            if (cubeY > jumpPeakY) jumpPeakY = cubeY;
        }

        static bool headWasAirborne = false;
        if (!playerState.isGrounded) {
            headWasAirborne = true;
        } else if (headWasAirborne) {
            headWasAirborne = false;
            float fallDistance = jumpPeakY - cubeY;
            if (fallDistance < 0) fallDistance = 0;
            float squashAmount = fallDistance / 300.0f;
            if (squashAmount > 1.0f) squashAmount = 1.0f;
            landingSquash = squashAmount * 0.75f;
            chargeSquash = 0.0f;
            squashScale = 1.0f - landingSquash;
            squashVelocity = 0.0f;

            if (fallDistance > 200.0f) {
                float shakeStrength = 6.0f + (fallDistance - 200.0f) / 50.0f;
                if (shakeStrength > 15.0f) shakeStrength = 15.0f;
                trigger_screen_shake(shakeStrength);
                spawn_impact_stars();
            }
            spawn_dust_particles(cubeX, cubeY, cubeZ, 2 + (int)(squashAmount * 2));
            jumpPeakY = cubeY;
        }

        if (torsoHasAnims && skeleton_is_valid(&torsoSkel)) {
            t3d_skeleton_update(&torsoSkel);
        }
    }
    // === PART_LEGS without animations loaded ===
    else if (currentPart == PART_LEGS && !fbHasAnims) {
        static bool fbNoAnimDebug = false;
        if (!fbNoAnimDebug) {
            debugf("WARNING: LEGS mode but fbHasAnims=false! Check animation loading.\n");
            fbNoAnimDebug = true;
        }
        // Still update skeleton if available
        if (skeleton_is_valid(&torsoSkel)) {
            t3d_skeleton_update(&torsoSkel);
        }
    }
    // Debug: catch case where ARMS mode is set but animations aren't loaded
    else if (currentPart == PART_ARMS && !armsHasAnims) {
        static bool armsNoAnimDebug = false;
        if (!armsNoAnimDebug) {
            debugf("WARNING: ARMS mode but armsHasAnims=false! Check animation loading.\n");
            armsNoAnimDebug = true;
        }
    }
    uint32_t perfAnimTime = get_ticks() - perfAnimStart;

    // Update decorations (set player position for AI, then update)
    // Skip during transitions to prevent accessing partially-cleared decoration data
    uint32_t perfStart = get_ticks();
    HEAP_CHECK("pre_deco_update");
    if (!isTransitioning) {
        map_set_player_pos(&mapRuntime, cubeX, cubeY, cubeZ);
        mapRuntime.playerVelY = playerState.velY;  // For platform detection

        // Reset oil puddle state before decoration updates (decorations will set it true)
        mapRuntime.playerOnOil = false;

        map_update_decorations(&mapRuntime, deltaTime);
    }
    HEAP_CHECK("post_deco_update");

    // Update level 3 special decoration UV offsets
    // level3_special_update removed

    // === CHARGEPAD BUFF VISUAL ===
    // (Particles removed - buff is now indicated by player glow/tint instead)

    // === SPIN ATTACK HITBOX CHECK ===
    // Decrement spin hit cooldown
    if (spinHitCooldown > 0.0f) {
        spinHitCooldown -= deltaTime;
    }

    // Reset hit tracking when not spinning (so next spin can hit same enemies again)
    if (!armsIsSpinning && !armsIsGliding && !fbIsSpinning && !fbIsSpinningAir && !fbIsCrouchAttacking) {
        spinHitEnemies = 0;
    }

    // Check if player is spinning (ground or air) or crouch attacking and damage nearby enemies
    bool isSpinAttacking = (armsIsSpinning || armsIsGliding) && currentPart == PART_ARMS;
    isSpinAttacking = isSpinAttacking || ((fbIsSpinning || fbIsSpinningAir || fbIsCrouchAttacking) && currentPart == PART_LEGS);
    if (isSpinAttacking) {
        // Simple horizontal radius check - arms spin horizontally
        // Must be larger than slime collision radius (~51 units) to hit them
        float spinRadius = 55.0f;  // Horizontal reach

        for (int i = 0; i < mapRuntime.decoCount && i < MAX_DECORATIONS; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            if (!deco->active) continue;

            // Check slimes (both regular and lava variants)
            if (deco->type == DECO_SLIME || deco->type == DECO_SLIME_LAVA) {
                // Skip invincible slimes (just spawned from parent)
                if (deco->state.slime.invincibleTimer > 0.0f) continue;
                // Skip slimes already dying
                if (deco->state.slime.isDying) continue;
                // Skip if already hit this spin (prevents multi-hit)
                if (i < 64 && (spinHitEnemies & (1ULL << i))) continue;

                float dx = deco->posX - cubeX;
                float dz = deco->posZ - cubeZ;

                // Slime center is at base (slimes are short and squat)
                float slimeCenterY = deco->posY + 2.0f * deco->scaleY;
                float playerCenterY = cubeY + 5.0f;  // Player center (arms are short)
                float dy = slimeCenterY - playerCenterY;

                // Horizontal distance check + vertical range scaled by slime size
                float distSq = dx * dx + dz * dz;
                float verticalRange = 10.0f + 5.0f * deco->scaleY;  // Flatter hitbox

                if (distSq < spinRadius * spinRadius && dy > -verticalRange && dy < verticalRange) {
                    // Hit! Gentle knockback (no vertical pop)
                    float knockDist = sqrtf(dx * dx + dz * dz);
                    if (knockDist > 0.1f) {
                        float knockX = dx / knockDist;
                        float knockZ = dz / knockDist;
                        deco->state.slime.velX = knockX * 1.5f;
                        deco->state.slime.velZ = knockZ * 1.5f;
                        deco->state.slime.velY = 0.0f;  // No vertical pop
                    }

                    // Violent jiggle
                    deco->state.slime.jiggleVelX += ((rand() % 400) - 200) / 25.0f;
                    deco->state.slime.jiggleVelZ += ((rand() % 400) - 200) / 25.0f;
                    deco->state.slime.stretchY = 0.5f;  // Big squash
                    deco->state.slime.stretchVelY = -4.0f;

                    // Damage slime
                    deco->state.slime.health--;

                    // Force AI update next frame (deferred AI processing)
                    deco_force_update(deco);

                    // Spawn oil particles
                    spawn_oil_particles(deco->posX, deco->posY, deco->posZ, 15);

                    // Check if dead - start death animation
                    if (deco->state.slime.health <= 0) {
                        deco->state.slime.isDying = true;
                        deco->state.slime.deathTimer = 0.0f;
                        if (deco->scaleX >= 0.7f) {  // SLIME_SCALE_MEDIUM or larger
                            deco->state.slime.pendingSplit = true;
                        }
                        // Hitstop on slime kill
                        game_trigger_hitstop(0.12f);
                    }

                    // Mark this enemy as hit this spin (prevents multi-hit)
                    if (i < 64) spinHitEnemies |= (1ULL << i);

                    debugf("Spin hit slime %d! HP: %d\n", deco->id, deco->state.slime.health);
                }
            }

            // Check rats
            if (deco->type == DECO_RAT) {
                // Skip if already hit this spin (prevents multi-hit)
                if (i < 64 && (spinHitEnemies & (1ULL << i))) continue;

                float dx = deco->posX - cubeX;
                float dz = deco->posZ - cubeZ;
                float dy = deco->posY - cubeY;

                // Horizontal distance check + generous vertical range
                float distSq = dx * dx + dz * dz;
                float verticalRange = 30.0f;

                if (distSq < spinRadius * spinRadius && dy > -verticalRange && dy < verticalRange) {
                    // Kill rat instantly
                    deco->active = false;
                    spawn_oil_particles(deco->posX, deco->posY, deco->posZ, 8);
                    // Mark this enemy as hit this spin (prevents multi-hit)
                    if (i < 64) spinHitEnemies |= (1ULL << i);
                    // Hitstop on rat kill
                    game_trigger_hitstop(0.12f);
                    debugf("Spin killed rat %d!\n", deco->id);
                }
            }

            // Check security droids (vulnerable during sec_shoot animation only)
            if (deco->type == DECO_DROID_SEC && !deco->state.droid.isDead) {
                // Droids are only vulnerable while shooting (sec_shoot animation)
                bool isVulnerable = (deco->state.droid.shootAnimTimer > 0);
                
                // Skip if invulnerable (blocking) or already hit this spin
                if (!isVulnerable || (i < 64 && (spinHitEnemies & (1ULL << i)))) continue;

                float dx = deco->posX - cubeX;
                float dz = deco->posZ - cubeZ;
                float dy = deco->posY - cubeY;

                // Horizontal distance check + generous vertical range
                float distSq = dx * dx + dz * dz;
                float verticalRange = 30.0f;

                if (distSq < spinRadius * spinRadius && dy > -verticalRange && dy < verticalRange) {
                    // Hit droid! Reduce health
                    deco->state.droid.health--;

                    if (deco->state.droid.health <= 0) {
                        // Droid dies
                        deco->state.droid.isDead = true;
                        deco->state.droid.deathTimer = 0.0f;
                        spawn_oil_particles(deco->posX, deco->posY + 10.0f, deco->posZ, 12);
                        debugf("Spin killed droid %d!\n", deco->id);

                        // Trigger activation on death (if activationId is set)
                        if (deco->activationId > 0) {
                            activation_set(deco->activationId, true);
                            debugf("Droid %d death triggered activation %d\n", deco->id, deco->activationId);
                        }
                    } else {
                        // Droid takes damage - interrupt shoot animation
                        deco->state.droid.shootAnimTimer = 0.0f;
                        deco->state.droid.shootCooldown = DROID_SHOOT_COOLDOWN;  // Reset cooldown
                        deco->state.droid.hasFiredProjectile = false;
                        spawn_oil_particles(deco->posX, deco->posY + 10.0f, deco->posZ, 4);
                        debugf("Spin hit droid %d! HP: %d\n", deco->id, deco->state.droid.health);
                    }

                    // Mark as hit this spin
                    if (i < 64) spinHitEnemies |= (1ULL << i);
                    // Hitstop on hit
                    game_trigger_hitstop(0.12f);
                }
            }

            // Check sniper turrets
            if (deco->type == DECO_TURRET && !deco->state.turret.isDead) {
                // Skip if already hit this spin
                if (i < 64 && (spinHitEnemies & (1ULL << i))) continue;

                float dx = deco->posX - cubeX;
                float dz = deco->posZ - cubeZ;
                float dy = deco->posY - cubeY;

                float distSq = dx * dx + dz * dz;
                float verticalRange = 40.0f;

                if (distSq < spinRadius * spinRadius && dy > -verticalRange && dy < verticalRange) {
                    deco->state.turret.health--;

                    if (deco->state.turret.health <= 0) {
                        deco->state.turret.isDead = true;
                        deco->state.turret.isActive = false;
                        // Big explosion at multiple heights
                        spawn_oil_particles(deco->posX, deco->posY + 5.0f, deco->posZ, 15);
                        spawn_oil_particles(deco->posX, deco->posY + 20.0f, deco->posZ, 20);
                        spawn_oil_particles(deco->posX, deco->posY + 35.0f, deco->posZ, 15);
                        debugf("Spin destroyed turret %d!\n", deco->id);
                    } else {
                        spawn_oil_particles(deco->posX, deco->posY + 15.0f, deco->posZ, 8);
                        debugf("Spin hit turret %d! HP: %d\n", deco->id, deco->state.turret.health);
                    }

                    if (i < 64) spinHitEnemies |= (1ULL << i);
                    game_trigger_hitstop(0.12f);
                }
            }

            // Check pulse turrets
            if (deco->type == DECO_TURRET_PULSE && !deco->state.pulseTurret.isDead) {
                // Skip if already hit this spin
                if (i < 64 && (spinHitEnemies & (1ULL << i))) continue;

                float dx = deco->posX - cubeX;
                float dz = deco->posZ - cubeZ;
                float dy = deco->posY - cubeY;

                float distSq = dx * dx + dz * dz;
                float verticalRange = 40.0f;

                if (distSq < spinRadius * spinRadius && dy > -verticalRange && dy < verticalRange) {
                    deco->state.pulseTurret.health--;

                    if (deco->state.pulseTurret.health <= 0) {
                        deco->state.pulseTurret.isDead = true;
                        deco->state.pulseTurret.isActive = false;
                        // Big explosion at multiple heights
                        spawn_oil_particles(deco->posX, deco->posY + 5.0f, deco->posZ, 15);
                        spawn_oil_particles(deco->posX, deco->posY + 20.0f, deco->posZ, 20);
                        spawn_oil_particles(deco->posX, deco->posY + 35.0f, deco->posZ, 15);
                        debugf("Spin destroyed pulse turret %d!\n", deco->id);
                    } else {
                        spawn_oil_particles(deco->posX, deco->posY + 15.0f, deco->posZ, 8);
                        debugf("Spin hit pulse turret %d! HP: %d\n", deco->id, deco->state.pulseTurret.health);
                    }

                    if (i < 64) spinHitEnemies |= (1ULL << i);
                    game_trigger_hitstop(0.12f);
                }
            }
        }
    }

    // Copy oil puddle state from map runtime to player state
    playerState.onOilPuddle = mapRuntime.playerOnOil;

    // Apply conveyor belt push to player
    if (playerState.isGrounded && !debugFlyMode) {
        for (int i = 0; i < mapRuntime.decoCount; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            if (deco->active && deco->type == DECO_CONVEYERLARGE &&
                deco->state.conveyor.playerOnBelt) {
                // Push player along conveyor direction
                float pushSpeed = deco->state.conveyor.speed * deltaTime;
                cubeX += deco->state.conveyor.pushX * pushSpeed;
                cubeZ += deco->state.conveyor.pushZ * pushSpeed;
                break;  // Only apply one conveyor at a time
            }
        }
    }

    // Apply fan upward blow (vertical wind)
    // Player can still move horizontally while being blown up
    // Works for both DECO_FAN and DECO_FAN2
    if (!debugFlyMode) {
        for (int i = 0; i < mapRuntime.decoCount; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            bool isInFanStream = (deco->type == DECO_FAN && deco->state.fan.playerInStream) ||
                                 (deco->type == DECO_FAN2 && deco->state.fan2.playerInStream);
            if (deco->active && isInFanStream) {
                // Consistent lift: apply constant upward force until max height
                float fanTopY = deco->posY + 10.0f * deco->scaleY;
                float maxHeight = fanTopY + FAN_BLOW_HEIGHT * deco->scaleY;

                // Apply constant upward velocity (capped by max height)
                // FAN2 uses weaker force than FAN
                float blowForce = (deco->type == DECO_FAN2) ? FAN2_BLOW_FORCE : FAN_BLOW_FORCE;
                float targetVelY = blowForce * deco->scaleY;

                // Slow down as approaching max height for smooth stop
                float distToMax = maxHeight - cubeY;
                if (distToMax < 30.0f && distToMax > 0.0f) {
                    targetVelY *= (distToMax / 30.0f);  // Ease out near top
                }

                // Apply lift
                if (playerState.velY < targetVelY) {
                    playerState.velY += targetVelY * 0.2f;
                    if (playerState.velY > targetVelY) playerState.velY = targetVelY;
                }

                // Player is airborne when in fan stream
                playerState.isGrounded = false;
                isJumping = true;  // Use jump animation
                break;  // Only apply one fan at a time
            }
        }
    }

    uint32_t perfDecoUpdate = get_ticks() - perfStart;

    // Update particles (simple physics, no collision)
    update_particles(deltaTime);

    // Update death decals (fade out over time)
    update_death_decals(deltaTime);

    // Update impact stars (spinning around player's head)
    update_impact_stars(deltaTime);

    // Update screen shake
    update_screen_shake(deltaTime);

    // Update invincibility timer and flash counter
    if (playerInvincibilityTimer > 0.0f) {
        playerInvincibilityTimer -= deltaTime;
        playerInvincibilityFlashFrame++;
        if (playerInvincibilityTimer <= 0.0f) {
            playerInvincibilityTimer = 0.0f;
            playerInvincibilityFlashFrame = 0;
        }
    }

    // Check decoration collisions with player (skip if dead, transitioning, or in debug mode)
    perfStart = get_ticks();
    if (!playerIsDead && !debugFlyMode && !isTransitioning) {
        map_check_deco_collisions(&mapRuntime, cubeX, cubeY, cubeZ, playerRadius);

        // Check turret projectile collisions (1 damage per hit)
        if (!playerIsHurt && !playerIsRespawning) {
            if (map_check_turret_projectiles(&mapRuntime, cubeX, cubeY, cubeZ, playerRadius, PLAYER_HEIGHT)) {
                player_take_damage(1);
            }
            // Check pulse turret homing projectile collisions (1 damage per hit)
            if (map_check_pulse_turret_projectiles(&mapRuntime, cubeX, cubeY, cubeZ, playerRadius, PLAYER_HEIGHT)) {
                player_take_damage(1);
            }
        }
    }
    uint32_t perfDecoCollision = get_ticks() - perfStart;

    static int perfFrameCount = 0;
    static uint32_t perfAnimTotal = 0;
    static uint32_t perfDecoUpdateTotal = 0;
    static uint32_t perfDecoCollisionTotal = 0;
    static uint32_t perfCollisionTotal = 0;
    static uint32_t perfWallTotal = 0;
    static uint32_t perfDecoWallTotal = 0;
    static uint32_t perfGroundTotal = 0;
    static uint32_t perfDecoGroundTotal = 0;
    static uint32_t perfFrameStart = 0;

    if (perfFrameCount == 0) {
        perfFrameStart = get_ticks();
    }

    perfAnimTotal += perfAnimTime;
    perfDecoUpdateTotal += perfDecoUpdate;
    perfDecoCollisionTotal += perfDecoCollision;
    perfCollisionTotal += perfCollisionTime;
    perfWallTotal += perfWallTime;
    perfDecoWallTotal += perfDecoWallTime;
    perfGroundTotal += perfGroundTime;
    perfDecoGroundTotal += perfDecoGroundTime;
    perfFrameCount++;

    if (perfFrameCount >= 60) {
        uint32_t frameTime = get_ticks() - perfFrameStart;
        float avgFPS = 60.0f / (frameTime / (float)TICKS_PER_SECOND);
        // Convert ticks to microseconds: ticks * 1000000 / TICKS_PER_SECOND
        // TICKS_PER_SECOND = 46875000, so approx 21.3ns per tick, or ~47 ticks per us
        #define TICKS_TO_US_AVG(t) ((int)((t) * 1000000ULL / TICKS_PER_SECOND / 60))
        // Calculate budget: 33333us per frame at 30fps
        int cpuLogicUs = TICKS_TO_US_AVG(perfAnimTotal + perfCollisionTotal + perfDecoUpdateTotal + perfDecoCollisionTotal);
        int frameUs = (int)(frameTime * 1000000ULL / TICKS_PER_SECOND / 60);
        g_lastFrameUs = frameUs;  // Store for HUD display
        int budgetLeft = 33333 - frameUs;
        debugf("=== PERF (60f avg) FPS=%.1f Frame=%dus CPU=%dus Budget=%dus ===\n",
            avgFPS, frameUs, cpuLogicUs, budgetLeft);
        debugf("  Player: Anim=%dus Collision=%dus (Wall=%d Gnd=%d DecoWall=%d DecoGnd=%d)\n",
            TICKS_TO_US_AVG(perfAnimTotal),
            TICKS_TO_US_AVG(perfCollisionTotal),
            TICKS_TO_US_AVG(perfWallTotal),
            TICKS_TO_US_AVG(perfGroundTotal),
            TICKS_TO_US_AVG(perfDecoWallTotal),
            TICKS_TO_US_AVG(perfDecoGroundTotal));
        debugf("  Decos: Update=%dus Collision=%dus\n",
            TICKS_TO_US_AVG(perfDecoUpdateTotal),
            TICKS_TO_US_AVG(perfDecoCollisionTotal));
        // Show slime-specific stats
        if (g_slimeUpdateCount > 0) {
            debugf("  Slime (%d): Ground=%dus DecoGnd=%dus Wall=%dus Math=%dus Spring=%dus\n",
                g_slimeUpdateCount / 60,
                TICKS_TO_US_AVG(g_slimeGroundTicks),
                TICKS_TO_US_AVG(g_slimeDecoGroundTicks),
                TICKS_TO_US_AVG(g_slimeWallTicks),
                TICKS_TO_US_AVG(g_slimeMathTicks),
                TICKS_TO_US_AVG(g_slimeSpringTicks));
        }
        // Render breakdown
        debugf("  Render: Total=%dus Map=%dus Deco=%dus Player=%dus Shadow=%dus HUD=%dus\n",
            TICKS_TO_US_AVG(g_renderTotalTicks),
            TICKS_TO_US_AVG(g_renderMapTicks),
            TICKS_TO_US_AVG(g_renderDecoTicks),
            TICKS_TO_US_AVG(g_renderPlayerTicks),
            TICKS_TO_US_AVG(g_renderShadowTicks),
            TICKS_TO_US_AVG(g_renderHUDTicks));
        // Other costs
        debugf("  Other: Audio=%dus Input=%dus Camera=%dus\n",
            TICKS_TO_US_AVG(g_audioTicks),
            TICKS_TO_US_AVG(g_inputTicks),
            TICKS_TO_US_AVG(g_cameraTicks));

        // Reset all counters
        g_slimeGroundTicks = 0;
        g_slimeDecoGroundTicks = 0;
        g_slimeWallTicks = 0;
        g_slimeMathTicks = 0;
        g_slimeSpringTicks = 0;
        g_slimeUpdateCount = 0;
        g_renderMapTicks = 0;
        g_renderDecoTicks = 0;
        g_renderPlayerTicks = 0;
        g_renderShadowTicks = 0;
        g_renderHUDTicks = 0;
        g_renderTotalTicks = 0;
        g_audioTicks = 0;
        g_inputTicks = 0;
        g_cameraTicks = 0;
        #undef TICKS_TO_US_AVG
        perfFrameCount = 0;
        perfAnimTotal = 0;
        perfDecoUpdateTotal = 0;
        perfDecoCollisionTotal = 0;
        perfCollisionTotal = 0;
        perfWallTotal = 0;
        perfDecoWallTotal = 0;
        perfGroundTotal = 0;
        perfDecoGroundTotal = 0;
    }

    // Death transition with iris effect
    if (playerIsDead && !playerIsRespawning) {
        playerDeathTimer += deltaTime;

        // Get player's screen position for iris targeting
        T3DVec3 playerWorld = {{cubeX, cubeY + 10.0f, cubeZ}};  // Slightly above feet
        T3DVec3 playerScreen = {{0, 0, -1}};  // Initialize to safe defaults (z < 0 will skip iris update)
        t3d_viewport_calc_viewspace_pos(&viewport, &playerScreen, &playerWorld);

        // Update iris target (player's screen position)
        if (playerScreen.v[2] > 0) {  // If player is in front of camera
            irisTargetX = playerScreen.v[0];
            irisTargetY = playerScreen.v[1];
        }

        // Start iris effect after death animation
        if (playerDeathTimer > DEATH_ANIMATION_TIME) {
            if (!irisActive) {
                // Initialize iris
                irisActive = true;
                irisRadius = 400.0f;  // Start larger than screen
                irisCenterX = 160.0f;  // Start at screen center
                irisCenterY = 120.0f;
                // Initialize targets to screen center to prevent garbage values causing RDP crashes
                irisTargetX = 160.0f;
                irisTargetY = 120.0f;
                irisPaused = false;
                irisPauseTimer = 0.0f;
            }

            // Lerp iris center toward player
            float centerLerp = 0.15f;
            irisCenterX += (irisTargetX - irisCenterX) * centerLerp;
            irisCenterY += (irisTargetY - irisCenterY) * centerLerp;

            // Shrink iris with dramatic pause when small
            if (irisPaused) {
                // Hold at small circle for dramatic effect
                irisPauseTimer += deltaTime;
                if (irisPauseTimer >= IRIS_PAUSE_DURATION) {
                    // Pause over, continue closing
                    irisPaused = false;
                }
            } else if (irisRadius > IRIS_PAUSE_RADIUS && !irisPaused && irisPauseTimer == 0.0f) {
                // Initial shrinking phase - exponential decay until we hit pause radius
                float shrinkSpeed = irisRadius * 0.06f;
                if (shrinkSpeed < 3.0f) shrinkSpeed = 3.0f;
                irisRadius -= shrinkSpeed;

                // Snap to pause radius when we reach it
                if (irisRadius <= IRIS_PAUSE_RADIUS) {
                    irisRadius = IRIS_PAUSE_RADIUS;
                    irisPaused = true;
                    irisPauseTimer = 0.0f;
                }
            } else if (irisRadius > 0.0f) {
                // Final closing phase - after pause, close the rest of the way
                float shrinkSpeed = irisRadius * 0.12f;  // Faster closing
                if (shrinkSpeed < 2.0f) shrinkSpeed = 2.0f;
                irisRadius -= shrinkSpeed;
                if (irisRadius < 0.0f) irisRadius = 0.0f;
            }

            // Set fadeAlpha based on iris (for respawn trigger)
            fadeAlpha = (irisRadius <= 0.0f) ? 1.0f : 0.0f;

            // When iris fully closed, respawn
            if (irisRadius <= 0.0f && !playerIsRespawning) {
                playerIsRespawning = true; g_playerIsRespawning = true;

                // Reset player health and state
                playerHealth = maxPlayerHealth;
                playerIsDead = false;
                playerIsHurt = false;
                playerHurtAnimTime = 0.0f;
                playerCurrentPainAnim = NULL;
                playerInvincibilityTimer = 0.0f;
                playerInvincibilityFlashFrame = 0;
                playerDeathTimer = 0.0f;

                // Reset activation system (buttons, lasers, moving platforms, etc.)
                activation_reset_all();

                // Reset player position to checkpoint or level start
                debugf("Respawn: g_checkpointActive=%d, checkpoint=(%.1f, %.1f, %.1f)\n",
                       g_checkpointActive, g_checkpointX, g_checkpointY, g_checkpointZ);
                if (g_checkpointActive) {
                    cubeX = g_checkpointX;
                    cubeY = g_checkpointY;
                    cubeZ = g_checkpointZ;
                    debugf("Respawning at CHECKPOINT: (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);
                } else {
                    // Find DECO_PLAYERSPAWN for level start position
                    bool foundSpawn = false;
                    for (int i = 0; i < mapRuntime.decoCount; i++) {
                        DecoInstance* deco = &mapRuntime.decorations[i];
                        if (deco->type == DECO_PLAYERSPAWN && deco->active) {
                            cubeX = deco->posX;
                            cubeY = deco->posY;
                            cubeZ = deco->posZ;
                            foundSpawn = true;
                            debugf("Respawning at PLAYERSPAWN: (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);
                            break;
                        }
                    }
                    // Fallback to level definition if no spawn decoration found
                    if (!foundSpawn) {
                        const LevelData* level = ALL_LEVELS[currentLevel];
                        cubeX = level->playerStartX;
                        cubeY = level->playerStartY;
                        cubeZ = level->playerStartZ;
                        debugf("Respawning at LEVEL START (fallback): (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);
                    }
                }

                // Snap to ground if possible, but TRUST checkpoint position if ground check fails
                // (checkpoint was valid when player touched it - ground check may fail due to dynamic platforms)
                float groundY = maploader_get_ground_height(&mapLoader, cubeX, cubeY + 100.0f, cubeZ);
                if (groundY > INVALID_GROUND_Y) {
                    cubeY = groundY;
                    debugf("Snapped to ground at Y=%.1f\n", groundY);
                } else if (g_checkpointActive) {
                    // Ground check failed but we have a checkpoint - trust it
                    // The player was just there, so the position should be valid
                    debugf("WARNING: No ground at checkpoint (%.1f, %.1f, %.1f), using checkpoint Y anyway\n", cubeX, cubeY, cubeZ);
                } else {
                    // No checkpoint and no ground found - try DECO_PLAYERSPAWN as last resort
                    debugf("WARNING: No ground at respawn pos (%.1f, %.1f, %.1f), searching for spawn...\n", cubeX, cubeY, cubeZ);
                    for (int i = 0; i < mapRuntime.decoCount; i++) {
                        DecoInstance* deco = &mapRuntime.decorations[i];
                        if (deco->type == DECO_PLAYERSPAWN && deco->active) {
                            cubeX = deco->posX;
                            cubeY = deco->posY;
                            cubeZ = deco->posZ;
                            groundY = maploader_get_ground_height(&mapLoader, cubeX, cubeY + 100.0f, cubeZ);
                            if (groundY > INVALID_GROUND_Y) {
                                cubeY = groundY;
                            }
                            debugf("Fallback to PLAYERSPAWN: (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);
                            break;
                        }
                    }
                }
                // Sync frame pacing positions
                prevCubeX = cubeX; prevCubeY = cubeY; prevCubeZ = cubeZ;
                renderCubeX = cubeX; renderCubeY = cubeY; renderCubeZ = cubeZ;
                playerState.velX = 0.0f;
                playerState.velY = 0.0f;
                playerState.velZ = 0.0f;
                playerState.isGrounded = false;

                // Reset animation states
                isCharging = false;
                isJumping = false;
                isLanding = false;
                jumpAnimPaused = false;
                jumpChargeTime = 0.0f;
                jumpComboCount = 0;
                jumpComboTimer = 0.0f;
                idleFrames = 0;
                playingFidget = false;
                fidgetPlayTime = 0.0f;

                // Reset arms mode state
                armsIsSpinning = false;
                armsIsWhipping = false;
                armsHasDoubleJumped = false;
                armsIsGliding = false;
                armsIsWallJumping = false;
                armsSpinTime = 0.0f;
                armsWhipTime = 0.0f;
                spinHitCooldown = 0.0f;

                // Reset torso wall jump state
                torsoIsWallJumping = false;
                wallJumpInputBuffer = 0;

                // Disable player controls during respawn (re-enabled when iris opens)
                playerState.canMove = false;
                playerState.canRotate = false;
                playerState.canJump = false;
                playerState.isGliding = false;
                playerState.isSliding = false;

                // Reset to idle animation immediately
                if (currentPart == PART_TORSO && torsoHasAnims && torsoAnimIdle.animRef != NULL) {
                    t3d_anim_attach(&torsoAnimIdle, &torsoSkel);
                    t3d_anim_set_time(&torsoAnimIdle, 0.0f);
                    t3d_anim_set_playing(&torsoAnimIdle, true);
                    t3d_anim_update(&torsoAnimIdle, 0.0f);  // Force update to apply pose
                    if (skeleton_is_valid(&torsoSkel)) {
                        t3d_skeleton_update(&torsoSkel);  // Update skeleton to show idle pose
                    }
                    debugf("Reset to idle animation on respawn\n");
                } else if (currentPart == PART_ARMS && armsHasAnims && armsAnimIdle.animRef != NULL) {
                    t3d_anim_attach(&armsAnimIdle, &torsoSkel);
                    t3d_anim_set_time(&armsAnimIdle, 0.0f);
                    t3d_anim_set_playing(&armsAnimIdle, true);
                    t3d_anim_update(&armsAnimIdle, 0.0f);
                    if (skeleton_is_valid(&torsoSkel)) {
                        t3d_skeleton_update(&torsoSkel);
                    }
                    debugf("Reset to idle animation on respawn (arms mode)\n");
                }

                // Reload level (decorations, etc.)
                map_runtime_free(&mapRuntime);
                map_runtime_init(&mapRuntime, FB_COUNT, VISIBILITY_RANGE);
                currentlyAttachedAnim = NULL;  // Reset animation tracking
                level_load(currentLevel, &mapLoader, &mapRuntime);
                mapRuntime.mapLoader = &mapLoader;  // Set collision reference for turret raycasts

                // Clear fog override so level starts with default fog
                game_clear_fog_override();

                // Restore checkpoint lighting (if any) or reinitialize from level
                if (lightingState.hasCheckpointLighting) {
                    restore_checkpoint_lighting();
                } else {
                    init_lighting_state(currentLevel);
                }

                // Set body part for this level
                currentPart = (RobotParts)level_get_body_part(currentLevel);

                // Reset all TransitionCollision triggered flags (allow re-triggering after respawn)
                for (int i = 0; i < mapRuntime.decoCount; i++) {
                    DecoInstance* deco = &mapRuntime.decorations[i];
                    if (deco->type == DECO_TRANSITIONCOLLISION && deco->active) {
                        deco->state.transition.triggered = false;
                    }
                }

                // Find first PlayerSpawn decoration and use it as respawn point
                // ONLY if we're NOT respawning at a checkpoint
                if (!g_checkpointActive) {
                    for (int i = 0; i < mapRuntime.decoCount; i++) {
                        DecoInstance* deco = &mapRuntime.decorations[i];
                        if (deco->type == DECO_PLAYERSPAWN && deco->active) {
                            cubeX = deco->posX;
                            cubeY = deco->posY;
                            cubeZ = deco->posZ;
                            break;
                        }
                    }
                    // If no PlayerSpawn found, position was already set from level data
                }

                // Keep screen black, start delay timer
                fadeAlpha = 1.0f;
                playerRespawnDelayTimer = 0.0f;
                debugf("Player respawned!\n");
            }
        }
    }

    // Fade back in after respawn delay (iris opens back up)
    if (playerIsRespawning) {
        playerRespawnDelayTimer += deltaTime;

        // Wait while screen is black, then open iris back up
        if (playerRespawnDelayTimer > RESPAWN_HOLD_TIME) {
            float fadeInProgress = (playerRespawnDelayTimer - RESPAWN_HOLD_TIME) / RESPAWN_FADE_IN_TIME;

            // Open iris back up (reverse of closing)
            irisRadius = fadeInProgress * 400.0f;
            if (irisRadius > 400.0f) irisRadius = 400.0f;

            // Center iris on player for respawn
            T3DVec3 playerWorld = {{cubeX, cubeY + 10.0f, cubeZ}};
            T3DVec3 playerScreen = {{0, 0, -1}};  // Initialize to safe defaults (z < 0 will skip iris update)
            t3d_viewport_calc_viewspace_pos(&viewport, &playerScreen, &playerWorld);
            if (playerScreen.v[2] > 0) {
                irisCenterX = playerScreen.v[0];
                irisCenterY = playerScreen.v[1];
            }

            fadeAlpha = 1.0f - fadeInProgress;
            if (fadeAlpha <= 0.0f) {
                fadeAlpha = 0.0f;
                playerIsRespawning = false; g_playerIsRespawning = false;
                playerRespawnDelayTimer = 0.0f;
                irisActive = false;  // Done with iris effect
                irisRadius = 400.0f;  // Reset for next time

                // Snap to ground when respawn completes so player doesn't hover
                float groundY = maploader_get_ground_height(&mapLoader, cubeX, cubeY + 50.0f, cubeZ);
                if (groundY > -9000.0f) {
                    cubeY = groundY;
                    prevCubeX = cubeX; prevCubeY = groundY; prevCubeZ = cubeZ;
                    renderCubeX = cubeX; renderCubeY = groundY; renderCubeZ = cubeZ;
                    playerState.isGrounded = true;
                    playerState.velY = 0.0f;
                    isJumping = false;
                    isLanding = false;
                }

                // Re-enable player controls now that respawn is complete
                playerState.canMove = true;
                playerState.canRotate = true;
                playerState.canJump = true;
            }
        }
    }

    // Pre-transition phase (wait, slide whistle, thud crash, Psyops logo before actual transition)
    if (isPreTransitioning) {
        preTransitionTimer += deltaTime;

        // Phase 1: At 2 seconds, play slide whistle
        if (preTransitionTimer >= PRE_TRANSITION_WAIT_TIME && preTransitionTimer - deltaTime < PRE_TRANSITION_WAIT_TIME) {
            wav64_play(&sfxSlideWhistle, 2);  // Play on channel 2 (SFX)
        }

        // Phase 2: At 2 + 1.5 = 3.5 seconds, play metallic thud crash
        float thudTime = PRE_TRANSITION_WAIT_TIME + PRE_TRANSITION_THUD_TIME;
        if (preTransitionTimer >= thudTime && preTransitionTimer - deltaTime < thudTime) {
            wav64_play(&sfxMetallicThud, 3);  // Play on channel 3 (SFX)
        }

        // Phase 3: At thud + delay = 4 seconds, load game logo sprite
        float logoStartTime = PRE_TRANSITION_WAIT_TIME + PRE_TRANSITION_THUD_TIME + PRE_TRANSITION_LOGO_DELAY;
        if (preTransitionTimer >= logoStartTime && preTransitionTimer - deltaTime < logoStartTime) {
            // Lazy load logo sprite for pre-transition
            if (preTransitionLogo == NULL) {
                rspq_wait();
                preTransitionLogo = sprite_load("rom:/Logo.sprite");
            }
        }

        // Phase 4: After wait + thud + delay + logo time, start actual transition
        float totalPreTransitionTime = PRE_TRANSITION_WAIT_TIME + PRE_TRANSITION_THUD_TIME + PRE_TRANSITION_LOGO_DELAY + PRE_TRANSITION_LOGO_TIME;
        if (preTransitionTimer >= totalPreTransitionTime) {
            start_actual_level_transition();
        }
    }

    // Level transition
    if (isTransitioning) {
        transitionTimer += deltaTime;

        // Phase 1: Fade out
        if (transitionTimer <= TRANSITION_FADE_OUT_TIME) {
            fadeAlpha = transitionTimer / TRANSITION_FADE_OUT_TIME;
            if (fadeAlpha > 1.0f) fadeAlpha = 1.0f;
        }
        // Phase 2: Load new level when fully black
        else if (transitionTimer > TRANSITION_FADE_OUT_TIME &&
                 transitionTimer <= TRANSITION_FADE_OUT_TIME + TRANSITION_HOLD_TIME) {
            // Ensure screen is fully black
            fadeAlpha = 1.0f;

            // Load new level (only do this once at the start of phase 2)
            if (transitionTimer - deltaTime <= TRANSITION_FADE_OUT_TIME) {
                debugf("Loading level %d via scene reinit...\n", targetTransitionLevel);

                // Save spawn index before deinit (will be used after init)
                int spawnToUse = targetTransitionSpawn;

                // Set target level before reinit
                currentLevel = (LevelID)targetTransitionLevel;
                selectedLevelID = targetTransitionLevel;

                // Deinit and reinit the game scene (handles everything: model loading, lighting, music, cutscenes, etc.)
                deinit_game_scene();
                init_game_scene();

                // Override spawn position if using non-default spawn point
                if (spawnToUse > 0) {
                    int spawnIndex = 0;
                    for (int i = 0; i < mapRuntime.decoCount; i++) {
                        DecoInstance* deco = &mapRuntime.decorations[i];
                        if (deco->type == DECO_PLAYERSPAWN && deco->active) {
                            if (spawnIndex == spawnToUse) {
                                cubeX = deco->posX;
                                cubeY = deco->posY;
                                cubeZ = deco->posZ;
                                // Snap to ground
                                float groundY = maploader_get_ground_height(&mapLoader, cubeX, cubeY + 50.0f, cubeZ);
                                if (groundY > -9000.0f) {
                                    cubeY = groundY;
                                }
                                // Sync positions
                                prevCubeX = cubeX; prevCubeY = cubeY; prevCubeZ = cubeZ;
                                renderCubeX = cubeX; renderCubeY = cubeY; renderCubeZ = cubeZ;
                                // Reset camera to new position
                                smoothCamX = cubeX;
                                smoothCamY = cubeY + 49.0f;
                                smoothCollisionCamX = cubeX;
                                smoothCollisionCamY = cubeY + 49.0f;
                                smoothCollisionCamZ = cubeZ - 120.0f;
                                camPos.v[0] = cubeX;
                                camPos.v[1] = cubeY + 49.0f;
                                camPos.v[2] = cubeZ - 120.0f;
                                camTarget.v[0] = cubeX;
                                camTarget.v[1] = cubeY;
                                debugf("Level transition: Using spawn point %d at (%.1f, %.1f, %.1f)\n",
                                    spawnToUse, cubeX, cubeY, cubeZ);
                                break;
                            }
                            spawnIndex++;
                        }
                    }
                }

                // Restore transition state (init_game_scene resets these)
                isTransitioning = true;
                transitionTimer = TRANSITION_FADE_OUT_TIME + 0.01f;  // In hold phase, past the load trigger
                fadeAlpha = 1.0f;  // Keep screen black

                debugf("Level transition complete!\n");
            }
        }
        // Phase 3: Fade in
        else if (transitionTimer > TRANSITION_FADE_OUT_TIME + TRANSITION_HOLD_TIME) {
            float fadeInProgress = (transitionTimer - TRANSITION_FADE_OUT_TIME - TRANSITION_HOLD_TIME) / TRANSITION_FADE_IN_TIME;
            fadeAlpha = 1.0f - fadeInProgress;
            if (fadeAlpha < 0.0f) fadeAlpha = 0.0f;

            // Transition complete
            if (fadeAlpha <= 0.0f) {
                isTransitioning = false;
                transitionTimer = 0.0f;
                // Player can move immediately (no countdown in single player)
            }
        }
    }

    // Update level banner animation
    level_banner_update(&levelBanner, deltaTime);

    // Update controls tutorial (blocks input while showing)
    if (tutorialActive) {
        joypad_poll();  // Need to poll since controls_update is skipped
    }
    tutorial_update(JOYPAD_PORT_1);

    // Pause (disabled during demo mode, tutorial, cutscenes, and other states)
    joypad_buttons_t pressedStart = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressedStart.start && !isPaused && !playerIsDead && !isTransitioning && !g_demoMode && !tutorialActive && !cs2Playing && !cs3Playing && !cs4Playing) {
        show_pause_menu();
    }

    // Debug: L+R+Z to trigger level complete (for testing)
    if (debugFlyMode) {
        joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
        if (held.l && held.r && pressedStart.z) {
            game_show_level_complete();
        }
    }

    // Macro bridge makes sync_player_state() unnecessary - state is updated directly
}

void draw_game_scene(void) {
    HEAP_CHECK("draw_start");

    uint32_t renderStart = get_ticks();

    // =========================================================================
    // FRAME PACING: Compute interpolated render position to reduce visual jitter
    // =========================================================================
    // On the N64, we have fixed timestep physics but rendering may not be perfectly
    // synchronized. Interpolating between previous and current position smooths
    // visual motion, especially when frames are dropped.
    // =========================================================================
#if FRAME_PACING_ENABLED
    renderCubeX = prevCubeX + (cubeX - prevCubeX) * frameLerpFactor;
    renderCubeY = prevCubeY + (cubeY - prevCubeY) * frameLerpFactor;
    renderCubeZ = prevCubeZ + (cubeZ - prevCubeZ) * frameLerpFactor;
#else
    renderCubeX = cubeX;
    renderCubeY = cubeY;
    renderCubeZ = cubeZ;
#endif

    surface_t *disp = display_get();
    surface_t *zbuf = display_get_zbuf();
    rdpq_attach(disp, zbuf);

    // Background color with smooth lerping toward fog color
    // Target is fog override color when active, otherwise level background
    uint8_t levelBgR, levelBgG, levelBgB;
    level_get_bg_color(currentLevel, &levelBgR, &levelBgG, &levelBgB);

    float targetBgR, targetBgG, targetBgB;
    if (fogOverrideActive) {
        // Lerp toward fog color when fog trigger is active
        targetBgR = fogOverrideR;
        targetBgG = fogOverrideG;
        targetBgB = fogOverrideB;
    } else {
        // Lerp toward level default background
        targetBgR = levelBgR;
        targetBgG = levelBgG;
        targetBgB = levelBgB;
    }

    // Initialize lerp on first frame or after level change
    if (!bgLerpInitialized) {
        bgLerpR = levelBgR;
        bgLerpG = levelBgG;
        bgLerpB = levelBgB;
        bgLerpInitialized = true;
    }

    // Smooth lerp toward target
    float lerpSpeed = BG_LERP_SPEED * DELTA_TIME;
    bgLerpR += (targetBgR - bgLerpR) * lerpSpeed;
    bgLerpG += (targetBgG - bgLerpG) * lerpSpeed;
    bgLerpB += (targetBgB - bgLerpB) * lerpSpeed;

    // Clamp and use lerped background color
    uint8_t bgR = (uint8_t)(bgLerpR < 0 ? 0 : (bgLerpR > 255 ? 255 : bgLerpR));
    uint8_t bgG = (uint8_t)(bgLerpG < 0 ? 0 : (bgLerpG > 255 ? 255 : bgLerpG));
    uint8_t bgB = (uint8_t)(bgLerpB < 0 ? 0 : (bgLerpB > 255 ? 255 : bgLerpB));
    rdpq_clear(RGBA32(bgR, bgG, bgB, 0xFF));
    rdpq_clear_z(0xFFFC);

    t3d_frame_start();
    t3d_viewport_attach(&viewport);

    // Camera (with screen shake offset applied)
    T3DVec3 shakenCamPos = {{camPos.v[0] + shakeOffsetX, camPos.v[1] + shakeOffsetY, camPos.v[2]}};
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(70.0f), 10.0f, 1000.0f);
    t3d_viewport_look_at(&viewport, &shakenCamPos, &camTarget, &(T3DVec3){{0, 1, 0}});

    // =========================================================================
    // TWO-LIGHT SYSTEM:
    // - MAP LIGHT: Static directional from level data, affects only stage geometry
    // - ENTITY LIGHT: Dynamic, can be changed by DECO_LIGHT_TRIGGER, affects player/decorations
    // =========================================================================

    // Ambient lighting (affects everything, can change via triggers)
    uint8_t ambientColor[4] = {lightingState.ambientR, lightingState.ambientG, lightingState.ambientB, 0xFF};

    // MAP DIRECTIONAL LIGHT - static from level data, only for stage geometry
    uint8_t mapLightColor[4] = {lightingState.directionalR, lightingState.directionalG, lightingState.directionalB, 0xFF};
    float mapLightDirX, mapLightDirY, mapLightDirZ;
    level_get_light_direction(currentLevel, &mapLightDirX, &mapLightDirY, &mapLightDirZ);
    T3DVec3 mapLightDir = {{mapLightDirX, mapLightDirY, mapLightDirZ}};
    t3d_vec3_norm(&mapLightDir);

    t3d_light_set_ambient(ambientColor);
    t3d_light_set_directional(0, mapLightColor, &mapLightDir);

    // Check for level-specific point light
    int lightCount = 1;  // Start with directional light
    float levelLightX, levelLightY, levelLightZ;
    if (level_get_point_light(currentLevel, &levelLightX, &levelLightY, &levelLightZ)) {
        uint8_t levelLightColor[4] = {255, 255, 255, 0xFF};
        T3DVec3 levelLightPos = {{levelLightX, levelLightY, levelLightZ}};
        t3d_light_set_point(lightCount, levelLightColor, &levelLightPos, 2000.0f, false);
        lightCount++;
    }

    // Add point lights from DECO_LIGHT decorations (affects map geometry)
    for (int i = 0; i < mapRuntime.decoCount && lightCount < 7; i++) {
        DecoInstance* inst = &mapRuntime.decorations[i];
        if (!inst->active || inst->type != DECO_LIGHT) continue;

        uint8_t decoLightColor[4] = {
            inst->state.light.colorR,
            inst->state.light.colorG,
            inst->state.light.colorB,
            0xFF
        };
        T3DVec3 decoLightPos = {{inst->posX, inst->posY, inst->posZ}};
        t3d_light_set_point(lightCount, decoLightColor, &decoLightPos, inst->state.light.radius, false);
        lightCount++;
    }

    // Set light count for map rendering (excludes DECO_LIGHT_NOMAP and buff light)
    t3d_light_set_count(lightCount);

    // Apply fog settings (party fog overrides level fog when active)
    // Note: rdpq_mode_fog() configures the RDP blender for fog - required for fog to work!
    if (partyFogIntensity > 0.01f) {
        // Party fog - color cycling fog that creeps in during jukebox party mode
        uint8_t partyR, partyG, partyB;
        hsv_to_rgb(partyFogHue, 0.5f, 0.6f, &partyR, &partyG, &partyB);  // Subtle colors
        rdpq_mode_fog(RDPQ_FOG_STANDARD);
        rdpq_set_fog_color(RGBA32(partyR, partyG, partyB, 255));

        // Fog starts close but fades slowly (visible haze, not obscuring)
        float fogNear = 60.0f - (partyFogIntensity * 30.0f);    // 60 -> 30 (close to camera)
        float fogFar = 450.0f - (partyFogIntensity * 100.0f);   // 450 -> 350 (far enough to see scene)
        t3d_fog_set_range(fogNear, fogFar);
        t3d_fog_set_enabled(true);
    } else {
        // Use per-level fog settings (with optional DECO_FOGCOLOR override)
        uint8_t fogR, fogG, fogB;
        float fogNear, fogFar;
        bool fogEnabled = level_get_fog_settings(currentLevel, &fogR, &fogG, &fogB, &fogNear, &fogFar);

        // Store current fog for DECO_FOGCOLOR to read
        currentFogR = fogR;
        currentFogG = fogG;
        currentFogB = fogB;
        currentFogNear = fogNear;
        currentFogFar = fogFar;

        // Apply fog override if active (from DECO_FOGCOLOR trigger)
        if (fogOverrideActive && fogEnabled) {
            fogR = fogOverrideR;
            fogG = fogOverrideG;
            fogB = fogOverrideB;
            fogNear = fogOverrideNear;
            fogFar = fogOverrideFar;
        }

        if (fogEnabled) {
            rdpq_mode_fog(RDPQ_FOG_STANDARD);
            rdpq_set_fog_color(RGBA32(fogR, fogG, fogB, 255));
            t3d_fog_set_range(fogNear, fogFar);
            t3d_fog_set_enabled(true);
        } else {
            rdpq_mode_fog(0);
            t3d_fog_set_enabled(false);
        }
    }

    HEAP_CHECK("pre_maps");

    // Draw all active maps using map loader (with frustum culling if BVH available)
    uint32_t mapStart = get_ticks();
    maploader_draw_culled(&mapLoader, frameIdx, &viewport);
    g_renderMapTicks += get_ticks() - mapStart;

    // Flush after map rendering to prevent RDP buffer overflow
    rspq_flush();

    // DEBUG: Full sync after maps to isolate if map rendering causes RSP hang
    rspq_wait();

    HEAP_CHECK("post_maps");

    // =========================================================================
    // SWITCH TO ENTITY LIGHT for decorations and player rendering
    // Entity light can be changed dynamically by DECO_LIGHT_TRIGGER
    // =========================================================================
    uint8_t entityLightColor[4] = {lightingState.entityDirectR, lightingState.entityDirectG, lightingState.entityDirectB, 0xFF};
    T3DVec3 entityLightDir = {{lightingState.entityLightDirX, lightingState.entityLightDirY, lightingState.entityLightDirZ}};
    t3d_vec3_norm(&entityLightDir);
    t3d_light_set_directional(0, entityLightColor, &entityLightDir);

    // Add DECO_LIGHT_NOMAP lights (only affects decorations and player, not map)
    for (int i = 0; i < mapRuntime.decoCount && lightCount < 7; i++) {
        DecoInstance* inst = &mapRuntime.decorations[i];
        if (!inst->active || inst->type != DECO_LIGHT_NOMAP) continue;

        uint8_t decoLightColor[4] = {
            inst->state.light.colorR,
            inst->state.light.colorG,
            inst->state.light.colorB,
            0xFF
        };
        T3DVec3 decoLightPos = {{inst->posX, inst->posY, inst->posZ}};
        t3d_light_set_point(lightCount, decoLightColor, &decoLightPos, inst->state.light.radius, false);
        lightCount++;
    }

    // Add blue point light emanating from player when chargepad buff is active
    bool hasBuff = buffJumpActive || buffGlideActive || buffSpeedTimer > 0.0f;
    if (hasBuff && lightCount < 7) {
        uint8_t buffLightColor[4] = {80, 160, 255, 0xFF};  // Light blue
        T3DVec3 playerLightPos = {{cubeX, cubeY + 10.0f, cubeZ}};
        t3d_light_set_point(lightCount, buffLightColor, &playerLightPos, 120.0f, false);
        lightCount++;
    }

    // Update light count for decoration/player rendering
    t3d_light_set_count(lightCount);

    // Draw decorations (only if within visibility range and active)
    uint32_t decoRenderStart = get_ticks();
    float checkX = debugFlyMode ? debugCamX : cubeX;

    // Skip decoration drawing during level transitions (models may be freed/reloaded)
    // This prevents use-after-free crashes when accessing stale model pointers
    bool skipDecoDrawing = isTransitioning &&
        transitionTimer > TRANSITION_FADE_OUT_TIME &&
        transitionTimer <= TRANSITION_FADE_OUT_TIME + TRANSITION_HOLD_TIME;

    // Draw decorations in order
    int decoDrawCount = 0;  // Counter for periodic RDP buffer flush
    for (int i = 0; i < mapRuntime.decoCount && !skipDecoDrawing; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (!deco->active || deco->type == DECO_NONE) continue;

        float dist = fabsf(deco->posX - checkX);
        float visRange = (deco->type == DECO_COG) ? COG_VISIBILITY_RANGE : DECO_VISIBILITY_RANGE;
        // HOTPIPE has intentional model offset, STAGE7/LEVEL3_STREAM are always rendered - skip visibility culling
        if (deco->type != DECO_HOTPIPE && deco->type != DECO_STAGE7 && deco->type != DECO_LEVEL3_STREAM && dist > visRange) continue;

        // Skip invisible triggers in normal gameplay (only show in debug mode)
        if (deco->type == DECO_PLAYERSPAWN && !debugFlyMode) continue;
        if (deco->type == DECO_DAMAGECOLLISION && !debugFlyMode) continue;
        if (deco->type == DECO_DAMAGECUBE_LIGHT && !debugFlyMode) continue;
        if (deco->type == DECO_TRANSITIONCOLLISION && !debugFlyMode) continue;
        if (deco->type == DECO_DIALOGUETRIGGER && !debugFlyMode) continue;
        if (deco->type == DECO_INTERACTTRIGGER && !debugFlyMode) continue;
        if (deco->type == DECO_PATROLPOINT && !debugFlyMode) continue;
        if (deco->type == DECO_PAIN_TUBE && !debugFlyMode) continue;
        if (deco->type == DECO_CHECKPOINT && !debugFlyMode) continue;
        if (deco->type == DECO_RAT && g_demoMode) continue;  // Hide rats in demo mode

        // Draw semi-transparent box for PlayerSpawn in debug mode
        if (deco->type == DECO_PLAYERSPAWN && debugFlyMode) {
            // PlayerSpawn now uses BlueCube model directly
            DecoTypeRuntime* spawnType = map_get_deco_type(&mapRuntime, DECO_PLAYERSPAWN);
            if (spawnType && spawnType->model) {
                int matIdx = frameIdx * MAX_DECORATIONS + i;
                float renderYOffset = 0.2f;
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Draw semi-transparent green box
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(RGBA32(0, 255, 0, 128));  // Semi-transparent green

                t3d_model_draw(spawnType->model);
                t3d_matrix_pop(1);
            }
            continue;
        }

        // Draw DamageCollision in debug mode
        if (deco->type == DECO_DAMAGECOLLISION && debugFlyMode) {
            DecoTypeRuntime* damageType = map_get_deco_type(&mapRuntime, DECO_DAMAGECOLLISION);
            if (damageType && damageType->model) {
                int matIdx = frameIdx * MAX_DECORATIONS + i;
                float renderYOffset = 0.2f;
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Draw semi-transparent red for danger
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(RGBA32(255, 0, 0, 128));  // Semi-transparent red

                t3d_model_draw(damageType->model);
                t3d_matrix_pop(1);
            }
            continue;
        }

        // Draw TransitionCollision in debug mode
        if (deco->type == DECO_TRANSITIONCOLLISION && debugFlyMode) {
            DecoTypeRuntime* transitionType = map_get_deco_type(&mapRuntime, DECO_TRANSITIONCOLLISION);
            if (transitionType && transitionType->model) {
                int matIdx = frameIdx * MAX_DECORATIONS + i;
                float renderYOffset = 0.2f;
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Draw semi-transparent cyan for transition
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(RGBA32(0, 255, 255, 128));  // Semi-transparent cyan

                t3d_model_draw(transitionType->model);
                t3d_matrix_pop(1);
            }
            continue;
        }

        // Draw DialogueTrigger in debug mode (yellow)
        if (deco->type == DECO_DIALOGUETRIGGER && debugFlyMode) {
            // Borrow BlueCube model from PLAYERSPAWN for visualization
            DecoTypeRuntime* cubeType = map_get_deco_type(&mapRuntime, DECO_PLAYERSPAWN);
            if (cubeType && cubeType->model) {
                int matIdx = frameIdx * MAX_DECORATIONS + i;
                // BlueCube is 128 units wide (-64 to +64), scale to match trigger radius
                float radius = deco->state.dialogueTrigger.triggerRadius;
                float scale = radius / 64.0f;
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){scale, scale * 0.3f, scale},  // Flatter to look like a zone
                    (float[3]){0.0f, 0.0f, 0.0f},
                    (float[3]){deco->posX, deco->posY + 5.0f, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(RGBA32(255, 255, 0, 100));  // Semi-transparent yellow

                t3d_model_draw(cubeType->model);
                t3d_matrix_pop(1);
            }
            continue;
        }

        // Draw InteractTrigger in debug mode (orange)
        if (deco->type == DECO_INTERACTTRIGGER && debugFlyMode) {
            // Borrow BlueCube model from PLAYERSPAWN for visualization
            DecoTypeRuntime* cubeType = map_get_deco_type(&mapRuntime, DECO_PLAYERSPAWN);
            if (cubeType && cubeType->model) {
                int matIdx = frameIdx * MAX_DECORATIONS + i;
                // BlueCube is 128 units wide (-64 to +64), scale to match trigger radius
                float radius = deco->state.interactTrigger.triggerRadius;
                float scale = radius / 64.0f;
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){scale, scale * 0.3f, scale},
                    (float[3]){0.0f, 0.0f, 0.0f},
                    (float[3]){deco->posX, deco->posY + 5.0f, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(RGBA32(255, 165, 0, 100));  // Semi-transparent orange

                t3d_model_draw(cubeType->model);
                t3d_matrix_pop(1);
            }
            continue;
        }

        DecoTypeRuntime* decoType = map_get_deco_type(&mapRuntime, deco->type);
        const DecoTypeDef* decoDef = &DECO_TYPES[deco->type];
        if (decoType && decoType->model) {
            int matIdx = frameIdx * MAX_DECORATIONS + i;
            // Add small Y-offset to prevent z-fighting with ground
            float renderYOffset = 0.2f;

            // Special handling for SIGN: use quaternion to rotate around global Z axis
            if (deco->type == DECO_SIGN) {
                // First rotation: around Y axis (baseRotY + 180 degrees)
                fm_quat_t quatY, quatZ, quatFinal;
                fm_vec3_t axisY = {{0, 1, 0}};
                fm_vec3_t axisZ = {{0, 0, 1}};
                fm_quat_from_axis_angle(&quatY, &axisY, deco->state.sign.baseRotY + 3.14159265f);
                // Second rotation: around GLOBAL Z axis (tilt)
                fm_quat_from_axis_angle(&quatZ, &axisZ, deco->state.sign.tilt);
                // Combine: first Y, then Z (in global space: Z * Y)
                fm_quat_mul(&quatFinal, &quatZ, &quatY);

                t3d_mat4fp_from_srt(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    quatFinal.v,
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
            } else if (deco->type == DECO_BOLT) {
                // BOLT: Skip rendering if already collected (pre-collected bolts are not spawned)
                // Note: wasPreCollected bolts are no longer spawned, so this check is for safety
                if (deco->state.bolt.wasPreCollected) {
                    continue;  // Skip - don't render already collected bolts
                }

                // Add scale pulse for "glow" effect
                float pulse = sinf(deco->state.bolt.spinAngle * 3.0f) * 0.15f + 1.0f;  // Oscillate 0.85 to 1.15
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX * pulse, deco->scaleY * pulse, deco->scaleZ * pulse},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
            } else if (deco->type == DECO_SLIME || deco->type == DECO_SLIME_LAVA) {
                // SLIME/SLIME_LAVA: Apply squash/stretch and shear for jiggle effect
                float jigX = deco->state.slime.jiggleX;
                float jigZ = deco->state.slime.jiggleZ;
                float stretchY = deco->state.slime.stretchY;

                // Volume preservation: if Y squashes, X/Z expand
                float stretchXZ = 1.0f + (1.0f - stretchY) * 0.5f;

                // Extra Y offset to prevent z-fighting with ground
                float slimeYOffset = renderYOffset + 1.0f;

                // Shake effect (after merging)
                float shakeOffX = 0.0f, shakeOffZ = 0.0f;
                if (deco->state.slime.shakeTimer > 0.0f) {
                    float shakeIntensity = deco->state.slime.shakeTimer * 8.0f * deco->scaleX;
                    shakeOffX = sinf(deco->state.slime.shakeTimer * 60.0f) * shakeIntensity;
                    shakeOffZ = cosf(deco->state.slime.shakeTimer * 45.0f) * shakeIntensity * 0.7f;
                }

                // First create base SRT matrix with squash/stretch
                T3DMat4FP* mat = &decoMatFP[matIdx];
                t3d_mat4fp_from_srt_euler(mat,
                    (float[3]){deco->scaleX * stretchXZ, deco->scaleY * stretchY, deco->scaleZ * stretchXZ},
                    (float[3]){0.0f, deco->rotY, 0.0f},
                    (float[3]){deco->posX + shakeOffX, deco->posY + slimeYOffset, deco->posZ + shakeOffZ}
                );

                // Apply shear transform by modifying the matrix directly
                // T3DMat4FP structure: m[row] where each row has i[col] (int) and f[col] (frac)
                // For shear: X += shearX * Y, Z += shearZ * Y
                // This means modifying column 1 (Y input) in rows 0 (X output) and 2 (Z output)
                // Convert jiggle to 16.16 fixed point shear values
                float shearScale = 2.5f;  // Strong shear for wobbly effect
                float shearX = jigX * shearScale;
                float shearZ = jigZ * shearScale;

                // Convert float shear to 16.16 fixed point (split into int and frac parts)
                int32_t shearX_fp = (int32_t)(shearX * 65536.0f);
                int32_t shearZ_fp = (int32_t)(shearZ * 65536.0f);

                // Add shear to Y column (column 1) affecting X output (row 0) and Z output (row 2)
                // m[0].i[1] and m[0].f[1] = Y's contribution to X
                // m[2].i[1] and m[2].f[1] = Y's contribution to Z
                int32_t existingX = ((int32_t)mat->m[0].i[1] << 16) | mat->m[0].f[1];
                int32_t existingZ = ((int32_t)mat->m[2].i[1] << 16) | mat->m[2].f[1];

                existingX += shearX_fp;
                existingZ += shearZ_fp;

                mat->m[0].i[1] = (int16_t)(existingX >> 16);
                mat->m[0].f[1] = (uint16_t)(existingX & 0xFFFF);
                mat->m[2].i[1] = (int16_t)(existingZ >> 16);
                mat->m[2].f[1] = (uint16_t)(existingZ & 0xFFFF);
            } else if (deco->type == DECO_ROUNDBUTTON) {
                // ROUNDBUTTON: Render bottom (base) at fixed position, top moves down when pressed
                float baseY = deco->posY + renderYOffset;  // Base stays fixed
                float topY = deco->posY + renderYOffset - 3.0f - deco->state.button.pressDepth;  // Top starts lower, moves down when pressed

                // Use two separate matrix slots to avoid any state bleeding
                int matIdxBottom = matIdx;
                int matIdxTop = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 3;  // Use a dedicated slot

                // Draw the bottom (base) - stays at original position
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxBottom],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, baseY, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdxBottom]);
                t3d_model_draw(decoType->model);  // Draw bottom (RoundButtonBottom)
                t3d_matrix_pop(1);

                // Draw the top - moves down by pressDepth
                if (mapRuntime.buttonTopModel) {
                    t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxTop],
                        (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                        (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                        (float[3]){deco->posX, topY, deco->posZ}
                    );
                    t3d_matrix_push(&decoMatFP[matIdxTop]);
                    t3d_model_draw(mapRuntime.buttonTopModel);  // Draw top (RoundButtonTop)
                    t3d_matrix_pop(1);
                }
                // Flush counter for special path (draws 2 models)
                decoDrawCount += 2;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;  // Skip normal draw path since we handled it
            } else if (deco->type == DECO_FAN) {
                // FAN: Render bottom (base) static, top rotates when active
                float baseY = deco->posY + renderYOffset;

                // Use two separate matrix slots (same pattern as button)
                int matIdxBottom = matIdx;
                int matIdxTop = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 4;  // Dedicated slot for fan top

                // Draw the bottom (base) - stays at original position, no rotation
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxBottom],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, baseY, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdxBottom]);
                t3d_model_draw(decoType->model);  // Draw bottom (FanBottom)
                t3d_matrix_pop(1);

                // Draw the top - rotates based on spinAngle
                if (mapRuntime.fanTopModel) {
                    // Add spin angle to the Y rotation
                    float spinRotY = deco->rotY + deco->state.fan.spinAngle;
                    t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxTop],
                        (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                        (float[3]){deco->rotX, spinRotY, deco->rotZ},
                        (float[3]){deco->posX, baseY, deco->posZ}
                    );
                    t3d_matrix_push(&decoMatFP[matIdxTop]);
                    t3d_model_draw(mapRuntime.fanTopModel);  // Draw top (FanTop)
                    t3d_matrix_pop(1);
                }
                // Flush counter for FAN (draws 2 models)
                decoDrawCount += 2;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;  // Skip normal draw path since we handled it
            } else if (deco->type == DECO_LASERWALL || deco->type == DECO_LASER) {
                // LASER: Show full laser wall when ON, frame only when OFF
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                if (deco->state.laser.isOn) {
                    // Laser is ON - draw full laser wall model (with lasers visible)
                    t3d_model_draw(decoType->model);
                } else {
                    // Laser is OFF - draw frame only model (no lasers)
                    if (mapRuntime.laserOffModel) {
                        t3d_model_draw(mapRuntime.laserOffModel);
                    }
                }

                t3d_matrix_pop(1);
                decoDrawCount++;
                if ((decoDrawCount & 3) == 0) rspq_flush();
                continue;  // Skip normal draw path since we handled it
            } else if (deco->type == DECO_CONVEYERLARGE) {
                // CONVEYOR: Draw frame normally, draw belt with texture scroll
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Draw frame (no scroll)
                t3d_model_draw(decoType->model);

                // Draw belt with texture scroll when active
                if (mapRuntime.conveyorBeltModel) {
                    // Initialize UVs on first draw
                    conveyor_belt_init_uvs(mapRuntime.conveyorBeltModel);

                    bool isActive = (deco->activationId == 0) || activation_get(deco->activationId);
                    if (isActive) {
                        // Scroll UVs based on texture offset
                        conveyor_belt_scroll_uvs(conveyor_get_offset());
                    }
                    t3d_model_draw(mapRuntime.conveyorBeltModel);
                }

                t3d_matrix_pop(1);
                // Flush counter for CONVEYERLARGE (draws 2 models)
                decoDrawCount += 2;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;  // Skip normal draw path since we handled it
            } else if (deco->type == DECO_TOXICPIPE) {
                // TOXICPIPE: Draw pipe normally, draw liquid with texture scroll
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Draw pipe (no scroll)
                t3d_model_draw(decoType->model);

                // Draw running goo with texture scroll
                if (mapRuntime.toxicPipeRunningModel) {
                    toxic_running_init_uvs(mapRuntime.toxicPipeRunningModel);
                    toxic_running_scroll_uvs(deco->state.toxicPipe.textureOffset);
                    t3d_model_draw(mapRuntime.toxicPipeRunningModel);
                }

                t3d_matrix_pop(1);
                // Flush counter for TOXICPIPE (draws 2 models)
                decoDrawCount += 2;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_TOXICRUNNING) {
                // TOXICRUNNING: Draw with texture scroll
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Initialize UVs on first draw
                toxic_running_init_uvs(decoType->model);
                toxic_running_scroll_uvs(deco->state.toxicRunning.textureOffset);
                t3d_model_draw(decoType->model);

                t3d_matrix_pop(1);
                // Flush counter for TOXICRUNNING (draws 1 model)
                decoDrawCount++;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_LAVAFLOOR) {
                // LAVAFLOOR: Draw with texture scroll
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Initialize UVs on first draw
                lavafloor_init_uvs(decoType->model);
                lavafloor_scroll_uvs(lavafloor_get_offset());
                t3d_model_draw(decoType->model);

                t3d_matrix_pop(1);
                decoDrawCount++;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_LAVAFALLS) {
                // LAVAFALLS: Draw with texture scroll (scrolls down for waterfall effect)
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Initialize UVs on first draw
                lavafalls_init_uvs(decoType->model);
                lavafalls_scroll_uvs(lavafalls_get_offset());
                t3d_model_draw(decoType->model);

                t3d_matrix_pop(1);
                decoDrawCount++;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_FAN2) {
                // FAN2: Draw with skeletal animation
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                if (deco->hasOwnSkeleton) {
                    t3d_model_draw_skinned(decoType->model, &deco->skeleton);
                } else {
                    t3d_model_draw(decoType->model);
                }

                t3d_matrix_pop(1);
                decoDrawCount++;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_CHARGEPAD) {
                // CHARGEPAD: Draw with faint blue glow when active
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Apply faint blue ambient boost when glowing
                if (deco->state.chargepad.glowing) {
                    uint8_t blueAmbient[4] = {100, 140, 200, 0xFF};
                    t3d_light_set_ambient(blueAmbient);
                }

                t3d_model_draw(decoType->model);

                // Restore normal ambient if we changed it
                if (deco->state.chargepad.glowing) {
                    t3d_light_set_ambient(ambientColor);
                }

                t3d_matrix_pop(1);
                decoDrawCount++;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_JUKEBOX) {
                // JUKEBOX: Draw main model with skeleton, then FX overlay with texture scroll
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Draw main jukebox model with skeleton animation
                // Validate skeleton before use to prevent crash on corrupted/freed data
                if (deco->hasOwnSkeleton && skeleton_is_valid(&deco->skeleton)) {
                    t3d_model_draw_skinned(decoType->model, &deco->skeleton);
                } else if (decoType->hasSkeleton && skeleton_is_valid(&decoType->skeleton)) {
                    t3d_model_draw_skinned(decoType->model, &decoType->skeleton);
                } else {
                    t3d_model_draw(decoType->model);
                }

                // Draw FX overlay with texture scroll (uses same skeleton as main model)
                if (mapRuntime.jukeboxFxModel) {
                    jukebox_fx_init_uvs(mapRuntime.jukeboxFxModel);
                    jukebox_fx_scroll_uvs(deco->state.jukebox.textureOffset);
                    if (deco->hasOwnSkeleton && skeleton_is_valid(&deco->skeleton)) {
                        t3d_model_draw_skinned(mapRuntime.jukeboxFxModel, &deco->skeleton);
                    } else if (decoType->hasSkeleton && skeleton_is_valid(&decoType->skeleton)) {
                        t3d_model_draw_skinned(mapRuntime.jukeboxFxModel, &decoType->skeleton);
                    } else {
                        t3d_model_draw(mapRuntime.jukeboxFxModel);
                    }
                }

                t3d_matrix_pop(1);
                // Flush counter for JUKEBOX (draws 2 models)
                decoDrawCount += 2;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_MONITORTABLE) {
                // MONITORTABLE: Draw table normally, draw monitor screen with texture scroll
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Draw table (no scroll)
                t3d_model_draw(decoType->model);

                // Draw monitor screen with texture scroll
                if (mapRuntime.monitorScreenModel) {
                    monitor_screen_init_uvs(mapRuntime.monitorScreenModel);
                    monitor_screen_scroll_uvs(deco->state.monitorTable.textureOffset);
                    t3d_model_draw(mapRuntime.monitorScreenModel);
                }

                t3d_matrix_pop(1);
                // Flush counter for MONITORTABLE (draws 2 models)
                decoDrawCount += 2;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_TURRET) {
                // Skip rendering if turret is dead
                if (deco->state.turret.isDead) continue;

                // TURRET: Rotating cannon (primary) + stationary base + projectiles
                float baseY = deco->posY + renderYOffset;
                float cannonY = baseY + TURRET_CANNON_HEIGHT * deco->scaleY;

                int matIdxCannon = matIdx;
                int matIdxBase = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS + 5;  // Slot beyond MAX_DECORATIONS
                // Don't pre-allocate matIdxProj - each projectile gets unique slot in loop below

                // Draw cannon (primary model with rotation and animation)
                // Uses quaternion composition so pitch is applied in cannon's local frame (after yaw)
                T3DQuat quatYaw, quatPitch, quatCombined;
                float axisY[3] = {0.0f, 1.0f, 0.0f};
                float axisX[3] = {1.0f, 0.0f, 0.0f};
                t3d_quat_from_rotation(&quatYaw, axisY, -deco->state.turret.cannonRotY);
                t3d_quat_from_rotation(&quatPitch, axisX, deco->state.turret.cannonRotX);
                t3d_quat_mul(&quatCombined, &quatYaw, &quatPitch);

                t3d_mat4fp_from_srt(&decoMatFP[matIdxCannon],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    quatCombined.v,
                    (float[3]){deco->posX, cannonY, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdxCannon]);

                // Standard skinned rendering path (animation handled by decoration system)
                if (deco->hasOwnSkeleton) {
                    t3d_skeleton_update(&deco->skeleton);
                    t3d_model_draw_skinned(decoType->model, &deco->skeleton);
                } else {
                    t3d_model_draw(decoType->model);
                }
                t3d_matrix_pop(1);

                // Draw base (stationary, positioned below cannon)
                if (mapRuntime.turretBaseModel) {
                    t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxBase],
                        (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                        (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                        (float[3]){deco->posX, baseY, deco->posZ}
                    );
                    t3d_matrix_push(&decoMatFP[matIdxBase]);
                    t3d_model_draw(mapRuntime.turretBaseModel);
                    t3d_matrix_pop(1);
                }

                // Draw active projectiles (only when fired, not held in front of cannon)
                if (mapRuntime.turretRailModel && deco->state.turret.activeProjectiles > 0) {
                    // Sync pipeline before drawing projectiles to reset render state
                    t3d_tri_sync();
                    rspq_flush();

                    for (int p = 0; p < TURRET_MAX_PROJECTILES; p++) {
                        if (deco->state.turret.projLife[p] <= 0.0f) continue;

                        // Use unique matrix slot for each projectile (beyond MAX_DECORATIONS to avoid collisions)
                        int matIdxProj = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS + 10 + p;

                        // Use cannon rotation stored at fire time (matches cannon's aim direction)
                        // Apply same quaternion composition as cannon for consistent orientation
                        float projRotX = deco->state.turret.projRotX[p];
                        float projRotY = deco->state.turret.projRotY[p];

                        T3DQuat projQuatYaw, projQuatPitch, projQuatCombined;
                        float axisY[3] = {0.0f, 1.0f, 0.0f};
                        float axisX[3] = {1.0f, 0.0f, 0.0f};
                        t3d_quat_from_rotation(&projQuatYaw, axisY, -projRotY);
                        t3d_quat_from_rotation(&projQuatPitch, axisX, projRotX);
                        t3d_quat_mul(&projQuatCombined, &projQuatYaw, &projQuatPitch);

                        // Use dedicated projectile slot (separate from cannon to avoid flickering)
                        // Scale increased to 1.5x for better visibility (was 0.5x)
                        t3d_mat4fp_from_srt(&decoMatFP[matIdxProj],
                            (float[3]){deco->scaleX * 1.5f, deco->scaleY * 1.5f, deco->scaleZ * 1.5f},
                            projQuatCombined.v,
                            (float[3]){deco->state.turret.projPosX[p], deco->state.turret.projPosY[p], deco->state.turret.projPosZ[p]}
                        );
                        t3d_matrix_push(&decoMatFP[matIdxProj]);
                        t3d_model_draw(mapRuntime.turretRailModel);
                        t3d_matrix_pop(1);
                    }
                }

                // Flush counter for TURRET (draws base + cannon + up to 4 projectiles)
                decoDrawCount += 2 + deco->state.turret.activeProjectiles;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_DROID_SEC) {
                // Skip rendering if droid is dead
                if (deco->state.droid.isDead) continue;

                // SECURITY DROID: Render droid body first, then projectiles
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);
                
                // Draw droid with skeleton animation
                if (deco->hasOwnSkeleton && skeleton_is_valid(&deco->skeleton)) {
                    t3d_model_draw_skinned(decoType->model, &deco->skeleton);
                } else {
                    t3d_model_draw(decoType->model);
                }
                t3d_matrix_pop(1);

                // Draw active projectiles
                if (mapRuntime.pulseTurretProjectileModel && deco->state.droid.activeProjectiles > 0) {
                    // Sync pipeline before drawing projectiles
                    t3d_tri_sync();
                    rspq_flush();
                    
                    for (int p = 0; p < DROID_MAX_PROJECTILES; p++) {
                        if (deco->state.droid.projLife[p] <= 0.0f) continue;

                        // Use unique matrix slot for each projectile (beyond MAX_DECORATIONS to avoid collisions)
                        int matIdxProj = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS + 20 + p;

                        // Scale projectile - grows from small over 1.5 seconds
                        float baseScale = 1.0f;
                        float lifeRemaining = deco->state.droid.projLife[p];
                        float lifeElapsed = DROID_BULLET_LIFETIME - lifeRemaining;

                        // Grow from 0.2x to 1.0x over first 1.5 seconds
                        float growDuration = 1.5f;
                        float growMult = 1.0f;
                        if (lifeElapsed < growDuration) {
                            float growProgress = lifeElapsed / growDuration;
                            growMult = 0.2f + (0.8f * growProgress);
                        }
                        float projScale = baseScale * growMult;

                        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxProj],
                            (float[3]){projScale, projScale, projScale},
                            (float[3]){0.0f, 0.0f, 0.0f},
                            (float[3]){deco->state.droid.projPosX[p], deco->state.droid.projPosY[p], deco->state.droid.projPosZ[p]}
                        );
                        t3d_matrix_push(&decoMatFP[matIdxProj]);
                        t3d_model_draw(mapRuntime.pulseTurretProjectileModel);
                        t3d_matrix_pop(1);
                    }
                }

                // Flush counter for DROID (draws body + up to 4 projectiles)
                decoDrawCount += 1 + deco->state.droid.activeProjectiles;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else if (deco->type == DECO_TURRET_PULSE) {
                // Skip rendering if turret is dead
                if (deco->state.pulseTurret.isDead) continue;

                // PULSE TURRET: Rotating cannon (primary with animation) + stationary base + homing projectiles
                float baseY = deco->posY + renderYOffset;
                float cannonY = baseY + PULSE_TURRET_CANNON_HEIGHT * deco->scaleY;

                int matIdxCannon = matIdx;
                int matIdxBase = 110;  // Fixed slot beyond decorations (matching new matrix allocation)
                // Don't pre-allocate matIdxProj - each projectile gets unique slot in loop below

                // Draw cannon (primary model with rotation and animation)
                // Uses quaternion composition so pitch is applied in cannon's local frame (after yaw)
                T3DQuat quatYaw, quatPitch, quatCombined;
                float axisY[3] = {0.0f, 1.0f, 0.0f};
                float axisX[3] = {1.0f, 0.0f, 0.0f};
                t3d_quat_from_rotation(&quatYaw, axisY, -deco->state.pulseTurret.cannonRotY);
                t3d_quat_from_rotation(&quatPitch, axisX, deco->state.pulseTurret.cannonRotX);
                t3d_quat_mul(&quatCombined, &quatYaw, &quatPitch);

                t3d_mat4fp_from_srt(&decoMatFP[matIdxCannon],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    quatCombined.v,
                    (float[3]){deco->posX, cannonY, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdxCannon]);

                // Standard skinned rendering path (animation handled by decoration system)
                if (deco->hasOwnSkeleton) {
                    t3d_skeleton_update(&deco->skeleton);
                    t3d_model_draw_skinned(decoType->model, &deco->skeleton);
                } else {
                    t3d_model_draw(decoType->model);
                }
                t3d_matrix_pop(1);

                // Draw base (stationary, positioned below cannon)
                if (mapRuntime.pulseTurretBaseModel) {
                    t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxBase],
                        (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                        (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                        (float[3]){deco->posX, baseY, deco->posZ}
                    );
                    t3d_matrix_push(&decoMatFP[matIdxBase]);
                    t3d_model_draw(mapRuntime.pulseTurretBaseModel);
                    t3d_matrix_pop(1);
                }

                // Draw active projectiles (homing pulse balls)
                if (mapRuntime.pulseTurretProjectileModel && deco->state.pulseTurret.activeProjectiles > 0) {
                    // Sync pipeline before drawing projectiles
                    t3d_tri_sync();
                    rspq_flush();

                    for (int p = 0; p < PULSE_TURRET_MAX_PROJECTILES; p++) {
                        if (deco->state.pulseTurret.projLife[p] <= 0.0f) continue;

                        // Use unique matrix slot for each projectile (beyond MAX_DECORATIONS to avoid collisions)
                        int matIdxProj = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS + 40 + p;

                        // Get projectile velocity direction for orientation
                        float velX = deco->state.pulseTurret.projVelX[p];
                        float velY = deco->state.pulseTurret.projVelY[p];
                        float velZ = deco->state.pulseTurret.projVelZ[p];
                        float velMag = sqrtf(velX*velX + velY*velY + velZ*velZ);

                        // Calculate yaw and pitch from velocity direction
                        float projRotY = 0.0f;
                        float projRotX = 0.0f;
                        if (velMag > 0.1f) {
                            projRotY = atan2f(-velX, velZ);
                            projRotX = -atan2f(velY, sqrtf(velX*velX + velZ*velZ));
                        }

                        T3DQuat projQuatYaw, projQuatPitch, projQuatCombined;
                        float axisY[3] = {0.0f, 1.0f, 0.0f};
                        float axisX[3] = {1.0f, 0.0f, 0.0f};
                        t3d_quat_from_rotation(&projQuatYaw, axisY, -projRotY);
                        t3d_quat_from_rotation(&projQuatPitch, axisX, projRotX);
                        t3d_quat_mul(&projQuatCombined, &projQuatYaw, &projQuatPitch);

                        // Scale projectile - grows from small over 3 seconds, pulsates, shrinks at end
                        float baseScale = 2.0f;
                        float lifeRemaining = deco->state.pulseTurret.projLife[p];
                        float lifeElapsed = PULSE_TURRET_PROJECTILE_LIFETIME - lifeRemaining;  // Time since fired

                        // Grow from 0.2x to 1.0x over first 3 seconds
                        float growDuration = 3.0f;
                        float growMult = 1.0f;
                        if (lifeElapsed < growDuration) {
                            // Ease out: fast growth at start, slows down at end
                            float growProgress = lifeElapsed / growDuration;
                            growMult = 0.2f + (0.8f * growProgress);
                        }

                        // Pulsating effect - oscillate scale with sine wave (after initial growth)
                        float pulseFreq = 6.0f;  // Pulses per second
                        float pulseAmount = 0.25f;  // +/- 25% scale variation
                        float pulse = 1.0f + sinf(lifeElapsed * pulseFreq * 6.28318f) * pulseAmount;

                        // Shrink during last 2 seconds
                        float shrinkStart = 2.0f;
                        float shrinkMult = 1.0f;
                        if (lifeRemaining < shrinkStart) {
                            shrinkMult = lifeRemaining / shrinkStart;
                            if (shrinkMult < 0.1f) shrinkMult = 0.1f;
                        }
                        float finalScale = baseScale * growMult * pulse * shrinkMult;

                        t3d_mat4fp_from_srt(&decoMatFP[matIdxProj],
                            (float[3]){deco->scaleX * finalScale, deco->scaleY * finalScale, deco->scaleZ * finalScale},
                            projQuatCombined.v,
                            (float[3]){deco->state.pulseTurret.projPosX[p], deco->state.pulseTurret.projPosY[p], deco->state.pulseTurret.projPosZ[p]}
                        );
                        t3d_matrix_push(&decoMatFP[matIdxProj]);
                        t3d_model_draw(mapRuntime.pulseTurretProjectileModel);
                        t3d_matrix_pop(1);
                    }
                }

                // Flush counter for PULSE TURRET (draws base + cannon + up to 4 projectiles)
                decoDrawCount += 2 + deco->state.pulseTurret.activeProjectiles;
                if ((decoDrawCount & 1) == 0) rspq_flush();
                continue;
            } else {
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
            }
            t3d_matrix_push(&decoMatFP[matIdx]);

            // Check if decoration uses vertex colors only (e.g., slime)
            const DecoTypeDef* decoDef = &DECO_TYPES[deco->type];
            if (decoDef->vertexColorsOnly) {
                // Sync pipeline and set combiner for pure vertex colors
                t3d_tri_sync();
                rdpq_sync_pipe();
                rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
            }

            // Use per-instance skeleton if available, otherwise shared skeleton
            // Note: T3D handles combiner setup based on model materials - don't override
            // Validate skeleton and model before use to prevent crash on corrupted/freed data
            if (!decoType->model) {
                // Model was freed or never loaded - skip draw
                t3d_matrix_pop(1);
                continue;
            }

            if (deco->hasOwnSkeleton && skeleton_is_valid(&deco->skeleton)) {
                // DEBUG: Validate skeleton matrices before draw
                if (!skeleton_matrices_valid(&deco->skeleton, "deco_own")) {
                    debugf("[CRASH] Deco %d (type %d) own skeleton corrupted!\n", i, deco->type);
                } else {
                    t3d_model_draw_skinned(decoType->model, &deco->skeleton);
                }
            } else if (decoType->hasSkeleton && skeleton_is_valid(&decoType->skeleton)) {
                // DEBUG: Validate skeleton matrices before draw
                if (!skeleton_matrices_valid(&decoType->skeleton, "deco_shared")) {
                    debugf("[CRASH] Deco %d (type %d) shared skeleton corrupted!\n", i, deco->type);
                } else {
                    t3d_model_draw_skinned(decoType->model, &decoType->skeleton);
                }
            } else {
                t3d_model_draw(decoType->model);
            }

            // Reset combiner after vertex-colors-only model so next textured model works
            if (decoDef->vertexColorsOnly) {
                t3d_tri_sync();
                rdpq_sync_pipe();
                rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
            }

            t3d_matrix_pop(1);

            // Periodic flush to prevent RDP buffer overflow
            // DEBUG: Flush after EVERY skinned model to catch which one causes hang
            decoDrawCount++;
            if (deco->hasOwnSkeleton || decoType->hasSkeleton) {
                rspq_flush();  // Force flush after every skinned model
            } else if ((decoDrawCount & 1) == 0) {
                rspq_flush();
            }
        }
    }

    // Draw highlight around selected decoration in debug camera mode (not placement mode)
    if (debugFlyMode && !debugPlacementMode && debugHighlightedDecoIndex >= 0) {
        DecoInstance* deco = &mapRuntime.decorations[debugHighlightedDecoIndex];
        if (deco->active && deco->type != DECO_NONE) {
            DecoTypeRuntime* decoType = map_get_deco_type(&mapRuntime, deco->type);
            if (decoType && decoType->model) {
                // Draw slightly larger, semi-transparent yellow outline
                int matIdx = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 2;  // Use dedicated slot
                float highlightScale = 1.15f;  // 15% larger for outline effect
                float renderYOffset = 0.2f;
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX * highlightScale, deco->scaleY * highlightScale, deco->scaleZ * highlightScale},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Draw with bright yellow/orange pulsing color
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                // Pulse effect based on frame time
                int pulse = (frameIdx * 40) % 255;
                int pulseColor = 180 + (pulse > 127 ? 255 - pulse : pulse) / 2;
                rdpq_set_prim_color(RGBA32(255, pulseColor, 0, 160));  // Yellow-orange pulse

                // Validate skeleton before use to prevent crash on corrupted/freed data
                if (deco->hasOwnSkeleton && skeleton_is_valid(&deco->skeleton)) {
                    t3d_model_draw_skinned(decoType->model, &deco->skeleton);
                } else if (decoType->hasSkeleton && skeleton_is_valid(&decoType->skeleton)) {
                    t3d_model_draw_skinned(decoType->model, &decoType->skeleton);
                } else {
                    t3d_model_draw(decoType->model);
                }
                t3d_matrix_pop(1);
            }
        }
    }

    // Draw placement preview in debug placement mode
    if (debugFlyMode && debugPlacementMode) {
        DecoTypeRuntime* previewType = map_get_deco_type(&mapRuntime, debugDecoType);
        const DecoTypeDef* previewDef = &DECO_TYPES[debugDecoType];
        if (previewType && previewType->model) {
            // Use a dedicated matrix slot for preview (after all decorations)
            int matIdx = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 1;
            float renderYOffset = 0.2f;
            t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                (float[3]){debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ},
                (float[3]){0.0f, debugDecoRotY, 0.0f},
                (float[3]){debugDecoX, debugDecoY + renderYOffset, debugDecoZ}
            );
            t3d_matrix_push(&decoMatFP[matIdx]);

            // Note: T3D handles combiner setup based on model materials - don't override
            // Validate skeleton before use to prevent crash on corrupted/freed data
            if (previewType->hasSkeleton && skeleton_is_valid(&previewType->skeleton)) {
                t3d_model_draw_skinned(previewType->model, &previewType->skeleton);
            } else {
                t3d_model_draw(previewType->model);
            }
            t3d_matrix_pop(1);
        }
    }

    // Draw patrol point preview in patrol placement mode
    if (debugFlyMode && patrolPlacementMode) {
        DecoTypeRuntime* patrolType = map_get_deco_type(&mapRuntime, DECO_PATROLPOINT);
        if (patrolType && patrolType->model) {
            // Use a dedicated matrix slot for patrol preview
            int matIdx = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 2;
            t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                (float[3]){0.1f, 0.1f, 0.1f},  // Small scale for patrol point
                (float[3]){0.0f, 0.0f, 0.0f},
                (float[3]){debugDecoX, debugDecoY + 0.2f, debugDecoZ}
            );
            t3d_matrix_push(&decoMatFP[matIdx]);
            t3d_model_draw(patrolType->model);
            t3d_matrix_pop(1);
        }
    }

    // Update squash/stretch spring physics
    // Two separate spring systems:
    // 1. Landing squash (< 1.0) springs back faster
    // 2. Jump stretch (> 1.0) springs back slower
    if (!isJumping || isCharging) {
        // Landing squash recovery (faster spring - original values * 2)
        const float SQUASH_SPRING = 16.0f;    // 2x faster for landing recovery
        const float SQUASH_DAMPING = 8.0f;    // 2x damping
        float targetLandingSquash = 0.0f;
        float springForce = (targetLandingSquash - landingSquash) * SQUASH_SPRING;
        squashVelocity += springForce * DELTA_TIME;
        squashVelocity *= (1.0f - SQUASH_DAMPING * DELTA_TIME);
        landingSquash += squashVelocity * DELTA_TIME;

        // Clamp landing squash to reasonable range
        if (landingSquash < 0.0f) landingSquash = 0.0f;
        if (landingSquash > 0.95f) landingSquash = 0.95f;

        // Update total squash scale
        squashScale = 1.0f - landingSquash - chargeSquash;

        // Clamp final scale
        if (squashScale < 0.05f) squashScale = 0.05f;
        if (squashScale > 1.3f) squashScale = 1.3f;
    } else {
        // Jump stretch recovery (slower spring - original values)
        const float STRETCH_SPRING = 8.0f;
        const float STRETCH_DAMPING = 4.0f;
        float targetScale = 1.0f;
        float springForce = (targetScale - squashScale) * STRETCH_SPRING;
        squashVelocity += springForce * DELTA_TIME;
        squashVelocity *= (1.0f - STRETCH_DAMPING * DELTA_TIME);
        squashScale += squashVelocity * DELTA_TIME;

        // Clamp to reasonable range
        if (squashScale < 0.05f) squashScale = 0.05f;
        if (squashScale > 1.3f) squashScale = 1.3f;
    }

    // Draw level 3 special decorations at origin (no culling, always visible)
    // Must sync before calling - required for UV scrolling to work correctly
    t3d_tri_sync();
    rdpq_sync_pipe();

    // level3_special_draw removed

    // Flush after decoration rendering to prevent RDP buffer overflow
    rspq_flush();

    HEAP_CHECK("post_decos");

    g_renderDecoTicks += get_ticks() - decoRenderStart;

    // Draw player model with skeleton based on current part
    // Skip render on alternating frames during invincibility (flash effect)
    // Also skip during level transitions when models are being freed/reloaded
    uint32_t playerRenderStart = get_ticks();
    bool shouldRenderPlayer = !skipDecoDrawing &&
                              ((playerInvincibilityTimer <= 0.0f) ||
                               ((playerInvincibilityFlashFrame / INVINCIBILITY_FLASH_RATE) % 2 == 0));

    // Render cutscene model if cutscene is playing
    if (cutsceneFalloffPlaying && cutsceneModelLoaded && cutsceneModel) {
        float csScale = 0.40f;  // Same scale as player
        t3d_mat4fp_from_srt_euler(&roboMatFP[frameIdx],
            (float[3]){csScale, csScale, csScale},
            (float[3]){0.0f, cutsceneFalloffRotY, 0.0f},
            (float[3]){cutsceneFalloffX, cutsceneFalloffY, cutsceneFalloffZ}
        );
        t3d_matrix_push(&roboMatFP[frameIdx]);
        t3d_model_draw_skinned(cutsceneModel, &cutsceneSkel);
        t3d_matrix_pop(1);
        rspq_flush();
    }
    // Render normal player if not hidden by cutscene
    else if (shouldRenderPlayer && torsoModel && !cutscenePlayerHidden) {
        // Apply squash: Y compressed, X/Z expanded to preserve volume
        // Also apply cheat scale (giant = 2.0, tiny = 0.5, normal = 1.0)
        float baseScale = 0.40f * g_playerScaleCheat;
        float scaleY = baseScale * squashScale;
        float scaleXZ = baseScale * (1.0f + (1.0f - squashScale) * 0.5f);  // Expand as Y squashes

        // Apply propeller jump tilt during hover (rising or falling with tilt)
        // World-frame tilt: model leans toward tilt direction regardless of facing
        float tiltMag = sqrtf(fbHoverTiltX * fbHoverTiltX + fbHoverTiltZ * fbHoverTiltZ);
        if (fbIsHovering && tiltMag > 0.5f) {
            // Build rotation: world-frame pitch/roll first, then yaw
            T3DQuat quatYaw, quatPitchWorld, quatRollWorld, quatTemp, quatCombined;
            float axisY[3] = {0.0f, 1.0f, 0.0f};
            float axisX[3] = {1.0f, 0.0f, 0.0f};  // World X axis (pitch axis for leaning toward +Z)
            float axisZ[3] = {0.0f, 0.0f, 1.0f};  // World Z axis (roll axis for leaning toward +X)

            // World-frame tilt:
            // fbHoverTiltZ = lean toward world +Z → rotate around world X (pitch forward)
            // fbHoverTiltX = lean toward screen direction (negative X = screen right)
            // Negate rollRad because: positive Z rotation tips toward -X (screen right)
            float pitchRad = fbHoverTiltZ * (M_PI / 180.0f);   // Lean toward +Z = pitch forward
            float rollRad = -fbHoverTiltX * (M_PI / 180.0f);   // Negative tiltX → positive roll → lean screen right
            
            // Compose rotations: tilt first (world frame), then yaw
            // Order: pitch_world * roll_world * yaw means:
            //   - First apply yaw (face direction)
            //   - Then roll around world Z (lean toward +X/-X)
            //   - Then pitch around world X (lean toward +Z/-Z)
            t3d_quat_from_rotation(&quatYaw, axisY, playerState.playerAngle);
            t3d_quat_from_rotation(&quatPitchWorld, axisX, pitchRad);
            t3d_quat_from_rotation(&quatRollWorld, axisZ, rollRad);

            // Build combined: pitch * roll * yaw (applies right-to-left)
            t3d_quat_mul(&quatTemp, &quatRollWorld, &quatYaw);        // roll * yaw
            t3d_quat_mul(&quatCombined, &quatPitchWorld, &quatTemp);  // pitch * (roll * yaw)
            
            t3d_mat4fp_from_srt(&roboMatFP[frameIdx],
                (float[3]){scaleXZ, scaleY, scaleXZ},
                quatCombined.v,
                (float[3]){renderCubeX, renderCubeY, renderCubeZ}
            );
        } else {
            // Normal rendering with just yaw rotation
            t3d_mat4fp_from_srt_euler(&roboMatFP[frameIdx],
                (float[3]){scaleXZ, scaleY, scaleXZ},
                (float[3]){0.0f, playerState.playerAngle, 0.0f},
                (float[3]){renderCubeX, renderCubeY, renderCubeZ}
            );
        }
        
        t3d_matrix_push(&roboMatFP[frameIdx]);
        if (torsoModel && skeleton_is_valid(&torsoSkel)) {
            // DEBUG: Validate skeleton matrices before draw to catch corruption
            if (!skeleton_matrices_valid(&torsoSkel, "player")) {
                debugf("[CRASH] Player skeleton matrices corrupted! Skipping draw.\n");
            } else {
                // Swap to FX texture when buff is active and in flash state
                bool useBuffFx = buffFlashState && buffFxSprite &&
                                 (buffJumpActive || buffGlideActive || buffSpeedTimer > 0.0f);
                sprite_t* savedTexture = NULL;

                if (useBuffFx) {
                    // Get first object's material and swap texture
                    T3DObject* obj = t3d_model_get_object_by_index(torsoModel, 0);
                    if (obj && obj->material && obj->material->textureA.texture) {
                        savedTexture = obj->material->textureA.texture;
                        obj->material->textureA.texture = buffFxSprite;
                    }
                }

                t3d_model_draw_skinned(torsoModel, &torsoSkel);

                // Restore original texture
                if (savedTexture) {
                    T3DObject* obj = t3d_model_get_object_by_index(torsoModel, 0);
                    if (obj && obj->material) {
                        obj->material->textureA.texture = savedTexture;
                    }
                }
            }
        }
        t3d_matrix_pop(1);

        // Flush after player model to prevent RDP buffer overflow
        rspq_flush();
    }
    g_renderPlayerTicks += get_ticks() - playerRenderStart;

    // Draw player shadow on ground using rdpq_triangle (screen-space) - only when airborne
    uint32_t shadowRenderStart = get_ticks();
    if (shadowSprite && bestGroundY > -9000.0f && !playerState.isGrounded) {
        // Get ground normal at player position (use interpolated position for visual consistency)
        float groundNX, groundNY, groundNZ;
        maploader_get_ground_height_normal(&mapLoader, renderCubeX, renderCubeY, renderCubeZ, &groundNX, &groundNY, &groundNZ);

        // Shadow size based on height above ground (smaller when higher)
        float heightAboveGround = renderCubeY - bestGroundY;
        float shadowAlpha = fmaxf(0.0f, 1.0f - heightAboveGround / 150.0f);
        if (shadowAlpha > 0.0f) {
            float shadowScale = 6.0f * fmaxf(0.4f, 1.0f - heightAboveGround / 100.0f);
            float shadowY = bestGroundY + 1.0f;  // Slightly above ground

            // Calculate 4 corner positions projected onto the slope
            // For each corner offset in XZ, calculate the Y based on slope
            // Y offset = -(normalX * dx + normalZ * dz) / normalY
            float sz = shadowScale;
            T3DVec3 corners[4];
            float offsets[4][2] = {{-sz, -sz}, {sz, -sz}, {-sz, sz}, {sz, sz}};

            for (int c = 0; c < 4; c++) {
                float dx = offsets[c][0];
                float dz = offsets[c][1];
                float dy = 0.0f;
                if (fabsf(groundNY) > 0.01f) {
                    // Project onto slope: solve for Y where point lies on plane
                    dy = -(groundNX * dx + groundNZ * dz) / groundNY;
                }
                corners[c] = (T3DVec3){{renderCubeX + dx, shadowY + dy, renderCubeZ + dz}};
            }

            // Convert world positions to screen positions
            T3DVec3 screenPos[4];
            for (int i = 0; i < 4; i++) {
                t3d_viewport_calc_viewspace_pos(&viewport, &screenPos[i], &corners[i]);
            }

            // Only draw if at least one corner is in front of camera
            if (!(screenPos[0].v[2] < 0 && screenPos[1].v[2] < 0 && screenPos[2].v[2] < 0 && screenPos[3].v[2] < 0)) {
                // Sync T3D before using rdpq directly
                t3d_tri_sync();
                rdpq_sync_pipe();

                // Set up render mode FIRST, then upload texture
                rdpq_set_mode_standard();
                rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
                rdpq_set_prim_color(RGBA32(0, 0, 0, (int)(shadowAlpha * 180)));
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

                // Upload shadow texture using sprite upload (handles format automatically)
                rdpq_sprite_upload(TILE0, shadowSprite, NULL);

                // Build vertex data for rdpq_triangle: X, Y, S, T, INV_W
                float v0[] = {screenPos[0].v[0], screenPos[0].v[1], 0.0f, 0.0f, 1.0f};
                float v1[] = {screenPos[1].v[0], screenPos[1].v[1], 8.0f, 0.0f, 1.0f};
                float v2[] = {screenPos[2].v[0], screenPos[2].v[1], 0.0f, 8.0f, 1.0f};
                float v3[] = {screenPos[3].v[0], screenPos[3].v[1], 8.0f, 8.0f, 1.0f};

                // Draw two triangles for the quad
                rdpq_triangle(&TRIFMT_TEX, v0, v1, v2);
                rdpq_triangle(&TRIFMT_TEX, v2, v1, v3);
            }
        }
    }

    // Draw slime oil trail decals as 3D quads (proper Z-buffer integration)
    if (shadowSprite) {
        // Set up static vertex data once (unit quad centered at origin)
        int16_t *pos0 = t3d_vertbuffer_get_pos(decalVerts, 0);
        int16_t *pos1 = t3d_vertbuffer_get_pos(decalVerts, 1);
        int16_t *pos2 = t3d_vertbuffer_get_pos(decalVerts, 2);
        int16_t *pos3 = t3d_vertbuffer_get_pos(decalVerts, 3);
        pos0[0] = -1; pos0[1] = 0; pos0[2] = -1;
        pos1[0] =  1; pos1[1] = 0; pos1[2] = -1;
        pos2[0] = -1; pos2[1] = 0; pos2[2] =  1;
        pos3[0] =  1; pos3[1] = 0; pos3[2] =  1;

        // UVs: full 8x8 texture coverage
        // Empirically determined: 8 * 32 * 2.5 = 640 works for 8x8 texture
        // Vertex layout: 0=(-1,-1), 1=(+1,-1), 2=(-1,+1), 3=(+1,+1)
        // UV origin is top-left, V increases downward
        int16_t uvMax = 8 * 80;  // 640
        int16_t *uv0 = t3d_vertbuffer_get_uv(decalVerts, 0);
        int16_t *uv1 = t3d_vertbuffer_get_uv(decalVerts, 1);
        int16_t *uv2 = t3d_vertbuffer_get_uv(decalVerts, 2);
        int16_t *uv3 = t3d_vertbuffer_get_uv(decalVerts, 3);
        // vert 0 (-1,-1) -> UV (0, uvMax) bottom-left
        // vert 1 (+1,-1) -> UV (uvMax, uvMax) bottom-right
        // vert 2 (-1,+1) -> UV (0, 0) top-left
        // vert 3 (+1,+1) -> UV (uvMax, 0) top-right
        uv0[0] = 0;     uv0[1] = uvMax;
        uv1[0] = uvMax; uv1[1] = uvMax;
        uv2[0] = 0;     uv2[1] = 0;
        uv3[0] = uvMax; uv3[1] = 0;

        // Set vertex colors (will be tinted by prim color)
        *t3d_vertbuffer_get_color(decalVerts, 0) = 0xFFFFFFFF;
        *t3d_vertbuffer_get_color(decalVerts, 1) = 0xFFFFFFFF;
        *t3d_vertbuffer_get_color(decalVerts, 2) = 0xFFFFFFFF;
        *t3d_vertbuffer_get_color(decalVerts, 3) = 0xFFFFFFFF;

        // Sync and set up render mode once
        t3d_tri_sync();
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_zbuf(true, false);  // Z compare but no write
        rdpq_sprite_upload(TILE0, shadowSprite, NULL);
        // No culling so both sides of quad are visible
        t3d_state_set_drawflags(T3D_FLAG_DEPTH | T3D_FLAG_TEXTURED);

        for (int i = 0; i < mapRuntime.decoCount; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            if (!deco->active || (deco->type != DECO_SLIME && deco->type != DECO_SLIME_LAVA)) continue;

            // Draw each active decal for this slime
            for (int d = 0; d < 5; d++) {
                float alpha = deco->state.slime.decalAlpha[d];
                if (alpha <= 0.01f) continue;

                float decalX = deco->state.slime.decalX[d];
                float decalY = deco->state.slime.decalY[d];
                float decalZ = deco->state.slime.decalZ[d];
                float decalScale = deco->state.slime.decalScale;

                // Skip if line of sight from camera to decal is blocked
                if (maploader_raycast_blocked(&mapLoader,
                    camPos.v[0], camPos.v[1], camPos.v[2],
                    decalX, decalY + 1.0f, decalZ)) {
                    continue;
                }

                // Set up transformation matrix for decal position (raised to avoid z-fighting)
                t3d_mat4fp_from_srt_euler(decalMatFP,
                    (float[3]){decalScale, 1.0f, decalScale},
                    (float[3]){0, 0, 0},
                    (float[3]){decalX, decalY + 3.0f, decalZ}
                );

                // Set color with alpha for this decal (lava slimes use orange/red, regular use dark)
                if (deco->type == DECO_SLIME_LAVA) {
                    rdpq_set_prim_color(RGBA32(255, 80, 20, (int)(alpha * 220)));  // Lava orange
                } else {
                    rdpq_set_prim_color(RGBA32(20, 20, 20, (int)(alpha * 200)));   // Oil dark
                }

                // Re-upload texture before each decal (t3d may overwrite TMEM)
                rdpq_sprite_upload(TILE0, shadowSprite, NULL);

                // Draw quad as two triangles
                t3d_matrix_push(decalMatFP);
                t3d_vert_load(decalVerts, 0, 4);
                // Verts: 0=(-1,-1), 1=(1,-1), 2=(-1,1), 3=(1,1)
                // Triangle 1: 0-1-3 (bottom-left, bottom-right, top-right)
                // Triangle 2: 0-3-2 (bottom-left, top-right, top-left)
                t3d_tri_draw(0, 1, 3);
                t3d_tri_draw(0, 3, 2);
                t3d_matrix_pop(1);

                // Sync after each decal to ensure matrix/verts are consumed before reuse
                rspq_flush();
            }
        }
    }

    // Draw oil puddle decals as 3D quads (proper Z-buffer integration)
    // Note: vertex data and render mode already set up by slime decals above
    if (shadowSprite) {
        // Re-sync and set up for oil puddles (different color)
        t3d_tri_sync();
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_zbuf(true, false);
        rdpq_sprite_upload(TILE0, shadowSprite, NULL);
        t3d_state_set_drawflags(T3D_FLAG_DEPTH | T3D_FLAG_TEXTURED);
        rdpq_set_prim_color(RGBA32(40, 20, 50, 180));

        for (int i = 0; i < mapRuntime.decoCount; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            if (!deco->active || deco->type != DECO_OILPUDDLE) continue;

            // Skip if line of sight from camera to puddle is blocked
            if (maploader_raycast_blocked(&mapLoader,
                camPos.v[0], camPos.v[1], camPos.v[2],
                deco->posX, deco->posY + 1.0f, deco->posZ)) {
                continue;
            }

            float sz = deco->state.oilpuddle.radius;

            // Set up transformation matrix for decal position (raised to avoid z-fighting)
            t3d_mat4fp_from_srt_euler(decalMatFP,
                (float[3]){sz, 1.0f, sz},
                (float[3]){0, 0, 0},
                (float[3]){deco->posX, deco->posY + 3.0f, deco->posZ}
            );

            // Re-upload texture before each decal (t3d may overwrite TMEM)
            rdpq_sprite_upload(TILE0, shadowSprite, NULL);

            // Draw quad as two triangles
            t3d_matrix_push(decalMatFP);
            t3d_vert_load(decalVerts, 0, 4);
            t3d_tri_draw(0, 1, 3);
            t3d_tri_draw(0, 3, 2);
            t3d_matrix_pop(1);

            // Sync after each decal to ensure matrix/verts are consumed before reuse
            rspq_flush();
        }
    }

    // Draw death decals (slime death splats that fade over time)
    if (shadowSprite) {
        t3d_tri_sync();
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_zbuf(true, false);
        rdpq_sprite_upload(TILE0, shadowSprite, NULL);
        t3d_state_set_drawflags(T3D_FLAG_DEPTH | T3D_FLAG_TEXTURED);

        for (int i = 0; i < MAX_DEATH_DECALS; i++) {
            DeathDecal* decal = &g_deathDecals[i];
            if (!decal->active) continue;

            // Set color with fading alpha (lava slimes use orange, regular use dark)
            uint8_t alpha = (uint8_t)(decal->alpha * 200.0f);
            if (alpha > 200) alpha = 200;
            if (decal->isLava) {
                rdpq_set_prim_color(RGBA32(255, 80, 20, alpha));  // Lava orange
            } else {
                rdpq_set_prim_color(RGBA32(20, 20, 20, alpha));   // Oil dark
            }

            // Set up transformation matrix
            t3d_mat4fp_from_srt_euler(decalMatFP,
                (float[3]){decal->scale, 1.0f, decal->scale},
                (float[3]){0, 0, 0},
                (float[3]){decal->x, decal->y + 3.0f, decal->z}
            );

            rdpq_sprite_upload(TILE0, shadowSprite, NULL);

            t3d_matrix_push(decalMatFP);
            t3d_vert_load(decalVerts, 0, 4);
            t3d_tri_draw(0, 1, 3);
            t3d_tri_draw(0, 3, 2);
            t3d_matrix_pop(1);

            rspq_flush();
        }
    }

    HEAP_CHECK("pre_particles");

    // Draw particles (simple screen-space quads)
    {
        bool hasActiveParticles = false;
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (g_particles[i].active) {
                hasActiveParticles = true;
                break;
            }
        }

        if (hasActiveParticles) {
            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_zbuf(true, false);  // Enable depth test (compare), disable depth write

            for (int i = 0; i < MAX_PARTICLES; i++) {
                Particle* p = &g_particles[i];
                if (!p->active) continue;

                // Convert world position to screen position
                T3DVec3 worldPos = {{p->x, p->y, p->z}};
                T3DVec3 screenPos;
                t3d_viewport_calc_viewspace_pos(&viewport, &screenPos, &worldPos);

                // Skip if behind camera
                if (screenPos.v[2] <= 0) continue;

                // Calculate alpha based on remaining life
                float lifeRatio = p->life / p->maxLife;
                uint8_t alpha = (uint8_t)(180.0f * lifeRatio);

                // Draw as a small filled rectangle
                float halfSize = p->size;
                float sx = screenPos.v[0];
                float sy = screenPos.v[1];

                // Skip if off screen (256x240 resolution)
                if (sx < -halfSize || sx > 256 + halfSize || sy < -halfSize || sy > 240 + halfSize) continue;

                rdpq_set_prim_color(RGBA32(p->r, p->g, p->b, alpha));

                // Draw as two triangles forming a quad with depth
                // screenPos.v[2] contains the depth value from viewport transformation
                float depth = screenPos.v[2];
                float v0[] = {sx - halfSize, sy - halfSize, depth};
                float v1[] = {sx + halfSize, sy - halfSize, depth};
                float v2[] = {sx - halfSize, sy + halfSize, depth};
                float v3[] = {sx + halfSize, sy + halfSize, depth};

                rdpq_triangle(&TRIFMT_ZBUF, v0, v1, v2);
                rdpq_triangle(&TRIFMT_ZBUF, v2, v1, v3);
            }
            rdpq_sync_pipe();  // Sync after particle triangles to prevent RDP command accumulation
        }
    }

    // Draw impact stars (orbiting around player's head)
    if (g_impactStarsTimer > 0.0f) {
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        // Calculate alpha based on remaining time (fade out in last 0.5 seconds)
        float fadeRatio = g_impactStarsTimer < 0.5f ? (g_impactStarsTimer / 0.5f) : 1.0f;
        uint8_t alpha = (uint8_t)(220.0f * fadeRatio);

        // Draw 4 stars orbiting around the player's head position (use interpolated position)
        for (int i = 0; i < IMPACT_STAR_COUNT; i++) {
            float angle = g_impactStarsAngle + (float)i * (6.28318f / IMPACT_STAR_COUNT);
            float offsetX = cosf(angle) * IMPACT_STAR_RADIUS;
            float offsetZ = sinf(angle) * IMPACT_STAR_RADIUS;

            T3DVec3 starWorld = {{renderCubeX + offsetX, renderCubeY + IMPACT_STAR_HEIGHT, renderCubeZ + offsetZ}};
            T3DVec3 starScreen;
            t3d_viewport_calc_viewspace_pos(&viewport, &starScreen, &starWorld);

            // Skip if behind camera
            if (starScreen.v[2] <= 0) continue;

            float sx = starScreen.v[0];
            float sy = starScreen.v[1];

            // Skip if off screen
            if (sx < -10 || sx > 330 || sy < -10 || sy > 250) continue;

            // Draw as a 4-pointed star (yellow)
            rdpq_set_prim_color(RGBA32(255, 255, 0, alpha));

            // Star points (simple 4-pointed star using triangles)
            float starSize = 4.0f;
            float innerSize = 1.5f;

            // Top point
            float top[] = {sx, sy - starSize};
            float topL[] = {sx - innerSize, sy};
            float topR[] = {sx + innerSize, sy};
            rdpq_triangle(&TRIFMT_FILL, top, topL, topR);

            // Right point
            float right[] = {sx + starSize, sy};
            float rightT[] = {sx, sy - innerSize};
            float rightB[] = {sx, sy + innerSize};
            rdpq_triangle(&TRIFMT_FILL, right, rightT, rightB);

            // Bottom point
            float bottom[] = {sx, sy + starSize};
            float bottomL[] = {sx - innerSize, sy};
            float bottomR[] = {sx + innerSize, sy};
            rdpq_triangle(&TRIFMT_FILL, bottom, bottomL, bottomR);

            // Left point
            float left[] = {sx - starSize, sy};
            float leftT[] = {sx, sy - innerSize};
            float leftB[] = {sx, sy + innerSize};
            rdpq_triangle(&TRIFMT_FILL, left, leftT, leftB);
        }
    }

    // Draw jump arc prediction IN 3D SPACE (while charging in torso mode, only after hop threshold)
    if (isCharging && jumpChargeTime >= hopThreshold && currentPart == PART_TORSO && arcDotVerts) {
        // CRITICAL: Sync and set up flat-shaded mode for bright arc dots
        t3d_tri_sync();
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);  // Flat color for bright visibility
        t3d_state_set_drawflags(T3D_FLAG_DEPTH | T3D_FLAG_CULL_BACK);

        // Calculate predicted jump velocity based on current charge and stick direction
        // Include triple jump combo power multiplier for accurate arc preview
        float comboPowerMult = (jumpComboCount == 0) ? JUMP_COMBO_POWER_MULT_1 :
                               (jumpComboCount == 1) ? JUMP_COMBO_POWER_MULT_2 :
                                                       JUMP_COMBO_POWER_MULT_3;
        float predictedVelY = (chargeJumpEarlyBase + jumpChargeTime * chargeJumpEarlyMultiplier) * comboPowerMult;
        // Account for chargepad 2x jump buff in arc preview
        if (buffJumpActive) {
            predictedVelY *= 2.0f;
        }

        // Use stick direction for arc - matches actual jump behavior
        float aimMag = sqrtf(jumpAimX * jumpAimX + jumpAimY * jumpAimY);
        if (aimMag > 1.0f) aimMag = 1.0f;

        // Horizontal scale must match the jump logic
        const float HORIZONTAL_SCALE = 0.4f;
        float predictedForward = (3.0f + 2.0f * jumpChargeTime) * FPS_SCALE * aimMag * comboPowerMult;
        float predictedVelX, predictedVelZ;
        if (aimMag > 0.1f) {
            // Apply horizontal scale uniformly to both axes to match actual jump
            predictedVelX = (jumpAimX / aimMag) * predictedForward * HORIZONTAL_SCALE;
            predictedVelZ = (jumpAimY / aimMag) * predictedForward * HORIZONTAL_SCALE;
        } else {
            predictedVelX = 0.0f;
            predictedVelZ = 0.0f;
        }

        // Simulate trajectory - check ground only every 10 frames to save performance
        float simVelX = predictedVelX;
        float simVelY = predictedVelY;
        float simVelZ = predictedVelZ;
        float simX = cubeX;
        float simY = cubeY;
        float simZ = cubeZ;
        float landX = cubeX, landY = groundLevel, landZ = cubeZ;

        const int maxDots = 6;
        int dotInterval = 4;
        int dotsDrawn = 0;

        for (int frame = 0; frame < 60; frame++) {
            simX += simVelX;
            simY += simVelY;
            simZ += simVelZ;
            simVelY -= GRAVITY;

            // Check ground only every 10 frames (expensive operation)
            if (frame % 10 == 0) {
                float groundY = maploader_get_ground_height(&mapLoader, simX, simY + 50.0f, simZ);
                if (groundY > INVALID_GROUND_Y + 10.0f) {
                    landY = groundY;
                }
            }

            // Check if we've landed
            if (simY <= landY + 2.0f) {
                landX = simX;
                landZ = simZ;
                break;
            }

            // Draw arc dots as cubes (color based on jump combo)
            if (frame > 2 && frame % dotInterval == 0 && dotsDrawn < maxDots) {
                float dotSize = 3.0f;  // World units
                t3d_mat4fp_from_srt_euler(&arcMatFP[dotsDrawn],
                    (float[3]){dotSize, dotSize, dotSize},
                    (float[3]){0, frame * 2.0f, 0},
                    (float[3]){simX, simY, simZ}
                );
                t3d_matrix_push(&arcMatFP[dotsDrawn]);
                // Arc color based on jump combo: blue (1st), orange (2nd), red (3rd)
                color_t arcColor = (jumpComboCount == 0) ? RGBA32(100, 200, 255, 220) :  // Blue
                                   (jumpComboCount == 1) ? RGBA32(255, 165, 50, 220) :   // Orange
                                                           RGBA32(255, 50, 50, 220);     // Red
                rdpq_set_prim_color(arcColor);
                t3d_vert_load(arcDotVerts, 0, 8);
                // Draw 6 faces (12 triangles) - CCW winding for front faces
                // Front face (z+): 3, 2, 6, 7
                t3d_tri_draw(3, 2, 6);
                t3d_tri_draw(3, 6, 7);
                // Back face (z-): 1, 0, 4, 5
                t3d_tri_draw(1, 0, 4);
                t3d_tri_draw(1, 4, 5);
                // Top face (y+): 7, 6, 5, 4
                t3d_tri_draw(7, 6, 5);
                t3d_tri_draw(7, 5, 4);
                // Bottom face (y-): 0, 1, 2, 3
                t3d_tri_draw(0, 1, 2);
                t3d_tri_draw(0, 2, 3);
                // Right face (x+): 2, 1, 5, 6
                t3d_tri_draw(2, 1, 5);
                t3d_tri_draw(2, 5, 6);
                // Left face (x-): 0, 3, 7, 4
                t3d_tri_draw(0, 3, 7);
                t3d_tri_draw(0, 7, 4);
                t3d_matrix_pop(1);
                dotsDrawn++;
            }

            // Update landing position
            landX = simX;
            landZ = simZ;
        }

        // Draw landing marker as cube (yellow, bright)
        float dotSize = 5.0f;  // Larger for landing marker
        t3d_mat4fp_from_srt_euler(&arcMatFP[dotsDrawn],
            (float[3]){dotSize, dotSize, dotSize},
            (float[3]){0, 0, 0},
            (float[3]){landX, landY + 3.0f, landZ}
        );
        t3d_matrix_push(&arcMatFP[dotsDrawn]);
        rdpq_set_prim_color(RGBA32(255, 255, 100, 240));  // Yellow
        t3d_vert_load(arcDotVerts, 0, 8);
        // Draw 6 faces (12 triangles) - CCW winding for front faces
        t3d_tri_draw(3, 2, 6); t3d_tri_draw(3, 6, 7);  // Front
        t3d_tri_draw(1, 0, 4); t3d_tri_draw(1, 4, 5);  // Back
        t3d_tri_draw(7, 6, 5); t3d_tri_draw(7, 5, 4);  // Top
        t3d_tri_draw(0, 1, 2); t3d_tri_draw(0, 2, 3);  // Bottom
        t3d_tri_draw(2, 1, 5); t3d_tri_draw(2, 5, 6);  // Right
        t3d_tri_draw(0, 3, 7); t3d_tri_draw(0, 7, 4);  // Left
        t3d_matrix_pop(1);

        // Sync T3D triangles after all arc dots are drawn
        t3d_tri_sync();

        // Store arc end X for camera lerping
        jumpArcEndX = landX;
    } else {
        // Reset arc end to player position when not charging
        jumpArcEndX = cubeX;
    }

    HEAP_CHECK("pre_2d");

    // Sync before switching to 2D mode for debug/UI
    t3d_tri_sync();
    rdpq_sync_pipe();
    rdpq_set_mode_standard();

    // Debug collision visualization - draw all collision triangles as wireframe
    if (debugFlyMode && debugShowCollision) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        // Draw collision triangles for active map segments
        for (int s = 0; s < mapLoader.count; s++) {
            MapSegment* seg = &mapLoader.segments[s];
            if (!seg->active || !seg->collision) continue;

            CollisionMesh* mesh = seg->collision;
            for (int i = 0; i < mesh->count; i++) {
                CollisionTriangle* t = &mesh->triangles[i];

                // Apply 90° Y rotation and scale (same as collision code)
                float x0 = -t->z0 * seg->scaleX + seg->posX;
                float y0 = t->y0 * seg->scaleY + seg->posY;
                float z0 = t->x0 * seg->scaleZ + seg->posZ;
                float x1 = -t->z1 * seg->scaleX + seg->posX;
                float y1 = t->y1 * seg->scaleY + seg->posY;
                float z1 = t->x1 * seg->scaleZ + seg->posZ;
                float x2 = -t->z2 * seg->scaleX + seg->posX;
                float y2 = t->y2 * seg->scaleY + seg->posY;
                float z2 = t->x2 * seg->scaleZ + seg->posZ;

                // Skip triangles far from camera
                float camX = debugFlyMode ? debugCamX : cubeX;
                float camZ = debugFlyMode ? debugCamZ : cubeZ;
                float centerX = (x0 + x1 + x2) / 3.0f;
                float centerZ = (z0 + z1 + z2) / 3.0f;
                float dist = sqrtf((centerX - camX) * (centerX - camX) + (centerZ - camZ) * (centerZ - camZ));
                if (dist > 300.0f) continue;

                // Calculate triangle normal to determine color
                float ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
                float vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
                float nx = uy * vz - uz * vy;
                float ny = uz * vx - ux * vz;
                float nz = ux * vy - uy * vx;
                float len = sqrtf(nx*nx + ny*ny + nz*nz);
                if (len > 0.001f) { nx /= len; ny /= len; nz /= len; }

                // Color based on normal: green=floor, red=wall, blue=ceiling
                color_t triColor;
                if (ny > 0.3f) {
                    triColor = RGBA32(0, 255, 0, 100);  // Floor/slope - green
                } else if (ny < -0.7f) {
                    triColor = RGBA32(0, 0, 255, 100);  // Ceiling - blue
                } else {
                    triColor = RGBA32(255, 0, 0, 100);  // Wall - red
                }

                // Project vertices to screen using view-projection matrix
                T3DVec3 world0 = {{x0, y0, z0}};
                T3DVec3 world1 = {{x1, y1, z1}};
                T3DVec3 world2 = {{x2, y2, z2}};
                T3DVec4 clip0, clip1, clip2;

                // Transform to clip space using the combined view-projection matrix
                t3d_mat4_mul_vec3(&clip0, &viewport.matCamProj, &world0);
                t3d_mat4_mul_vec3(&clip1, &viewport.matCamProj, &world1);
                t3d_mat4_mul_vec3(&clip2, &viewport.matCamProj, &world2);

                // Skip if all behind camera (w < 0 or very small)
                if (clip0.v[3] < 0.1f && clip1.v[3] < 0.1f && clip2.v[3] < 0.1f) continue;

                // Perspective divide and convert to screen coordinates
                float screenW = 160.0f;  // Half screen width
                float screenH = 120.0f;  // Half screen height

                float sx0 = (clip0.v[3] > 0.1f) ? (screenW + clip0.v[0] / clip0.v[3] * screenW) : -1000;
                float sy0 = (clip0.v[3] > 0.1f) ? (screenH - clip0.v[1] / clip0.v[3] * screenH) : -1000;
                float sx1 = (clip1.v[3] > 0.1f) ? (screenW + clip1.v[0] / clip1.v[3] * screenW) : -1000;
                float sy1 = (clip1.v[3] > 0.1f) ? (screenH - clip1.v[1] / clip1.v[3] * screenH) : -1000;
                float sx2 = (clip2.v[3] > 0.1f) ? (screenW + clip2.v[0] / clip2.v[3] * screenW) : -1000;
                float sy2 = (clip2.v[3] > 0.1f) ? (screenH - clip2.v[1] / clip2.v[3] * screenH) : -1000;

                // Skip if any vertex is way off screen (simple clipping)
                if (sx0 < -500 || sx0 > 820 || sy0 < -500 || sy0 > 740) continue;
                if (sx1 < -500 || sx1 > 820 || sy1 < -500 || sy1 > 740) continue;
                if (sx2 < -500 || sx2 > 820 || sy2 < -500 || sy2 > 740) continue;

                // Draw filled triangle
                rdpq_set_prim_color(triColor);
                float v0[] = {sx0, sy0};
                float v1[] = {sx1, sy1};
                float v2[] = {sx2, sy2};
                rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
            }
        }

        // Draw decoration collision meshes (transparent blue)
        for (int d = 0; d < mapRuntime.decoCount; d++) {
            DecoInstance* deco = &mapRuntime.decorations[d];
            if (!deco->active || deco->type == DECO_NONE) continue;

            DecoTypeRuntime* decoType = &mapRuntime.decoTypes[deco->type];
            if (!decoType->loaded || !decoType->collision) continue;

            CollisionMesh* mesh = decoType->collision;

            // For SIGN: apply extra rotation to match visual (baseRotY + 180° and tilt on Z)
            float signExtraRotY = 0.0f;
            float signTiltZ = 0.0f;
            if (deco->type == DECO_SIGN) {
                signExtraRotY = 3.14159265f;  // +180° to match visual
                signTiltZ = deco->state.sign.tilt;
            }
            float cosExtraY = cosf(signExtraRotY);
            float sinExtraY = sinf(signExtraRotY);
            float cosTiltZ = cosf(signTiltZ);
            float sinTiltZ = sinf(signTiltZ);

            for (int i = 0; i < mesh->count; i++) {
                CollisionTriangle* t = &mesh->triangles[i];

                // Apply 90° Y rotation and scale (same as collision code)
                float lx0 = -t->z0 * deco->scaleX;
                float ly0 = t->y0 * deco->scaleY;
                float lz0 = t->x0 * deco->scaleZ;
                float lx1 = -t->z1 * deco->scaleX;
                float ly1 = t->y1 * deco->scaleY;
                float lz1 = t->x1 * deco->scaleZ;
                float lx2 = -t->z2 * deco->scaleX;
                float ly2 = t->y2 * deco->scaleY;
                float lz2 = t->x2 * deco->scaleZ;

                // For SIGN: apply tilt (rotZ) then extra Y rotation
                if (deco->type == DECO_SIGN) {
                    // Apply tilt around Z axis
                    float tx, ty;
                    tx = lx0 * cosTiltZ - ly0 * sinTiltZ; ty = lx0 * sinTiltZ + ly0 * cosTiltZ; lx0 = tx; ly0 = ty;
                    tx = lx1 * cosTiltZ - ly1 * sinTiltZ; ty = lx1 * sinTiltZ + ly1 * cosTiltZ; lx1 = tx; ly1 = ty;
                    tx = lx2 * cosTiltZ - ly2 * sinTiltZ; ty = lx2 * sinTiltZ + ly2 * cosTiltZ; lx2 = tx; ly2 = ty;

                    // Apply extra Y rotation (+180°)
                    float tz;
                    tx = lx0 * cosExtraY + lz0 * sinExtraY; tz = -lx0 * sinExtraY + lz0 * cosExtraY; lx0 = tx; lz0 = tz;
                    tx = lx1 * cosExtraY + lz1 * sinExtraY; tz = -lx1 * sinExtraY + lz1 * cosExtraY; lx1 = tx; lz1 = tz;
                    tx = lx2 * cosExtraY + lz2 * sinExtraY; tz = -lx2 * sinExtraY + lz2 * cosExtraY; lx2 = tx; lz2 = tz;
                }

                float x0 = lx0 + deco->posX;
                float y0 = ly0 + deco->posY;
                float z0 = lz0 + deco->posZ;
                float x1 = lx1 + deco->posX;
                float y1 = ly1 + deco->posY;
                float z1 = lz1 + deco->posZ;
                float x2 = lx2 + deco->posX;
                float y2 = ly2 + deco->posY;
                float z2 = lz2 + deco->posZ;

                // Skip triangles far from camera
                float camX = debugFlyMode ? debugCamX : cubeX;
                float camZ = debugFlyMode ? debugCamZ : cubeZ;
                float centerX = (x0 + x1 + x2) / 3.0f;
                float centerZ = (z0 + z1 + z2) / 3.0f;
                float dist = sqrtf((centerX - camX) * (centerX - camX) + (centerZ - camZ) * (centerZ - camZ));
                if (dist > 300.0f) continue;

                // Project vertices to screen
                T3DVec3 world0 = {{x0, y0, z0}};
                T3DVec3 world1 = {{x1, y1, z1}};
                T3DVec3 world2 = {{x2, y2, z2}};
                T3DVec4 clip0, clip1, clip2;

                t3d_mat4_mul_vec3(&clip0, &viewport.matCamProj, &world0);
                t3d_mat4_mul_vec3(&clip1, &viewport.matCamProj, &world1);
                t3d_mat4_mul_vec3(&clip2, &viewport.matCamProj, &world2);

                if (clip0.v[3] < 0.1f && clip1.v[3] < 0.1f && clip2.v[3] < 0.1f) continue;

                float screenW = 160.0f;
                float screenH = 120.0f;

                float sx0 = (clip0.v[3] > 0.1f) ? (screenW + clip0.v[0] / clip0.v[3] * screenW) : -1000;
                float sy0 = (clip0.v[3] > 0.1f) ? (screenH - clip0.v[1] / clip0.v[3] * screenH) : -1000;
                float sx1 = (clip1.v[3] > 0.1f) ? (screenW + clip1.v[0] / clip1.v[3] * screenW) : -1000;
                float sy1 = (clip1.v[3] > 0.1f) ? (screenH - clip1.v[1] / clip1.v[3] * screenH) : -1000;
                float sx2 = (clip2.v[3] > 0.1f) ? (screenW + clip2.v[0] / clip2.v[3] * screenW) : -1000;
                float sy2 = (clip2.v[3] > 0.1f) ? (screenH - clip2.v[1] / clip2.v[3] * screenH) : -1000;

                if (sx0 < -500 || sx0 > 820 || sy0 < -500 || sy0 > 740) continue;
                if (sx1 < -500 || sx1 > 820 || sy1 < -500 || sy1 > 740) continue;
                if (sx2 < -500 || sx2 > 820 || sy2 < -500 || sy2 > 740) continue;

                // Draw transparent blue triangle for decoration collision
                rdpq_set_prim_color(RGBA32(0, 100, 255, 120));
                float v0[] = {sx0, sy0};
                float v1[] = {sx1, sy1};
                float v2[] = {sx2, sy2};
                rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
            }
        }
    }

    g_renderShadowTicks += get_ticks() - shadowRenderStart;

    // CRITICAL: Sync RDP pipeline before switching from 3D/triangle rendering to 2D text
    // Without this sync, RDP hardware bug triggers when incompatible commands collide
    rdpq_sync_pipe();
    rdpq_set_mode_standard();

    // Debug info / HUD
    uint32_t hudStart = get_ticks();
    int activeCount = 0;
    for (int i = 0; i < mapLoader.count; i++) {
        if (mapLoader.segments[i].active) activeCount++;
    }

    if (debugFlyMode) {
        // Debug mode HUD
        const char* modeStr = debugPlacementMode ? "[PLACE]" : "[CAM]";
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 10, "DEBUG %s", modeStr);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 22, "Deco: %s", DECO_TYPES[debugDecoType].name);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 34, "Placed: %d", mapRuntime.decoCount);

        if (debugPlacementMode) {
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 50, "Pos: %.1f %.1f %.1f", debugDecoX, debugDecoY, debugDecoZ);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 62, "Rot: %.2f", debugDecoRotY);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 74, "Scale: %.2f %.2f %.2f", debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 188, "Stick=Move C-U/D=Y C-L/R=Rot");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 200, "Z+Stick=ScaleXZ Z+C=ScaleY");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 212, "A=Place B=Camera L/R=Type");
        } else {
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 50, "Cam: %.0f %.0f %.0f", debugCamX, debugCamY, debugCamZ);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 62, "Collision: %s", debugShowCollision ? "ON" : "OFF");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 188, "D-Left=Collision");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 200, "Stick=Move C=Look A/Z=Up/Dn");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 212, "B=Place D-Right=Del L/R=Type");

            // Show highlighted decoration name in top-right corner
            if (debugHighlightedDecoIndex >= 0) {
                DecoInstance* hlDeco = &mapRuntime.decorations[debugHighlightedDecoIndex];
                if (hlDeco->active && hlDeco->type != DECO_NONE && hlDeco->type < DECO_TYPE_COUNT) {
                    const char* typeName = DECO_TYPES[hlDeco->type].name;
                    // Draw on right side of screen (320 - text width estimate)
                    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 180, 10, "SELECT: %s", typeName);
                    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 180, 22, "Pos: %.0f,%.0f,%.0f", hlDeco->posX, hlDeco->posY, hlDeco->posZ);
                    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 180, 34, "[D-Right to delete]");
                }
            }
        }
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 224, "D-Up=Exit Debug");
    } else {
        // Normal gameplay HUD - minimal for performance
        // Health HUD (sprite-based, slides down from top)
        if (healthHudY > HEALTH_HUD_HIDE_Y + 1.0f) {
            // Determine sprite index: 0 = full health (3), 3 = dead (0)
            int healthIdx = maxPlayerHealth - playerHealth;
            if (healthIdx < 0) healthIdx = 0;
            if (healthIdx > 3) healthIdx = 3;

            sprite_t *healthSprite = healthSprites[healthIdx];
            if (healthSprite) {
                int spriteX = 128 - 32;  // Center the 64x64 sprite on 256px width (32x32 at 2x scale)
                int spriteY = (int)healthHudY;

                // Sync TMEM before loading new texture (prevents SYNC_LOAD warnings)
                rdpq_sync_tile();
                rdpq_set_mode_standard();
                rdpq_mode_alphacompare(1);

                // Flash effect: alternate visibility during flash timer
                bool showSprite = true;
                if (healthFlashTimer > 0.0f) {
                    // Flash every 0.1 seconds
                    int flashPhase = (int)(healthFlashTimer * 10.0f) % 2;
                    showSprite = (flashPhase == 0);
                }

                if (showSprite) {
                    rdpq_sprite_blit(healthSprite, spriteX, spriteY, &(rdpq_blitparms_t){
                        .scale_x = 2.0f,
                        .scale_y = 2.0f
                    });
                }
            }
        }

        // Bolt/Screw HUD (slides in from right)
        // Only draw if visible or animating
        if (screwHudX < SCREW_HUD_HIDE_X - 1.0f) {
            sprite_t *screwSprite = screwSprites[screwAnimFrame];
            if (screwSprite) {
                int spriteX = (int)screwHudX;
                int spriteY = 8;  // Top right, aligned with health bar

                // Sync TMEM before loading new texture (prevents SYNC_LOAD warnings)
                rdpq_sync_tile();
                rdpq_set_mode_standard();
                rdpq_mode_alphacompare(1);

                rdpq_sprite_blit(screwSprite, spriteX, spriteY, &(rdpq_blitparms_t){
                    .scale_x = 1.5f,
                    .scale_y = 1.5f
                });

                // Draw bolt count next to the screw sprite
                int collected = save_get_total_bolts_collected();
                int total = g_saveLevelInfo.totalBolts;
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, spriteX + 48, spriteY + 28, "%d/%d", collected, total);
            }
        }

        // Golden Screw HUD (slides in from left, below bolt HUD level)
        // Only draw if visible or animating (X > hide position means visible)
        if (goldenHudX > GOLDEN_HUD_HIDE_X + 1.0f) {
            sprite_t *goldenSprite = goldenSprites[goldenAnimFrame];
            if (goldenSprite) {
                int spriteX = (int)goldenHudX;
                int spriteY = (int)GOLDEN_HUD_SHOW_Y;

                // Sync TMEM before loading new texture
                rdpq_sync_tile();
                rdpq_set_mode_standard();
                rdpq_mode_alphacompare(1);

                rdpq_sprite_blit(goldenSprite, spriteX, spriteY, &(rdpq_blitparms_t){
                    .scale_x = 2.0f,
                    .scale_y = 2.0f
                });

                // Draw golden screw count next to the sprite (offset for 2x scale)
                int collected = save_get_total_screwg_collected();
                int total = g_saveLevelInfo.totalScrewg;
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, spriteX + 48, spriteY + 28, "%d/%d", collected, total);
            }
        }

    }

    // Draw hit flash overlay (red flash when damaged)
    if (hitFlashTimer > 0.0f) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        uint8_t flashAlpha = (uint8_t)(hitFlashTimer / 0.15f * 100.0f);  // Max 100 alpha
        rdpq_set_prim_color(RGBA32(255, 0, 0, flashAlpha));
        rdpq_fill_rectangle(0, 0, 256, 240);
    }

    // Draw bolt flash overlay (white flash when collecting bolt)
    if (boltFlashTimer > 0.0f) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        uint8_t flashAlpha = (uint8_t)(boltFlashTimer / 0.1f * 80.0f);  // Max 80 alpha (less intense)
        rdpq_set_prim_color(RGBA32(255, 255, 255, flashAlpha));
        rdpq_fill_rectangle(0, 0, 256, 240);
    }

    // Draw iris effect (death/respawn transition)
    if (irisActive) {
        // Validate iris values to prevent RDP crashes from NaN/garbage/corruption
        // The N64 FPU will crash on:
        // - NaN/Infinity (obvious)
        // - Denormalized floats (values near zero with unusual bit patterns)
        // - Values that overflow when converted to fixed point (rdpq uses s13.2)
        // IRIS EFFECT DISABLED - rdpq_triangle causes RDP hardware crashes on real N64
        // Using simple alpha fade instead for stability
        // irisRadius goes from 400 (fully open) to 0 (fully closed)
        // Convert to alpha: radius 400 = alpha 0, radius 0 = alpha 255
        float r = irisRadius;

        // Validate radius
        if (isnan(r) || isinf(r) || r < 0.0f) {
            r = 0.0f;  // Treat as fully closed
        }
        if (r > 400.0f) {
            r = 400.0f;  // Clamp to max
        }

        // Convert radius to alpha (inverted - smaller radius = more black)
        int alpha = (int)((1.0f - (r / 400.0f)) * 255.0f);
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;

        // Only draw if there's something to show
        if (alpha > 0) {
            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(0, 0, 0, (uint8_t)alpha));
            rdpq_fill_rectangle(0, 0, 256, 240);
        }
    }
    // Pre-transition overlay (Psyops logo before level transition)
    else if (isPreTransitioning) {
        // Calculate fade progress during initial wait phase
        float waitProgress = preTransitionTimer / PRE_TRANSITION_WAIT_TIME;
        if (waitProgress > 1.0f) waitProgress = 1.0f;

        // Draw black background (fading in during wait, then solid for rest)
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        uint8_t bgAlpha = (uint8_t)(waitProgress * 255.0f);
        rdpq_set_prim_color(RGBA32(0, 0, 0, bgAlpha));
        rdpq_fill_rectangle(0, 0, 256, 240);

        // Draw game logo after wait + thud + delay phase
        float logoStartTime = PRE_TRANSITION_WAIT_TIME + PRE_TRANSITION_THUD_TIME + PRE_TRANSITION_LOGO_DELAY;
        if (preTransitionTimer >= logoStartTime && preTransitionLogo != NULL) {
            // Calculate logo fade (fade in over 0.3s, hold, then solid)
            float logoTime = preTransitionTimer - logoStartTime;
            float logoAlpha = logoTime / 0.3f;
            if (logoAlpha > 1.0f) logoAlpha = 1.0f;

            rdpq_set_mode_standard();
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(255, 255, 255, (uint8_t)(logoAlpha * 255.0f)));
            rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, PRIM, 0), (TEX0, 0, PRIM, 0)));

            // Draw game logo (128x128, centered on screen)
            int logoW = preTransitionLogo->width;
            int logoH = preTransitionLogo->height;
            int logoX = (256 - logoW) / 2;
            int logoY = (240 - logoH) / 2;
            rdpq_sprite_blit(preTransitionLogo, logoX, logoY, NULL);
        }
    }
    // Regular fade overlay (for level transitions, not death)
    else if (fadeAlpha > 0.0f) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        uint8_t alpha = (uint8_t)(fadeAlpha * 255.0f);
        rdpq_set_prim_color(RGBA32(0, 0, 0, alpha));
        rdpq_fill_rectangle(0, 0, 256, 240);
    }

    // Draw countdown (3-2-1-GO!) - under pause menu, only when not fading
    if (!irisActive && fadeAlpha <= 0.0f) {
        countdown_draw();
    }

    // Draw demo overlay (Press Start) - only in demo mode
    if (g_demoMode && g_demoOverlayDraw) {
        g_demoOverlayDraw();
    }

    // Draw pause menu with dark overlay
    if (isPaused) {
        // Draw semi-transparent dark overlay for better readability
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0, 0, 0, 160));
        rdpq_fill_rectangle(0, 0, 256, 240);

        // Draw the appropriate menu
        if (isInOptionsMenu) {
            option_draw(&optionsMenu);
        } else {
            option_draw(&pauseMenu);
        }
    }

    // Draw dialogue (for interact triggers and cutscenes)
    if (scriptRunning || g_cutsceneActive) {
        dialogue_draw(&dialogueBox);
    }

    // Draw level banner (shows level name on entry and when paused)
    level_banner_draw(&levelBanner);

    // Draw controls tutorial overlay
    tutorial_draw();

    // Draw celebration UI overlay (level complete stats + fireworks)
    if (celebratePhase == CELEBRATE_UI_SHOWING || celebratePhase == CELEBRATE_FIREWORKS) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        // Draw firework sparks as 2D screen-space particles
        // Use x/z as screen X, y as screen Y (simple 2D rendering)
        int screenCenterX = SCREEN_WIDTH / 2;
        int screenBottomY = SCREEN_HEIGHT - 40;
        for (int i = 0; i < MAX_CELEBRATE_SPARKS; i++) {
            if (celebrateSparks[i].active) {
                // Treat x as screen X offset from center, y as screen Y (inverted)
                int screenX = screenCenterX + (int)(celebrateSparks[i].x - celebrateWorldX);
                int screenY = screenBottomY - (int)(celebrateSparks[i].y - celebrateWorldY);

                if (screenX >= 0 && screenX < SCREEN_WIDTH && screenY >= 0 && screenY < SCREEN_HEIGHT) {
                    float lifeRatio = celebrateSparks[i].life / celebrateSparks[i].maxLife;
                    color_t c = CELEBRATE_COLORS[celebrateSparks[i].colorIdx];
                    c.a = (uint8_t)(lifeRatio * 255.0f);
                    rdpq_set_prim_color(c);
                    rdpq_fill_rectangle(screenX - 1, screenY - 1, screenX + 2, screenY + 2);
                }
            }
        }

        // Draw rising firework rockets as 2D
        for (int i = 0; i < MAX_CELEBRATE_FIREWORKS; i++) {
            if (celebrateFireworks[i].active && !celebrateFireworks[i].exploded) {
                int screenX = screenCenterX + (int)(celebrateFireworks[i].x - celebrateWorldX);
                int screenY = screenBottomY - (int)(celebrateFireworks[i].y - celebrateWorldY);

                if (screenX >= 0 && screenX < SCREEN_WIDTH && screenY >= 0 && screenY < SCREEN_HEIGHT) {
                    color_t c = CELEBRATE_COLORS[celebrateFireworks[i].colorIdx];
                    rdpq_set_prim_color(c);
                    rdpq_fill_rectangle(screenX - 1, screenY, screenX + 2, screenY + 6);
                }
            }
        }

        // Draw the UI box when showing UI phase
        if (celebratePhase == CELEBRATE_UI_SHOWING) {
            // Use consistent UI styling with ui_draw_box
            // Size box to fit 256x240 (and scale proportionally for larger screens)
            int boxW = 180, boxH = 140;
            int boxX = (SCREEN_WIDTH - boxW) / 2;
            int boxY = (SCREEN_HEIGHT - boxH) / 2 - 10;

            // Draw box using game's UI system (green terminal style with sprite borders)
            ui_draw_box(boxX, boxY, boxW, boxH, UI_COLOR_BG, UI_COLOR_BORDER);

            // Title - centered in box
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_set_prim_color(UI_COLOR_TEXT);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 24, boxY + 18, "LEVEL COMPLETE!");

            // Level name
            const char* levelName = get_level_name(currentLevel);
            if (levelName) {
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 50, boxY + 35, "%s", levelName);
            }

            // Stats
            int totalBolts = get_level_bolt_count(currentLevel);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 16, boxY + 55,
                "Bolts: %d / %d", levelBoltsCollected, totalBolts);

            int mins = (int)(levelTime / 60.0f);
            int secs = (int)levelTime % 60;
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 16, boxY + 70,
                "Time:  %d:%02d", mins, secs);

            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 16, boxY + 85,
                "Deaths: %d", levelDeaths);

            // Draw rank box (positioned inside the main UI box, using CRT sprite border)
            {
                int rankX = boxX + boxW - 64 - 8;
                int rankY = boxY + 45;
                int rankBoxW = 64;  // Wide enough for sprite borders (minimum 32 = 16+16 for corners)
                int rankBoxH = 64;

                // Choose rank color
                color_t rankColor;
                switch (celebrateRank) {
                    case 'S': rankColor = RGBA32(0xFF, 0xD7, 0x00, 0xFF); break;  // Gold
                    case 'A': rankColor = RGBA32(0x00, 0xFF, 0x00, 0xFF); break;  // Green
                    case 'B': rankColor = RGBA32(0x00, 0xAA, 0xFF, 0xFF); break;  // Blue
                    case 'C': rankColor = RGBA32(0xFF, 0xAA, 0x00, 0xFF); break;  // Orange
                    default:  rankColor = RGBA32(0xAA, 0xAA, 0xAA, 0xFF); break;  // Gray
                }

                // Use transparent background so parent box shows through
                ui_draw_box(rankX, rankY, rankBoxW, rankBoxH, RGBA32(0, 0, 0, 0), UI_COLOR_BORDER);

                // Load rank sprite if needed (lazy load)
                if (rankSprite == NULL || rankSpriteChar != celebrateRank) {
                    // Free old sprite if switching ranks
                    if (rankSprite != NULL) {
                        sprite_free(rankSprite);
                        rankSprite = NULL;
                    }
                    // CRITICAL: Wait for RSP queue to flush before loading from ROM
                    rspq_wait();
                    // Load the appropriate rank sprite
                    switch (celebrateRank) {
                        case 'S': rankSprite = sprite_load("rom:/S_Score.sprite"); break;
                        case 'A': rankSprite = sprite_load("rom:/A_Score.sprite"); break;
                        case 'B': rankSprite = sprite_load("rom:/B_Score.sprite"); break;
                        case 'C': rankSprite = sprite_load("rom:/C_Score.sprite"); break;
                        default:  rankSprite = sprite_load("rom:/D_Score.sprite"); break;
                    }
                    rankSpriteChar = celebrateRank;
                }

                // Draw the rank sprite centered in the box (16x16 scaled to 48x48)
                // Sync TMEM before loading new texture (prevents SYNC_LOAD warnings)
                rdpq_sync_tile();
                rdpq_set_mode_standard();
                rdpq_mode_alphacompare(1);
                float rankScale = 3.0f;  // 16x16 * 3 = 48x48
                int spriteX = rankX + (rankBoxW - 48) / 2;
                int spriteY = rankY + (rankBoxH - 48) / 2;
                rdpq_sprite_blit(rankSprite, spriteX, spriteY, &(rdpq_blitparms_t){
                    .scale_x = rankScale,
                    .scale_y = rankScale
                });

                // Show best rank if different
                uint8_t bestRankVal = save_get_best_rank(currentLevel);
                char bestRankChar = save_rank_to_char(bestRankVal);
                if (bestRankChar != celebrateRank && bestRankVal > 0) {
                    rdpq_set_prim_color(RGBA32(0x88, 0x88, 0x88, 0xFF));
                    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, rankX + 8, rankY + rankBoxH + 10, "Best: %c", bestRankChar);
                }

                // Restore text color for bonus messages
                rdpq_set_prim_color(UI_COLOR_TEXT);
            }

            // Bonus messages (positioned relative to box)
            int bonusMsgY = boxY + boxH - 22;
            if (levelDeaths == 0 && levelBoltsCollected == totalBolts) {
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 50, bonusMsgY, "PERFECT RUN!");
            } else if (levelDeaths == 0) {
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 55, bonusMsgY, "Deathless!");
            } else if (levelBoltsCollected == totalBolts) {
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 35, bonusMsgY, "All Bolts Found!");
            }

            // Button prompts: A = Continue, B = Return to Menu (below the box)
            int buttonY = boxY + boxH + 8;
            if (celebrateBlinkTimer < UI_BLINK_RATE) {
                // A button prompt with "Continue" label
                rdpq_text_printf(NULL, 2, boxX + 20, buttonY, "a");  // A button icon (font 2)
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 35, buttonY + 1, "Continue");

                // B button prompt with "Menu" label
                rdpq_text_printf(NULL, 2, boxX + 110, buttonY, "b");  // B button icon (font 2)
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, boxX + 125, buttonY + 1, "Menu");
            }
        }
    }

    // Draw A button prompt for nearby interact triggers
    if (!isPaused && !scriptRunning && !playerIsDead && !playerIsHurt && !playerIsRespawning && !isTransitioning && !debugFlyMode) {
        for (int i = 0; i < mapRuntime.decoCount; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            if (deco->active && deco->type == DECO_INTERACTTRIGGER &&
                deco->state.interactTrigger.playerInRange && !deco->state.interactTrigger.interacting) {
                // Draw A button prompt in bottom right
                rdpq_textparms_t parms = {0};
                parms.align = ALIGN_RIGHT;
                parms.valign = VALIGN_BOTTOM;
                // Draw with button font (font ID 2)
                rdpq_text_printf(&parms, 2, 305, 220, "a");  // lowercase 'a' = A button icon
                break;
            }
        }
    }

    // Draw reward popup (100% completion notification)
    if (rewardPopupActive && rewardPopupTimer > 0.0f) {
        // Centered popup box using game's UI style
        int boxWidth = 220;
        int boxHeight = 56;
        int boxX = (SCREEN_WIDTH - boxWidth) / 2;
        int boxY = (SCREEN_HEIGHT - boxHeight) / 2 - 20;  // Slightly above center

        // Draw box using the game's UI system (with sprite borders)
        ui_draw_box(boxX, boxY, boxWidth, boxHeight, UI_COLOR_BG, UI_COLOR_BORDER);

        // Draw text centered in box
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        // Title text (gold/yellow) - offset right to account for UI border sprites
        rdpq_set_prim_color(RGBA32(255, 215, 100, 255));
        const char* title = "100% COMPLETE!";
        int titleLen = 14;
        int titleX = boxX + (boxWidth - titleLen * 8) / 2 + 12;  // +12 offset for border
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, titleX, boxY + 20, "%s", title);

        // Subtitle text (white) - offset right to account for UI border sprites
        rdpq_set_prim_color(UI_COLOR_TEXT);
        const char* subtitle = "Check Main Menu for reward!";
        int subtitleLen = 27;
        int subtitleX = boxX + (boxWidth - subtitleLen * 8) / 2 + 12;  // +12 offset for border
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, subtitleX, boxY + 36, "%s", subtitle);
    }

    // Draw debug menu (shared module)
    debug_menu_draw();

    g_renderHUDTicks += get_ticks() - hudStart;
    g_renderTotalTicks += get_ticks() - renderStart;

    // Update performance graph with this frame's render time
    {
        uint32_t frameEndTicks = get_ticks();
        int frameUs = (int)((frameEndTicks - renderStart) * 1000000ULL / TICKS_PER_SECOND);
        perfGraphData[perfGraphHead] = frameUs;
        perfGraphHead = (perfGraphHead + 1) % PERF_GRAPH_WIDTH;
    }

    // Performance stats (always visible when perfGraphEnabled)
    HEAP_CHECK("pre_perf");

    if (perfGraphEnabled) {
        // CRITICAL: Clear FPU exception flags left by T3D 3D rendering
        // Without this, rdpq_text_printf's internal float-to-int conversions
        // will trigger "Floating point integer cast overflow" exceptions
        fpu_flush_denormals();

        // CRITICAL: Set up 2D mode before any rdpq drawing after 3D rendering
        rdpq_sync_pipe();
        rdpq_set_mode_standard();

        struct mallinfo mi = mallinfo();
        int ramUsedKB = mi.uordblks / 1024;
        int ramTotalKB = 4 * 1024;  // 4MB base RAM (8MB with expansion pak)
        int ramFreeKB = ramTotalKB - ramUsedKB;
        if (ramFreeKB < 0) ramFreeKB = 0;

        // CPU usage based on actual frame time vs budget (33333us at 30fps)
        // Use integer math to avoid float formatting in printf (which uses _Balloc and crashes on corrupted heap)
        int cpuUsePctInt = (g_lastFrameUs * 100) / 33333;  // 33333us = 100%
        if (cpuUsePctInt > 999) cpuUsePctInt = 999;
        int cpuFreePctInt = 100 - cpuUsePctInt;
        if (cpuFreePctInt < 0) cpuFreePctInt = 0;

        HEAP_CHECK("pre_printf");

        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 205, 200, "RAM:%4dK/%4dK", ramUsedKB, ramTotalKB);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 205, 210, "Free:%4dK", ramFreeKB);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 205, 220, "CPU:%3d%% F:%3d%%", cpuUsePctInt, cpuFreePctInt);

        // Draw performance graph (frame time history)
        {
            int graphX = 10;
            int graphY = 180;

            // Draw background
            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(0, 0, 0, 180));
            rdpq_fill_rectangle(graphX - 2, graphY - PERF_GRAPH_HEIGHT - 12,
                               graphX + PERF_GRAPH_WIDTH + 2, graphY + 2);

            // Draw target line (33.3ms = 30 FPS)
            rdpq_sync_pipe();
            rdpq_set_prim_color(RGBA32(0, 255, 0, 128));
            int targetY = graphY - (PERF_GRAPH_TARGET_US * PERF_GRAPH_HEIGHT / 66666);
            rdpq_fill_rectangle(graphX, targetY, graphX + PERF_GRAPH_WIDTH, targetY + 1);

            // Draw bars - batch by color to reduce RDP command count
            rdpq_sync_pipe();

            // Green bars (under budget)
            rdpq_set_prim_color(RGBA32(0, 200, 0, 255));
            for (int i = 0; i < PERF_GRAPH_WIDTH; i++) {
                int idx = (perfGraphHead + i) % PERF_GRAPH_WIDTH;
                int frameUs = perfGraphData[idx];
                if (frameUs < PERF_GRAPH_TARGET_US) {
                    int barHeight = (frameUs * PERF_GRAPH_HEIGHT) / 66666;
                    if (barHeight > PERF_GRAPH_HEIGHT) barHeight = PERF_GRAPH_HEIGHT;
                    if (barHeight < 1) barHeight = 1;
                    rdpq_fill_rectangle(graphX + i, graphY - barHeight, graphX + i + 1, graphY);
                }
            }

            // Yellow bars (over budget)
            rdpq_sync_pipe();
            rdpq_set_prim_color(RGBA32(255, 200, 0, 255));
            for (int i = 0; i < PERF_GRAPH_WIDTH; i++) {
                int idx = (perfGraphHead + i) % PERF_GRAPH_WIDTH;
                int frameUs = perfGraphData[idx];
                if (frameUs >= PERF_GRAPH_TARGET_US && frameUs < 50000) {
                    int barHeight = (frameUs * PERF_GRAPH_HEIGHT) / 66666;
                    if (barHeight > PERF_GRAPH_HEIGHT) barHeight = PERF_GRAPH_HEIGHT;
                    if (barHeight < 1) barHeight = 1;
                    rdpq_fill_rectangle(graphX + i, graphY - barHeight, graphX + i + 1, graphY);
                }
            }

            // Red bars (way over budget)
            rdpq_sync_pipe();
            rdpq_set_prim_color(RGBA32(255, 0, 0, 255));
            for (int i = 0; i < PERF_GRAPH_WIDTH; i++) {
                int idx = (perfGraphHead + i) % PERF_GRAPH_WIDTH;
                int frameUs = perfGraphData[idx];
                if (frameUs >= 50000) {
                    int barHeight = (frameUs * PERF_GRAPH_HEIGHT) / 66666;
                    if (barHeight > PERF_GRAPH_HEIGHT) barHeight = PERF_GRAPH_HEIGHT;
                    if (barHeight < 1) barHeight = 1;
                    rdpq_fill_rectangle(graphX + i, graphY - barHeight, graphX + i + 1, graphY);
                }
            }

            // Label
            rdpq_sync_pipe();
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, graphX, graphY - PERF_GRAPH_HEIGHT - 8, "Frame ms");
        }

        // Draw memory usage graph (bottom-right)
        {
            // Memory graph dimensions
            int memGraphX = SCREEN_WIDTH - 75;
            int memGraphY = 180;
            int memBarWidth = 60;
            int memBarHeight = 8;

            // Get memory stats
            int heapUsedKB = mi.uordblks / 1024;
            int heapTotalKB = mi.arena / 1024;
            if (heapTotalKB < 1) heapTotalKB = 1;

            // Stack usage estimation - use proper inline asm to read SP
            // The stack top depends on expansion pak: 0x80400000 (4MB) or 0x80800000 (8MB)
            // But libdragon places stack at end of heap, not at RAM top, so this is approximate
            uint32_t current_sp;
            __asm__ volatile ("move %0, $sp" : "=r"(current_sp));
            uint32_t stack_top = is_memory_expanded() ? 0x80800000 : 0x80400000;
            int stackUsed = 0;
            int stackTotal = 64 * 1024;
            // Only calculate if SP is in valid RDRAM range
            if (current_sp >= 0x80000000 && current_sp < stack_top) {
                stackUsed = (int)(stack_top - current_sp);
                if (stackUsed < 0) stackUsed = 0;
                if (stackUsed > stackTotal) stackUsed = stackTotal;
            }

            // Total memory
            int totalUsedKB = heapUsedKB + (stackUsed / 1024);
            int totalAvailKB = is_memory_expanded() ? (8 * 1024) : (4 * 1024);

            // Draw background
            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(0, 0, 0, 180));
            rdpq_fill_rectangle(memGraphX - 4, memGraphY - 50, memGraphX + memBarWidth + 4, memGraphY + 4);

            // Helper macro with proper syncs and mode setup
            #define DRAW_MEM_BAR(label, used, total, yOffset, r, g, b) do { \
                int barY = memGraphY - yOffset; \
                int fillWidth = (used * memBarWidth) / (total > 0 ? total : 1); \
                if (fillWidth > memBarWidth) fillWidth = memBarWidth; \
                rdpq_sync_pipe(); \
                rdpq_set_mode_standard(); \
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT); \
                rdpq_set_prim_color(RGBA32(40, 40, 40, 255)); \
                rdpq_fill_rectangle(memGraphX, barY, memGraphX + memBarWidth, barY + memBarHeight); \
                if (fillWidth > 0) { \
                    rdpq_set_prim_color(RGBA32(r, g, b, 255)); \
                    rdpq_fill_rectangle(memGraphX, barY, memGraphX + fillWidth, barY + memBarHeight); \
                } \
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, memGraphX, barY - 2, label); \
            } while(0)

            // Heap bar
            int heapPct = (heapUsedKB * 100) / heapTotalKB;
            uint8_t heapR = heapPct > 80 ? 255 : (heapPct > 60 ? 255 : 0);
            uint8_t heapG = heapPct > 80 ? 0 : (heapPct > 60 ? 200 : 200);
            DRAW_MEM_BAR("Heap", heapUsedKB, heapTotalKB, 40, heapR, heapG, 0);

            // Stack bar
            int stackPct = (stackUsed * 100) / stackTotal;
            uint8_t stackR = stackPct > 80 ? 255 : (stackPct > 60 ? 255 : 0);
            uint8_t stackG = stackPct > 80 ? 0 : (stackPct > 60 ? 200 : 200);
            DRAW_MEM_BAR("Stack", stackUsed / 1024, stackTotal / 1024, 25, stackR, stackG, stackG);

            // Total bar
            int totalPct = (totalUsedKB * 100) / totalAvailKB;
            uint8_t totalR = totalPct > 80 ? 255 : (totalPct > 60 ? 255 : 100);
            uint8_t totalG = totalPct > 80 ? 0 : (totalPct > 60 ? 200 : 100);
            uint8_t totalB = totalPct > 80 ? 0 : (totalPct > 60 ? 0 : 255);
            DRAW_MEM_BAR("Total", totalUsedKB, totalAvailKB, 10, totalR, totalG, totalB);

            #undef DRAW_MEM_BAR
        }
    }

    // Draw cutscene 2 slideshow overlay (covers entire screen)
    if (cs2Playing && cs2CurrentSprite) {
        float cs2ScreenW = display_get_width();
        float cs2ScreenH = display_get_height();

        // Sync RDP pipeline before cutscene overlay
        rdpq_sync_pipe();

        // Fill screen with black
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
        rdpq_fill_rectangle(0, 0, cs2ScreenW, cs2ScreenH);

        // Sync after fill, before sprite
        rdpq_sync_pipe();
        rdpq_sync_tile();

        // Draw the sprite scaled 1.5x and centered on screen
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        float cs2Scale = 1.5f;
        float scaledSize = 128 * cs2Scale;
        float spriteX = (cs2ScreenW - scaledSize) / 2.0f;
        float spriteY = (cs2ScreenH - scaledSize) / 2.0f;
        rdpq_sprite_blit(cs2CurrentSprite, spriteX, spriteY, &(rdpq_blitparms_t){
            .scale_x = cs2Scale,
            .scale_y = cs2Scale
        });
    }

    // Draw cutscene 3 slideshow overlay (covers entire screen)
    if (cs3Playing && cs3CurrentSprite) {
        float cs3ScreenW = display_get_width();
        float cs3ScreenH = display_get_height();

        // Sync RDP pipeline before cutscene overlay
        rdpq_sync_pipe();

        // Fill screen with black
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
        rdpq_fill_rectangle(0, 0, cs3ScreenW, cs3ScreenH);

        // Sync after fill, before sprite
        rdpq_sync_pipe();
        rdpq_sync_tile();

        // Draw the sprite scaled 1.5x and centered on screen
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        float cs3Scale = 1.5f;
        float scaledSize = 128 * cs3Scale;
        float spriteX = (cs3ScreenW - scaledSize) / 2.0f;
        float spriteY = (cs3ScreenH - scaledSize) / 2.0f;
        rdpq_sprite_blit(cs3CurrentSprite, spriteX, spriteY, &(rdpq_blitparms_t){
            .scale_x = cs3Scale,
            .scale_y = cs3Scale
        });
    }

    // Draw cutscene 4 slideshow overlay (arms explanation)
    if (cs4Playing && cs4CurrentSprite) {
        float cs4ScreenW = display_get_width();
        float cs4ScreenH = display_get_height();

        rdpq_sync_pipe();
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
        rdpq_fill_rectangle(0, 0, cs4ScreenW, cs4ScreenH);

        rdpq_sync_pipe();
        rdpq_sync_tile();

        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        float cs4Scale = 1.5f;
        float scaledSize = 128 * cs4Scale;
        float spriteX = (cs4ScreenW - scaledSize) / 2.0f;
        float spriteY = (cs4ScreenH - scaledSize) / 2.0f;
        rdpq_sprite_blit(cs4CurrentSprite, spriteX, spriteY, &(rdpq_blitparms_t){
            .scale_x = cs4Scale,
            .scale_y = cs4Scale
        });
    }

    // Draw demo iris overlay (must be last, drawn over everything)
    if (g_demoMode && g_demoIrisDraw) {
        g_demoIrisDraw();
    }

    // CRITICAL: Ensure RDP finishes all commands before frame swap
    // Without this sync, display_get() on next frame can deadlock waiting
    // for a buffer that the RDP is still rendering to
    rdpq_sync_full(NULL, NULL);
    rdpq_detach_show();
}
