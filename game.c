// #include <libdragon.h>
// #include <rdpq_tri.h>
// #include <rdpq_sprite.h>
// #include <fgeom.h>
// #include <t3d/t3d.h>
// #include <t3d/t3dmath.h>
// #include <t3d/t3dmodel.h>
// #include <t3d/t3dskeleton.h>
// #include <t3d/t3danim.h>
// #include <t3d/tpx.h>
// #include "game.h"
// #include "../scene.h"
// #include "../controls.h"
// #include "../mapLoader.h"
// #include "../mapData.h"
// #define LEVELS_AUDIO_IMPLEMENTATION
// #include "../levels.h"
// #include "../PsyopsCode.h"
// #include "../constants.h"
// #include "../save.h"
// #include "../ui.h"
// #include "../debug_menu.h"
// #include "level_select.h"
// #include "level_complete.h"
// #include <stddef.h>
// #include <stdbool.h>
// #include <math.h>
// #include <malloc.h>

// #define FB_COUNT 3

// // Robot parts enum
// typedef enum {
//     PART_HEAD,
//     PART_TORSO,
//     PART_ARMS,
//     PART_LEGS,
//     PART_COUNT
// } RobotParts;

// static RobotParts currentPart = PART_TORSO;
// static int partSwitchCooldown = 0;

// int partsObtained = 3; // 0 = head, 1 = torso, 2 = arms, 3 = legs

// // Sound effects
// wav64_t sfxBoltCollect;

// // Slime performance profiling counters
// uint32_t g_slimeGroundTicks = 0;
// uint32_t g_slimeDecoGroundTicks = 0;
// uint32_t g_slimeWallTicks = 0;
// uint32_t g_slimeMathTicks = 0;
// uint32_t g_slimeSpringTicks = 0;
// int g_slimeUpdateCount = 0;

// // Global render profiling (accessible from draw function)
// static uint32_t g_renderMapTicks = 0;
// static uint32_t g_renderDecoTicks = 0;
// static uint32_t g_renderPlayerTicks = 0;
// static uint32_t g_renderShadowTicks = 0;
// static uint32_t g_renderHUDTicks = 0;
// static uint32_t g_renderTotalTicks = 0;
// static uint32_t g_audioTicks = 0;
// static uint32_t g_inputTicks = 0;
// static uint32_t g_cameraTicks = 0;
// __attribute__((unused)) static uint32_t g_physicsTicks = 0;
// static int g_lastFrameUs = 0;  // Average frame time in microseconds (for HUD)

// // Performance graph (frame time history)
// #define PERF_GRAPH_WIDTH 64    // Number of samples to display
// #define PERF_GRAPH_HEIGHT 40   // Height in pixels
// #define PERF_GRAPH_TARGET_US 33333  // Target frame time (30 FPS = 33.3ms)
// static int perfGraphData[PERF_GRAPH_WIDTH] = {0};  // Frame time history in microseconds
// static int perfGraphHead = 0;          // Circular buffer head
// static bool perfGraphEnabled = false;  // Toggle with C-Left

// static sprite_t *test_sprite = NULL;
// static sprite_t *shadowSprite = NULL;

// // Health HUD sprites and state (lazy-loaded on first damage)
// static sprite_t *healthSprites[4] = {NULL, NULL, NULL, NULL};  // health1-4.sprite
// static bool healthSpritesLoaded = false;  // Lazy load flag
// static float healthHudY = -40.0f;      // Y position (negative = off screen)
// static float healthHudTargetY = -40.0f; // Target Y position
// static float healthFlashTimer = 0.0f;  // Flash effect timer
// static bool healthHudVisible = false;  // Whether HUD should be shown
// static float healthHudHideTimer = 0.0f; // Timer to auto-hide after damage
// #define HEALTH_HUD_SHOW_Y 8.0f         // Y position when visible
// #define HEALTH_HUD_HIDE_Y -70.0f       // Y position when hidden (64px sprite at 2x scale)
// #define HEALTH_HUD_SPEED 8.0f          // Animation speed
// #define HEALTH_FLASH_DURATION 0.8f     // How long to flash
// #define HEALTH_DISPLAY_DURATION 2.0f   // How long to show after damage

// // Screw/Bolt HUD sprites and state (lazy-loaded on first bolt collection)
// #define SCREW_HUD_FRAME_COUNT 6
// static sprite_t *screwSprites[SCREW_HUD_FRAME_COUNT] = {NULL};
// static bool screwSpritesLoaded = false;  // Lazy load flag
// static float screwHudX = 330.0f;       // X position (positive = off screen right)
// static float screwHudTargetX = 330.0f; // Target X position
// static int screwAnimFrame = 0;         // Current animation frame (0-12)
// static float screwAnimTimer = 0.0f;    // Timer for animation
// static bool screwHudVisible = false;   // Whether HUD should be shown
// static float screwHudHideTimer = 0.0f; // Timer to auto-hide after collection
// #define SCREW_HUD_SHOW_X 230.0f        // X position when visible (right side)
// #define SCREW_HUD_HIDE_X 330.0f        // X position when hidden (off screen right)
// #define SCREW_HUD_SPEED 8.0f           // Slide animation speed
// #define SCREW_ANIM_FPS 8.0f            // Animation frames per second (slower spin)
// #define SCREW_DISPLAY_DURATION 2.5f    // How long to show after collection

// // Conveyor belt texture scrolling via vertex UV modification
// static int16_t* g_conveyorBaseUVs = NULL;  // Store original UVs
// static int g_conveyorVertCount = 0;
// static T3DVertPacked* g_conveyorVerts = NULL;

// static void conveyor_belt_init_uvs(T3DModel* beltModel) {
//     if (g_conveyorBaseUVs != NULL) return;  // Already initialized

//     // Get vertex buffer from the belt model
//     T3DModelIter iter = t3d_model_iter_create(beltModel, T3D_CHUNK_TYPE_OBJECT);
//     while (t3d_model_iter_next(&iter)) {
//         T3DObject* obj = iter.object;
//         if (!obj->numParts) continue;

//         T3DObjectPart* part = &obj->parts[0];
//         g_conveyorVerts = part->vert;
//         g_conveyorVertCount = part->vertLoadCount;

//         // Allocate storage for base UVs (2 per vertex: S and T)
//         g_conveyorBaseUVs = malloc(g_conveyorVertCount * 2 * sizeof(int16_t));

//         // Store original UVs
//         for (int i = 0; i < g_conveyorVertCount; i++) {
//             int16_t* uv = t3d_vertbuffer_get_uv(g_conveyorVerts, i);
//             g_conveyorBaseUVs[i * 2] = uv[0];
//             g_conveyorBaseUVs[i * 2 + 1] = uv[1];
//         }
//         debugf("Conveyor belt UVs initialized: %d verts\n", g_conveyorVertCount);
//         break;
//     }
// }

// static void conveyor_belt_scroll_uvs(float offset) {
//     if (!g_conveyorBaseUVs || !g_conveyorVerts) return;

//     // UV is in 10.5 fixed point, 32 units = 1 texel
//     int16_t scrollAmount = (int16_t)(offset * 32.0f * 18.0f);

//     for (int i = 0; i < g_conveyorVertCount; i++) {
//         int16_t* uv = t3d_vertbuffer_get_uv(g_conveyorVerts, i);
//         // Scroll T coordinate (negative to match push direction)
//         uv[1] = g_conveyorBaseUVs[i * 2 + 1] - scrollAmount;
//     }

//     // Flush cache so RSP can see the modified vertex data
//     int packedCount = (g_conveyorVertCount + 1) / 2;
//     data_cache_hit_writeback(g_conveyorVerts, packedCount * sizeof(T3DVertPacked));
// }

// // Toxic pipe liquid texture scrolling via vertex UV modification
// static int16_t* g_toxicPipeLiquidBaseUVs = NULL;
// static int g_toxicPipeLiquidVertCount = 0;
// static T3DVertPacked* g_toxicPipeLiquidVerts = NULL;

// static void toxic_pipe_liquid_init_uvs(T3DModel* liquidModel) {
//     if (g_toxicPipeLiquidBaseUVs != NULL) return;  // Already initialized

//     T3DModelIter iter = t3d_model_iter_create(liquidModel, T3D_CHUNK_TYPE_OBJECT);
//     while (t3d_model_iter_next(&iter)) {
//         T3DObject* obj = iter.object;
//         if (!obj->numParts) continue;

//         T3DObjectPart* part = &obj->parts[0];
//         g_toxicPipeLiquidVerts = part->vert;
//         g_toxicPipeLiquidVertCount = part->vertLoadCount;

//         g_toxicPipeLiquidBaseUVs = malloc(g_toxicPipeLiquidVertCount * 2 * sizeof(int16_t));

//         for (int i = 0; i < g_toxicPipeLiquidVertCount; i++) {
//             int16_t* uv = t3d_vertbuffer_get_uv(g_toxicPipeLiquidVerts, i);
//             g_toxicPipeLiquidBaseUVs[i * 2] = uv[0];
//             g_toxicPipeLiquidBaseUVs[i * 2 + 1] = uv[1];
//         }
//         debugf("Toxic pipe liquid UVs initialized: %d verts\n", g_toxicPipeLiquidVertCount);
//         break;
//     }
// }

// static void toxic_pipe_liquid_scroll_uvs(float offset) {
//     if (!g_toxicPipeLiquidBaseUVs || !g_toxicPipeLiquidVerts) return;

//     int16_t scrollAmount = (int16_t)(offset * 32.0f * 32.0f);

//     for (int i = 0; i < g_toxicPipeLiquidVertCount; i++) {
//         int16_t* uv = t3d_vertbuffer_get_uv(g_toxicPipeLiquidVerts, i);
//         uv[1] = g_toxicPipeLiquidBaseUVs[i * 2 + 1] - scrollAmount;
//     }

//     int packedCount = (g_toxicPipeLiquidVertCount + 1) / 2;
//     data_cache_hit_writeback(g_toxicPipeLiquidVerts, packedCount * sizeof(T3DVertPacked));
// }

// // Toxic running texture scrolling via vertex UV modification
// static int16_t* g_toxicRunningBaseUVs = NULL;
// static int g_toxicRunningVertCount = 0;
// static T3DVertPacked* g_toxicRunningVerts = NULL;

// static void toxic_running_init_uvs(T3DModel* toxicModel) {
//     if (g_toxicRunningBaseUVs != NULL) return;  // Already initialized

//     T3DModelIter iter = t3d_model_iter_create(toxicModel, T3D_CHUNK_TYPE_OBJECT);
//     while (t3d_model_iter_next(&iter)) {
//         T3DObject* obj = iter.object;
//         if (!obj->numParts) continue;

//         T3DObjectPart* part = &obj->parts[0];
//         g_toxicRunningVerts = part->vert;
//         g_toxicRunningVertCount = part->vertLoadCount;

//         g_toxicRunningBaseUVs = malloc(g_toxicRunningVertCount * 2 * sizeof(int16_t));

//         for (int i = 0; i < g_toxicRunningVertCount; i++) {
//             int16_t* uv = t3d_vertbuffer_get_uv(g_toxicRunningVerts, i);
//             g_toxicRunningBaseUVs[i * 2] = uv[0];
//             g_toxicRunningBaseUVs[i * 2 + 1] = uv[1];
//         }
//         debugf("Toxic running UVs initialized: %d verts\n", g_toxicRunningVertCount);
//         break;
//     }
// }

// static void toxic_running_scroll_uvs(float offset) {
//     if (!g_toxicRunningBaseUVs || !g_toxicRunningVerts) return;

//     int16_t scrollAmount = (int16_t)(offset * 32.0f * 32.0f);

//     for (int i = 0; i < g_toxicRunningVertCount; i++) {
//         int16_t* uv = t3d_vertbuffer_get_uv(g_toxicRunningVerts, i);
//         uv[1] = g_toxicRunningBaseUVs[i * 2 + 1] - scrollAmount;
//     }

//     int packedCount = (g_toxicRunningVertCount + 1) / 2;
//     data_cache_hit_writeback(g_toxicRunningVerts, packedCount * sizeof(T3DVertPacked));
// }

// // Shadow quad vertices (4 corners of a quad)
// static T3DVertPacked *shadowVerts = NULL;

// // Decal vertices for slime oil trails (4 verts = 2 packed)
// static T3DVertPacked *decalVerts = NULL;
// static T3DMat4FP *decalMatFP = NULL;

// // Map loader
// static MapLoader mapLoader;
// static LevelID currentLevel;

// // Decorations
// static MapRuntime mapRuntime;
// static T3DMat4FP* decoMatFP;

// // Player controls
// static ControlConfig controlConfig;
// static PlayerState playerState;

// // Player collision parameters
// float playerRadius = PLAYER_RADIUS;
// float playerHeight = PLAYER_HEIGHT;
// T3DVec3 camTarget = {{0, PLAYER_CAMERA_OFFSET_Y, 0}};

// T3DModel *playerModel;

// // Torso/Arms model and animations (RoboPlayer_DC)
// T3DModel *torsoModel;
// T3DSkeleton torsoSkel;

// // Simple cube model for jump arc visualization
// T3DModel *arcDotModel;
// T3DSkeleton torsoSkelBlend;
// T3DAnim torsoAnimIdle;
// T3DAnim torsoAnimWalk;
// T3DAnim torsoAnimJumpCharge;
// T3DAnim torsoAnimJumpLaunch;
// T3DAnim torsoAnimJumpLand;
// T3DAnim torsoAnimWait;
// T3DAnim torsoAnimPain1;
// T3DAnim torsoAnimPain2;
// T3DAnim torsoAnimDeath;
// T3DAnim torsoAnimSlideFront;
// T3DAnim torsoAnimSlideFrontRecover;
// T3DAnim torsoAnimSlideBack;
// T3DAnim torsoAnimSlideBackRecover;
// bool torsoHasAnims = false;

// // Arms mode animations (different character/controls)
// T3DAnim armsAnimIdle;
// T3DAnim armsAnimWalk1;
// T3DAnim armsAnimWalk2;
// T3DAnim armsAnimJump;
// T3DAnim armsAnimJumpLand;
// T3DAnim armsAnimAtkSpin;
// T3DAnim armsAnimAtkWhip;
// T3DAnim armsAnimDeath;
// T3DAnim armsAnimPain1;
// T3DAnim armsAnimPain2;
// T3DAnim armsAnimSlide;
// bool armsHasAnims = false;

// // Arms mode state
// static bool isArmsMode = false;          // true = arms mode, false = torso mode
// static bool armsIsSpinning = false;      // Playing spin attack
// static bool armsIsWhipping = false;      // Playing whip attack
// static float armsSpinTime = 0.0f;        // Time in spin animation
// static float armsWhipTime = 0.0f;        // Time in whip animation
// static bool armsHasDoubleJumped = false; // Has used double jump this airtime
// static bool armsIsGliding = false;       // Currently in glide/double-jump state
// static bool armsIsWallJumping = false;   // Currently in wall jump (face wall until land)
// static float armsWallJumpAngle = 0.0f;   // Angle facing into wall during wall jump
// static float spinHitCooldown = 0.0f;     // Cooldown between spin attack hits
// static bool isCharging = false;          // Jump charge state (torso)
// static bool isJumping = false;           // In jump release animation
// static bool jumpAnimPaused = false;      // Jump release paused mid-air
// static bool isLanding = false;           // Playing landing animation
// static bool isBufferedCharge = false;    // Current charge started from buffered jump
// static bool torsoIsWallJumping = false;  // Currently in wall jump (face away until land)
// static float torsoWallJumpAngle = 0.0f;  // Angle facing away from wall during wall jump
// static float jumpChargeTime = 0.0f;   // How long charge held
// static float jumpAimX = 0.0f;         // Stick X input during charge (for jump direction)
// static float jumpAimY = 0.0f;         // Stick Y input during charge (for jump direction)
// static float jumpArcEndX = 0.0f;      // X position where jump arc ends (for camera)
// static float smoothCamTargetX = 0.0f; // Smoothed camera target X for arc aiming

// // Squash and stretch
// static float squashScale = 1.0f;      // Current squash scale (1.0 = normal, <1 = squashed)
// static float squashVelocity = 0.0f;   // Spring velocity for squash recovery
// static float landingSquash = 0.0f;    // Landing squash component (springs back separately)
// static float chargeSquash = 0.0f;     // Charge squash component (held constant)
// static float jumpPeakY = 0.0f;        // Highest Y position during current jump/fall

// // Coyote time and jump buffering
// static float coyoteTimer = 0.0f;      // Time since leaving ground (can still jump if < COYOTE_TIME)
// static float jumpBufferTimer = 0.0f;  // Time since A pressed in air (triggers jump on land if < BUFFER_TIME)
// static const float COYOTE_TIME = 0.3f;       // Increased for more forgiving controls
// static const float JUMP_BUFFER_TIME = 0.25f; // Increased from 0.13f to compensate for input lag
// static const float BUFFERED_CHARGE_BONUS = 3.0f; // Bonus charge rate multiplier for buffered jumps
// static int idleFrames = 0;            // Frames spent idle (not moving, grounded)
// static bool playingFidget = false;    // Currently in fidget animation
// static float fidgetPlayTime = 0.0f;   // How long fidget has been playing

// // Slide animation state
// static bool wasSliding = false;       // Was sliding last frame (for recovery detection)
// static bool isSlidingFront = true;    // Sliding direction (true=front, false=back)
// static bool isSlideRecovering = false; // Playing slide recovery animation
// static float slideRecoverTime = 0.0f;  // Time in recovery animation

// // Track currently attached animation to avoid expensive re-attaching every frame
// static T3DAnim* currentlyAttachedAnim = NULL;

// // Helper: Only attach animation if it's different from current (avoids expensive re-attach)
// static inline void attach_anim_if_different(T3DAnim* anim, T3DSkeleton* skel) {
//     if (currentlyAttachedAnim != anim) {
//         t3d_anim_attach(anim, skel);
//         currentlyAttachedAnim = anim;
//     }
// }

// // Health and damage system
// static int playerHealth = 3;
// static int maxPlayerHealth = 3;
// static bool isDead = false;
// static bool isHurt = false;
// static float hurtAnimTime = 0.0f;
// static T3DAnim* currentPainAnim = NULL;  // Track which pain animation is playing

// // Invincibility frames system
// static float invincibilityTimer = 0.0f;   // Time remaining for invincibility
// static int invincibilityFlashFrame = 0;   // Counter for flash effect
// #define INVINCIBILITY_DURATION 1.5f       // Seconds of invincibility after damage
// #define INVINCIBILITY_FLASH_RATE 4        // Flash every N frames (lower = faster)

// // Death transition
// static float deathTimer = 0.0f;
// static float fadeAlpha = 0.0f;  // 0.0 = transparent, 1.0 = fully black
// static bool isRespawning = false;
// static float respawnDelayTimer = 0.0f;  // Time spent in black screen after respawn

// // Death iris effect
// static float irisRadius = 400.0f;       // Current iris radius (starts large)
// static float irisCenterX = 160.0f;      // Iris center X (screen coords)
// static float irisCenterY = 120.0f;      // Iris center Y (screen coords)
// static float irisTargetX = 160.0f;      // Target center (player screen pos)
// static float irisTargetY = 120.0f;
// static bool irisActive = false;         // Is iris effect running?
// static float irisPauseTimer = 0.0f;     // Dramatic pause timer
// static bool irisPaused = false;         // In pause phase?
// #define IRIS_PAUSE_RADIUS 25.0f         // Radius at which to pause
// #define IRIS_PAUSE_DURATION 0.33f       // How long to hold the small circle (seconds)

// // Level transition
// static bool isTransitioning = false;
// static float transitionTimer = 0.0f;
// static int targetTransitionLevel = 0;
// static int targetTransitionSpawn = 0;

// // Level-local stats (for level complete screen)
// static int levelDeaths = 0;           // Deaths in current level attempt
// static float levelTime = 0.0f;        // Time spent in current level
// static int levelBoltsCollected = 0;   // Bolts collected in this level

// // Pause menu
// static OptionPrompt pauseMenu;
// static bool isPaused = false;
// static bool pauseMenuInitialized = false;
// static bool isQuittingToMenu = false;  // Iris out then go to menu

// // Dialogue/Script system for interact triggers
// static DialogueBox dialogueBox;
// static DialogueScript activeScript;
// static bool scriptRunning = false;
// static DecoInstance* activeInteractTrigger = NULL;  // Currently interacting trigger

// // ============================================================
// // SCREEN SHAKE & HIT FLASH
// // ============================================================
// static float shakeIntensity = 0.0f;   // Current shake strength (decays over time)
// static float shakeOffsetX = 0.0f;     // Current frame's X offset
// static float shakeOffsetY = 0.0f;     // Current frame's Y offset
// static float hitFlashTimer = 0.0f;    // Time remaining for hit flash (0 = no flash)

// // Trigger screen shake (call when taking damage, landing hard, etc.)
// static inline void trigger_screen_shake(float intensity) {
//     if (intensity > shakeIntensity) {
//         shakeIntensity = intensity;
//     }
// }

// // Trigger hit flash (call when player takes damage)
// static inline void trigger_hit_flash(float duration) {
//     hitFlashTimer = duration;
// }

// // Update shake (call each frame)
// static inline void update_screen_shake(float deltaTime) {
//     if (shakeIntensity > 0.1f) {
//         // Random offset based on intensity
//         shakeOffsetX = ((rand() % 200) - 100) / 100.0f * shakeIntensity;
//         shakeOffsetY = ((rand() % 200) - 100) / 100.0f * shakeIntensity;
//         // Decay shake
//         shakeIntensity *= 0.85f;
//     } else {
//         shakeIntensity = 0.0f;
//         shakeOffsetX = 0.0f;
//         shakeOffsetY = 0.0f;
//     }

//     // Decay hit flash
//     if (hitFlashTimer > 0.0f) {
//         hitFlashTimer -= deltaTime;
//         if (hitFlashTimer < 0.0f) hitFlashTimer = 0.0f;
//     }
// }

// // ============================================================
// // SIMPLE PARTICLE SYSTEM (screen-space, no collision)
// // ============================================================
// #define MAX_PARTICLES 24

// typedef struct {
//     float x, y, z;           // World position
//     float velX, velY, velZ;  // Velocity
//     float life;              // Remaining lifetime (0 = dead)
//     float maxLife;           // Initial lifetime (for fade calc)
//     uint8_t r, g, b;         // Color
//     float size;              // Particle size
//     bool active;             // Is this slot in use?
// } Particle;

// static Particle g_particles[MAX_PARTICLES];

// // Death decals - persistent ground splats when slimes die
// #define MAX_DEATH_DECALS 8
// typedef struct {
//     float x, y, z;
//     float scale;
//     float alpha;
//     bool active;
// } DeathDecal;
// static DeathDecal g_deathDecals[MAX_DEATH_DECALS];

// // Spawn a death decal at position
// static inline void spawn_death_decal(float x, float y, float z, float scale) {
//     // Find oldest or inactive slot
//     int slot = 0;
//     float lowestAlpha = 999.0f;
//     for (int i = 0; i < MAX_DEATH_DECALS; i++) {
//         if (!g_deathDecals[i].active) {
//             slot = i;
//             break;
//         }
//         if (g_deathDecals[i].alpha < lowestAlpha) {
//             lowestAlpha = g_deathDecals[i].alpha;
//             slot = i;
//         }
//     }
//     g_deathDecals[slot].x = x;
//     g_deathDecals[slot].y = y;
//     g_deathDecals[slot].z = z;
//     g_deathDecals[slot].scale = scale;
//     g_deathDecals[slot].alpha = 1.0f;
//     g_deathDecals[slot].active = true;
// }

// // Update death decals (fade out)
// static inline void update_death_decals(float deltaTime) {
//     for (int i = 0; i < MAX_DEATH_DECALS; i++) {
//         if (g_deathDecals[i].active) {
//             g_deathDecals[i].alpha -= deltaTime * 0.3f;  // Fade over ~3 seconds
//             if (g_deathDecals[i].alpha <= 0.0f) {
//                 g_deathDecals[i].active = false;
//             }
//         }
//     }
// }

// // Initialize particle system (call once at startup)
// static inline void init_particles(void) {
//     for (int i = 0; i < MAX_PARTICLES; i++) {
//         g_particles[i].active = false;
//         g_particles[i].life = 0.0f;
//     }
//     for (int i = 0; i < MAX_DEATH_DECALS; i++) {
//         g_deathDecals[i].active = false;
//     }
// }

// // Spawn particles in an arc (for slime splash)
// static inline void spawn_splash_particles(float x, float y, float z, int count,
//                                           uint8_t r, uint8_t g, uint8_t b) {
//     int spawned = 0;
//     for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
//         if (!g_particles[i].active) {
//             Particle* p = &g_particles[i];
//             p->active = true;
//             p->x = x;
//             p->y = y + 2.0f;
//             p->z = z;

//             // Random outward velocity
//             float angle = (float)(rand() % 628) / 100.0f;
//             float speed = 1.5f + (float)(rand() % 100) / 50.0f;
//             p->velX = cosf(angle) * speed;
//             p->velZ = sinf(angle) * speed;
//             p->velY = 3.0f + (float)(rand() % 100) / 50.0f;

//             p->maxLife = 1.0f + (float)(rand() % 30) / 100.0f;
//             p->life = p->maxLife;
//             p->r = r;
//             p->g = g;
//             p->b = b;
//             p->size = 2.0f + (float)(rand() % 15) / 10.0f;
//             spawned++;
//         }
//     }
// }

// // Spawn dust puffs (big, slow, floaty - for player landing)
// static inline void spawn_dust_particles(float x, float y, float z, int count) {
//     int spawned = 0;
//     for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
//         if (!g_particles[i].active) {
//             Particle* p = &g_particles[i];
//             p->active = true;
//             p->x = x + ((rand() % 20) - 10);  // Slight random offset
//             p->y = y + 3.0f;
//             p->z = z + ((rand() % 20) - 10);

//             // Slow outward and upward drift
//             float angle = (float)(rand() % 628) / 100.0f;
//             float speed = 0.3f + (float)(rand() % 50) / 100.0f;
//             p->velX = cosf(angle) * speed;
//             p->velZ = sinf(angle) * speed;
//             p->velY = 0.8f + (float)(rand() % 50) / 100.0f;  // Gentle upward

//             p->maxLife = 0.25f + (float)(rand() % 15) / 100.0f;
//             p->life = p->maxLife;
//             // Brownish-gray dust color
//             p->r = 140 + (rand() % 30);
//             p->g = 120 + (rand() % 30);
//             p->b = 100 + (rand() % 30);
//             p->size = 4.0f + (float)(rand() % 30) / 10.0f;  // Big puffs
//             spawned++;
//         }
//     }
// }

// // Spawn oil splash particles (brown, goopy - for slime hits)
// static inline void spawn_oil_particles(float x, float y, float z, int count) {
//     int spawned = 0;
//     for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
//         if (!g_particles[i].active) {
//             Particle* p = &g_particles[i];
//             p->active = true;
//             p->x = x + ((rand() % 16) - 8);
//             p->y = y + 5.0f;
//             p->z = z + ((rand() % 16) - 8);

//             // Fast outward splatter
//             float angle = (float)(rand() % 628) / 100.0f;
//             float speed = 2.5f + (float)(rand() % 150) / 50.0f;
//             p->velX = cosf(angle) * speed;
//             p->velZ = sinf(angle) * speed;
//             p->velY = 4.0f + (float)(rand() % 200) / 50.0f;

//             p->maxLife = 0.6f + (float)(rand() % 30) / 100.0f;
//             p->life = p->maxLife;
//             // Brown/dark oil colors
//             p->r = 60 + (rand() % 40);
//             p->g = 40 + (rand() % 30);
//             p->b = 20 + (rand() % 20);
//             p->size = 3.0f + (float)(rand() % 20) / 10.0f;
//             spawned++;
//         }
//     }
// }

// // Spawn sparks (small, fast, arcing - for bolt collection)
// static inline void spawn_spark_particles(float x, float y, float z, int count) {
//     int spawned = 0;
//     for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
//         if (!g_particles[i].active) {
//             Particle* p = &g_particles[i];
//             p->active = true;
//             p->x = x;
//             p->y = y;
//             p->z = z;

//             // Fast outward velocity in all directions
//             float angle = (float)(rand() % 628) / 100.0f;
//             float speed = 2.0f + (float)(rand() % 150) / 50.0f;
//             p->velX = cosf(angle) * speed;
//             p->velZ = sinf(angle) * speed;
//             p->velY = 2.0f + (float)(rand() % 200) / 50.0f;  // Upward arc

//             p->maxLife = 0.4f + (float)(rand() % 20) / 100.0f;  // Short lived
//             p->life = p->maxLife;
//             // Yellow/orange spark colors
//             p->r = 255;
//             p->g = 200 + (rand() % 55);
//             p->b = 50 + (rand() % 100);
//             p->size = 1.5f + (float)(rand() % 10) / 10.0f;  // Small sparks
//             spawned++;
//         }
//     }
// }

// // Update all particles
// static inline void update_particles(float deltaTime) {
//     for (int i = 0; i < MAX_PARTICLES; i++) {
//         Particle* p = &g_particles[i];
//         if (!p->active) continue;

//         // Physics
//         p->velY -= 0.4f;
//         p->x += p->velX;
//         p->y += p->velY;
//         p->z += p->velZ;

//         // Lifetime
//         p->life -= deltaTime;

//         // Deactivate dead particles
//         if (p->life <= 0.0f) {
//             p->active = false;
//         }
//     }
// }

// // ============================================================
// // SM64-STYLE PHYSICS SYSTEM
// // ============================================================
// // Based on Super Mario 64's ground physics from mario_step.c and
// // mario_actions_moving.c. Key features:
// // - Forward velocity + facing angle (not separate X/Z velocity)
// // - Slope steepness affects movement speed
// // - Surface friction classes (normal, slippery, very slippery)
// // - Acceleration/deceleration curves
// // ============================================================

// // SM64 physics constants (tuned for this game's scale)
// #define SM64_WALK_ACCEL 0.8f           // Acceleration per frame
// #define SM64_WALK_SPEED_CAP 6.0f       // Normal max walk speed
// #define SM64_RUN_SPEED_CAP 10.0f       // Absolute max speed
// #define SM64_FRICTION_NORMAL 0.50f     // Ground friction (lower = stops faster, tighter)
// #define SM64_FRICTION_SLIPPERY 0.85f   // Slippery surface friction
// #define SM64_FRICTION_VERY_SLIPPERY 0.92f  // Oil/ice friction

// // SM64-style sliding - uses constants from constants.h
// // The key insight: acceleration is CONTINUOUS and scales with steepness
// // No discrete buckets - just physics responding to the actual slope angle

// // Calculate slope steepness from normal (SM64 style)
// static inline float sm64_get_steepness(float nx, float ny, float nz) {
//     (void)ny;
//     return sqrtf(nx * nx + nz * nz);
// }

// // SM64-style sliding physics - acceleration and friction
// // steepness = sqrt(nx^2 + nz^2) - the horizontal component of the normal
// // downhillX/Z = normalized direction gravity would push you
// static void sm64_apply_slide_physics(PlayerState* state, float downhillX, float downhillZ, float steepness) {
//     // Choose acceleration and friction based on surface
//     float accel = state->onOilPuddle ? SLIDE_ACCEL_SLIPPERY : SLIDE_ACCEL;
//     float friction = state->onOilPuddle ? SLIDE_FRICTION_SLIPPERY : SLIDE_FRICTION;

//     // Apply gravity acceleration in downhill direction
//     // Acceleration scales with steepness (steeper = faster acceleration)
//     if (steepness > 0.01f) {
//         state->slideVelX += accel * steepness * downhillX;
//         state->slideVelZ += accel * steepness * downhillZ;
//     }

//     // Apply friction - stronger on flat ground to stop sliding
//     float effectiveFriction = friction;
//     if (steepness < 0.1f) {
//         // Lerp to flat-ground friction as slope becomes flat
//         float t = steepness / 0.1f;  // 0 when flat, 1 at steepness=0.1
//         effectiveFriction = SLIDE_FRICTION_FLAT + t * (friction - SLIDE_FRICTION_FLAT);
//     }
//     state->slideVelX *= effectiveFriction;
//     state->slideVelZ *= effectiveFriction;

//     // Update facing direction based on velocity
//     float speed = sqrtf(state->slideVelX * state->slideVelX + state->slideVelZ * state->slideVelZ);
//     if (speed > 1.0f) {
//         state->slideYaw = atan2f(state->slideVelZ, state->slideVelX);
//     }
// }

// // SM64-style sliding update - returns true if sliding should stop
// // This handles steering, physics, speed cap, and stop detection
// static bool sm64_update_sliding(PlayerState* state, float inputX, float inputZ, float downhillX, float downhillZ, float steepness) {
//     float inputMag = sqrtf(inputX * inputX + inputZ * inputZ);
//     float speed = sqrtf(state->slideVelX * state->slideVelX + state->slideVelZ * state->slideVelZ);

//     // SM64-style steering: rotate velocity based on input
//     if (inputMag > 0.1f && speed > 1.0f) {
//         float steerFactor = inputMag * 0.05f;
//         float oldVelX = state->slideVelX;
//         float oldVelZ = state->slideVelZ;

//         // Rotate velocity (SM64's asymmetric steering)
//         state->slideVelX += oldVelZ * inputX * steerFactor;
//         state->slideVelZ -= oldVelX * inputZ * steerFactor;

//         // Preserve speed after steering
//         float newSpeed = sqrtf(state->slideVelX * state->slideVelX + state->slideVelZ * state->slideVelZ);
//         if (newSpeed > 0.1f) {
//             state->slideVelX = state->slideVelX * speed / newSpeed;
//             state->slideVelZ = state->slideVelZ * speed / newSpeed;
//         }
//     }

//     // Apply gravity acceleration and friction
//     sm64_apply_slide_physics(state, downhillX, downhillZ, steepness);

//     // Cap speed
//     speed = sqrtf(state->slideVelX * state->slideVelX + state->slideVelZ * state->slideVelZ);
//     if (speed > SLIDE_MAX_SPEED) {
//         float scale = SLIDE_MAX_SPEED / speed;
//         state->slideVelX *= scale;
//         state->slideVelZ *= scale;
//         speed = SLIDE_MAX_SPEED;
//     }

//     // Stop sliding only on flat ground when speed is low
//     // On steep slopes, always keep sliding (gravity will accelerate)
//     bool isFlat = steepness < 0.1f;
//     if (isFlat && speed < SLIDE_STOP_THRESHOLD) {
//         state->slideVelX = 0.0f;
//         state->slideVelZ = 0.0f;
//         return true;  // Stopped
//     }

//     return false;  // Still sliding
// }

// // Update walking speed (SM64's update_walking_speed)
// // Returns the target forward speed based on input, with slope influence
// __attribute__((unused))
// static float sm64_update_walking_speed(PlayerState* state, float inputMagnitude, float nx, float ny, float nz) {
//     // Calculate current forward speed
//     float forwardSpeed = sqrtf(state->velX * state->velX + state->velZ * state->velZ);

//     // Target speed based on input (0 to SM64_WALK_SPEED_CAP)
//     float targetSpeed = inputMagnitude * SM64_WALK_SPEED_CAP;

//     // Accelerate toward target speed
//     // SM64: accel = 1.1f - forwardSpeed / 43.0f (speed-dependent)
//     float accel = SM64_WALK_ACCEL - forwardSpeed / 43.0f;
//     if (accel < 0.1f) accel = 0.1f;

//     if (forwardSpeed < targetSpeed) {
//         forwardSpeed += accel;
//         if (forwardSpeed > targetSpeed) forwardSpeed = targetSpeed;
//     } else if (forwardSpeed > targetSpeed) {
//         // Decelerate
//         forwardSpeed -= accel * 2.0f;  // Decel faster than accel
//         if (forwardSpeed < targetSpeed) forwardSpeed = targetSpeed;
//     }

//     // Apply slope influence on steep slopes
//     float steepness = sm64_get_steepness(nx, ny, nz);
//     if (steepness > 0.1f && forwardSpeed > 1.0f) {
//         // Get movement direction
//         float moveDirX = sinf(state->playerAngle);
//         float moveDirZ = cosf(state->playerAngle);

//         // Dot product of movement direction and downhill direction
//         // Positive = going uphill (against normal), Negative = going downhill
//         float uphillDot = (moveDirX * nx + moveDirZ * nz) / steepness;

//         if (uphillDot > 0.0f) {
//             // Going uphill - reduce speed based on steepness and how directly uphill
//             float uphillPenalty = 1.0f - steepness * uphillDot * 0.7f;
//             if (uphillPenalty < 0.3f) uphillPenalty = 0.3f;
//             forwardSpeed *= uphillPenalty;
//         } else {
//             // Going downhill - slight speed boost
//             float downhillBoost = 1.0f - uphillDot * steepness * 0.3f;  // uphillDot is negative
//             if (downhillBoost > 1.5f) downhillBoost = 1.5f;
//             forwardSpeed *= downhillBoost;
//         }
//     }

//     // Cap at absolute maximum
//     if (forwardSpeed > SM64_RUN_SPEED_CAP) forwardSpeed = SM64_RUN_SPEED_CAP;

//     return forwardSpeed;
// }

// // Apply ground friction when stopping (SM64's apply_landing_accel)
// __attribute__((unused))
// static void sm64_apply_friction(PlayerState* state) {
//     float friction;
//     if (state->onOilPuddle) {
//         friction = SM64_FRICTION_VERY_SLIPPERY;
//     } else {
//         friction = SM64_FRICTION_NORMAL;
//     }

//     state->velX *= friction;
//     state->velZ *= friction;

//     // Zero out tiny velocities
//     if (state->velX * state->velX + state->velZ * state->velZ < 0.5f) {
//         state->velX = 0.0f;
//         state->velZ = 0.0f;
//     }
// }

// T3DViewport viewport;
// T3DMat4FP* playerMatFP;
// T3DMat4FP* roboMatFP;
// T3DMat4FP* arcMatFP;  // Matrices for jump arc dots

// int frameIdx = 0;
// rspq_block_t *dplDraw = NULL;
// static bool gameSceneInitialized = false;

// T3DVec3 camPos = {{0, -71.0f, -120.0f}};

// // Player position
// float cubeX = 0.0f;
// float cubeY = 100.0f;
// float cubeZ = 0.0f;

// // Ground level (fallback)
// float groundLevel = -100.0f;
// float bestGroundY = -9999.0f;

// // Camera smoothing
// static float smoothCamX = 0.0f;
// static float smoothCamY = 49.0f;

// // Debug fly camera
// static float debugCamX = 0.0f;
// static float debugCamY = 100.0f;
// static float debugCamZ = -120.0f;
// static float debugCamYaw = 0.0f;
// static float debugCamPitch = 0.0f;

// // Saved camera state when entering debug mode
// static float savedCamZ = -120.0f;

// // Debug placement mode
// static bool debugPlacementMode = false;
// static DecoType debugDecoType = DECO_BARREL;
// static float debugDecoX = 0.0f;
// static float debugDecoY = 0.0f;
// static float debugDecoZ = 0.0f;
// static float debugDecoRotY = 0.0f;
// static float debugDecoScaleX = 1.0f;
// static float debugDecoScaleY = 1.0f;
// static float debugDecoScaleZ = 1.0f;
// static int debugTriggerCooldown = 0;

// // Patrol route placement mode
// static bool patrolPlacementMode = false; // true while placing patrol route for a rat (or other future decorations)
// static DecoType patrolDecoHolder; // holds whichever decoration is being placed while patrol route is being defined
// static int patrolPointCount = 0; // number of patrol points placed for current decoration being placed
// static T3DVec3* patrolPoints = NULL; // dynamic array of patrol points for current decoration being placed

// // Debug delete mode (raycast selection in camera mode)
// static int debugHighlightedDecoIndex = -1;  // Index of decoration currently highlighted for deletion
// static float debugDeleteCooldown = 0.0f;    // Cooldown to prevent accidental rapid deletion

// // Debug collision visualization
// static bool debugShowCollision = false;  // Toggle with D-Left in debug mode

// // Reverse gravity
// bool reverseGravity = false;

// // Tweakable gameplay parameters (charge jump) - scaled for 30 FPS
// static float chargeJumpMaxBase = 4.0f * FPS_SCALE_SQRT;
// static float chargeJumpMaxMultiplier = 1.5f * FPS_SCALE_SQRT;
// static float chargeJumpEarlyBase = 3.0f * FPS_SCALE_SQRT;
// static float chargeJumpEarlyMultiplier = 2.0f * FPS_SCALE_SQRT;
// static float maxChargeTime = 1.5f;

// // Small hop parameters (quick A tap)
// static float hopThreshold = 0.15f;                      // Time threshold for hop vs charge (seconds)
// static float hopVelocityY = 5.0f * FPS_SCALE_SQRT;      // Small hop vertical velocity
// static float hopForwardSpeed = 1.0f * FPS_SCALE;        // Small hop forward speed (gentle)

// void init_game_scene(void) {
//     // Seed random number generator for pain animation variety
//     srand(TICKS_READ());

//     // Initialize particle system
//     init_particles();

//     // UI sprites and sounds are loaded globally in main.c

//     // Set current level from level select (or default to LEVEL_1 if coming from non-debug start)
//     currentLevel = (LevelID)selectedLevelID;
//     if (currentLevel >= LEVEL_COUNT) {
//         currentLevel = LEVEL_1;
//     }

//     // Reset level-local stats for level complete screen
//     levelDeaths = 0;
//     levelTime = 0.0f;
//     levelBoltsCollected = 0;

//     // Reset all state
//     frameIdx = 0;
//     debugFlyMode = false;

//     // Reset player position and velocity
//     cubeX = 0.0f;
//     cubeY = 100.0f;
//     cubeZ = 0.0f;
//     bestGroundY = -9999.0f;

//     // Reset camera
//     camPos = (T3DVec3){{0, -71.0f, -120.0f}};
//     camTarget = (T3DVec3){{0, -100.0f, 0}};
//     smoothCamX = 0.0f;
//     smoothCamY = 49.0f;

//     // Reset debug fly camera
//     debugCamX = 0.0f;
//     debugCamY = 100.0f;
//     debugCamZ = -120.0f;
//     debugCamYaw = 0.0f;
//     debugCamPitch = 0.0f;

//     // Reset debug placement mode
//     debugPlacementMode = false;
//     debugDecoType = DECO_BARREL;
//     debugDecoX = 0.0f;
//     debugDecoY = 0.0f;
//     debugDecoZ = 0.0f;
//     debugDecoRotY = 0.0f;
//     debugDecoScaleX = 1.0f;
//     debugDecoScaleY = 1.0f;
//     debugDecoScaleZ = 1.0f;
//     debugTriggerCooldown = 0;
//     debugHighlightedDecoIndex = -1;
//     debugDeleteCooldown = 0.0f;

//     // Initialize debug menu with physics items
//     debug_menu_init("PHYSICS DEBUG");
//     debug_menu_add_float("Move Speed", &controlConfig.moveSpeed, 0.1f, 20.0f, 0.1f);
//     debug_menu_add_float("Jump Force", &controlConfig.jumpForce, 0.1f, 50.0f, 0.1f);
//     debug_menu_add_float("Gravity", &controlConfig.gravity, 0.01f, 2.0f, 0.01f);
//     debug_menu_add_float("Player Radius", &playerRadius, 1.0f, 50.0f, 1.0f);
//     debug_menu_add_float("Player Height", &playerHeight, 1.0f, 100.0f, 1.0f);
//     debug_menu_add_float("ChgJmp MaxBase", &chargeJumpMaxBase, 0.1f, 50.0f, 0.1f);
//     debug_menu_add_float("ChgJmp MaxMult", &chargeJumpMaxMultiplier, 0.1f, 10.0f, 0.1f);
//     debug_menu_add_float("ChgJmp ErlBase", &chargeJumpEarlyBase, 0.1f, 50.0f, 0.1f);
//     debug_menu_add_float("ChgJmp ErlMult", &chargeJumpEarlyMultiplier, 0.1f, 10.0f, 0.1f);
//     debug_menu_add_float("Max Charge", &maxChargeTime, 0.1f, 5.0f, 0.1f);

//     // Initialize controls
//     controls_init(&controlConfig);
//     playerState.velX = 0.0f;
//     playerState.velY = 0.0f;
//     playerState.velZ = 0.0f;
//     playerState.playerAngle = T3D_DEG_TO_RAD(-90.0f);
//     playerState.isGrounded = false;
//     playerState.isOnSlope = false;
//     playerState.isSliding = false;
//     playerState.slopeType = SLOPE_FLAT;
//     playerState.slopeNormalX = 0.0f;
//     playerState.slopeNormalY = 1.0f;
//     playerState.slopeNormalZ = 0.0f;
//     playerState.currentJumps = 0;
//     playerState.groundedFrames = 0;
//     playerState.canMove = true;
//     playerState.canRotate = true;
//     playerState.canJump = true;
//     playerState.slideVelX = 0.0f;
//     playerState.slideVelZ = 0.0f;
//     playerState.slideYaw = 0.0f;
//     playerState.hitWall = false;
//     playerState.wallNormalX = 0.0f;
//     playerState.wallNormalZ = 0.0f;
//     playerState.wallHitTimer = 0;

//     test_sprite = sprite_load("rom:/adult_fp.sprite");

//     // Load sound effects (audio already initialized in main.c)
//     wav64_open(&sfxBoltCollect, "rom:/BoltCollected.wav64");

//     // Initialize map loader and decoration runtime
//     maploader_init(&mapLoader, FB_COUNT, VISIBILITY_RANGE);
//     map_runtime_init(&mapRuntime, FB_COUNT, VISIBILITY_RANGE);

//     // Reset animation tracking (prevents stale pointer to freed animations)
//     currentlyAttachedAnim = NULL;

//     // Load current level (map segments + decorations)
//     level_load(currentLevel, &mapLoader, &mapRuntime);

//     // Set body part for this level
//     currentPart = (RobotParts)level_get_body_part(currentLevel);
//     debugf("Level body part: %d\n", currentPart);

//     // Find first PlayerSpawn decoration and use it as spawn point
//     bool foundSpawn = false;
//     for (int i = 0; i < mapRuntime.decoCount; i++) {
//         DecoInstance* deco = &mapRuntime.decorations[i];
//         if (deco->type == DECO_PLAYERSPAWN && deco->active) {
//             cubeX = deco->posX;
//             cubeY = deco->posY;
//             cubeZ = deco->posZ;
//             foundSpawn = true;
//             debugf("Player spawn found at: (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);
//             break;
//         }
//     }
//     if (!foundSpawn) {
//         debugf("No PlayerSpawn found, using default position\n");
//     }

//     decoMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT * MAX_DECORATIONS);

//     // Allocate player matrices
//     playerMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
//     roboMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
//     arcMatFP = malloc_uncached(sizeof(T3DMat4FP) * 15);  // Max arc dots

//     // Load shadow sprite and create shadow quad vertices
//     shadowSprite = sprite_load("rom:/shadow.sprite");
//     shadowVerts = malloc_uncached(sizeof(T3DVertPacked) * 2);  // 4 verts = 2 packed

//     // Health HUD - lazy loaded on first damage
//     healthSpritesLoaded = false;
//     healthHudY = HEALTH_HUD_HIDE_Y;
//     healthHudTargetY = HEALTH_HUD_HIDE_Y;
//     healthHudVisible = false;
//     healthFlashTimer = 0.0f;
//     healthHudHideTimer = 0.0f;

//     // Screw HUD - lazy loaded on first bolt collection
//     screwSpritesLoaded = false;
//     screwHudX = SCREW_HUD_HIDE_X;
//     screwHudTargetX = SCREW_HUD_HIDE_X;
//     screwHudVisible = false;
//     screwAnimFrame = 0;
//     screwAnimTimer = 0.0f;
//     screwHudHideTimer = 0.0f;

//     // Allocate decal vertices and matrix for slime oil trails
//     decalVerts = malloc_uncached(sizeof(T3DVertPacked) * 2);  // 4 verts = 2 packed
//     decalMatFP = malloc_uncached(sizeof(T3DMat4FP));

//     // Load player model (RoboPlayer_DC with torso and arms animations)
//     torsoModel = t3d_model_load("rom:/RoboPlayer_DC.t3dm");
//     if (torsoModel) {
//         torsoSkel = t3d_skeleton_create(torsoModel);
//         torsoSkelBlend = t3d_skeleton_clone(&torsoSkel, false);

//         // Load torso animations
//         // Idle is the default standing pose - loops continuously
//         torsoAnimIdle = t3d_anim_create(torsoModel, "torso_idle");
//         t3d_anim_set_looping(&torsoAnimIdle, true);
//         t3d_anim_attach(&torsoAnimIdle, &torsoSkel);  // Attach idle to main skeleton
//         t3d_anim_set_playing(&torsoAnimIdle, true);

//         torsoAnimWalk = t3d_anim_create(torsoModel, "torso_walk");
//         t3d_anim_attach(&torsoAnimWalk, &torsoSkelBlend);
//         t3d_anim_set_looping(&torsoAnimWalk, true);
//         t3d_anim_set_playing(&torsoAnimWalk, true);

//         torsoAnimJumpCharge = t3d_anim_create(torsoModel, "torso_jump_charge");
//         t3d_anim_set_looping(&torsoAnimJumpCharge, false);

//         torsoAnimJumpLaunch = t3d_anim_create(torsoModel, "torso_jump_launch");
//         t3d_anim_set_looping(&torsoAnimJumpLaunch, false);
//         debugf("torso_jump_launch: %s\n", torsoAnimJumpLaunch.animRef ? "OK" : "MISSING");

//         torsoAnimJumpLand = t3d_anim_create(torsoModel, "torso_jump_land");
//         t3d_anim_set_looping(&torsoAnimJumpLand, false);
//         debugf("torso_jump_land: %s\n", torsoAnimJumpLand.animRef ? "OK" : "MISSING");

//         // Wait is the fidget animation - plays once every 7 seconds
//         torsoAnimWait = t3d_anim_create(torsoModel, "torso_wait");
//         t3d_anim_set_looping(&torsoAnimWait, false);

//         // Pain animations - play when taking damage
//         torsoAnimPain1 = t3d_anim_create(torsoModel, "torso_pain_1");
//         t3d_anim_set_looping(&torsoAnimPain1, false);
//         debugf("torso_pain_1: %s\n", torsoAnimPain1.animRef ? "OK" : "MISSING");

//         torsoAnimPain2 = t3d_anim_create(torsoModel, "torso_pain_2");
//         t3d_anim_set_looping(&torsoAnimPain2, false);
//         debugf("torso_pain_2: %s\n", torsoAnimPain2.animRef ? "OK" : "MISSING");

//         // Death animation - play when health reaches 0
//         torsoAnimDeath = t3d_anim_create(torsoModel, "torso_death");
//         t3d_anim_set_looping(&torsoAnimDeath, false);
//         debugf("torso_death: %s\n", torsoAnimDeath.animRef ? "OK" : "MISSING");

//         // Slide animations - play once and hold last frame
//         torsoAnimSlideFront = t3d_anim_create(torsoModel, "torso_slide_front");
//         t3d_anim_set_looping(&torsoAnimSlideFront, false);
//         debugf("torso_slide_front: %s\n", torsoAnimSlideFront.animRef ? "OK" : "MISSING");

//         torsoAnimSlideFrontRecover = t3d_anim_create(torsoModel, "torso_slide_front_recover");
//         t3d_anim_set_looping(&torsoAnimSlideFrontRecover, false);
//         debugf("torso_slide_front_recover: %s\n", torsoAnimSlideFrontRecover.animRef ? "OK" : "MISSING");

//         torsoAnimSlideBack = t3d_anim_create(torsoModel, "torso_slide_back");
//         t3d_anim_set_looping(&torsoAnimSlideBack, false);
//         debugf("torso_slide_back: %s\n", torsoAnimSlideBack.animRef ? "OK" : "MISSING");

//         torsoAnimSlideBackRecover = t3d_anim_create(torsoModel, "torso_slide_back_recover");
//         t3d_anim_set_looping(&torsoAnimSlideBackRecover, false);
//         debugf("torso_slide_back_recover: %s\n", torsoAnimSlideBackRecover.animRef ? "OK" : "MISSING");

//         torsoHasAnims = true;
//         debugf("Loaded Torso model with animations\n");

//         // Load arms mode animations (same model, different animation set)
//         armsAnimIdle = t3d_anim_create(torsoModel, "arms_idle");
//         t3d_anim_set_looping(&armsAnimIdle, true);
//         debugf("arms_idle: %s\n", armsAnimIdle.animRef ? "OK" : "MISSING");

//         armsAnimWalk1 = t3d_anim_create(torsoModel, "arms_walk_1");
//         t3d_anim_set_looping(&armsAnimWalk1, true);
//         debugf("arms_walk_1: %s\n", armsAnimWalk1.animRef ? "OK" : "MISSING");

//         armsAnimWalk2 = t3d_anim_create(torsoModel, "arms_walk_2");
//         t3d_anim_set_looping(&armsAnimWalk2, true);
//         debugf("arms_walk_2: %s\n", armsAnimWalk2.animRef ? "OK" : "MISSING");

//         armsAnimJump = t3d_anim_create(torsoModel, "arms_jump");
//         t3d_anim_set_looping(&armsAnimJump, false);
//         debugf("arms_jump: %s\n", armsAnimJump.animRef ? "OK" : "MISSING");

//         armsAnimJumpLand = t3d_anim_create(torsoModel, "arms_jump_land");
//         t3d_anim_set_looping(&armsAnimJumpLand, false);
//         debugf("arms_jump_land: %s\n", armsAnimJumpLand.animRef ? "OK" : "MISSING");

//         armsAnimAtkSpin = t3d_anim_create(torsoModel, "arms_atk_spin");
//         t3d_anim_set_looping(&armsAnimAtkSpin, false);
//         debugf("arms_atk_spin: %s\n", armsAnimAtkSpin.animRef ? "OK" : "MISSING");

//         armsAnimAtkWhip = t3d_anim_create(torsoModel, "arms_atk_whip");
//         t3d_anim_set_looping(&armsAnimAtkWhip, false);
//         debugf("arms_atk_whip: %s\n", armsAnimAtkWhip.animRef ? "OK" : "MISSING");

//         armsAnimDeath = t3d_anim_create(torsoModel, "arms_death");
//         t3d_anim_set_looping(&armsAnimDeath, false);
//         debugf("arms_death: %s\n", armsAnimDeath.animRef ? "OK" : "MISSING");

//         armsAnimPain1 = t3d_anim_create(torsoModel, "arms_pain_1");
//         t3d_anim_set_looping(&armsAnimPain1, false);
//         debugf("arms_pain_1: %s\n", armsAnimPain1.animRef ? "OK" : "MISSING");

//         armsAnimPain2 = t3d_anim_create(torsoModel, "arms_pain_2");
//         t3d_anim_set_looping(&armsAnimPain2, false);
//         debugf("arms_pain_2: %s\n", armsAnimPain2.animRef ? "OK" : "MISSING");

//         armsAnimSlide = t3d_anim_create(torsoModel, "arms_slide");
//         t3d_anim_set_looping(&armsAnimSlide, true);
//         debugf("arms_slide: %s\n", armsAnimSlide.animRef ? "OK" : "MISSING");

//         armsHasAnims = armsAnimIdle.animRef && armsAnimWalk1.animRef && armsAnimAtkSpin.animRef;
//         debugf("Arms mode animations: %s\n", armsHasAnims ? "OK" : "MISSING");
//     }

//     // Load simple cube for jump arc visualization
//     arcDotModel = t3d_model_load("rom:/TransitionCollision.t3dm");

//     // Reset torso state
//     isCharging = false;
//     isJumping = false;
//     jumpAnimPaused = false;
//     jumpChargeTime = 0.0f;
//     jumpAimX = 0.0f;
//     jumpAimY = 0.0f;
//     idleFrames = 0;
//     playingFidget = false;
//     fidgetPlayTime = 0.0f;
//     wasSliding = false;
//     isSlidingFront = true;
//     isSlideRecovering = false;
//     slideRecoverTime = 0.0f;

//     // Reset arms mode state
//     isArmsMode = false;
//     armsIsSpinning = false;
//     armsIsWhipping = false;
//     armsSpinTime = 0.0f;
//     armsWhipTime = 0.0f;

//     // Reset health state
//     playerHealth = maxPlayerHealth;
//     isDead = false;
//     isHurt = false;
//     hurtAnimTime = 0.0f;
//     currentPainAnim = NULL;
//     invincibilityTimer = 0.0f;
//     invincibilityFlashFrame = 0;

//     // Reset death transition
//     deathTimer = 0.0f;
//     fadeAlpha = 0.0f;
//     isRespawning = false;
//     respawnDelayTimer = 0.0f;

//     // Reset level transition
//     isTransitioning = false;
//     transitionTimer = 0.0f;
//     targetTransitionLevel = 0;
//     targetTransitionSpawn = 0;

//     // Check if we should start with iris opening effect (from menu transition)
//     if (startWithIrisOpen) {
//         irisActive = true;
//         irisRadius = 0.0f;  // Start fully closed
//         irisCenterX = 160.0f;
//         irisCenterY = 120.0f;
//         isRespawning = true;  // Use respawn logic to open iris
//         respawnDelayTimer = 0.5f;  // Skip most of the delay, go straight to opening
//         startWithIrisOpen = false;  // Clear the flag
//     } else {
//         irisActive = false;
//         irisRadius = 400.0f;
//     }

//     // currentPart is already set by level_get_body_part() earlier
//     partSwitchCooldown = 0;

//     // Reset pause state
//     isPaused = false;
//     pauseMenuInitialized = false;
//     isQuittingToMenu = false;

//     // Initialize dialogue system for interact triggers
//     dialogue_init(&dialogueBox);
//     scriptRunning = false;
//     activeInteractTrigger = NULL;

//     viewport = t3d_viewport_create_buffered(FB_COUNT);

//     gameSceneInitialized = true;
// }

// void deinit_game_scene(void) {
//     if (!gameSceneInitialized) return;

//     if (test_sprite) {
//         sprite_free(test_sprite);
//         test_sprite = NULL;
//     }
//     // Free health HUD sprites
//     for (int i = 0; i < 4; i++) {
//         if (healthSprites[i]) {
//             sprite_free(healthSprites[i]);
//             healthSprites[i] = NULL;
//         }
//     }
//     healthSpritesLoaded = false;
//     // Free screw HUD sprites
//     for (int i = 0; i < SCREW_HUD_FRAME_COUNT; i++) {
//         if (screwSprites[i]) {
//             sprite_free(screwSprites[i]);
//             screwSprites[i] = NULL;
//         }
//     }
//     screwSpritesLoaded = false;
//     if (torsoModel) {
//         if (torsoHasAnims) {
//             t3d_anim_destroy(&torsoAnimIdle);
//             t3d_anim_destroy(&torsoAnimWalk);
//             t3d_anim_destroy(&torsoAnimJumpCharge);
//             t3d_anim_destroy(&torsoAnimJumpLaunch);
//             t3d_anim_destroy(&torsoAnimJumpLand);
//             t3d_anim_destroy(&torsoAnimWait);
//             t3d_anim_destroy(&torsoAnimPain1);
//             t3d_anim_destroy(&torsoAnimPain2);
//             t3d_anim_destroy(&torsoAnimDeath);
//             t3d_anim_destroy(&torsoAnimSlideFront);
//             t3d_anim_destroy(&torsoAnimSlideFrontRecover);
//             t3d_anim_destroy(&torsoAnimSlideBack);
//             t3d_anim_destroy(&torsoAnimSlideBackRecover);
//             currentlyAttachedAnim = NULL;  // Reset animation tracking after destroying animations
//             torsoHasAnims = false;
//         }
//         if (armsHasAnims) {
//             t3d_anim_destroy(&armsAnimIdle);
//             t3d_anim_destroy(&armsAnimWalk1);
//             t3d_anim_destroy(&armsAnimWalk2);
//             t3d_anim_destroy(&armsAnimJump);
//             t3d_anim_destroy(&armsAnimJumpLand);
//             t3d_anim_destroy(&armsAnimAtkSpin);
//             t3d_anim_destroy(&armsAnimAtkWhip);
//             armsHasAnims = false;
//         }
//         t3d_skeleton_destroy(&torsoSkelBlend);
//         t3d_skeleton_destroy(&torsoSkel);
//         t3d_model_free(torsoModel);
//         torsoModel = NULL;
//     }
//     if (arcDotModel) {
//         t3d_model_free(arcDotModel);
//         arcDotModel = NULL;
//     }
//     maploader_free(&mapLoader);
//     map_runtime_free(&mapRuntime);
//     t3d_viewport_destroy(&viewport);
//     if (decoMatFP) {
//         free_uncached(decoMatFP);
//         decoMatFP = NULL;
//     }
//     if (playerMatFP) {
//         free_uncached(playerMatFP);
//         playerMatFP = NULL;
//     }
//     if (roboMatFP) {
//         free_uncached(roboMatFP);
//         roboMatFP = NULL;
//     }
//     if (arcMatFP) {
//         free_uncached(arcMatFP);
//         arcMatFP = NULL;
//     }

//     // Cleanup conveyor belt UV data
//     if (g_conveyorBaseUVs) {
//         free(g_conveyorBaseUVs);
//         g_conveyorBaseUVs = NULL;
//     }
//     g_conveyorVertCount = 0;
//     g_conveyorVerts = NULL;

//     // Cleanup toxic pipe liquid UV data
//     if (g_toxicPipeLiquidBaseUVs) {
//         free(g_toxicPipeLiquidBaseUVs);
//         g_toxicPipeLiquidBaseUVs = NULL;
//     }
//     g_toxicPipeLiquidVertCount = 0;
//     g_toxicPipeLiquidVerts = NULL;

//     // Cleanup toxic running UV data
//     if (g_toxicRunningBaseUVs) {
//         free(g_toxicRunningBaseUVs);
//         g_toxicRunningBaseUVs = NULL;
//     }
//     g_toxicRunningVertCount = 0;
//     g_toxicRunningVerts = NULL;

//     // Cleanup shadow sprite and verts
//     if (shadowSprite) {
//         sprite_free(shadowSprite);
//         shadowSprite = NULL;
//     }
//     if (shadowVerts) {
//         free_uncached(shadowVerts);
//         shadowVerts = NULL;
//     }

//     // Cleanup decal verts and matrix
//     if (decalVerts) {
//         free_uncached(decalVerts);
//         decalVerts = NULL;
//     }
//     if (decalMatFP) {
//         free_uncached(decalMatFP);
//         decalMatFP = NULL;
//     }

//     // Cleanup patrol points (debug feature)
//     if (patrolPoints) {
//         free(patrolPoints);
//         patrolPoints = NULL;
//     }

//     // Cleanup audio (don't close mixer/audio - they're managed globally in main.c)
//     level_stop_music();  // Stop level music first
//     wav64_close(&sfxBoltCollect);

//     // UI sprites and sounds are managed globally in main.c

//     gameSceneInitialized = false;
// }

// // Check if player is dead (can be called from decoration callbacks)
// bool player_is_dead(void) {
//     return isDead;
// }

// // Spawn splash particles (can be called from decoration callbacks)
// void game_spawn_splash_particles(float x, float y, float z, int count, uint8_t r, uint8_t g, uint8_t b) {
//     spawn_splash_particles(x, y, z, count, r, g, b);
// }

// // Spawn death decal (can be called from decoration callbacks)
// void game_spawn_death_decal(float x, float y, float z, float scale) {
//     spawn_death_decal(x, y, z, scale);
// }

// // Spawn spark particles (can be called from decoration callbacks)
// void game_spawn_spark_particles(float x, float y, float z, int count) {
//     spawn_spark_particles(x, y, z, count);
// }

// // Trigger health HUD display with optional flash effect
// static void trigger_health_display(bool withFlash) {
//     // Lazy load health sprites on first use
//     if (!healthSpritesLoaded) {
//         debugf("Lazy loading health sprites\n");
//         healthSprites[0] = sprite_load("rom:/health1.sprite");
//         healthSprites[1] = sprite_load("rom:/health2.sprite");
//         healthSprites[2] = sprite_load("rom:/health3.sprite");
//         healthSprites[3] = sprite_load("rom:/health4.sprite");
//         healthSpritesLoaded = true;
//     }

//     healthHudVisible = true;
//     healthHudTargetY = HEALTH_HUD_SHOW_Y;
//     if (withFlash) {
//         healthFlashTimer = HEALTH_FLASH_DURATION;
//         healthHudHideTimer = HEALTH_DISPLAY_DURATION;  // Auto-hide after duration
//     }
// }

// // Hide health HUD
// static void hide_health_display(void) {
//     healthHudVisible = false;
//     healthHudTargetY = HEALTH_HUD_HIDE_Y;
//     healthHudHideTimer = 0.0f;
// }

// // Trigger screw/bolt HUD display (slides in from right with spinning animation)
// static void trigger_screw_display(bool autoHide) {
//     // Lazy load screw sprites on first use (6 frames: 1,3,5,7,9,11)
//     if (!screwSpritesLoaded) {
//         debugf("Lazy loading screw sprites\n");
//         const int frameNumbers[SCREW_HUD_FRAME_COUNT] = {1, 3, 5, 7, 9, 11};
//         for (int i = 0; i < SCREW_HUD_FRAME_COUNT; i++) {
//             char path[32];
//             sprintf(path, "rom:/ScrewUI%d.sprite", frameNumbers[i]);
//             screwSprites[i] = sprite_load(path);
//         }
//         screwSpritesLoaded = true;
//     }

//     screwHudVisible = true;
//     screwHudTargetX = SCREW_HUD_SHOW_X;
//     if (autoHide) {
//         screwHudHideTimer = SCREW_DISPLAY_DURATION;
//     }
// }

// // Hide screw/bolt HUD
// static void hide_screw_display(void) {
//     screwHudVisible = false;
//     screwHudTargetX = SCREW_HUD_HIDE_X;
//     screwHudHideTimer = 0.0f;
// }

// // Global function to damage the player (can be called from decoration callbacks)
// void player_take_damage(int damage) {
//     // Ignore damage while invincible, dead, or in hurt animation
//     if (isDead || invincibilityTimer > 0.0f) return;

//     // Cancel any charged jump
//     if (isCharging) {
//         isCharging = false;
//     }

//     playerHealth -= damage;
//     debugf("Player took %d damage! Health: %d/%d\n", damage, playerHealth, maxPlayerHealth);

//     // Screen shake and hit flash on any damage
//     trigger_screen_shake(8.0f);
//     trigger_hit_flash(0.15f);
//     trigger_health_display(true);  // Show health HUD with flash

//     if (playerHealth <= 0) {
//         playerHealth = 0;
//         isDead = true;
//         isHurt = false;
//         invincibilityTimer = 0.0f;  // No invincibility when dead
//         deathTimer = 0.0f;  // Start death transition
//         trigger_screen_shake(15.0f);  // Extra shake on death

//         // Track death in save system
//         save_increment_deaths();
//         save_auto_save();
//         levelDeaths++;  // Track for level complete screen

//         // Start death animation (if available)
//         if (currentPart == PART_TORSO && torsoHasAnims && torsoAnimDeath.animRef != NULL) {
//             attach_anim_if_different(&torsoAnimDeath, &torsoSkel);
//             t3d_anim_set_time(&torsoAnimDeath, 0.0f);
//             t3d_anim_set_playing(&torsoAnimDeath, true);
//         } else if (currentPart == PART_ARMS && armsHasAnims && armsAnimDeath.animRef != NULL) {
//             attach_anim_if_different(&armsAnimDeath, &torsoSkel);
//             t3d_anim_set_time(&armsAnimDeath, 0.0f);
//             t3d_anim_set_playing(&armsAnimDeath, true);
//         }
//         debugf("Player died!\n");
//     } else {
//         // Start invincibility frames
//         invincibilityTimer = INVINCIBILITY_DURATION;
//         invincibilityFlashFrame = 0;

//         // Start hurt animation (if available)
//         isHurt = true;
//         hurtAnimTime = 0.0f;
//         currentPainAnim = NULL;
//         if (currentPart == PART_TORSO && torsoHasAnims) {
//             // Randomly choose pain_1 or pain_2
//             int painChoice = rand() % 2;
//             T3DAnim* painAnim = (painChoice == 0) ? &torsoAnimPain1 : &torsoAnimPain2;
//             if (painAnim->animRef != NULL) {
//                 currentPainAnim = painAnim;
//                 attach_anim_if_different(painAnim, &torsoSkel);
//                 t3d_anim_set_time(painAnim, 0.0f);
//                 t3d_anim_set_playing(painAnim, true);
//                 debugf("Playing torso pain animation %d\n", painChoice + 1);
//             } else {
//                 // Animation not loaded - skip hurt state
//                 isHurt = false;
//                 debugf("Warning: Torso pain animation not loaded!\n");
//             }
//         } else if (currentPart == PART_ARMS && armsHasAnims) {
//             // Randomly choose arms pain_1 or pain_2
//             int painChoice = rand() % 2;
//             T3DAnim* painAnim = (painChoice == 0) ? &armsAnimPain1 : &armsAnimPain2;
//             if (painAnim->animRef != NULL) {
//                 currentPainAnim = painAnim;
//                 attach_anim_if_different(painAnim, &torsoSkel);
//                 t3d_anim_set_time(painAnim, 0.0f);
//                 t3d_anim_set_playing(painAnim, true);
//                 debugf("Playing arms pain animation %d\n", painChoice + 1);
//             } else {
//                 // Animation not loaded - skip hurt state
//                 isHurt = false;
//                 debugf("Warning: Arms pain animation not loaded!\n");
//             }
//         }
//     }
// }

// // Global function to squash the player model (can be called from decoration callbacks)
// void player_squash(float amount) {
//     squashScale = amount;
//     squashVelocity = 0.0f;
// }

// // Global function to knock back the player (can be called from decoration callbacks)
// void player_knockback(float fromX, float fromZ, float strength) {
//     // Calculate direction from source to player (away from attacker)
//     float dx = cubeX - fromX;
//     float dz = cubeZ - fromZ;
//     float dist = sqrtf(dx * dx + dz * dz);
//     if (dist > 0.01f) {
//         dx /= dist;
//         dz /= dist;
//     } else {
//         // Default knockback direction if on top of source
//         dx = 0.0f;
//         dz = 1.0f;
//     }
//     // Apply immediate position offset for knockback (works even when hurt)
//     cubeX += dx * strength * 4.0f;
//     cubeZ += dz * strength * 4.0f;
//     // Small upward pop
//     playerState.velY = 2.0f;
//     playerState.isGrounded = false;
// }

// // Global function to bounce the player upward (stomp reward)
// void player_bounce(float strength) {
//     playerState.velY = strength;
//     playerState.isGrounded = false;
// }

// // Called when player collects a bolt - saves to save file
// void game_on_bolt_collected(int levelId, int boltIndex) {
//     save_collect_bolt(levelId, boltIndex);
//     levelBoltsCollected++;  // Track for level complete screen
//     // Auto-save after collecting a bolt
//     save_auto_save();
//     // Show screw HUD with spinning animation
//     trigger_screw_display(true);
// }

// // Restart the current level (called from pause menu)
// void game_restart_level(void) {
//     debugf("Restarting level %d\n", currentLevel);

//     // Reset player state
//     playerHealth = maxPlayerHealth;
//     isDead = false;
//     isHurt = false;
//     hurtAnimTime = 0.0f;
//     currentPainAnim = NULL;
//     invincibilityTimer = 0.0f;
//     invincibilityFlashFrame = 0;

//     // Reset death/transition state
//     deathTimer = 0.0f;
//     fadeAlpha = 0.0f;
//     isRespawning = false;
//     respawnDelayTimer = 0.0f;
//     isTransitioning = false;
//     transitionTimer = 0.0f;
//     irisActive = false;
//     irisRadius = 400.0f;

//     // Reset player velocity
//     playerState.velX = 0.0f;
//     playerState.velY = 0.0f;
//     playerState.velZ = 0.0f;
//     playerState.isGrounded = false;
//     playerState.isOnSlope = false;
//     playerState.isSliding = false;
//     playerState.currentJumps = 0;
//     playerState.canMove = true;
//     playerState.canRotate = true;
//     playerState.canJump = true;

//     // Reset animation states
//     isCharging = false;
//     isJumping = false;
//     isLanding = false;
//     jumpAnimPaused = false;
//     jumpChargeTime = 0.0f;
//     idleFrames = 0;

//     // Clear decorations and reload level
//     map_clear_decorations(&mapRuntime);
//     level_load(currentLevel, &mapLoader, &mapRuntime);

//     // Set body part for this level
//     currentPart = (RobotParts)level_get_body_part(currentLevel);

//     // Find first PlayerSpawn and use it
//     for (int i = 0; i < mapRuntime.decoCount; i++) {
//         DecoInstance* deco = &mapRuntime.decorations[i];
//         if (deco->type == DECO_PLAYERSPAWN && deco->active) {
//             cubeX = deco->posX;
//             cubeY = deco->posY;
//             cubeZ = deco->posZ;
//             debugf("Player respawned at: (%.1f, %.1f, %.1f)\n", cubeX, cubeY, cubeZ);
//             break;
//         }
//     }

//     debugf("Level %d restarted\n", currentLevel);
// }

// // Pause menu callback
// static void on_pause_menu_select(int choice) {
//     switch (choice) {
//         case 0:  // Resume
//             isPaused = false;
//             hide_health_display();
//             hide_screw_display();
//             break;
//         case 1:  // Restart
//             isPaused = false;
//             game_restart_level();
//             break;
//         case 2:  // Save & Return to Menu
//             save_auto_save();
//             level_stop_music();
//             isPaused = false;
//             // Start iris close effect, will change scene when fully closed
//             isQuittingToMenu = true;
//             irisActive = true;
//             irisRadius = 400.0f;
//             irisCenterX = 160.0f;
//             irisCenterY = 120.0f;
//             // Tell menu scene to open with iris effect
//             menuStartWithIrisOpen = true;
//             break;
//     }
// }

// // Open pause menu
// static void show_pause_menu(void) {
//     if (!pauseMenuInitialized) {
//         option_init(&pauseMenu);
//         pauseMenuInitialized = true;
//     }

//     option_set_title(&pauseMenu, "PAUSED");
//     option_add(&pauseMenu, "Resume");
//     option_add(&pauseMenu, "Restart Level");
//     option_add(&pauseMenu, "Save & Quit");
//     option_show(&pauseMenu, on_pause_menu_select, NULL);
//     isPaused = true;
//     trigger_health_display(false);  // Show health without flash when paused
//     trigger_screw_display(false);   // Show bolt counter without auto-hide when paused
// }

// // Show level complete screen with current stats
// void game_show_level_complete(void) {
//     int totalBoltsInLevel = get_level_bolt_count(currentLevel);
//     level_complete_set_data(currentLevel, levelBoltsCollected, totalBoltsInLevel, levelDeaths, levelTime);
//     change_scene(LEVEL_COMPLETE);
// }

// // Global function to trigger level transition (can be called from decoration callbacks)
// void trigger_level_transition(int targetLevel, int targetSpawn) {
//     // Validate target level
//     if (targetLevel < 0 || targetLevel >= LEVEL_COUNT) {
//         debugf("ERROR: Invalid target level %d\n", targetLevel);
//         return;
//     }

//     // Don't trigger if already transitioning
//     if (isTransitioning) {
//         return;
//     }

//     debugf("Triggering level transition to Level %d, Spawn %d\n", targetLevel, targetSpawn);

//     // Mark current level as completed (player reached transition point)
//     save_complete_level(currentLevel);
//     debugf("Level %d marked as completed\n", currentLevel);

//     // Start transition (fade out)
//     isTransitioning = true;
//     transitionTimer = 0.0f;
//     targetTransitionLevel = targetLevel;
//     targetTransitionSpawn = targetSpawn;
// }

// // Start a script by ID (for interact triggers)
// static void start_interact_script(int scriptId) {
//     script_init(&activeScript);

//     switch (scriptId) {
//         case 1: {
//             // Script 1: Generic NPC greeting
//             script_add_dialogue(&activeScript, "Hello there!", "NPC", -1);
//             break;
//         }
//         case 2: {
//             // Script 2: Sign/info
//             script_add_dialogue(&activeScript, "Welcome to this area!", NULL, -1);
//             break;
//         }
//         default: {
//             // Unknown script - show placeholder
//             char msg[64];
//             snprintf(msg, sizeof(msg), "Interact script %d not defined.", scriptId);
//             script_add_dialogue(&activeScript, msg, "System", -1);
//             break;
//         }
//     }

//     script_start(&activeScript, &dialogueBox, NULL);
//     scriptRunning = true;
// }

// void update_game_scene(void) {
//     frameIdx = (frameIdx + 1) % FB_COUNT;
//     float deltaTime = DELTA_TIME;  // 1/30 second at 30 FPS

//     // Update audio mixer (always, even when paused)
//     uint32_t audioStart = get_ticks();
//     if (audio_can_write()) {
//         short *buf = audio_write_begin();
//         mixer_poll(buf, audio_get_buffer_length());
//         audio_write_end();
//     }
//     g_audioTicks += get_ticks() - audioStart;

//     // Update health HUD animation (runs even when paused)
//     if (healthHudY != healthHudTargetY) {
//         float diff = healthHudTargetY - healthHudY;
//         healthHudY += diff * HEALTH_HUD_SPEED * deltaTime;
//         // Snap when close enough
//         if (fabsf(diff) < 0.5f) {
//             healthHudY = healthHudTargetY;
//         }
//     }
//     if (healthFlashTimer > 0.0f) {
//         healthFlashTimer -= deltaTime;
//     }
//     if (healthHudHideTimer > 0.0f) {
//         healthHudHideTimer -= deltaTime;
//         if (healthHudHideTimer <= 0.0f && !isPaused) {
//             hide_health_display();
//         }
//     }

//     // Update screw HUD position (slides in/out from right)
//     if (screwHudX != screwHudTargetX) {
//         float diff = screwHudTargetX - screwHudX;
//         screwHudX += diff * SCREW_HUD_SPEED * deltaTime;
//         if (fabsf(diff) < 0.5f) {
//             screwHudX = screwHudTargetX;
//         }
//     }
//     // Update screw animation (spinning effect)
//     if (screwHudVisible || screwHudX < SCREW_HUD_HIDE_X - 1.0f) {
//         screwAnimTimer += deltaTime;
//         if (screwAnimTimer >= 1.0f / SCREW_ANIM_FPS) {
//             screwAnimTimer -= 1.0f / SCREW_ANIM_FPS;
//             screwAnimFrame = (screwAnimFrame + 1) % SCREW_HUD_FRAME_COUNT;
//         }
//     }
//     // Auto-hide screw HUD after timer
//     if (screwHudHideTimer > 0.0f) {
//         screwHudHideTimer -= deltaTime;
//         if (screwHudHideTimer <= 0.0f && !isPaused) {
//             hide_screw_display();
//         }
//     }

//     // Handle pause menu
//     if (isPaused) {
//         joypad_poll();  // Need to poll since controls_update is skipped
//         option_update(&pauseMenu, JOYPAD_PORT_1);
//         // Check if menu was closed by callback or B button
//         if (!option_is_active(&pauseMenu)) {
//             isPaused = false;
//             hide_health_display();
//             hide_screw_display();
//         }
//         return;  // Skip all game updates while paused
//     }

//     // Handle quit-to-menu iris transition
//     if (isQuittingToMenu && irisActive) {
//         // Shrink iris toward center
//         float shrinkSpeed = irisRadius * 0.08f;
//         if (shrinkSpeed < 3.0f) shrinkSpeed = 3.0f;
//         irisRadius -= shrinkSpeed;

//         if (irisRadius <= 0.0f) {
//             irisRadius = 0.0f;
//             // Iris fully closed - transition to menu
//             isQuittingToMenu = false;
//             irisActive = false;
//             change_scene(MENU_SCENE);
//         }
//         return;  // Skip game updates during quit transition
//     }

//     // Handle dialogue for interact triggers
//     if (scriptRunning) {
//         joypad_poll();
//         if (dialogue_update(&dialogueBox, JOYPAD_PORT_1)) {
//             // Dialogue consumed input
//             if (!dialogue_is_active(&dialogueBox)) {
//                 // Dialogue just ended
//                 scriptRunning = false;
//                 if (activeInteractTrigger) {
//                     // Restore player angle
//                     playerState.playerAngle = activeInteractTrigger->state.interactTrigger.savedPlayerAngle;
//                     // Clear interacting flag
//                     activeInteractTrigger->state.interactTrigger.interacting = false;
//                     // Mark as triggered if once-only
//                     if (activeInteractTrigger->state.interactTrigger.onceOnly) {
//                         activeInteractTrigger->state.interactTrigger.hasTriggered = true;
//                     }
//                     activeInteractTrigger = NULL;
//                 }
//             }
//         }
//         return;  // Skip game updates while dialogue active
//     }

//     // Check for interact trigger A button press (before regular input)
//     if (!isDead && !isHurt && !isRespawning && !isTransitioning && !debugFlyMode) {
//         for (int i = 0; i < mapRuntime.decoCount; i++) {
//             DecoInstance* deco = &mapRuntime.decorations[i];
//             if (deco->active && deco->type == DECO_INTERACTTRIGGER &&
//                 deco->state.interactTrigger.playerInRange && !deco->state.interactTrigger.interacting) {
//                 // Check if A button was pressed
//                 joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
//                 if (pressed.a) {
//                     // Save player angle
//                     deco->state.interactTrigger.savedPlayerAngle = playerState.playerAngle;
//                     // Rotate player to look at specified angle
//                     playerState.playerAngle = deco->state.interactTrigger.lookAtAngle;
//                     // Start dialogue
//                     deco->state.interactTrigger.interacting = true;
//                     activeInteractTrigger = deco;
//                     start_interact_script(deco->state.interactTrigger.scriptId);
//                     ui_play_ui_open_sound();  // Play UI open sound
//                     break;
//                 }
//             }
//         }
//     }

//     // Track play time (only when not dead/respawning)
//     if (!isDead && !isRespawning) {
//         save_add_play_time(deltaTime);
//         levelTime += deltaTime;  // Track for level complete screen
//     }

//     // Process controls (disabled when dead, hurt, respawning, or transitioning)
//     uint32_t inputStart = get_ticks();
//     if (!isDead && !isHurt && !isRespawning && !isTransitioning) {
//         controls_update(&playerState, &controlConfig, JOYPAD_PORT_1);
//     }
//     g_inputTicks += get_ticks() - inputStart;

//     // Check for debug toggle (cheat code is entered on title screen)
//     // Skip toggle if menu is open to prevent conflicts
//     if (!debug_menu_is_open() && psyops_check_debug_toggle(JOYPAD_PORT_1)) {
//         debugFlyMode = psyops_is_debug_active();
//         if (debugFlyMode) {
//             // Save camera state and initialize debug camera at current view
//             savedCamZ = camPos.v[2];
//             debugCamX = camPos.v[0];
//             debugCamY = camPos.v[1];
//             debugCamZ = camPos.v[2];
//             debugCamYaw = 3.14159f;  // Start facing opposite direction
//             debugCamPitch = 0.0f;
//             debugPlacementMode = false;
//         } else {
//             // Restore camera Z position when leaving debug mode
//             camPos.v[2] = savedCamZ;
//             debugPlacementMode = false;
//         }
//     }

//     // Handle debug menu (shared module)
//     if (debug_menu_update(JOYPAD_PORT_1, deltaTime)) {
//         return;  // Debug menu consumed input
//     }

//     // D-Pad Left toggles reverse gravity (only when debug menu closed)
//     if (!debug_menu_is_open() && !isDead && !isHurt && !isRespawning) {
//         joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
//         reverseGravity = held.d_left;
//     }

//     if (debugFlyMode) {
//         joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
//         joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
//         joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

//         // Toggle performance graph with C-Left + C-Right
//         static bool perfGraphToggleCooldown = false;
//         if (held.c_left && held.c_right) {
//             if (!perfGraphToggleCooldown) {
//                 perfGraphEnabled = !perfGraphEnabled;
//                 perfGraphToggleCooldown = true;
//             }
//         } else {
//             perfGraphToggleCooldown = false;
//         }

//         float stickX = apply_deadzone(inputs.stick_x / 128.0f);
//         float stickY = apply_deadzone(inputs.stick_y / 128.0f);
//         float cosYaw = cosf(debugCamYaw);
//         float sinYaw = sinf(debugCamYaw);

//         // L/R triggers cycle decoration type
//         if (debugTriggerCooldown > 0) debugTriggerCooldown--;
//         if (debugTriggerCooldown == 0) {
//             if (held.l && !patrolPlacementMode) {
//                 debugDecoType = (debugDecoType + DECO_TYPE_COUNT - 1) % DECO_TYPE_COUNT;
//                 map_get_deco_model(&mapRuntime, debugDecoType);
//                 debugTriggerCooldown = 15;
//             }
//             if (held.r && !patrolPlacementMode) {
//                 debugDecoType = (debugDecoType + 1) % DECO_TYPE_COUNT;
//                 map_get_deco_model(&mapRuntime, debugDecoType);
//                 debugTriggerCooldown = 15;
//             }
//         }

//         if (!debugPlacementMode) {
//             // === CAMERA MODE ===
//             float flySpeed = 5.0f;
//             float rotSpeed = 0.05f;

//             if (held.c_left) debugCamYaw -= rotSpeed;
//             if (held.c_right) debugCamYaw += rotSpeed;
//             if (held.c_up) debugCamPitch += rotSpeed;
//             if (held.c_down) debugCamPitch -= rotSpeed;

//             if (debugCamPitch > 1.5f) debugCamPitch = 1.5f;
//             if (debugCamPitch < -1.5f) debugCamPitch = -1.5f;

//             debugCamX += (stickX * cosYaw + stickY * sinYaw) * flySpeed;
//             debugCamZ += (stickX * sinYaw - stickY * cosYaw) * flySpeed;

//             if (held.a) debugCamY += flySpeed;
//             if (held.z) debugCamY -= flySpeed;

//             // === RAYCAST SELECTION FOR DELETION ===
//             // Calculate camera look direction from yaw and pitch
//             float cosPitch = cosf(debugCamPitch);
//             float sinPitch = sinf(debugCamPitch);
//             float lookDirX = sinYaw * cosPitch;
//             float lookDirY = sinPitch;
//             float lookDirZ = -cosYaw * cosPitch;

//             // Find decoration closest to camera look ray
//             float bestScore = 999999.0f;
//             int bestIndex = -1;
//             float maxSelectDist = 500.0f;  // Max distance to select
//             float selectRadius = 30.0f;     // Lenient selection radius

//             for (int i = 0; i < mapRuntime.decoCount; i++) {
//                 DecoInstance* deco = &mapRuntime.decorations[i];
//                 if (!deco->active || deco->type == DECO_NONE) continue;

//                 // Vector from camera to decoration
//                 float toCamX = deco->posX - debugCamX;
//                 float toCamY = deco->posY - debugCamY;
//                 float toCamZ = deco->posZ - debugCamZ;

//                 // Distance along look direction (dot product)
//                 float alongRay = toCamX * lookDirX + toCamY * lookDirY + toCamZ * lookDirZ;

//                 // Skip if behind camera or too far
//                 if (alongRay < 10.0f || alongRay > maxSelectDist) continue;

//                 // Perpendicular distance from ray (cross product magnitude)
//                 float perpX = toCamY * lookDirZ - toCamZ * lookDirY;
//                 float perpY = toCamZ * lookDirX - toCamX * lookDirZ;
//                 float perpZ = toCamX * lookDirY - toCamY * lookDirX;
//                 float perpDist = sqrtf(perpX * perpX + perpY * perpY + perpZ * perpZ);

//                 // Score based on perpendicular distance (lower is better)
//                 // Also factor in distance to prefer closer objects
//                 if (perpDist < selectRadius) {
//                     float score = perpDist + alongRay * 0.1f;  // Slight preference for closer
//                     if (score < bestScore) {
//                         bestScore = score;
//                         bestIndex = i;
//                     }
//                 }
//             }

//             debugHighlightedDecoIndex = bestIndex;

//             // Update delete cooldown
//             if (debugDeleteCooldown > 0.0f) {
//                 debugDeleteCooldown -= DELTA_TIME;
//             }

//             // D-pad right deletes highlighted decoration
//             if (pressed.d_right && debugHighlightedDecoIndex >= 0 && debugDeleteCooldown <= 0.0f) {
//                 DecoInstance* deco = &mapRuntime.decorations[debugHighlightedDecoIndex];
//                 debugf("Deleted decoration: type=%d at (%.1f, %.1f, %.1f)\n",
//                     deco->type, deco->posX, deco->posY, deco->posZ);
//                 map_remove_decoration(&mapRuntime, debugHighlightedDecoIndex);
//                 debugHighlightedDecoIndex = -1;
//                 debugDeleteCooldown = 0.3f;  // 300ms cooldown
//             }

//             // D-pad left toggles collision debug visualization
//             if (pressed.d_left) {
//                 debugShowCollision = !debugShowCollision;
//                 debugf("Collision debug: %s\n", debugShowCollision ? "ON" : "OFF");
//             }

//             // B enters placement mode
//             if (pressed.b) {
//                 debugPlacementMode = true;
//                 debugDecoX = debugCamX + sinYaw * 50.0f;
//                 debugDecoY = debugCamY - 20.0f;
//                 debugDecoZ = debugCamZ - cosYaw * 50.0f;
//                 debugDecoRotY = 0.0f;
//                 debugDecoScaleX = 1.0f;
//                 debugDecoScaleY = 1.0f;
//                 debugDecoScaleZ = 1.0f;
//             }
//         } else if (debugPlacementMode) {
//             // === PLACEMENT MODE ===
//             float moveSpeed = 2.0f;
//             float rotSpeed = 0.05f;
//             float scaleSpeed = 0.02f;

//             if (held.z) {
//                 // Z held: scale mode
//                 debugDecoScaleX += stickX * scaleSpeed;
//                 debugDecoScaleZ += stickY * scaleSpeed;
//                 if (held.c_up) debugDecoScaleY += scaleSpeed;
//                 if (held.c_down) debugDecoScaleY -= scaleSpeed;

//                 if (debugDecoScaleX < 0.1f) debugDecoScaleX = 0.1f;
//                 if (debugDecoScaleY < 0.1f) debugDecoScaleY = 0.1f;
//                 if (debugDecoScaleZ < 0.1f) debugDecoScaleZ = 0.1f;
//             } else {
//                 // Normal: move and rotate
//                 debugDecoX += sinYaw * stickY * moveSpeed;
//                 debugDecoZ -= cosYaw * stickY * moveSpeed;
//                 debugDecoX += cosYaw * stickX * moveSpeed;
//                 debugDecoZ += sinYaw * stickX * moveSpeed;

//                 if (held.c_up) debugDecoY += moveSpeed;
//                 if (held.c_down) debugDecoY -= moveSpeed;
//                 if (held.c_left) debugDecoRotY -= rotSpeed;
//                 if (held.c_right) debugDecoRotY += rotSpeed;
//             }

//             // A places the decoration
//             if (pressed.a) {
//                 if(debugDecoType == DECO_RAT){
//                     // Enter patrol placement mode for rats
//                     patrolPlacementMode = true;
//                     patrolDecoHolder = debugDecoType;
//                     debugDecoType = DECO_PATROLPOINT;

//                     // Initialize patrol points array with the rat's initial position
//                     patrolPointCount = 1;
//                     patrolPoints = malloc(sizeof(T3DVec3) * patrolPointCount);
//                     patrolPoints[0].v[0] = debugDecoX;
//                     patrolPoints[0].v[1] = debugDecoY;
//                     patrolPoints[0].v[2] = debugDecoZ;

//                     // Place visual marker for first patrol point
//                     map_add_decoration(&mapRuntime, DECO_PATROLPOINT,
//                         debugDecoX, debugDecoY, debugDecoZ, debugDecoRotY,
//                         debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ);

//                     debugf("Entering patrol placement mode\n");
//                     debugf("Press A to place patrol points, Z to undo, B to finish\n");
//                     return;
//                 }
//                 int idx = map_add_decoration(&mapRuntime, debugDecoType,
//                     debugDecoX, debugDecoY, debugDecoZ, debugDecoRotY,
//                     debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ);
//                 if (idx >= 0) {
//                     map_print_all_decorations(&mapRuntime);
//                 }
//             }

//             // B returns to camera mode
//             if (pressed.b) {
//                 debugPlacementMode = false;
//             }
//         } else if(patrolPlacementMode){ //=== PATROL POINT PLACEMENT MODE ===
//             // Move patrol point preview
//             float moveSpeed = 2.0f;
//             if (held.c_up) debugDecoY += moveSpeed;
//             if (held.c_down) debugDecoY -= moveSpeed;
//             // Note: Use stick for XZ movement (handled in debugFlyMode camera section)

//             // B finalizes and exits patrol placement mode
//             if (pressed.b) {
//                 debugf("Finalizing patrol route - %d patrol points\n", patrolPointCount);

//                 // Place decoration with patrol route using map_add_decoration_patrol
//                 int idx = map_add_decoration_patrol(&mapRuntime, patrolDecoHolder,
//                     patrolPoints[0].v[0], patrolPoints[0].v[1], patrolPoints[0].v[2],
//                     debugDecoRotY, debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ,
//                     patrolPoints, patrolPointCount);

//                 if (idx >= 0) {
//                     debugf("Placed %s with %d patrol points\n", DECO_TYPES[patrolDecoHolder].name, patrolPointCount);
//                     map_print_all_decorations(&mapRuntime);
//                 }

//                 // Clean up temporary patrol data
//                 free(patrolPoints);
//                 patrolPoints = NULL;
//                 patrolPointCount = 0;

//                 // Restore decoration type and exit patrol mode
//                 debugDecoType = patrolDecoHolder;
//                 patrolPlacementMode = false;
//                 debugPlacementMode = true;
//             }

//             // Z removes last patrol point (or cancels if removing first point)
//             if (pressed.z) {
//                 if (patrolPointCount > 1) {
//                     // Remove last patrol point
//                     patrolPointCount--;
//                     // Safe realloc: use temp variable to avoid memory leak on failure
//                     T3DVec3* temp = realloc(patrolPoints, sizeof(T3DVec3) * patrolPointCount);
//                     if (temp) {
//                         patrolPoints = temp;
//                     }
//                     // If realloc fails, keep old (larger) buffer - it's still valid

//                     // Remove last patrol point marker from map
//                     for (int i = mapRuntime.decoCount - 1; i >= 0; i--) {
//                         if (mapRuntime.decorations[i].type == DECO_PATROLPOINT && mapRuntime.decorations[i].active) {
//                             map_remove_decoration(&mapRuntime, i);
//                             debugf("Removed patrol point %d\n", patrolPointCount + 1);
//                             break;
//                         }
//                     }
//                 } else {
//                     debugf("Cancelling patrol placement (no points)\n");

//                     // Clean up patrol data
//                     free(patrolPoints);
//                     patrolPoints = NULL;
//                     patrolPointCount = 0;

//                     // Remove first patrol point marker from map
//                     for (int i = mapRuntime.decoCount - 1; i >= 0; i--) {
//                         if (mapRuntime.decorations[i].type == DECO_PATROLPOINT && mapRuntime.decorations[i].active) {
//                             map_remove_decoration(&mapRuntime, i);
//                             break;
//                         }
//                     }

//                     // Restore decoration type and exit patrol mode
//                     debugDecoType = patrolDecoHolder;
//                     patrolPlacementMode = false;
//                     debugPlacementMode = true;
//                 }
//             }

//             // A places a patrol point
//             if (pressed.a) {
//                 // Safe realloc: use temp variable to avoid memory leak on failure
//                 T3DVec3* temp = realloc(patrolPoints, sizeof(T3DVec3) * (patrolPointCount + 1));
//                 if (!temp) {
//                     debugf("ERROR: Failed to allocate patrol point\n");
//                 } else {
//                     patrolPoints = temp;
//                     patrolPoints[patrolPointCount].v[0] = debugDecoX;
//                     patrolPoints[patrolPointCount].v[1] = debugDecoY;
//                     patrolPoints[patrolPointCount].v[2] = debugDecoZ;
//                     patrolPointCount++;

//                     // Add visual marker for patrol point
//                     map_add_decoration(&mapRuntime, DECO_PATROLPOINT,
//                         debugDecoX, debugDecoY, debugDecoZ, 0.0f,
//                         0.1f, 0.1f, 0.1f);
//                     debugf("Placed patrol point %d at (%.1f, %.1f, %.1f)\n", patrolPointCount,
//                         debugDecoX, debugDecoY, debugDecoZ);
//                 }
//             }
//         }
//     } else {
//         // =================================================================
//         // SM64-STYLE QUARTER STEPS (prevents tunneling through walls)
//         // =================================================================
//         // Instead of moving full velocity then checking collision,
//         // we move in 4 substeps, checking walls after each one.
//         // This prevents high-speed charge jumps from going through walls.
//         // =================================================================
//         #define NUM_SUBSTEPS 4

//         // Reset wall hit flag (but preserve normal if timer is active!)
//         playerState.hitWall = false;

//         // Decrement wall hit timer - only reset normal when timer expires
//         if (playerState.wallHitTimer > 0) {
//             playerState.wallHitTimer--;
//             // Keep wallNormalX/Z for wall kick!
//         } else {
//             // Timer expired, clear the normal
//             playerState.wallNormalX = 0.0f;
//             playerState.wallNormalZ = 0.0f;
//         }

//         if (!isCharging && !isDead && !isHurt && !isRespawning && !isTransitioning) {
//             // Only do substep collision if map is loaded (check segment count)
//             bool mapReady = (mapLoader.count > 0);

//             if (mapReady) {
//                 float stepVelX = playerState.velX / NUM_SUBSTEPS;
//                 float stepVelZ = playerState.velZ / NUM_SUBSTEPS;

//                 for (int step = 0; step < NUM_SUBSTEPS; step++) {
//                     // Move one substep
//                     float nextX = cubeX + stepVelX;
//                     float nextZ = cubeZ + stepVelZ;

//                     // Check wall collision at new position
//                     float pushX = 0.0f, pushZ = 0.0f;
//                     bool hitWall = maploader_check_walls_ex(&mapLoader, nextX, cubeY, nextZ,
//                         playerRadius, playerHeight, &pushX, &pushZ, bestGroundY);

//                     // Also check decoration walls
//                     float decoPushX = 0.0f, decoPushZ = 0.0f;
//                     bool hitDecoWall = map_check_deco_walls(&mapRuntime, nextX, cubeY, nextZ,
//                         playerRadius, playerHeight, &decoPushX, &decoPushZ);

//                     if (hitWall || hitDecoWall) {
//                         // We hit a wall - apply push and record wall hit
//                         nextX += pushX + decoPushX;
//                         nextZ += pushZ + decoPushZ;

//                         // Calculate wall normal from push direction (normalized)
//                         float totalPushX = pushX + decoPushX;
//                         float totalPushZ = pushZ + decoPushZ;
//                         float pushLen = sqrtf(totalPushX * totalPushX + totalPushZ * totalPushZ);
//                         if (pushLen > 0.01f) {
//                             playerState.hitWall = true;
//                             playerState.wallNormalX = totalPushX / pushLen;
//                             playerState.wallNormalZ = totalPushZ / pushLen;
//                             playerState.wallHitTimer = 5;  // 5 frame window for wall kick

//                             // Kill velocity into wall (but keep tangent velocity)
//                             // dot = how much velocity goes into wall
//                             float dot = playerState.velX * playerState.wallNormalX +
//                                        playerState.velZ * playerState.wallNormalZ;
//                             if (dot < 0) {  // Only if moving into wall
//                                 playerState.velX -= dot * playerState.wallNormalX;
//                                 playerState.velZ -= dot * playerState.wallNormalZ;
//                                 // Update substep velocity for remaining steps
//                                 stepVelX = playerState.velX / NUM_SUBSTEPS;
//                                 stepVelZ = playerState.velZ / NUM_SUBSTEPS;
//                             }
//                         }
//                     }

//                     cubeX = nextX;
//                     cubeZ = nextZ;
//                 }
//             } else {
//                 // Map not ready - just move directly without collision
//                 cubeX += playerState.velX;
//                 cubeZ += playerState.velZ;
//             }
//         }

//         // Y movement (no substeps needed - walls are vertical)
//         if (!isDead && !isRespawning && !isTransitioning) {
//             cubeY += playerState.velY;
//         }
//     }

//     // =================================================================
//     // PLATFORM DISPLACEMENT (SM64-style)
//     // =================================================================
//     // Check ALL platforms in one call, apply displacement BEFORE collision
//     // This is the clean way - platforms provide velocity, physics runs after
//     // =================================================================
//     PlatformResult platformResult = platform_get_displacement(&mapRuntime, cubeX, cubeY, cubeZ);

//     if (platformResult.onPlatform && !isDead && !isRespawning && !isTransitioning) {
//         cubeX += platformResult.deltaX;
//         cubeY += platformResult.deltaY;
//         cubeZ += platformResult.deltaZ;
//     }

//     // Apply cog wall collision push (always, regardless of standing on it)
//     if (platformResult.hitWall && !isDead && !isRespawning && !isTransitioning) {
//         cubeX += platformResult.wallPushX;
//         cubeZ += platformResult.wallPushZ;
//     }

//     // Update map visibility based on player XZ position
//     float checkX = debugFlyMode ? debugCamX : cubeX;
//     float checkZ = debugFlyMode ? debugCamZ : cubeZ;
//     maploader_update_visibility(&mapLoader, checkX, checkZ);

//     // Collision (skip in fly mode) - wall collision now handled in substeps above
//     uint32_t perfCollisionStart = get_ticks();
//     uint32_t perfWallTime = 0, perfDecoWallTime = 0, perfGroundTime = 0, perfDecoGroundTime = 0;

//     if (!debugFlyMode) {
//         // Wall collision already handled in substeps above for normal movement
//         // This section only needed for sliding or other special movement

//         // =================================================================
//         // PLATFORM GROUND OVERRIDE
//         // =================================================================
//         // If player is on a platform that overrides ground physics (like cog),
//         // set grounded state and skip normal ground collision
//         // =================================================================
//         if (platformResult.overrideGroundPhysics) {
//             playerState.isGrounded = true;
//             playerState.velY = 0.0f;
//             playerState.isSliding = false;
//             playerState.isOnSlope = false;
//             playerState.groundedFrames++;
//         }

//         // =================================================================
//         // GROUND COLLISION
//         // =================================================================
//         // "Step up" logic like classic 3D platformers:
//         // 1. Search for ground from above (finds slopes above us)
//         // 2. If ground is within step-up range, snap to it
//         // 3. If ground is below, apply gravity
//         // =================================================================

//         uint32_t groundStart = get_ticks();

//         // Skip normal ground collision if platform handles it
//         if (platformResult.overrideGroundPhysics) goto skip_ground_collision;

//         // How high the player can "step up" onto surfaces (like walking up stairs)
//         const float MAX_STEP_UP = 15.0f;
//         // How far down to check for ground when airborne
//         (void)MAX_STEP_UP; // May be unused in some code paths

//         float groundNX = 0.0f, groundNY = 1.0f, groundNZ = 0.0f;

//         // Search for ground from well above the player
//         // This finds slopes/surfaces that are above current position
//         float searchFromY = cubeY + MAX_STEP_UP;
//         bestGroundY = maploader_get_ground_height_normal(&mapLoader, cubeX, searchFromY, cubeZ,
//             &groundNX, &groundNY, &groundNZ);

//         // Also check decoration ground height (search from same height)
//         float decoGroundY = map_get_deco_ground_height(&mapRuntime, cubeX, searchFromY, cubeZ);
//         if (decoGroundY > bestGroundY) {
//             bestGroundY = decoGroundY;
//             groundNX = 0.0f; groundNY = 1.0f; groundNZ = 0.0f;
//         }

//         perfGroundTime = get_ticks() - groundStart;
//         perfDecoGroundTime = 0;

//         // Store slope info for other systems (shadow, facing direction, etc)
//         // Normals are already in world space from collision system
//         playerState.slopeNormalX = groundNX;
//         playerState.slopeNormalY = groundNY;
//         playerState.slopeNormalZ = groundNZ;

//         // SM64-style slope classification (continuous physics, simple categories)
//         // SLOPE_FLAT: Can walk freely (normal.y >= 0.866, i.e. < 30°)
//         // SLOPE_STEEP: Too steep to walk up, forces sliding (normal.y < 0.866)
//         // SLOPE_WALL: Too steep to stand on at all (normal.y < 0.5)
//         SlopeType slopeType;
//         if (groundNY < SLOPE_WALL_THRESHOLD) {
//             slopeType = SLOPE_WALL;
//         } else if (groundNY < SLOPE_STEEP_THRESHOLD) {
//             slopeType = SLOPE_STEEP;
//         } else {
//             slopeType = SLOPE_FLAT;
//         }
//         playerState.slopeType = slopeType;

//         // Track if we were grounded last frame for peak height tracking
//         bool wasGroundedLastFrame = playerState.isGrounded;

//         // Reset grounded state each frame (will be set below if on ground)
//         // NOTE: isSliding is NOT reset here - it persists until friction stops us
//         // This allows sliding to continue across different slope types
//         playerState.isGrounded = false;

//         // Track peak height while airborne (for squash effect on landing)
//         if (wasGroundedLastFrame) {
//             // Just left the ground - reset peak to current position
//             jumpPeakY = cubeY;
//             // Start coyote timer (only if we walked off, not if we jumped)
//             if (!isJumping && !isCharging) {
//                 coyoteTimer = COYOTE_TIME;
//             }
//         } else {
//             // In the air - update peak Y if we're higher
//             if (cubeY > jumpPeakY) {
//                 jumpPeakY = cubeY;
//             }
//             // Count down coyote timer
//             if (coyoteTimer > 0.0f) {
//                 coyoteTimer -= deltaTime;
//             }
//         }

//         // Check if we found valid ground
//         bool hasGround = bestGroundY > INVALID_GROUND_Y + 10.0f;


//         if (hasGround) {
//             // Ground surface (where player's feet would be)
//             float groundSurface = bestGroundY + 2.0f;
//             // How far is ground from player?
//             float groundDist = cubeY - groundSurface;

//             // Can we step onto this surface?
//             // groundDist < 0 means ground is above us
//             // groundDist > 0 means ground is below us
//             bool canStepUp = groundDist >= -MAX_STEP_UP;  // Not too high above
//             bool isNearGround = groundDist <= 3.0f;        // Close enough to touch

//             // Calculate downhill direction from surface normal.
//             // NOTE: groundNX/NZ are already in world space (collision calculates normals
//             // from rotated vertices), so no additional rotation is needed.
//             // Downhill = negative XZ of the normal (points where gravity would pull)
//             float steepness = sqrtf(groundNX * groundNX + groundNZ * groundNZ);
//             float downhillX = 0.0f, downhillZ = 0.0f;
//             if (steepness > 0.001f) {
//                 // Downhill is the SAME direction as where the normal points in XZ
//                 // (because the normal points outward from the slope surface,
//                 // which is the direction gravity would push you)
//                 downhillX = groundNX / steepness;
//                 downhillZ = groundNZ / steepness;
//             }

//             // Can we stand on this surface at all?
//             if (canStepUp && isNearGround) {
//                 // Snap to ground
//                 cubeY = groundSurface;
//                 playerState.velY = 0.0f;
//                 playerState.isGrounded = true;
//                 playerState.groundedFrames++;
//                 coyoteTimer = 0.0f;  // Reset coyote timer on landing

//                 // Reset jumps after being grounded for 5 frames (failsafe)
//                 if (playerState.groundedFrames >= 5) {
//                     playerState.currentJumps = 0;
//                 }

//                 // =========================================================
//                 // SM64-STYLE SLOPE PHYSICS
//                 // Simple rules:
//                 // - On steep slopes (normal.y < 0.866): immediately slide
//                 // - Sliding continues until friction stops you on flat ground
//                 // - No complex timers or phases - just physics
//                 // =========================================================

//                 // Track slope state
//                 playerState.isOnSlope = (slopeType != SLOPE_FLAT);

//                 // Determine if we should be sliding
//                 bool shouldSlide = false;

//                 if (playerState.isSliding) {
//                     // Already sliding - continue until stopped by friction
//                     shouldSlide = true;
//                     playerState.steepSlopeTimer = 0.0f;
//                 } else if (slopeType == SLOPE_STEEP) {
//                     // Steep slope - SM64 style struggle-then-slide
//                     // Player can briefly try to walk up, but speed lerps to zero
//                     playerState.steepSlopeTimer += DELTA_TIME;

//                     // Struggle phase: lerp walking speed toward zero
//                     // Takes about 0.3 seconds to fully stop
//                     float struggleTime = 0.3f;
//                     float t = playerState.steepSlopeTimer / struggleTime;
//                     if (t > 1.0f) t = 1.0f;

//                     // Reduce walking speed based on time on slope
//                     float speedMult = 1.0f - t;  // 1.0 -> 0.0 over struggleTime
//                     playerState.velX *= speedMult;
//                     playerState.velZ *= speedMult;

//                     // After struggle time, start sliding
//                     if (playerState.steepSlopeTimer >= struggleTime) {
//                         shouldSlide = true;

//                         // Cancel any active charge jump
//                         if (isCharging) {
//                             isCharging = false;
//                             jumpChargeTime = 0.0f;
//                             landingSquash = 0.0f;
//                             chargeSquash = 0.0f;
//                             squashScale = 1.0f;
//                             squashVelocity = 0.0f;
//                         }

//                         // Initialize slide velocity in downhill direction
//                         float initSpeed = 15.0f;
//                         playerState.slideVelX = downhillX * initSpeed;
//                         playerState.slideVelZ = downhillZ * initSpeed;
//                     }
//                 } else {
//                     // Not on steep slope - reset timer
//                     playerState.steepSlopeTimer = 0.0f;
//                 }

//                 if (shouldSlide) {
//                     playerState.isSliding = true;

//                     // Prevent uphill sliding - if moving uphill on a slope, redirect downhill
//                     float slideSpeed = sqrtf(playerState.slideVelX * playerState.slideVelX +
//                                              playerState.slideVelZ * playerState.slideVelZ);
//                     float downhillDot = playerState.slideVelX * downhillX + playerState.slideVelZ * downhillZ;

//                     if (steepness > 0.05f && downhillDot < 0 && slideSpeed < 20.0f) {
//                         // Moving uphill with low speed - redirect downhill
//                         float minSpeed = 10.0f + steepness * 30.0f;
//                         playerState.slideVelX = downhillX * minSpeed;
//                         playerState.slideVelZ = downhillZ * minSpeed;
//                     }

//                     // Get player input for slide steering
//                     joypad_inputs_t joypad = joypad_get_inputs(JOYPAD_PORT_1);
//                     float inputX = apply_deadzone(joypad.stick_x / 128.0f);
//                     float inputZ = -apply_deadzone(joypad.stick_y / 128.0f);

//                     // SM64-style sliding physics
//                     bool stopped = sm64_update_sliding(&playerState, inputX, inputZ, downhillX, downhillZ, steepness);

//                     if (stopped) {
//                         // Friction brought us to a stop on flat ground
//                         playerState.isSliding = false;
//                         playerState.isOnSlope = false;
//                     } else {
//                         // Spawn dust particles while sliding fast
//                         slideSpeed = sqrtf(playerState.slideVelX * playerState.slideVelX +
//                                            playerState.slideVelZ * playerState.slideVelZ);
//                         if (slideSpeed > 5.0f && (rand() % 3) == 0) {
//                             float behindX = cubeX - (playerState.slideVelX / slideSpeed) * 8.0f;
//                             float behindZ = cubeZ - (playerState.slideVelZ / slideSpeed) * 8.0f;
//                             spawn_dust_particles(behindX, cubeY, behindZ, 1);
//                         }

//                         // Apply slide velocity
//                         float newX = cubeX + playerState.slideVelX * DELTA_TIME;
//                         float newZ = cubeZ + playerState.slideVelZ * DELTA_TIME;

//                         // Stick to ground at new position
//                         float newGroundNX, newGroundNY, newGroundNZ;
//                         float newGroundY = maploader_get_ground_height_normal(&mapLoader,
//                             newX, cubeY + 50.0f, newZ, &newGroundNX, &newGroundNY, &newGroundNZ);

//                         if (newGroundY > INVALID_GROUND_Y + 10.0f) {
//                             cubeX = newX;
//                             cubeZ = newZ;
//                             cubeY = newGroundY + 2.0f;
//                             playerState.velY = 0.0f;
//                         }
//                     }

//                     // Clear walking velocity while sliding
//                     playerState.velX = 0.0f;
//                     playerState.velZ = 0.0f;
//                 }
//             }
//             // Ground exists but too far to step - check if falling onto it
//             else if (groundDist > 0 && playerState.velY < 0) {
//                 // We're above ground and falling - will we hit it this frame?
//                 float nextY = cubeY + playerState.velY;
//                 if (nextY <= groundSurface) {
//                     // Capture falling velocity BEFORE zeroing it (for momentum transfer)
//                     float landingVelY = playerState.velY;  // Negative value (falling down)

//                     // Land on it
//                     cubeY = groundSurface;
//                     playerState.velY = 0.0f;
//                     playerState.isGrounded = true;
//                     playerState.groundedFrames++;

//                     // SM64-style: Transfer landing momentum into slide on steep slopes
//                     if (slopeType == SLOPE_STEEP) {
//                         float fallSpeed = -landingVelY;  // Make positive
//                         float slopeFactor = 1.0f - groundNY;

//                         // Transfer fall momentum to slide
//                         float momentumSpeed = fallSpeed * (0.5f + slopeFactor) * 2.0f;
//                         if (momentumSpeed < 15.0f) momentumSpeed = 15.0f;

//                         // Add horizontal momentum if moving downhill
//                         float horizDot = playerState.velX * downhillX + playerState.velZ * downhillZ;
//                         if (horizDot > 0) {
//                             momentumSpeed += horizDot * 0.5f;
//                         }

//                         playerState.slideVelX = downhillX * momentumSpeed;
//                         playerState.slideVelZ = downhillZ * momentumSpeed;
//                         playerState.isSliding = true;
//                         playerState.velX = 0.0f;
//                         playerState.velZ = 0.0f;
//                     }
//                 }
//             }
//             // Already sliding in air - maintain momentum
//             else if (playerState.isSliding) {
//                 cubeX += playerState.slideVelX * DELTA_TIME;
//                 cubeZ += playerState.slideVelZ * DELTA_TIME;
//             }
//         }

//         // Fallback ground (safety net)
//         if (!playerState.isGrounded && cubeY <= groundLevel + 2.0f) {
//             cubeY = groundLevel + 2.0f;
//             playerState.velY = 0.0f;
//             playerState.isGrounded = true;
//             // Reset slide on fallback floor
//             if (playerState.isSliding) {
//                 playerState.isSliding = false;
//             }
//             playerState.groundedFrames++;
//         }

//         // Reset grounded frame counter when airborne
//         if (!playerState.isGrounded) {
//             playerState.groundedFrames = 0;
//         }

//         skip_ground_collision:;  // Label for cog physics to skip ground handling
//     }
//     uint32_t perfCollisionTime = get_ticks() - perfCollisionStart;

//     // Camera
//     uint32_t cameraStart = get_ticks();
//     if (debugFlyMode) {
//         camPos.v[0] = debugCamX;
//         camPos.v[1] = debugCamY;
//         camPos.v[2] = debugCamZ;
//         float cosPitch = cosf(debugCamPitch);
//         camTarget.v[0] = debugCamX + sinf(debugCamYaw) * cosPitch * 100.0f;
//         camTarget.v[1] = debugCamY + sinf(debugCamPitch) * 100.0f;
//         camTarget.v[2] = debugCamZ - cosf(debugCamYaw) * cosPitch * 100.0f;
//     } else {
//         // Camera follows player with fixed offsets
//         // Y offset: camera above player, Z offset: camera behind player
//         const float CAM_Y_OFFSET = 49.0f;
//         const float CAM_Z_OFFSET = -120.0f;

//         float targetX = cubeX;
//         float targetY = cubeY + CAM_Y_OFFSET;

//         // Smooth camera follow - lag lets you see ahead
//         float smoothFactorX = 0.08f;  // Horizontal lag (lower = more lag)
//         float smoothFactorY = 0.06f;  // Vertical lag (slower = more lookahead when jumping)
//         smoothCamX += (targetX - smoothCamX) * smoothFactorX;
//         smoothCamY += (targetY - smoothCamY) * smoothFactorY;

//         camPos.v[0] = smoothCamX;
//         camPos.v[1] = smoothCamY;
//         camPos.v[2] = cubeZ + CAM_Z_OFFSET;  // Fixed Z offset from player

//         // When charging, look toward the arc direction (smoothly lerped)
//         float desiredTargetX = cubeX;
//         if (isCharging && jumpChargeTime >= hopThreshold) {
//             float arcOffsetX = jumpArcEndX - cubeX;
//             desiredTargetX = cubeX + arcOffsetX * 0.3f;  // Look 30% toward arc end
//         }
//         smoothCamTargetX += (desiredTargetX - smoothCamTargetX) * 0.15f;
//         camTarget.v[0] = smoothCamTargetX;
//         camTarget.v[1] = cubeY;
//         camTarget.v[2] = cubeZ;
//     }
//     g_cameraTicks += get_ticks() - cameraStart;

//     // Debug: L/R triggers to switch robot parts
//     joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
//     joypad_buttons_t pressed_test = joypad_get_buttons_pressed(JOYPAD_PORT_1);
//     if (partSwitchCooldown > 0) partSwitchCooldown--;
//     if (partSwitchCooldown == 0) {
//         RobotParts prevPart = currentPart;
//         if (held.l) {
//             currentPart = (currentPart + PART_COUNT - 1) % PART_COUNT;
//             partSwitchCooldown = 15;
//         }
//         if (held.r) {
//             currentPart = (currentPart + 1) % PART_COUNT;
//             partSwitchCooldown = 15;
//         }
//         // Reset state when switching parts
//         if (currentPart != prevPart) {
//             debugf("Switched to part: %d\n", currentPart);
//             // Reset torso state
//             isCharging = false;
//             isJumping = false;
//             isLanding = false;
//             jumpAnimPaused = false;
//             jumpChargeTime = 0.0f;
//             idleFrames = 0;
//             playingFidget = false;
//             // Reset arms state
//             armsIsSpinning = false;
//             armsIsWhipping = false;
//             armsSpinTime = 0.0f;
//             armsWhipTime = 0.0f;
//             // Reset player state
//             playerState.canMove = true;
//             playerState.canRotate = true;
//             playerState.canJump = true;
//             currentlyAttachedAnim = NULL;  // Force animation re-attach
//         }
//     }

//     // Test damage system: C-Down takes 1 damage (for testing only, not in debug mode)
//     if (pressed_test.c_down && currentPart == PART_TORSO && !debugFlyMode && !isDead && !isHurt && !isRespawning) {
//         player_take_damage(1);
//     }

//     // Disable normal jump for torso (charge jump only) and arms (handles its own jump)
//     disableNormalJump = (currentPart == PART_TORSO || currentPart == PART_ARMS);

//     // Air control: only torso lacks air control (arms and legs can steer in air)
//     playerState.hasAirControl = (currentPart != PART_TORSO);

//     // Update player skeleton/animation
//     uint32_t perfAnimStart = get_ticks();
//     bool isMoving = (fabsf(playerState.velX) > 0.1f || fabsf(playerState.velZ) > 0.1f);

//     if (currentPart == PART_TORSO && torsoHasAnims) {
//         joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
//         joypad_buttons_t released = joypad_get_buttons_released(JOYPAD_PORT_1);

//         // === DEATH STATE (highest priority) ===
//         if (isDead) {
//             // Play death animation and hold final frame
//             if (torsoAnimDeath.animRef != NULL) {
//                 if (!torsoAnimDeath.isPlaying) {
//                     // Animation finished - hold final frame
//                     float duration = torsoAnimDeath.animRef->duration;
//                     t3d_anim_set_time(&torsoAnimDeath, duration);
//                 } else {
//                     t3d_anim_update(&torsoAnimDeath, deltaTime);
//                 }
//             }
//         }
//         // === HURT STATE ===
//         else if (isHurt) {
//             hurtAnimTime += deltaTime;

//             // Check if pain animation finished
//             if (currentPainAnim != NULL && currentPainAnim->animRef != NULL) {
//                 if (!currentPainAnim->isPlaying || hurtAnimTime > currentPainAnim->animRef->duration) {
//                     isHurt = false;
//                     hurtAnimTime = 0.0f;
//                     currentPainAnim = NULL;
//                 } else {
//                     t3d_anim_update(currentPainAnim, deltaTime);
//                 }
//             } else {
//                 // No valid animation - end hurt state
//                 isHurt = false;
//                 hurtAnimTime = 0.0f;
//                 currentPainAnim = NULL;
//             }
//         }
//         // === NORMAL STATES ===
//         else {
//         // Determine current state
//         bool canIdle = playerState.isGrounded && !isMoving && !isCharging && !isJumping;

//         // === JUMP BUFFER ===
//         // Track A press in the air for jump buffering
//         if (pressed.a && !playerState.isGrounded && !isCharging) {
//             jumpBufferTimer = JUMP_BUFFER_TIME;
//         }
//         // Count down buffer timer
//         if (jumpBufferTimer > 0.0f) {
//             jumpBufferTimer -= deltaTime;
//         }

//         // === JUMP CHARGE ===
//         // Allow starting a charge immediately upon landing (even if still in jumping state)
//         // Player stops moving immediately when A is pressed
//         // Coyote time: can jump for a brief window after leaving ground
//         // Jump buffer: if A was pressed recently in air, trigger jump on land
//         bool canStartJump = playerState.isGrounded || coyoteTimer > 0.0f;
//         bool wantsJump = pressed.a || jumpBufferTimer > 0.0f;
//         float holdX, holdZ;
//         // Disable jump while sliding
//         // Allow canceling landing animation with jump (3)
//         // ALSO allow jump even if canJump is false during landing (to fix 1-frame delay)
//         if (wantsJump && canStartJump && !isCharging && (playerState.canJump || isLanding) && !playerState.isSliding) {
//             isCharging = true;
//             isBufferedCharge = false;  // This is a normal charge, not buffered
//             coyoteTimer = 0.0f;      // Consume coyote time
//             jumpBufferTimer = 0.0f;  // Consume buffer
//             // If we were considered 'jumping' from the previous air state, clear it to enter charging state
//             isJumping = false;
//             isLanding = false;  // Cancel landing animation (3)
//             isMoving = false; // STOP movement immediately when A pressed
//             jumpChargeTime = 0.0f;
//             // Stop player movement immediately
//             holdX = playerState.velX;
//             holdZ = playerState.velZ;
//             playerState.velX = 0.0f;
//             playerState.velZ = 0.0f;
//             playerState.canMove = false;
//             playerState.canRotate = true; // allow aiming while charging
//             playerState.canJump = true;
//             // TODO(2): jumpChargeTime += BONUS_CHARGE_RATE * deltaTime; // Faster charge if buffered from air
//             idleFrames = 0;
//             playingFidget = false;
//             fidgetPlayTime = 0.0f;
//             // Stay in idle animation initially - will switch to charge anim after hopThreshold
//             attach_anim_if_different(&torsoAnimIdle, &torsoSkel);
//             t3d_anim_set_time(&torsoAnimIdle, 0.0f);
//             t3d_anim_set_playing(&torsoAnimIdle, true);
//         }

//         if (isCharging) {
//             // Reset idle state during charge
//             idleFrames = 0;
//             playingFidget = false;
//             fidgetPlayTime = 0.0f;

//             // Read stick input for jump aiming (X inverted to match world coords, with deadzone)
//             joypad_inputs_t aimInputs = joypad_get_inputs(JOYPAD_PORT_1);
//             jumpAimX = -apply_deadzone(aimInputs.stick_x / 128.0f);
//             jumpAimY = apply_deadzone(aimInputs.stick_y / 128.0f);

//             // Calculate aim magnitude (0 = straight up, 1 = full directional)
//             float aimMag = sqrtf(jumpAimX * jumpAimX + jumpAimY * jumpAimY);
//             if (aimMag > 1.0f) aimMag = 1.0f;

//             // Update player facing to match stick direction (if stick is pushed)
//             if (aimMag > 0.3f) {
//                 playerState.playerAngle = atan2f(-jumpAimX, jumpAimY);
//             }

//             float prevChargeTime = jumpChargeTime;
//             float chargeRate = isBufferedCharge ? (deltaTime * BUFFERED_CHARGE_BONUS) : deltaTime;
//             jumpChargeTime += chargeRate;

//             // Squash down while charging (anticipation before stretch on release)
//             // Additive with landing squash - charge adds more squash on top
//             float chargeRatio = jumpChargeTime / maxChargeTime;
//             if (chargeRatio > 1.0f) chargeRatio = 1.0f;
            
//             // Charge squash is separate and additive
//             chargeSquash = chargeRatio * 0.25f;  // 0 to 0.25
            
//             // Total squash = 1.0 - (landingSquash + chargeSquash)
//             // Spring physics only affects landingSquash, chargeSquash is held constant
//             squashScale = 1.0f - landingSquash - chargeSquash;
//             squashVelocity = 0.0f;  // Hold the squash, don't spring back yet

//             // Transition to charge animation after hop threshold is passed
//             if (prevChargeTime < hopThreshold && jumpChargeTime >= hopThreshold) {
//                 attach_anim_if_different(&torsoAnimJumpCharge, &torsoSkel);
//                 t3d_anim_set_time(&torsoAnimJumpCharge, 0.0f);
//                 t3d_anim_set_playing(&torsoAnimJumpCharge, true);
//             }

//             // Update the appropriate animation
//             if (jumpChargeTime >= hopThreshold) {
//                 if (torsoAnimJumpCharge.animRef != NULL && torsoAnimJumpCharge.isPlaying) {
//                     t3d_anim_update(&torsoAnimJumpCharge, deltaTime);
//                 }
//             } else {
//                 if (torsoAnimIdle.animRef != NULL && torsoAnimIdle.isPlaying) {
//                     t3d_anim_update(&torsoAnimIdle, deltaTime);
//                 }
//             }

//             // Horizontal range scale (reduce sideways movement)
//             const float HORIZONTAL_SCALE = 0.4f;

//             // Clamp charge time at max - hold squish until player releases
//             if (jumpChargeTime > maxChargeTime) {
//                 jumpChargeTime = maxChargeTime;
//                 // Hold at max charge squash
//                 chargeSquash = 0.25f;  // Max charge squash
//                 squashScale = 1.0f - landingSquash - chargeSquash;
//                 squashVelocity = 0.0f;
//             }

//             // Manual release - check if it's a quick tap (hop) or longer hold (charge jump)
//             if (released.a) {
//                 isCharging = false;
//                 isJumping = true;
//                 isLanding = false;
//                 isMoving = false;
//                 playerState.canJump = false;
//                 playerState.canMove = false;
//                 playerState.canRotate = false;
//                 jumpAnimPaused = false;

//                 if (jumpChargeTime < hopThreshold) {
//                     // Quick tap = small hop - use stick direction, or facing direction if no stick
//                     playerState.velY = hopVelocityY;
//                     // Small stretch for hop (no landing squash component)
//                     landingSquash = 0.0f;  // Remove landing squash
//                     chargeSquash = 0.0f;   // No charge either
//                     squashScale = 1.1f;
//                     squashVelocity = 1.0f;
//                     if (aimMag > 0.1f) {
//                         playerState.velX += holdX * (jumpAimX / aimMag) * aimMag * HORIZONTAL_SCALE;
//                         playerState.velZ += holdZ * (jumpAimY / aimMag) * aimMag * HORIZONTAL_SCALE;
//                     } else {
//                         // No stick = hop forward in facing direction
//                         playerState.velX += -sinf(playerState.playerAngle) * holdX;
//                         playerState.velZ += cosf(playerState.playerAngle) * holdZ;
//                     }
//                 } else {
//                     // Longer hold = charge jump - use stick direction
//                     playerState.velY = chargeJumpEarlyBase + jumpChargeTime * chargeJumpEarlyMultiplier;
//                     float forward = (3.0f + 2.0f * jumpChargeTime) * FPS_SCALE * aimMag;
//                     if (aimMag > 0.1f) {
//                         playerState.velX = (jumpAimX / aimMag) * forward * HORIZONTAL_SCALE;
//                         playerState.velZ = (jumpAimY / aimMag) * forward * HORIZONTAL_SCALE;
//                     } else {
//                         playerState.velX = 0.0f;
//                         playerState.velZ = 0.0f;
//                     }
//                     // Stretch based on charge amount
//                     landingSquash = 0.0f;  // Remove landing squash component
//                     chargeSquash = 0.0f;   // Clear charge squash too
//                     float chargeRatio = jumpChargeTime / maxChargeTime;
//                     squashScale = 1.1f + chargeRatio * 0.15f;  // 1.1 to 1.25
//                     squashVelocity = 1.0f + chargeRatio * 1.0f;
//                 }
//                 attach_anim_if_different(&torsoAnimJumpLaunch, &torsoSkel);
//                 t3d_anim_set_time(&torsoAnimJumpLaunch, 0.0f);
//                 t3d_anim_set_playing(&torsoAnimJumpLaunch, true);
//             }

//             if (isCharging && pressed.b) {
//                 // Cancel jump charge
//                 isCharging = false;
//                 isMoving = false;
//                 jumpChargeTime = 0.0f;
//                 landingSquash = 0.0f;
//                 chargeSquash = 0.0f;
//                 squashScale = 1.0f;
//                 squashVelocity = 0.0f;
//                 jumpAimX = 0.0f;
//                 jumpAimY = 0.0f;
//                 attach_anim_if_different(&torsoAnimIdle, &torsoSkel);
//                 t3d_anim_set_time(&torsoAnimIdle, 0.0f);
//                 t3d_anim_set_playing(&torsoAnimIdle, true);
//                 playerState.canMove = true;
//                 playerState.canRotate = true;
//                 playerState.canJump = true;
//             }
//         }
//         // === JUMP RELEASE ===
//         else if (isJumping) {
//             // If we landed on a slope and started sliding, exit jump state immediately
//             // Let the sliding animation block handle it
//             if (playerState.isSliding && playerState.isGrounded) {
//                 isJumping = false;
//                 isLanding = false;
//                 jumpAnimPaused = false;
//                 playerState.currentJumps = 0;
//                 // canJump stays false - will be reset when slide ends
//                 // Fall through to sliding block on next frame
//             }

//             // Reset idle state during jump
//             idleFrames = 0;
//             playingFidget = false;
//             fidgetPlayTime = 0.0f;

//             // === LANDING PHASE ===
//             if (isLanding) {
//                 // Update landing animation
//                 if (torsoAnimJumpLand.animRef != NULL) {
//                     if (torsoAnimJumpLand.isPlaying) {
//                         playerState.canJump = true;  // Allow jump to cancel landing (3)
//                         t3d_anim_update(&torsoAnimJumpLand, deltaTime);
//                         // Slow down movement during landing (but don't cancel animation with stick)
//                         playerState.velX *= 0.8f;
//                         playerState.velZ *= 0.8f;
//                     }
//                     // Check if landing animation finished - hold last frame briefly then exit
//                     if (!torsoAnimJumpLand.isPlaying) {
//                         // Hold last frame - animation already stopped at end
//                         t3d_anim_set_time(&torsoAnimJumpLand, torsoAnimJumpLand.animRef->duration);
//                         // Jump fully finished
//                         isJumping = false;
//                         isLanding = false;
//                         isMoving = true;
//                         playerState.canMove = true;
//                         playerState.canRotate = true;
//                     }
//                 } else {
//                     // No land animation - just finish
//                     isJumping = false;
//                     isLanding = false;
//                     isMoving = true;
//                     playerState.canMove = true;
//                     playerState.canRotate = true;
//                 }
//             }
//             // === LAUNCH PHASE (ascending / mid-air) ===
//             else if (!jumpAnimPaused) {
//                 // === WALL KICK CHECK ===
//                 // SM64-style: if we hit a wall recently and press A, kick off it
//                 // IMPORTANT: Fixed velocity, not based on incoming speed!
//                 if (playerState.wallHitTimer > 0 && pressed.a && !playerState.isGrounded) {
//                     // Wall kick! Fixed, consistent jump - same every time
//                     #define WALL_KICK_VEL_Y 8.0f     // Fixed upward velocity
//                     #define WALL_KICK_VEL_XZ 4.0f    // Fixed horizontal push away from wall

//                     // Wall normal points AWAY from wall (it's the push direction)
//                     float awayX = playerState.wallNormalX;
//                     float awayZ = playerState.wallNormalZ;

//                     // Reflect player's FACING ANGLE across the wall normal
//                     // This preserves the player's intended direction and feels intuitive
//                     // It's also skill-based: players can aim wall kicks by turning mid-air!
                    
//                     // Convert player angle to direction vector
//                     float facingX = -sinf(playerState.playerAngle);
//                     float facingZ = cosf(playerState.playerAngle);
                    
//                     // Reflect facing direction across wall normal
//                     // Formula: reflected = incident - 2 * (incident · normal) * normal
//                     float dot = facingX * awayX + facingZ * awayZ;
//                     float reflectedX = facingX - 2.0f * dot * awayX;
//                     float reflectedZ = facingZ - 2.0f * dot * awayZ;
                    
//                     // Normalize and apply fixed speed
//                     float reflectedLen = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
//                     if (reflectedLen > 0.01f) {
//                         playerState.velX = (reflectedX / reflectedLen) * WALL_KICK_VEL_XZ;
//                         playerState.velZ = (reflectedZ / reflectedLen) * WALL_KICK_VEL_XZ;
//                         torsoWallJumpAngle = atan2f(-(reflectedX / reflectedLen), (reflectedZ / reflectedLen));
//                     } else {
//                         // Fallback: kick straight away from wall
//                         playerState.velX = awayX * WALL_KICK_VEL_XZ;
//                         playerState.velZ = awayZ * WALL_KICK_VEL_XZ;
//                         torsoWallJumpAngle = atan2f(-awayX, awayZ);
//                     }
                    
//                     playerState.velY = WALL_KICK_VEL_Y;
//                     playerState.playerAngle = torsoWallJumpAngle;
//                     torsoIsWallJumping = true;

//                     // Lock movement and rotation during wall jump arc
//                     playerState.canMove = false;
//                     playerState.canRotate = false;

//                     // Reset wall hit timer so we can't chain infinitely fast
//                     playerState.wallHitTimer = 0;

//                     // Reset jump peak for landing squash calculation
//                     jumpPeakY = cubeY;

//                     // Restart jump animation
//                     if (torsoAnimJumpLaunch.animRef != NULL) {
//                         t3d_anim_set_time(&torsoAnimJumpLaunch, 0.0f);
//                         t3d_anim_set_playing(&torsoAnimJumpLaunch, true);
//                     }

//                     // Spawn dust particles at wall contact point
//                     // Offset slightly toward the wall (opposite of away direction)
//                     spawn_dust_particles(cubeX - awayX * 5.0f, cubeY, cubeZ - awayZ * 5.0f, 3);

//                     debugf("Wall kick! Away: %.2f, %.2f\n", awayX, awayZ);
//                 }
//                 if (torsoAnimJumpLaunch.animRef != NULL) {
//                     t3d_anim_update(&torsoAnimJumpLaunch, deltaTime);
//                     // Guard against missing animation data before dereferencing animRef
//                     if (torsoAnimJumpLaunch.animRef->duration > 0.0f) {
//                         float animProgress = torsoAnimJumpLaunch.time / torsoAnimJumpLaunch.animRef->duration;
//                         if (animProgress > 0.5f && !playerState.isGrounded) {
//                             jumpAnimPaused = true;
//                             t3d_anim_set_playing(&torsoAnimJumpLaunch, false);
//                         }
//                     }
//                 } else {
//                     // No jumpLaunch animation available - fall back to a simple heuristic
//                     if (playerState.velY < 0.0f && !playerState.isGrounded) {
//                         jumpAnimPaused = true;
//                     }
//                 }

//                 // If we land while still in launch animation, transition to landing
//                 if (playerState.isGrounded && !playerState.isSliding) {
//                     playerState.velX *= 0.8f;
//                     playerState.velZ *= 0.8f;
//                     // Force transition to landing state
//                     jumpAnimPaused = true;
//                 }
//             }
//             // === WALL KICK CHECK (while falling/paused) ===
//             // Also check during the falling phase when animation is paused
//             if (jumpAnimPaused && !playerState.isGrounded && playerState.wallHitTimer > 0 && pressed.a) {
//                 // Wall kick! Fixed, consistent jump
//                 float awayX = playerState.wallNormalX;
//                 float awayZ = playerState.wallNormalZ;

//                 // Reflect player's facing angle across wall normal
//                 float facingX = -sinf(playerState.playerAngle);
//                 float facingZ = cosf(playerState.playerAngle);
//                 float dot = facingX * awayX + facingZ * awayZ;
//                 float reflectedX = facingX - 2.0f * dot * awayX;
//                 float reflectedZ = facingZ - 2.0f * dot * awayZ;
                
//                 float reflectedLen = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
//                 if (reflectedLen > 0.01f) {
//                     playerState.velX = (reflectedX / reflectedLen) * WALL_KICK_VEL_XZ;
//                     playerState.velZ = (reflectedZ / reflectedLen) * WALL_KICK_VEL_XZ;
//                     torsoWallJumpAngle = atan2f(-(reflectedX / reflectedLen), (reflectedZ / reflectedLen));
//                 } else {
//                     playerState.velX = awayX * WALL_KICK_VEL_XZ;
//                     playerState.velZ = awayZ * WALL_KICK_VEL_XZ;
//                     torsoWallJumpAngle = atan2f(-awayX, awayZ);
//                 }
                
//                 playerState.velY = WALL_KICK_VEL_Y;
//                 playerState.playerAngle = torsoWallJumpAngle;
//                 torsoIsWallJumping = true;

//                 // Lock movement and rotation during wall jump arc
//                 playerState.canMove = false;
//                 playerState.canRotate = false;

//                 playerState.wallHitTimer = 0;
//                 jumpPeakY = cubeY;
//                 jumpAnimPaused = false;  // Unpause to restart jump
//                 if (torsoAnimJumpLaunch.animRef != NULL) {
//                     t3d_anim_set_time(&torsoAnimJumpLaunch, 0.0f);
//                     t3d_anim_set_playing(&torsoAnimJumpLaunch, true);
//                 }

//                 // Spawn dust particles at wall contact point
//                 spawn_dust_particles(cubeX - awayX * 5.0f, cubeY, cubeZ - awayZ * 5.0f, 3);

//                 debugf("Wall kick (falling)! Away: %.2f, %.2f\n", awayX, awayZ);
//             }
//             // === TRANSITION TO LANDING ===
//             // Landed while in air (paused or just now) - start landing animation
//             if (jumpAnimPaused && playerState.isGrounded && !playerState.isSliding) {
//                 jumpAnimPaused = false;
//                 playerState.canJump = true; // can chain jumps
//                 torsoIsWallJumping = false; // Reset wall jump on landing
//                 playerState.canMove = true;   // Restore movement on landing
//                 playerState.canRotate = true; // Restore rotation on landing

//                 // Calculate fall distance for landing effects (always calculate, used for both paths)
//                 float fallDistance = jumpPeakY - cubeY;
//                 float squashAmount = (fallDistance - 30.0f) / 170.0f;
//                 if (squashAmount < 0.0f) squashAmount = 0.0f;
//                 if (squashAmount > 1.0f) squashAmount = 1.0f;

//                 // Check for buffered jump - if A is held/pressed during landing, immediately start charging
//                 joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
//                 bool wantsBufferedJump = (jumpBufferTimer > 0.0f || held.a || pressed.a);
                
//                 if (wantsBufferedJump) {
//                     // BUFFERED JUMP: Skip landing animation, immediate charge with visual feedback
//                     isLanding = false;
//                     isJumping = false;
//                     isCharging = true;
//                     isBufferedCharge = true;  // Mark this as a buffered charge for bonus rate
//                     jumpChargeTime = 0.01f; // Start just above 0 to show we're charging
//                     jumpBufferTimer = 0.0f;  // Consume buffer
//                     playerState.velX = 0.0f;
//                     playerState.velZ = 0.0f;
//                     playerState.canMove = false;
//                     playerState.canRotate = true;
                    
//                     // Apply landing squash component (will spring back)
//                     landingSquash = squashAmount * 0.75f;  // 0.0 to 0.75
//                     chargeSquash = 0.0f;  // Start with no charge squash
//                     squashScale = 1.0f - landingSquash - chargeSquash;
//                     squashVelocity = 0.0f;
                    
//                     // Spawn dust particles on landing (even for buffered jumps)
//                     if (fallDistance > 20.0f) {
//                         int dustCount = 2 + (int)(squashAmount * 2);
//                         spawn_dust_particles(cubeX, bestGroundY, cubeZ, dustCount);
//                     }
                    
//                     // Initialize jump aim with current stick input for immediate arc display
//                     joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
//                     jumpAimX = inputs.stick_x / 128.0f;
//                     jumpAimY = inputs.stick_y / 128.0f;
                    
//                     // Start with charge animation if we have enough charge time
//                     // (arc will appear once jumpChargeTime >= hopThreshold in draw code)
//                     if (torsoAnimJumpCharge.animRef != NULL) {
//                         attach_anim_if_different(&torsoAnimJumpCharge, &torsoSkel);
//                         t3d_anim_set_time(&torsoAnimJumpCharge, 0.0f);
//                         t3d_anim_set_playing(&torsoAnimJumpCharge, true);
//                     }
                    
//                     debugf("Buffered jump: immediate charge (fall dist: %.1f)\n", fallDistance);
//                 } else {
//                     // NORMAL LANDING: Play full landing animation with effects
//                     isLanding = true;
                    
//                     // Apply landing squash component
//                     landingSquash = squashAmount * 0.75f;
//                     chargeSquash = 0.0f;
//                     squashScale = 1.0f - landingSquash;
//                     squashVelocity = 0.0f;

//                     // Spawn dust particles on significant landings
//                     if (fallDistance > 40.0f) {
//                         int dustCount = 2 + (int)(squashAmount * 2);  // 2-4 dust puffs
//                         spawn_dust_particles(cubeX, bestGroundY, cubeZ, dustCount);
//                     }

//                     // Screen shake on hard landings (200+ height)
//                     if (fallDistance > 200.0f) {
//                         float shakeStrength = 6.0f + (fallDistance - 200.0f) / 50.0f;
//                         if (shakeStrength > 15.0f) shakeStrength = 15.0f;
//                         trigger_screen_shake(shakeStrength);
//                     }

//                     // Switch to landing animation
//                     if (torsoAnimJumpLand.animRef != NULL) {
//                         attach_anim_if_different(&torsoAnimJumpLand, &torsoSkel);
//                         t3d_anim_set_time(&torsoAnimJumpLand, 0.0f);
//                         t3d_anim_set_playing(&torsoAnimJumpLand, true);
//                     } else {
//                         // No land animation - just finish
//                         isJumping = false;
//                         isLanding = false;
//                         isMoving = true;
//                         playerState.canMove = true;
//                         playerState.canRotate = true;
//                     }
//                 }
//             }
//         }
//         // === FALLING THROUGH AIR (not grounded, not jumping state) ===
//         // Don't enter falling state if sliding - player is still "grounded" on slope
//         else if (!playerState.isGrounded && !isJumping && !playerState.isSliding) {
//             idleFrames = 0;
//             playingFidget = false;
//             fidgetPlayTime = 0.0f;
//             isMoving = false; // prevent playerAngle changes while falling
//             isJumping = true; // Enter jumping state when falling
//             isLanding = false;
//             jumpAnimPaused = true;
//             // If we have a jump land animation, use its start frame as the falling pose
//             if (torsoAnimJumpLand.animRef != NULL) {
//                 attach_anim_if_different(&torsoAnimJumpLand, &torsoSkel);
//                 t3d_anim_set_time(&torsoAnimJumpLand, 0.0f);
//                 t3d_anim_set_playing(&torsoAnimJumpLand, false);
//             } else if (torsoAnimJumpLaunch.animRef != NULL) {
//                 // Fallback: use end of launch animation
//                 attach_anim_if_different(&torsoAnimJumpLaunch, &torsoSkel);
//                 t3d_anim_set_time(&torsoAnimJumpLaunch, torsoAnimJumpLaunch.animRef->duration);
//                 t3d_anim_set_playing(&torsoAnimJumpLaunch, false);
//             }
//             // While falling, lock rotation and movement
//             playerState.canMove = false;
//             playerState.canRotate = false;
//         }
//         // === SLIDING ON SLOPE ===
//         else if (playerState.isSliding) {
//             // Player is sliding down a slope - play slide anim and turn to face downhill
//             idleFrames = 0;
//             playingFidget = false;
//             isJumping = false;
//             jumpAnimPaused = false;
//             isSlideRecovering = false;
//             playerState.canMove = true;
//             playerState.canRotate = false;  // We handle rotation manually

//             // Slope downhill direction is (slopeNormalX, slopeNormalZ) projected to XZ plane
//             float slopeDirX = playerState.slopeNormalX;
//             float slopeDirZ = playerState.slopeNormalZ;

//             // Calculate target angle to face downhill direction
//             // atan2(-x, z) gives angle in our coordinate system
//             float targetAngle = atan2f(-slopeDirX, slopeDirZ);

//             // Lerp player angle toward target (smooth turn)
//             float angleDiff = targetAngle - playerState.playerAngle;
//             // Normalize angle difference to [-PI, PI]
//             while (angleDiff > 3.14159265f) angleDiff -= 6.28318530f;
//             while (angleDiff < -3.14159265f) angleDiff += 6.28318530f;

//             // Lerp speed (radians per second)
//             const float SLIDE_TURN_SPEED = 8.0f;
//             float maxTurn = SLIDE_TURN_SPEED * deltaTime;
//             if (angleDiff > maxTurn) angleDiff = maxTurn;
//             else if (angleDiff < -maxTurn) angleDiff = -maxTurn;

//             playerState.playerAngle += angleDiff;
//             // Normalize final angle
//             while (playerState.playerAngle > 3.14159265f) playerState.playerAngle -= 6.28318530f;
//             while (playerState.playerAngle < -3.14159265f) playerState.playerAngle += 6.28318530f;

//             // Always use front slide animation since we're turning to face downhill
//             isSlidingFront = true;
//             if (torsoHasAnims && torsoAnimSlideFront.animRef != NULL) {
//                 // First frame of sliding - reset animation to start
//                 if (!wasSliding) {
//                     t3d_anim_set_time(&torsoAnimSlideFront, 0.0f);
//                     t3d_anim_set_playing(&torsoAnimSlideFront, true);
//                 }
//                 attach_anim_if_different(&torsoAnimSlideFront, &torsoSkel);

//                 // Play once and hold last frame
//                 float animDuration = torsoAnimSlideFront.animRef->duration;
//                 if (torsoAnimSlideFront.time < animDuration) {
//                     t3d_anim_update(&torsoAnimSlideFront, deltaTime);
//                 }
//                 // else: animation finished, hold last frame (don't update)
//             }
//             wasSliding = true;
//         }
//         // === SLIDE RECOVERY (just stopped sliding) ===
//         else if (wasSliding && !isSlideRecovering) {
//             // Start recovery animation
//             isSlideRecovering = true;
//             slideRecoverTime = 0.0f;
//             playerState.canMove = false;    // Lock movement during recovery
//             playerState.canRotate = false;  // Lock rotation during recovery
//             T3DAnim* recoverAnim = isSlidingFront ? &torsoAnimSlideFrontRecover : &torsoAnimSlideBackRecover;
//             if (torsoHasAnims && recoverAnim->animRef != NULL) {
//                 attach_anim_if_different(recoverAnim, &torsoSkel);
//                 t3d_anim_set_time(recoverAnim, 0.0f);
//                 t3d_anim_set_playing(recoverAnim, true);
//             } else {
//                 // No recovery anim, skip recovery state
//                 isSlideRecovering = false;
//                 playerState.canMove = true;
//                 playerState.canRotate = true;
//                 playerState.canJump = true;
//             }
//             wasSliding = false;
//         }
//         else if (isSlideRecovering) {
//             // Continue recovery animation - keep movement locked
//             playerState.canMove = false;
//             playerState.canRotate = false;
//             T3DAnim* recoverAnim = isSlidingFront ? &torsoAnimSlideFrontRecover : &torsoAnimSlideBackRecover;
//             if (torsoHasAnims && recoverAnim->animRef != NULL) {
//                 slideRecoverTime += deltaTime;
//                 t3d_anim_update(recoverAnim, deltaTime);

//                 // Check if recovery animation finished
//                 if (slideRecoverTime >= recoverAnim->animRef->duration) {
//                     isSlideRecovering = false;
//                     slideRecoverTime = 0.0f;
//                     playerState.canMove = true;
//                     playerState.canRotate = true;
//                     playerState.canJump = true;
//                 }
//             } else {
//                 // No recovery anim, just end immediately
//                 isSlideRecovering = false;
//                 playerState.canMove = true;
//                 playerState.canRotate = true;
//                 playerState.canJump = true;
//             }
//         }
//         // === WALKING ===
//         else if (isMoving) {
//             idleFrames = 0;
//             playingFidget = false;
//             fidgetPlayTime = 0.0f;
//             attach_anim_if_different(&torsoAnimWalk, &torsoSkel);
//             t3d_anim_update(&torsoAnimWalk, deltaTime);
//         }
//         // === IDLE STATE (grounded, not moving, not jumping) ===
//         else if (canIdle) {
//             float fidgetDuration = torsoAnimWait.animRef->duration;

//             // Currently playing fidget (wait animation)?
//             if (playingFidget) {
//                 fidgetPlayTime += deltaTime;
//                 if (fidgetPlayTime < fidgetDuration) {
//                     t3d_anim_update(&torsoAnimWait, deltaTime);
//                 } else {
//                     playingFidget = false;
//                     fidgetPlayTime = 0.0f;
//                     idleFrames = 0;
//                 }
//             }
//             // Time to start fidget?
//             else if (idleFrames >= IDLE_WAIT_FRAMES) {
//                 playingFidget = true;
//                 fidgetPlayTime = 0.0f;
//                 idleFrames = 0;
//                 attach_anim_if_different(&torsoAnimWait, &torsoSkel);
//                 t3d_anim_set_time(&torsoAnimWait, 0.0f);
//                 t3d_anim_set_playing(&torsoAnimWait, true);
//             }
//             // Default: idle animation (normal standing pose)
//             else {
//                 idleFrames++;
//                 attach_anim_if_different(&torsoAnimIdle, &torsoSkel);
//                 t3d_anim_update(&torsoAnimIdle, deltaTime);
//             }
//         }
//         // === IN AIR (not grounded, not jumping state) ===
//         else {
//             idleFrames = 0;
//             playingFidget = false;
//             fidgetPlayTime = 0.0f;
//             // Keep idle pose while in air
//             attach_anim_if_different(&torsoAnimIdle, &torsoSkel);
//             t3d_anim_update(&torsoAnimIdle, deltaTime);
//         }
//         }  // End of normal states

//         // Keep facing away from wall during wall jump
//         if (torsoIsWallJumping && !playerState.isGrounded) {
//             playerState.playerAngle = torsoWallJumpAngle;
//         }

//         t3d_skeleton_update(&torsoSkel);
//     }
//     // === ARMS MODE ===
//     else if (currentPart == PART_ARMS && armsHasAnims) {
//         joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
//         joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

//         // === JUMP BUFFER (Arms Mode) ===
//         // Track A press in the air for jump buffering (2)
//         if (pressed.a && !playerState.isGrounded && !isJumping) {
//             jumpBufferTimer = JUMP_BUFFER_TIME;
//         }
//         // Count down buffer timer
//         if (jumpBufferTimer > 0.0f) {
//             jumpBufferTimer -= deltaTime;
//         }

//         // === DEATH STATE (highest priority) ===
//         if (isDead) {
//             if (armsAnimDeath.animRef != NULL) {
//                 attach_anim_if_different(&armsAnimDeath, &torsoSkel);
//                 t3d_anim_update(&armsAnimDeath, deltaTime);
//             }
//         }
//         // === HURT STATE ===
//         else if (isHurt) {
//             hurtAnimTime += deltaTime;
//             if (currentPainAnim != NULL && currentPainAnim->animRef != NULL) {
//                 if (!currentPainAnim->isPlaying || hurtAnimTime > currentPainAnim->animRef->duration) {
//                     isHurt = false;
//                     hurtAnimTime = 0.0f;
//                     currentPainAnim = NULL;
//                 } else {
//                     t3d_anim_update(currentPainAnim, deltaTime);
//                 }
//             } else {
//                 isHurt = false;
//                 hurtAnimTime = 0.0f;
//                 currentPainAnim = NULL;
//             }
//         }
//         // === SPIN ATTACK (B button) ===
//         else if (armsIsSpinning) {
//             armsSpinTime += deltaTime;
//             t3d_anim_update(&armsAnimAtkSpin, deltaTime);

//             if (armsAnimAtkSpin.animRef) {
//                 float spinProgress = armsSpinTime / armsAnimAtkSpin.animRef->duration;

//                 // Allow movement during first 55% of spin
//                 if (spinProgress < 0.55f) {
//                     playerState.canMove = true;
//                     playerState.canRotate = true;
//                 } else {
//                     // Stop movement at the end (last 45%)
//                     playerState.canMove = false;
//                     playerState.canRotate = false;
//                     // Quickly decelerate
//                     playerState.velX *= 0.8f;
//                     playerState.velZ *= 0.8f;
//                 }

//                 // Check if spin animation finished
//                 if (armsSpinTime >= armsAnimAtkSpin.animRef->duration) {
//                     armsIsSpinning = false;
//                     armsSpinTime = 0.0f;
//                     playerState.canMove = true;
//                     playerState.canRotate = true;
//                     // Stop completely
//                     playerState.velX = 0.0f;
//                     playerState.velZ = 0.0f;
//                 }
//             }
//         }
//         // === GLIDING (double jump in air) ===
//         else if (armsIsGliding) {
//             armsSpinTime += deltaTime;
//             t3d_anim_update(&armsAnimAtkSpin, deltaTime);

//             // End glide when animation finishes or we land
//             if (playerState.isGrounded) {
//                 armsIsGliding = false;
//                 armsHasDoubleJumped = false;
//                 playerState.isGliding = false;  // Disable reduced gravity
//                 armsSpinTime = 0.0f;
//             } else if (armsAnimAtkSpin.animRef && armsSpinTime >= armsAnimAtkSpin.animRef->duration) {
//                 // Animation finished but still in air - stop gliding state but keep hasDoubleJumped
//                 armsIsGliding = false;
//                 playerState.isGliding = false;  // Disable reduced gravity
//                 armsSpinTime = 0.0f;
//             }
//         }
//         // === WHIP ATTACK (C-up) ===
//         else if (armsIsWhipping) {
//             armsWhipTime += deltaTime;
//             t3d_anim_update(&armsAnimAtkWhip, deltaTime);

//             // Check if whip animation finished
//             if (armsAnimAtkWhip.animRef && armsWhipTime >= armsAnimAtkWhip.animRef->duration) {
//                 armsIsWhipping = false;
//                 armsWhipTime = 0.0f;
//                 playerState.canMove = true;
//                 playerState.canRotate = true;
//             }
//         }
//         // === JUMPING/LANDING ===
//         else if (isJumping) {
//             // === WALL KICK CHECK (A button near wall) ===
//             if (playerState.wallHitTimer > 0 && pressed.a && !playerState.isGrounded) {
//                 // Wall kick! Fixed, consistent jump
//                 #define ARMS_WALL_KICK_VEL_Y 8.0f
//                 #define ARMS_WALL_KICK_VEL_XZ 4.0f

//                 float awayX = playerState.wallNormalX;
//                 float awayZ = playerState.wallNormalZ;

//                 // Reflect player's facing angle across wall normal
//                 float facingX = -sinf(playerState.playerAngle);
//                 float facingZ = cosf(playerState.playerAngle);
//                 float dot = facingX * awayX + facingZ * awayZ;
//                 float reflectedX = facingX - 2.0f * dot * awayX;
//                 float reflectedZ = facingZ - 2.0f * dot * awayZ;
                
//                 float reflectedLen = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
//                 if (reflectedLen > 0.01f) {
//                     playerState.velX = (reflectedX / reflectedLen) * ARMS_WALL_KICK_VEL_XZ;
//                     playerState.velZ = (reflectedZ / reflectedLen) * ARMS_WALL_KICK_VEL_XZ;
//                     armsWallJumpAngle = atan2f(-(reflectedX / reflectedLen), (reflectedZ / reflectedLen));
//                 } else {
//                     playerState.velX = awayX * ARMS_WALL_KICK_VEL_XZ;
//                     playerState.velZ = awayZ * ARMS_WALL_KICK_VEL_XZ;
//                     armsWallJumpAngle = atan2f(-awayX, awayZ);
//                 }
                
//                 playerState.velY = ARMS_WALL_KICK_VEL_Y;
//                 playerState.playerAngle = armsWallJumpAngle;
//                 armsIsWallJumping = true;

//                 // Lock movement and rotation during wall jump arc
//                 playerState.canMove = false;
//                 playerState.canRotate = false;

//                 playerState.wallHitTimer = 0;

//                 // Reset double jump so we can glide after wall kick
//                 armsHasDoubleJumped = false;
//                 armsIsGliding = false;
//                 playerState.isGliding = false;

//                 // Restart jump animation
//                 attach_anim_if_different(&armsAnimJump, &torsoSkel);
//                 t3d_anim_set_time(&armsAnimJump, 0.0f);
//                 t3d_anim_set_playing(&armsAnimJump, true);

//                 // Spawn dust particles at wall
//                 spawn_dust_particles(cubeX - awayX * 5.0f, cubeY, cubeZ - awayZ * 5.0f, 3);

//                 debugf("Arms wall kick! Away: %.2f, %.2f\n", awayX, awayZ);
//             }
//             // Check for double jump / glide (B button in air) - spin attack animation
//             // Can cancel wall jump into spin attack
//             else if (pressed.b && !armsHasDoubleJumped && !armsIsGliding) {
//                 armsHasDoubleJumped = true;
//                 armsIsGliding = true;
//                 playerState.isGliding = true;  // Enable reduced gravity
//                 armsSpinTime = 0.0f;
//                 // Cancel wall jump state - regain control
//                 armsIsWallJumping = false;
//                 playerState.canMove = true;
//                 playerState.canRotate = true;
//                 // Give a small upward boost and forward momentum
//                 playerState.velY = 2.0f;
//                 // Add forward momentum in facing direction
//                 float forwardBoost = 4.0f;
//                 playerState.velX += sinf(playerState.playerAngle) * forwardBoost;
//                 playerState.velZ += cosf(playerState.playerAngle) * forwardBoost;
//                 // Play spin animation
//                 attach_anim_if_different(&armsAnimAtkSpin, &torsoSkel);
//                 t3d_anim_set_time(&armsAnimAtkSpin, 0.0f);
//                 t3d_anim_set_playing(&armsAnimAtkSpin, true);
//             }
//             // Arms jump animation (when not gliding)
//             else if (!armsIsGliding && armsAnimJump.animRef) {
//                 t3d_anim_update(&armsAnimJump, deltaTime);
//             }
//         }
//         else if (isLanding) {
//             // Arms landing animation
//             // Allow canceling landing with jump (check A button during landing)
//             bool wantsJump = (pressed.a || jumpBufferTimer > 0.0f || held.a);
//             bool canJump = (playerState.canJump || isLanding); // Allow jump even during landing
            
//             if (wantsJump && canJump) {
//                 // Cancel landing and jump again!
//                 isLanding = false;
//                 isJumping = true;
//                 playerState.velY = 8.0f;
//                 playerState.isGrounded = false;
//                 playerState.canJump = false;
//                 armsHasDoubleJumped = false;
//                 jumpBufferTimer = 0.0f;
//                 coyoteTimer = 0.0f;
                
//                 // Spawn dust particles on jump-cancel
//                 spawn_dust_particles(cubeX, cubeY, cubeZ, 2);
                
//                 attach_anim_if_different(&armsAnimJump, &torsoSkel);
//                 t3d_anim_set_time(&armsAnimJump, 0.0f);
//                 t3d_anim_set_playing(&armsAnimJump, true);
//             } else if (armsAnimJumpLand.animRef) {
//                 // Continue playing landing animation
//                 playerState.canJump = true;  // Keep canJump true so we can cancel
//                 t3d_anim_update(&armsAnimJumpLand, deltaTime);
//                 if (!armsAnimJumpLand.isPlaying) {
//                     isLanding = false;
//                     playerState.canMove = true;
//                     playerState.canRotate = true;
//                     playerState.canJump = true;
//                 }
//             } else {
//                 isLanding = false;
//             }
//         }
//         // === NORMAL STATES ===
//         else {
//             // Start spin attack (B button)
//             if (pressed.b && playerState.isGrounded && !armsIsSpinning && !armsIsWhipping) {
//                 armsIsSpinning = true;
//                 armsSpinTime = 0.0f;
//                 playerState.canMove = true;   // Allow movement during spin
//                 playerState.canRotate = true; // Allow rotation during spin
//                 attach_anim_if_different(&armsAnimAtkSpin, &torsoSkel);
//                 t3d_anim_set_time(&armsAnimAtkSpin, 0.0f);
//                 t3d_anim_set_playing(&armsAnimAtkSpin, true);
//             }
//             // Start whip attack (C-up)
//             else if (pressed.c_up && playerState.isGrounded && !armsIsSpinning && !armsIsWhipping) {
//                 armsIsWhipping = true;
//                 armsWhipTime = 0.0f;
//                 playerState.canMove = false;
//                 playerState.canRotate = false;
//                 attach_anim_if_different(&armsAnimAtkWhip, &torsoSkel);
//                 t3d_anim_set_time(&armsAnimAtkWhip, 0.0f);
//                 t3d_anim_set_playing(&armsAnimAtkWhip, true);
//             }
//             // Jump (A button) - simple jump, not charge-based
//             // Support coyote time and jump buffer (1, 2)
//             else if ((pressed.a || jumpBufferTimer > 0.0f) && (playerState.isGrounded || coyoteTimer > 0.0f) && (playerState.canJump || isLanding)) {
//                 // Jump initiated
//                 isJumping = true;
//                 isLanding = false;  // Cancel landing animation if active (3)
//                 playerState.velY = 8.0f;  // Arms mode jump (lower than other modes)
//                 playerState.isGrounded = false;
//                 playerState.canJump = false;
//                 armsHasDoubleJumped = false;  // Reset double jump when starting new jump
//                 armsIsGliding = false;
//                 coyoteTimer = 0.0f;      // Consume coyote time (1)
//                 jumpBufferTimer = 0.0f;  // Consume buffer (2)
//                 attach_anim_if_different(&armsAnimJump, &torsoSkel);
//                 t3d_anim_set_time(&armsAnimJump, 0.0f);
//                 t3d_anim_set_playing(&armsAnimJump, true);
//             }
//             // Sliding on steep slope
//             else if (playerState.isSliding && playerState.isGrounded) {
//                 if (armsAnimSlide.animRef) {
//                     attach_anim_if_different(&armsAnimSlide, &torsoSkel);
//                     t3d_anim_update(&armsAnimSlide, deltaTime);
//                 }
//                 // Turn to face downhill direction
//                 float slopeDirX = playerState.slopeNormalX;
//                 float slopeDirZ = playerState.slopeNormalZ;
//                 float targetAngle = atan2f(-slopeDirX, slopeDirZ);
//                 float angleDiff = targetAngle - playerState.playerAngle;
//                 while (angleDiff > 3.14159265f) angleDiff -= 6.28318530f;
//                 while (angleDiff < -3.14159265f) angleDiff += 6.28318530f;
//                 const float SLIDE_TURN_SPEED = 8.0f;
//                 float maxTurn = SLIDE_TURN_SPEED * deltaTime;
//                 if (angleDiff > maxTurn) angleDiff = maxTurn;
//                 else if (angleDiff < -maxTurn) angleDiff = -maxTurn;
//                 playerState.playerAngle += angleDiff;
//             }
//             // Walking - 2 speeds based on stick magnitude
//             else if (isMoving) {
//                 // Calculate stick magnitude for walk speed
//                 float stickMag = sqrtf(playerState.velX * playerState.velX + playerState.velZ * playerState.velZ);
//                 float walkThreshold = 3.0f;  // Threshold for fast walk

//                 if (stickMag > walkThreshold && armsAnimWalk2.animRef) {
//                     // Fast walk
//                     attach_anim_if_different(&armsAnimWalk2, &torsoSkel);
//                     t3d_anim_update(&armsAnimWalk2, deltaTime);
//                 } else if (armsAnimWalk1.animRef) {
//                     // Slow walk
//                     attach_anim_if_different(&armsAnimWalk1, &torsoSkel);
//                     t3d_anim_update(&armsAnimWalk1, deltaTime);
//                 }
//             }
//             // Idle
//             else if (playerState.isGrounded) {
//                 attach_anim_if_different(&armsAnimIdle, &torsoSkel);
//                 t3d_anim_update(&armsAnimIdle, deltaTime);
//             }
//             // In air (not jumping state)
//             else {
//                 attach_anim_if_different(&armsAnimIdle, &torsoSkel);
//                 t3d_anim_update(&armsAnimIdle, deltaTime);
//             }
//         }

//         // Handle landing detection for arms mode
//         if (isJumping && playerState.isGrounded) {
//             // Check for buffered jump input (2) - if player pressed/held A during landing window
//             // Check BEFORE clearing isJumping to maintain state continuity
//             if (jumpBufferTimer > 0.0f || held.a || pressed.a) {
//                 // Immediately start another jump (cancel landing animation)
//                 // Keep isJumping true
//                 isLanding = false;
//                 playerState.velY = 8.0f;
//                 playerState.isGrounded = false;
//                 playerState.canJump = false;
//                 armsHasDoubleJumped = false;
//                 jumpBufferTimer = 0.0f;  // Consume buffer
//                 coyoteTimer = 0.0f;      // Reset coyote timer
                
//                 // Spawn dust particles on buffered jump
//                 spawn_dust_particles(cubeX, cubeY, cubeZ, 2);
                
//                 attach_anim_if_different(&armsAnimJump, &torsoSkel);
//                 t3d_anim_set_time(&armsAnimJump, 0.0f);
//                 t3d_anim_set_playing(&armsAnimJump, true);
//             } else {
//                 // Normal landing - enter landing state
//                 isJumping = false;
//                 isLanding = true;
//                 armsHasDoubleJumped = false;  // Reset double jump on landing
//                 armsIsGliding = false;
//                 armsIsWallJumping = false;    // Reset wall jump on landing
//                 playerState.isGliding = false;  // Disable reduced gravity
//                 playerState.canMove = true;   // Restore movement on landing
//                 playerState.canRotate = true; // Restore rotation on landing
                
//                 // Spawn dust particles on landing
//                 spawn_dust_particles(cubeX, cubeY, cubeZ, 2);
                
//                 if (armsAnimJumpLand.animRef) {
//                     attach_anim_if_different(&armsAnimJumpLand, &torsoSkel);
//                     t3d_anim_set_time(&armsAnimJumpLand, 0.0f);
//                     t3d_anim_set_playing(&armsAnimJumpLand, true);
//                 } else {
//                     isLanding = false;
//                     playerState.canJump = true;
//                 }
//             }
//         }

//         // Keep facing wall during wall jump
//         if (armsIsWallJumping && !playerState.isGrounded) {
//             playerState.playerAngle = armsWallJumpAngle;
//         }
//         // Also reset double jump if we land without being in isJumping state (e.g., walked off edge)
//         else if (playerState.isGrounded && armsHasDoubleJumped) {
//             armsHasDoubleJumped = false;
//             armsIsGliding = false;
//             playerState.isGliding = false;  // Disable reduced gravity
//         }

//         t3d_skeleton_update(&torsoSkel);
//     }
//     uint32_t perfAnimTime = get_ticks() - perfAnimStart;

//     // Update decorations (set player position for AI, then update)
//     uint32_t perfStart = get_ticks();
//     mapRuntime.playerX = cubeX;
//     mapRuntime.playerY = cubeY;
//     mapRuntime.playerZ = cubeZ;
//     mapRuntime.playerVelY = playerState.velY;  // For platform detection (SM64-style)

//     // Reset oil puddle state before decoration updates (decorations will set it true)
//     mapRuntime.playerOnOil = false;

//     map_update_decorations(&mapRuntime, deltaTime);

//     // === SPIN ATTACK HITBOX CHECK ===
//     // Decrement spin hit cooldown
//     if (spinHitCooldown > 0.0f) {
//         spinHitCooldown -= deltaTime;
//     }

//     // Check if player is spinning (ground or air) and damage nearby enemies
//     if ((armsIsSpinning || armsIsGliding) && currentPart == PART_ARMS && spinHitCooldown <= 0.0f) {
//         // Simple horizontal radius check - arms spin horizontally
//         // Must be larger than slime collision radius (~51 units) to hit them
//         float spinRadius = 55.0f;  // Horizontal reach

//         for (int i = 0; i < mapRuntime.decoCount; i++) {
//             DecoInstance* deco = &mapRuntime.decorations[i];
//             if (!deco->active) continue;

//             // Check slimes
//             if (deco->type == DECO_SLIME) {
//                 // Skip invincible slimes (just spawned from parent)
//                 if (deco->state.slime.invincibleTimer > 0.0f) continue;
//                 // Skip slimes already dying
//                 if (deco->state.slime.isDying) continue;

//                 float dx = deco->posX - cubeX;
//                 float dz = deco->posZ - cubeZ;

//                 // Slime center is at base (slimes are short and squat)
//                 float slimeCenterY = deco->posY + 2.0f * deco->scaleY;
//                 float playerCenterY = cubeY + 5.0f;  // Player center (arms are short)
//                 float dy = slimeCenterY - playerCenterY;

//                 // Horizontal distance check + vertical range scaled by slime size
//                 float distSq = dx * dx + dz * dz;
//                 float verticalRange = 10.0f + 5.0f * deco->scaleY;  // Flatter hitbox

//                 if (distSq < spinRadius * spinRadius && dy > -verticalRange && dy < verticalRange) {
//                     // Hit! Gentle knockback (no vertical pop)
//                     float knockDist = sqrtf(dx * dx + dz * dz);
//                     if (knockDist > 0.1f) {
//                         float knockX = dx / knockDist;
//                         float knockZ = dz / knockDist;
//                         deco->state.slime.velX = knockX * 1.5f;
//                         deco->state.slime.velZ = knockZ * 1.5f;
//                         deco->state.slime.velY = 0.0f;  // No vertical pop
//                     }

//                     // Violent jiggle
//                     deco->state.slime.jiggleVelX += ((rand() % 400) - 200) / 25.0f;
//                     deco->state.slime.jiggleVelZ += ((rand() % 400) - 200) / 25.0f;
//                     deco->state.slime.stretchY = 0.5f;  // Big squash
//                     deco->state.slime.stretchVelY = -4.0f;

//                     // Damage slime
//                     deco->state.slime.health--;

//                     // Spawn oil particles
//                     spawn_oil_particles(deco->posX, deco->posY, deco->posZ, 15);

//                     // Check if dead - start death animation
//                     if (deco->state.slime.health <= 0) {
//                         deco->state.slime.isDying = true;
//                         deco->state.slime.deathTimer = 0.0f;
//                         if (deco->scaleX >= 0.7f) {  // SLIME_SCALE_MEDIUM or larger
//                             deco->state.slime.pendingSplit = true;
//                         }
//                     }

//                     // Set cooldown so we don't hit again immediately
//                     spinHitCooldown = 0.3f;

//                     debugf("Spin hit slime %d! HP: %d\n", deco->id, deco->state.slime.health);
//                 }
//             }

//             // Check rats
//             if (deco->type == DECO_RAT) {
//                 float dx = deco->posX - cubeX;
//                 float dz = deco->posZ - cubeZ;
//                 float dy = deco->posY - cubeY;

//                 // Horizontal distance check + generous vertical range
//                 float distSq = dx * dx + dz * dz;
//                 float verticalRange = 30.0f;

//                 if (distSq < spinRadius * spinRadius && dy > -verticalRange && dy < verticalRange) {
//                     // Kill rat instantly
//                     deco->active = false;
//                     spawn_oil_particles(deco->posX, deco->posY, deco->posZ, 8);
//                     spinHitCooldown = 0.3f;
//                     debugf("Spin killed rat %d!\n", deco->id);
//                 }
//             }
//         }
//     }

//     // Copy oil puddle state from map runtime to player state
//     playerState.onOilPuddle = mapRuntime.playerOnOil;

//     // Apply conveyor belt push to player
//     if (playerState.isGrounded && !debugFlyMode) {
//         for (int i = 0; i < mapRuntime.decoCount; i++) {
//             DecoInstance* deco = &mapRuntime.decorations[i];
//             if (deco->active && deco->type == DECO_CONVEYERLARGE &&
//                 deco->state.conveyor.playerOnBelt) {
//                 // Push player along conveyor direction
//                 float pushSpeed = deco->state.conveyor.speed * deltaTime;
//                 cubeX += deco->state.conveyor.pushX * pushSpeed;
//                 cubeZ += deco->state.conveyor.pushZ * pushSpeed;
//                 break;  // Only apply one conveyor at a time
//             }
//         }
//     }

//     // Apply fan upward blow (SM64-style vertical wind)
//     // Player can still move horizontally while being blown up
//     if (!debugFlyMode) {
//         for (int i = 0; i < mapRuntime.decoCount; i++) {
//             DecoInstance* deco = &mapRuntime.decorations[i];
//             if (deco->active && deco->type == DECO_FAN &&
//                 deco->state.fan.playerInStream) {
//                 // SM64-style: max velocity decreases with height, acceleration is 1/8 of max
//                 float fanTopY = deco->posY + 10.0f * deco->scaleY;
//                 float heightAboveFan = cubeY - fanTopY;
//                 if (heightAboveFan < 0.0f) heightAboveFan = 0.0f;

//                 // Max upward velocity decreases as player rises (SM64 formula adapted)
//                 float maxVelY = (FAN_BLOW_FORCE * deco->scaleY) / (heightAboveFan + 50.0f) * 50.0f;
//                 if (maxVelY > FAN_BLOW_FORCE) maxVelY = FAN_BLOW_FORCE;

//                 // Accelerate toward max velocity (1/8 per frame like SM64)
//                 if (playerState.velY < maxVelY) {
//                     playerState.velY += maxVelY * 0.125f;
//                     if (playerState.velY > maxVelY) playerState.velY = maxVelY;
//                 }

//                 // Player is airborne when in fan stream
//                 playerState.isGrounded = false;
//                 isJumping = true;  // Use jump animation
//                 break;  // Only apply one fan at a time
//             }
//         }
//     }

//     uint32_t perfDecoUpdate = get_ticks() - perfStart;

//     // Update particles (simple physics, no collision)
//     update_particles(deltaTime);

//     // Update death decals (fade out over time)
//     update_death_decals(deltaTime);

//     // Update screen shake
//     update_screen_shake(deltaTime);

//     // Update invincibility timer and flash counter
//     if (invincibilityTimer > 0.0f) {
//         invincibilityTimer -= deltaTime;
//         invincibilityFlashFrame++;
//         if (invincibilityTimer <= 0.0f) {
//             invincibilityTimer = 0.0f;
//             invincibilityFlashFrame = 0;
//         }
//     }

//     // Check decoration collisions with player (skip if dead or in debug mode)
//     perfStart = get_ticks();
//     if (!isDead && !debugFlyMode) {
//         map_check_deco_collisions(&mapRuntime, cubeX, cubeY, cubeZ, playerRadius);
//     }
//     uint32_t perfDecoCollision = get_ticks() - perfStart;

//     static int perfFrameCount = 0;
//     static uint32_t perfAnimTotal = 0;
//     static uint32_t perfDecoUpdateTotal = 0;
//     static uint32_t perfDecoCollisionTotal = 0;
//     static uint32_t perfCollisionTotal = 0;
//     static uint32_t perfWallTotal = 0;
//     static uint32_t perfDecoWallTotal = 0;
//     static uint32_t perfGroundTotal = 0;
//     static uint32_t perfDecoGroundTotal = 0;
//     static uint32_t perfFrameStart = 0;

//     if (perfFrameCount == 0) {
//         perfFrameStart = get_ticks();
//     }

//     perfAnimTotal += perfAnimTime;
//     perfDecoUpdateTotal += perfDecoUpdate;
//     perfDecoCollisionTotal += perfDecoCollision;
//     perfCollisionTotal += perfCollisionTime;
//     perfWallTotal += perfWallTime;
//     perfDecoWallTotal += perfDecoWallTime;
//     perfGroundTotal += perfGroundTime;
//     perfDecoGroundTotal += perfDecoGroundTime;
//     perfFrameCount++;

//     if (perfFrameCount >= 60) {
//         uint32_t frameTime = get_ticks() - perfFrameStart;
//         float avgFPS = 60.0f / (frameTime / (float)TICKS_PER_SECOND);
//         // Convert ticks to microseconds: ticks * 1000000 / TICKS_PER_SECOND
//         // TICKS_PER_SECOND = 46875000, so approx 21.3ns per tick, or ~47 ticks per us
//         #define TICKS_TO_US_AVG(t) ((int)((t) * 1000000ULL / TICKS_PER_SECOND / 60))
//         // Calculate budget: 33333us per frame at 30fps
//         int cpuLogicUs = TICKS_TO_US_AVG(perfAnimTotal + perfCollisionTotal + perfDecoUpdateTotal + perfDecoCollisionTotal);
//         int frameUs = (int)(frameTime * 1000000ULL / TICKS_PER_SECOND / 60);
//         g_lastFrameUs = frameUs;  // Store for HUD display
//         int budgetLeft = 33333 - frameUs;
//         debugf("=== PERF (60f avg) FPS=%.1f Frame=%dus CPU=%dus Budget=%dus ===\n",
//             avgFPS, frameUs, cpuLogicUs, budgetLeft);
//         debugf("  Player: Anim=%dus Collision=%dus (Wall=%d Gnd=%d DecoWall=%d DecoGnd=%d)\n",
//             TICKS_TO_US_AVG(perfAnimTotal),
//             TICKS_TO_US_AVG(perfCollisionTotal),
//             TICKS_TO_US_AVG(perfWallTotal),
//             TICKS_TO_US_AVG(perfGroundTotal),
//             TICKS_TO_US_AVG(perfDecoWallTotal),
//             TICKS_TO_US_AVG(perfDecoGroundTotal));
//         debugf("  Decos: Update=%dus Collision=%dus\n",
//             TICKS_TO_US_AVG(perfDecoUpdateTotal),
//             TICKS_TO_US_AVG(perfDecoCollisionTotal));
//         // Show slime-specific stats
//         if (g_slimeUpdateCount > 0) {
//             debugf("  Slime (%d): Ground=%dus DecoGnd=%dus Wall=%dus Math=%dus Spring=%dus\n",
//                 g_slimeUpdateCount / 60,
//                 TICKS_TO_US_AVG(g_slimeGroundTicks),
//                 TICKS_TO_US_AVG(g_slimeDecoGroundTicks),
//                 TICKS_TO_US_AVG(g_slimeWallTicks),
//                 TICKS_TO_US_AVG(g_slimeMathTicks),
//                 TICKS_TO_US_AVG(g_slimeSpringTicks));
//         }
//         // Render breakdown
//         debugf("  Render: Total=%dus Map=%dus Deco=%dus Player=%dus Shadow=%dus HUD=%dus\n",
//             TICKS_TO_US_AVG(g_renderTotalTicks),
//             TICKS_TO_US_AVG(g_renderMapTicks),
//             TICKS_TO_US_AVG(g_renderDecoTicks),
//             TICKS_TO_US_AVG(g_renderPlayerTicks),
//             TICKS_TO_US_AVG(g_renderShadowTicks),
//             TICKS_TO_US_AVG(g_renderHUDTicks));
//         // Other costs
//         debugf("  Other: Audio=%dus Input=%dus Camera=%dus\n",
//             TICKS_TO_US_AVG(g_audioTicks),
//             TICKS_TO_US_AVG(g_inputTicks),
//             TICKS_TO_US_AVG(g_cameraTicks));

//         // Reset all counters
//         g_slimeGroundTicks = 0;
//         g_slimeDecoGroundTicks = 0;
//         g_slimeWallTicks = 0;
//         g_slimeMathTicks = 0;
//         g_slimeSpringTicks = 0;
//         g_slimeUpdateCount = 0;
//         g_renderMapTicks = 0;
//         g_renderDecoTicks = 0;
//         g_renderPlayerTicks = 0;
//         g_renderShadowTicks = 0;
//         g_renderHUDTicks = 0;
//         g_renderTotalTicks = 0;
//         g_audioTicks = 0;
//         g_inputTicks = 0;
//         g_cameraTicks = 0;
//         #undef TICKS_TO_US_AVG
//         perfFrameCount = 0;
//         perfAnimTotal = 0;
//         perfDecoUpdateTotal = 0;
//         perfDecoCollisionTotal = 0;
//         perfCollisionTotal = 0;
//         perfWallTotal = 0;
//         perfDecoWallTotal = 0;
//         perfGroundTotal = 0;
//         perfDecoGroundTotal = 0;
//     }

//     // Death transition with iris effect
//     if (isDead && !isRespawning) {
//         deathTimer += deltaTime;

//         // Get player's screen position for iris targeting
//         T3DVec3 playerWorld = {{cubeX, cubeY + 10.0f, cubeZ}};  // Slightly above feet
//         T3DVec3 playerScreen;
//         t3d_viewport_calc_viewspace_pos(&viewport, &playerScreen, &playerWorld);

//         // Update iris target (player's screen position)
//         if (playerScreen.v[2] > 0) {  // If player is in front of camera
//             irisTargetX = playerScreen.v[0];
//             irisTargetY = playerScreen.v[1];
//         }

//         // Start iris effect after death animation
//         if (deathTimer > DEATH_ANIMATION_TIME) {
//             if (!irisActive) {
//                 // Initialize iris
//                 irisActive = true;
//                 irisRadius = 400.0f;  // Start larger than screen
//                 irisCenterX = 160.0f;  // Start at screen center
//                 irisCenterY = 120.0f;
//                 irisPaused = false;
//                 irisPauseTimer = 0.0f;
//             }

//             // Lerp iris center toward player
//             float centerLerp = 0.15f;
//             irisCenterX += (irisTargetX - irisCenterX) * centerLerp;
//             irisCenterY += (irisTargetY - irisCenterY) * centerLerp;

//             // Shrink iris with dramatic pause when small
//             if (irisPaused) {
//                 // Hold at small circle for dramatic effect
//                 irisPauseTimer += deltaTime;
//                 if (irisPauseTimer >= IRIS_PAUSE_DURATION) {
//                     // Pause over, continue closing
//                     irisPaused = false;
//                 }
//             } else if (irisRadius > IRIS_PAUSE_RADIUS && !irisPaused && irisPauseTimer == 0.0f) {
//                 // Initial shrinking phase - exponential decay until we hit pause radius
//                 float shrinkSpeed = irisRadius * 0.06f;
//                 if (shrinkSpeed < 3.0f) shrinkSpeed = 3.0f;
//                 irisRadius -= shrinkSpeed;

//                 // Snap to pause radius when we reach it
//                 if (irisRadius <= IRIS_PAUSE_RADIUS) {
//                     irisRadius = IRIS_PAUSE_RADIUS;
//                     irisPaused = true;
//                     irisPauseTimer = 0.0f;
//                 }
//             } else if (irisRadius > 0.0f) {
//                 // Final closing phase - after pause, close the rest of the way
//                 float shrinkSpeed = irisRadius * 0.12f;  // Faster closing
//                 if (shrinkSpeed < 2.0f) shrinkSpeed = 2.0f;
//                 irisRadius -= shrinkSpeed;
//                 if (irisRadius < 0.0f) irisRadius = 0.0f;
//             }

//             // Set fadeAlpha based on iris (for respawn trigger)
//             fadeAlpha = (irisRadius <= 0.0f) ? 1.0f : 0.0f;

//             // When iris fully closed, respawn
//             if (irisRadius <= 0.0f && !isRespawning) {
//                 isRespawning = true;

//                 // Reset player health and state
//                 playerHealth = maxPlayerHealth;
//                 isDead = false;
//                 isHurt = false;
//                 hurtAnimTime = 0.0f;
//                 currentPainAnim = NULL;
//                 invincibilityTimer = 0.0f;
//                 invincibilityFlashFrame = 0;
//                 deathTimer = 0.0f;

//                 // Reset player position to level start
//                 const LevelData* level = ALL_LEVELS[currentLevel];
//                 cubeX = level->playerStartX;
//                 cubeY = level->playerStartY;
//                 cubeZ = level->playerStartZ;
//                 playerState.velX = 0.0f;
//                 playerState.velY = 0.0f;
//                 playerState.velZ = 0.0f;
//                 playerState.isGrounded = false;

//                 // Reset animation states
//                 isCharging = false;
//                 isJumping = false;
//                 isLanding = false;
//                 jumpAnimPaused = false;
//                 jumpChargeTime = 0.0f;
//                 idleFrames = 0;
//                 playingFidget = false;
//                 fidgetPlayTime = 0.0f;

//                 // Reset to idle animation immediately
//                 if (currentPart == PART_TORSO && torsoHasAnims && torsoAnimIdle.animRef != NULL) {
//                     t3d_anim_attach(&torsoAnimIdle, &torsoSkel);
//                     t3d_anim_set_time(&torsoAnimIdle, 0.0f);
//                     t3d_anim_set_playing(&torsoAnimIdle, true);
//                     t3d_anim_update(&torsoAnimIdle, 0.0f);  // Force update to apply pose
//                     t3d_skeleton_update(&torsoSkel);  // Update skeleton to show idle pose
//                     debugf("Reset to idle animation on respawn\n");
//                 }

//                 // Reload level (decorations, etc.)
//                 map_runtime_free(&mapRuntime);
//                 map_runtime_init(&mapRuntime, FB_COUNT, VISIBILITY_RANGE);
//                 currentlyAttachedAnim = NULL;  // Reset animation tracking
//                 level_load(currentLevel, &mapLoader, &mapRuntime);

//                 // Set body part for this level
//                 currentPart = (RobotParts)level_get_body_part(currentLevel);

//                 // Reset all TransitionCollision triggered flags (allow re-triggering after respawn)
//                 for (int i = 0; i < mapRuntime.decoCount; i++) {
//                     DecoInstance* deco = &mapRuntime.decorations[i];
//                     if (deco->type == DECO_TRANSITIONCOLLISION && deco->active) {
//                         deco->state.transition.triggered = false;
//                     }
//                 }

//                 // Find first PlayerSpawn decoration and use it as respawn point
//                 for (int i = 0; i < mapRuntime.decoCount; i++) {
//                     DecoInstance* deco = &mapRuntime.decorations[i];
//                     if (deco->type == DECO_PLAYERSPAWN && deco->active) {
//                         cubeX = deco->posX;
//                         cubeY = deco->posY;
//                         cubeZ = deco->posZ;
//                         break;
//                     }
//                 }
//                 // If no PlayerSpawn found, position was already set from level data

//                 // Keep screen black, start delay timer
//                 fadeAlpha = 1.0f;
//                 respawnDelayTimer = 0.0f;
//                 debugf("Player respawned!\n");
//             }
//         }
//     }

//     // Fade back in after respawn delay (iris opens back up)
//     if (isRespawning) {
//         respawnDelayTimer += deltaTime;

//         // Wait while screen is black, then open iris back up
//         if (respawnDelayTimer > RESPAWN_HOLD_TIME) {
//             float fadeInProgress = (respawnDelayTimer - RESPAWN_HOLD_TIME) / RESPAWN_FADE_IN_TIME;

//             // Open iris back up (reverse of closing)
//             irisRadius = fadeInProgress * 400.0f;
//             if (irisRadius > 400.0f) irisRadius = 400.0f;

//             // Center iris on player for respawn
//             T3DVec3 playerWorld = {{cubeX, cubeY + 10.0f, cubeZ}};
//             T3DVec3 playerScreen;
//             t3d_viewport_calc_viewspace_pos(&viewport, &playerScreen, &playerWorld);
//             if (playerScreen.v[2] > 0) {
//                 irisCenterX = playerScreen.v[0];
//                 irisCenterY = playerScreen.v[1];
//             }

//             fadeAlpha = 1.0f - fadeInProgress;
//             if (fadeAlpha <= 0.0f) {
//                 fadeAlpha = 0.0f;
//                 isRespawning = false;
//                 respawnDelayTimer = 0.0f;
//                 irisActive = false;  // Done with iris effect
//                 irisRadius = 400.0f;  // Reset for next time
//             }
//         }
//     }

//     // Level transition
//     if (isTransitioning) {
//         transitionTimer += deltaTime;

//         // Phase 1: Fade out
//         if (transitionTimer <= TRANSITION_FADE_OUT_TIME) {
//             fadeAlpha = transitionTimer / TRANSITION_FADE_OUT_TIME;
//             if (fadeAlpha > 1.0f) fadeAlpha = 1.0f;
//         }
//         // Phase 2: Load new level when fully black
//         else if (transitionTimer > TRANSITION_FADE_OUT_TIME &&
//                  transitionTimer <= TRANSITION_FADE_OUT_TIME + TRANSITION_HOLD_TIME) {
//             // Ensure screen is fully black
//             fadeAlpha = 1.0f;

//             // Load new level (only do this once at the start of phase 2)
//             if (transitionTimer - deltaTime <= TRANSITION_FADE_OUT_TIME) {
//                 debugf("Loading level %d...\n", targetTransitionLevel);

//                 // Update current level
//                 currentLevel = (LevelID)targetTransitionLevel;

//                 // Reset player velocity and state
//                 playerState.velX = 0.0f;
//                 playerState.velY = 0.0f;
//                 playerState.velZ = 0.0f;
//                 playerState.isGrounded = false;
//                 playerState.canMove = true;
//                 playerState.canRotate = true;
//                 playerState.canJump = true;

//                 // Reset animation states
//                 isCharging = false;
//                 isJumping = false;
//                 isLanding = false;
//                 jumpAnimPaused = false;
//                 jumpChargeTime = 0.0f;
//                 idleFrames = 0;
//                 playingFidget = false;
//                 fidgetPlayTime = 0.0f;

//                 // Reset to idle animation
//                 if (currentPart == PART_TORSO && torsoHasAnims && torsoAnimIdle.animRef != NULL) {
//                     t3d_anim_attach(&torsoAnimIdle, &torsoSkel);
//                     t3d_anim_set_time(&torsoAnimIdle, 0.0f);
//                     t3d_anim_set_playing(&torsoAnimIdle, true);
//                     t3d_anim_update(&torsoAnimIdle, 0.0f);
//                     t3d_skeleton_update(&torsoSkel);
//                 }

//                 // Free old level data (both map segments and decorations)
//                 map_runtime_free(&mapRuntime);
//                 maploader_free(&mapLoader);

//                 // Reload map loader and runtime for new level
//                 maploader_init(&mapLoader, FB_COUNT, VISIBILITY_RANGE);
//                 map_runtime_init(&mapRuntime, FB_COUNT, VISIBILITY_RANGE);
//                 currentlyAttachedAnim = NULL;  // Reset animation tracking
//                 level_load(currentLevel, &mapLoader, &mapRuntime);

//                 // Set body part for this level
//                 currentPart = (RobotParts)level_get_body_part(currentLevel);

//                 // Reset all TransitionCollision triggered flags (allow re-triggering in new level)
//                 for (int i = 0; i < mapRuntime.decoCount; i++) {
//                     DecoInstance* deco = &mapRuntime.decorations[i];
//                     if (deco->type == DECO_TRANSITIONCOLLISION && deco->active) {
//                         deco->state.transition.triggered = false;
//                     }
//                 }

//                 // Find the Nth PlayerSpawn decoration (targetTransitionSpawn is 0-indexed)
//                 int spawnIndex = 0;
//                 bool foundSpawn = false;
//                 for (int i = 0; i < mapRuntime.decoCount; i++) {
//                     DecoInstance* deco = &mapRuntime.decorations[i];
//                     if (deco->type == DECO_PLAYERSPAWN && deco->active) {
//                         if (spawnIndex == targetTransitionSpawn) {
//                             // Found the target spawn point
//                             cubeX = deco->posX;
//                             cubeY = deco->posY;
//                             cubeZ = deco->posZ;
//                             foundSpawn = true;
//                             debugf("Level transition: Player spawned at spawn point %d: (%.1f, %.1f, %.1f)\n",
//                                 targetTransitionSpawn, cubeX, cubeY, cubeZ);
//                             break;
//                         }
//                         spawnIndex++;
//                     }
//                 }

//                 if (!foundSpawn) {
//                     // Target spawn not found - use level default or first spawn
//                     if (spawnIndex > 0) {
//                         // We found some spawns but not enough - use the first one
//                         debugf("WARNING: Spawn point %d not found, using first spawn\n", targetTransitionSpawn);
//                         for (int i = 0; i < mapRuntime.decoCount; i++) {
//                             DecoInstance* deco = &mapRuntime.decorations[i];
//                             if (deco->type == DECO_PLAYERSPAWN && deco->active) {
//                                 cubeX = deco->posX;
//                                 cubeY = deco->posY;
//                                 cubeZ = deco->posZ;
//                                 break;
//                             }
//                         }
//                     } else {
//                         // No spawns found - use level default position
//                         debugf("WARNING: No PlayerSpawn found, using level default position\n");
//                         const LevelData* level = ALL_LEVELS[currentLevel];
//                         cubeX = level->playerStartX;
//                         cubeY = level->playerStartY;
//                         cubeZ = level->playerStartZ;
//                     }
//                 }

//                 // Reset camera
//                 smoothCamX = cubeX;
//                 smoothCamY = cubeY + 49.0f;
//                 camPos.v[0] = cubeX;
//                 camPos.v[1] = cubeY + 49.0f;
//                 camTarget.v[0] = cubeX;
//                 camTarget.v[1] = cubeY;

//                 debugf("Level transition complete!\n");
//             }
//         }
//         // Phase 3: Fade in
//         else if (transitionTimer > TRANSITION_FADE_OUT_TIME + TRANSITION_HOLD_TIME) {
//             float fadeInProgress = (transitionTimer - TRANSITION_FADE_OUT_TIME - TRANSITION_HOLD_TIME) / TRANSITION_FADE_IN_TIME;
//             fadeAlpha = 1.0f - fadeInProgress;
//             if (fadeAlpha < 0.0f) fadeAlpha = 0.0f;

//             // Transition complete
//             if (fadeAlpha <= 0.0f) {
//                 isTransitioning = false;
//                 transitionTimer = 0.0f;
//             }
//         }
//     }

//     // Pause
//     joypad_buttons_t pressedStart = joypad_get_buttons_pressed(JOYPAD_PORT_1);
//     if (pressedStart.start && !isPaused && !isDead && !isTransitioning) {
//         show_pause_menu();
//     }

//     // Debug: L+R+Z to trigger level complete (for testing)
//     if (debugFlyMode) {
//         joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
//         if (held.l && held.r && pressedStart.z) {
//             game_show_level_complete();
//         }
//     }
// }

// void draw_game_scene(void) {
//     uint32_t renderStart = get_ticks();

//     surface_t *disp = display_get();
//     surface_t *zbuf = display_get_zbuf();
//     rdpq_attach(disp, zbuf);
//     rdpq_clear(RGBA32(0x00, 0x40, 0x00, 0xFF));
//     rdpq_clear_z(0xFFFC);

//     t3d_frame_start();
//     t3d_viewport_attach(&viewport);

//     // Camera (with screen shake offset applied)
//     T3DVec3 shakenCamPos = {{camPos.v[0] + shakeOffsetX, camPos.v[1] + shakeOffsetY, camPos.v[2]}};
//     t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(70.0f), 10.0f, 1000.0f);
//     t3d_viewport_look_at(&viewport, &shakenCamPos, &camTarget, &(T3DVec3){{0, 1, 0}});

//     // Lighting
//     uint8_t ambientColor[4] = {80, 80, 80, 0xFF};
//     uint8_t lightColor[4] = {255, 255, 255, 0xFF};
//     T3DVec3 lightDir = {{1.0f, 1.0f, 1.0f}};
//     t3d_vec3_norm(&lightDir);
//     t3d_light_set_ambient(ambientColor);
//     t3d_light_set_directional(0, lightColor, &lightDir);
//     t3d_light_set_count(1);

//     // Draw all active maps using map loader (with frustum culling if BVH available)
//     uint32_t mapStart = get_ticks();
//     maploader_draw_culled(&mapLoader, frameIdx, &viewport);
//     g_renderMapTicks += get_ticks() - mapStart;

//     // Draw decorations (only if within visibility range and active)
//     uint32_t decoRenderStart = get_ticks();
//     float checkX = debugFlyMode ? debugCamX : cubeX;

//     // Draw decorations in order
//     for (int i = 0; i < mapRuntime.decoCount; i++) {
//         DecoInstance* deco = &mapRuntime.decorations[i];
//         if (!deco->active || deco->type == DECO_NONE) continue;

//         float dist = fabsf(deco->posX - checkX);
//         float visRange = (deco->type == DECO_COG) ? COG_VISIBILITY_RANGE : DECO_VISIBILITY_RANGE;
//         if (dist > visRange) continue;

//         // Skip invisible triggers in normal gameplay (only show in debug mode)
//         if (deco->type == DECO_PLAYERSPAWN && !debugFlyMode) continue;
//         if (deco->type == DECO_DAMAGECOLLISION && !debugFlyMode) continue;
//         if (deco->type == DECO_TRANSITIONCOLLISION && !debugFlyMode) continue;
//         if (deco->type == DECO_DIALOGUETRIGGER && !debugFlyMode) continue;
//         if (deco->type == DECO_INTERACTTRIGGER && !debugFlyMode) continue;
//         if (deco->type == DECO_PATROLPOINT && !debugFlyMode) continue;

//         // Draw semi-transparent box for PlayerSpawn in debug mode
//         if (deco->type == DECO_PLAYERSPAWN && debugFlyMode) {
//             // PlayerSpawn now uses BlueCube model directly
//             DecoTypeRuntime* spawnType = map_get_deco_type(&mapRuntime, DECO_PLAYERSPAWN);
//             if (spawnType && spawnType->model) {
//                 int matIdx = frameIdx * MAX_DECORATIONS + i;
//                 float renderYOffset = 0.2f;
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdx]);

//                 // Draw semi-transparent green box
//                 rdpq_set_mode_standard();
//                 rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//                 rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//                 rdpq_set_prim_color(RGBA32(0, 255, 0, 128));  // Semi-transparent green

//                 t3d_model_draw(spawnType->model);
//                 t3d_matrix_pop(1);
//             }
//             continue;
//         }

//         // Draw DamageCollision in debug mode
//         if (deco->type == DECO_DAMAGECOLLISION && debugFlyMode) {
//             DecoTypeRuntime* damageType = map_get_deco_type(&mapRuntime, DECO_DAMAGECOLLISION);
//             if (damageType && damageType->model) {
//                 int matIdx = frameIdx * MAX_DECORATIONS + i;
//                 float renderYOffset = 0.2f;
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdx]);

//                 // Draw semi-transparent red for danger
//                 rdpq_set_mode_standard();
//                 rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//                 rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//                 rdpq_set_prim_color(RGBA32(255, 0, 0, 128));  // Semi-transparent red

//                 t3d_model_draw(damageType->model);
//                 t3d_matrix_pop(1);
//             }
//             continue;
//         }

//         // Draw TransitionCollision in debug mode
//         if (deco->type == DECO_TRANSITIONCOLLISION && debugFlyMode) {
//             DecoTypeRuntime* transitionType = map_get_deco_type(&mapRuntime, DECO_TRANSITIONCOLLISION);
//             if (transitionType && transitionType->model) {
//                 int matIdx = frameIdx * MAX_DECORATIONS + i;
//                 float renderYOffset = 0.2f;
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdx]);

//                 // Draw semi-transparent cyan for transition
//                 rdpq_set_mode_standard();
//                 rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//                 rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//                 rdpq_set_prim_color(RGBA32(0, 255, 255, 128));  // Semi-transparent cyan

//                 t3d_model_draw(transitionType->model);
//                 t3d_matrix_pop(1);
//             }
//             continue;
//         }

//         // Draw DialogueTrigger in debug mode (yellow)
//         if (deco->type == DECO_DIALOGUETRIGGER && debugFlyMode) {
//             // Borrow BlueCube model from PLAYERSPAWN for visualization
//             DecoTypeRuntime* cubeType = map_get_deco_type(&mapRuntime, DECO_PLAYERSPAWN);
//             if (cubeType && cubeType->model) {
//                 int matIdx = frameIdx * MAX_DECORATIONS + i;
//                 // BlueCube is 128 units wide (-64 to +64), scale to match trigger radius
//                 float radius = deco->state.dialogueTrigger.triggerRadius;
//                 float scale = radius / 64.0f;
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){scale, scale * 0.3f, scale},  // Flatter to look like a zone
//                     (float[3]){0.0f, 0.0f, 0.0f},
//                     (float[3]){deco->posX, deco->posY + 5.0f, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdx]);

//                 rdpq_set_mode_standard();
//                 rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//                 rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//                 rdpq_set_prim_color(RGBA32(255, 255, 0, 100));  // Semi-transparent yellow

//                 t3d_model_draw(cubeType->model);
//                 t3d_matrix_pop(1);
//             }
//             continue;
//         }

//         // Draw InteractTrigger in debug mode (orange)
//         if (deco->type == DECO_INTERACTTRIGGER && debugFlyMode) {
//             // Borrow BlueCube model from PLAYERSPAWN for visualization
//             DecoTypeRuntime* cubeType = map_get_deco_type(&mapRuntime, DECO_PLAYERSPAWN);
//             if (cubeType && cubeType->model) {
//                 int matIdx = frameIdx * MAX_DECORATIONS + i;
//                 // BlueCube is 128 units wide (-64 to +64), scale to match trigger radius
//                 float radius = deco->state.interactTrigger.triggerRadius;
//                 float scale = radius / 64.0f;
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){scale, scale * 0.3f, scale},
//                     (float[3]){0.0f, 0.0f, 0.0f},
//                     (float[3]){deco->posX, deco->posY + 5.0f, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdx]);

//                 rdpq_set_mode_standard();
//                 rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//                 rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//                 rdpq_set_prim_color(RGBA32(255, 165, 0, 100));  // Semi-transparent orange

//                 t3d_model_draw(cubeType->model);
//                 t3d_matrix_pop(1);
//             }
//             continue;
//         }

//         DecoTypeRuntime* decoType = map_get_deco_type(&mapRuntime, deco->type);
//         const DecoTypeDef* decoDef = &DECO_TYPES[deco->type];
//         if (decoType && decoType->model) {
//             int matIdx = frameIdx * MAX_DECORATIONS + i;
//             // Add small Y-offset to prevent z-fighting with ground
//             float renderYOffset = 0.2f;

//             // Special handling for SIGN: use quaternion to rotate around global Z axis
//             if (deco->type == DECO_SIGN) {
//                 // First rotation: around Y axis (baseRotY + 180 degrees)
//                 fm_quat_t quatY, quatZ, quatFinal;
//                 fm_vec3_t axisY = {{0, 1, 0}};
//                 fm_vec3_t axisZ = {{0, 0, 1}};
//                 fm_quat_from_axis_angle(&quatY, &axisY, deco->state.sign.baseRotY + 3.14159265f);
//                 // Second rotation: around GLOBAL Z axis (tilt)
//                 fm_quat_from_axis_angle(&quatZ, &axisZ, deco->state.sign.tilt);
//                 // Combine: first Y, then Z (in global space: Z * Y)
//                 fm_quat_mul(&quatFinal, &quatZ, &quatY);

//                 t3d_mat4fp_from_srt(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     quatFinal.v,
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//             } else if (deco->type == DECO_BOLT) {
//                 // BOLT: Add scale pulse for "glow" effect
//                 float pulse = sinf(deco->state.bolt.spinAngle * 3.0f) * 0.15f + 1.0f;  // Oscillate 0.85 to 1.15
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX * pulse, deco->scaleY * pulse, deco->scaleZ * pulse},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//             } else if (deco->type == DECO_SLIME) {
//                 // SLIME: Apply squash/stretch and shear for jiggle effect
//                 float jigX = deco->state.slime.jiggleX;
//                 float jigZ = deco->state.slime.jiggleZ;
//                 float stretchY = deco->state.slime.stretchY;

//                 // Volume preservation: if Y squashes, X/Z expand
//                 float stretchXZ = 1.0f + (1.0f - stretchY) * 0.5f;

//                 // Extra Y offset to prevent z-fighting with ground
//                 float slimeYOffset = renderYOffset + 1.0f;

//                 // Shake effect (after merging)
//                 float shakeOffX = 0.0f, shakeOffZ = 0.0f;
//                 if (deco->state.slime.shakeTimer > 0.0f) {
//                     float shakeIntensity = deco->state.slime.shakeTimer * 8.0f * deco->scaleX;
//                     shakeOffX = sinf(deco->state.slime.shakeTimer * 60.0f) * shakeIntensity;
//                     shakeOffZ = cosf(deco->state.slime.shakeTimer * 45.0f) * shakeIntensity * 0.7f;
//                 }

//                 // First create base SRT matrix with squash/stretch
//                 T3DMat4FP* mat = &decoMatFP[matIdx];
//                 t3d_mat4fp_from_srt_euler(mat,
//                     (float[3]){deco->scaleX * stretchXZ, deco->scaleY * stretchY, deco->scaleZ * stretchXZ},
//                     (float[3]){0.0f, deco->rotY, 0.0f},
//                     (float[3]){deco->posX + shakeOffX, deco->posY + slimeYOffset, deco->posZ + shakeOffZ}
//                 );

//                 // Apply shear transform by modifying the matrix directly
//                 // T3DMat4FP structure: m[row] where each row has i[col] (int) and f[col] (frac)
//                 // For shear: X += shearX * Y, Z += shearZ * Y
//                 // This means modifying column 1 (Y input) in rows 0 (X output) and 2 (Z output)
//                 // Convert jiggle to 16.16 fixed point shear values
//                 float shearScale = 2.5f;  // Strong shear for wobbly effect
//                 float shearX = jigX * shearScale;
//                 float shearZ = jigZ * shearScale;

//                 // Convert float shear to 16.16 fixed point (split into int and frac parts)
//                 int32_t shearX_fp = (int32_t)(shearX * 65536.0f);
//                 int32_t shearZ_fp = (int32_t)(shearZ * 65536.0f);

//                 // Add shear to Y column (column 1) affecting X output (row 0) and Z output (row 2)
//                 // m[0].i[1] and m[0].f[1] = Y's contribution to X
//                 // m[2].i[1] and m[2].f[1] = Y's contribution to Z
//                 int32_t existingX = ((int32_t)mat->m[0].i[1] << 16) | mat->m[0].f[1];
//                 int32_t existingZ = ((int32_t)mat->m[2].i[1] << 16) | mat->m[2].f[1];

//                 existingX += shearX_fp;
//                 existingZ += shearZ_fp;

//                 mat->m[0].i[1] = (int16_t)(existingX >> 16);
//                 mat->m[0].f[1] = (uint16_t)(existingX & 0xFFFF);
//                 mat->m[2].i[1] = (int16_t)(existingZ >> 16);
//                 mat->m[2].f[1] = (uint16_t)(existingZ & 0xFFFF);
//             } else if (deco->type == DECO_ROUNDBUTTON) {
//                 // ROUNDBUTTON: Render bottom (base) at fixed position, top moves down when pressed
//                 float baseY = deco->posY + renderYOffset;  // Base stays fixed
//                 float topY = deco->posY + renderYOffset - 3.0f - deco->state.button.pressDepth;  // Top starts lower, moves down when pressed

//                 // Use two separate matrix slots to avoid any state bleeding
//                 int matIdxBottom = matIdx;
//                 int matIdxTop = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 3;  // Use a dedicated slot

//                 // Draw the bottom (base) - stays at original position
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxBottom],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, baseY, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdxBottom]);
//                 t3d_model_draw(decoType->model);  // Draw bottom (RoundButtonBottom)
//                 t3d_matrix_pop(1);

//                 // Draw the top - moves down by pressDepth
//                 if (mapRuntime.buttonTopModel) {
//                     t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxTop],
//                         (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                         (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                         (float[3]){deco->posX, topY, deco->posZ}
//                     );
//                     t3d_matrix_push(&decoMatFP[matIdxTop]);
//                     t3d_model_draw(mapRuntime.buttonTopModel);  // Draw top (RoundButtonTop)
//                     t3d_matrix_pop(1);
//                 }
//                 continue;  // Skip normal draw path since we handled it
//             } else if (deco->type == DECO_FAN) {
//                 // FAN: Render bottom (base) static, top rotates when active
//                 float baseY = deco->posY + renderYOffset;

//                 // Use two separate matrix slots (same pattern as button)
//                 int matIdxBottom = matIdx;
//                 int matIdxTop = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 4;  // Dedicated slot for fan top

//                 // Draw the bottom (base) - stays at original position, no rotation
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxBottom],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, baseY, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdxBottom]);
//                 t3d_model_draw(decoType->model);  // Draw bottom (FanBottom)
//                 t3d_matrix_pop(1);

//                 // Draw the top - rotates based on spinAngle
//                 if (mapRuntime.fanTopModel) {
//                     // Add spin angle to the Y rotation
//                     float spinRotY = deco->rotY + deco->state.fan.spinAngle;
//                     t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxTop],
//                         (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                         (float[3]){deco->rotX, spinRotY, deco->rotZ},
//                         (float[3]){deco->posX, baseY, deco->posZ}
//                     );
//                     t3d_matrix_push(&decoMatFP[matIdxTop]);
//                     t3d_model_draw(mapRuntime.fanTopModel);  // Draw top (FanTop)
//                     t3d_matrix_pop(1);
//                 }
//                 continue;  // Skip normal draw path since we handled it
//             } else if (deco->type == DECO_CONVEYERLARGE) {
//                 // CONVEYOR: Draw frame normally, draw belt with texture scroll
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdx]);

//                 // Draw frame (no scroll)
//                 t3d_model_draw(decoType->model);

//                 // Draw belt with texture scroll when active
//                 if (mapRuntime.conveyorBeltModel) {
//                     // Initialize UVs on first draw
//                     conveyor_belt_init_uvs(mapRuntime.conveyorBeltModel);

//                     bool isActive = (deco->activationId == 0) || activation_get(deco->activationId);
//                     if (isActive) {
//                         // Scroll UVs based on texture offset
//                         conveyor_belt_scroll_uvs(deco->state.conveyor.textureOffset);
//                     }
//                     t3d_model_draw(mapRuntime.conveyorBeltModel);
//                 }

//                 t3d_matrix_pop(1);
//                 continue;  // Skip normal draw path since we handled it
//             } else if (deco->type == DECO_TOXICPIPE) {
//                 // TOXICPIPE: Draw pipe normally, draw liquid with texture scroll
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdx]);

//                 // Draw pipe (no scroll)
//                 t3d_model_draw(decoType->model);

//                 // Draw liquid with texture scroll
//                 if (mapRuntime.toxicPipeLiquidModel) {
//                     toxic_pipe_liquid_init_uvs(mapRuntime.toxicPipeLiquidModel);
//                     toxic_pipe_liquid_scroll_uvs(deco->state.toxicPipe.textureOffset);
//                     t3d_model_draw(mapRuntime.toxicPipeLiquidModel);
//                 }

//                 t3d_matrix_pop(1);
//                 continue;
//             } else if (deco->type == DECO_TOXICRUNNING) {
//                 // TOXICRUNNING: Draw with texture scroll
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdx]);

//                 // Initialize UVs on first draw
//                 toxic_running_init_uvs(decoType->model);
//                 toxic_running_scroll_uvs(deco->state.toxicRunning.textureOffset);
//                 t3d_model_draw(decoType->model);

//                 t3d_matrix_pop(1);
//                 continue;
//             } else {
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//             }
//             t3d_matrix_push(&decoMatFP[matIdx]);

//             // For models with only vertex colors (no textures), set shade-only mode
//             if (decoDef->vertexColorsOnly) {
//                 t3d_tri_sync();
//                 rdpq_sync_pipe();
//                 rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
//             }

//             // Use per-instance skeleton if available, otherwise shared skeleton
//             if (deco->hasOwnSkeleton) {
//                 t3d_model_draw_skinned(decoType->model, &deco->skeleton);
//             } else if (decoType->hasSkeleton) {
//                 t3d_model_draw_skinned(decoType->model, &decoType->skeleton);
//             } else {
//                 t3d_model_draw(decoType->model);
//             }

//             // Reset combiner after vertex-colors-only model
//             if (decoDef->vertexColorsOnly) {
//                 t3d_tri_sync();
//                 rdpq_sync_pipe();
//                 rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
//             }
//             t3d_matrix_pop(1);
//         }
//     }

//     // Draw highlight around selected decoration in debug camera mode (not placement mode)
//     if (debugFlyMode && !debugPlacementMode && debugHighlightedDecoIndex >= 0) {
//         DecoInstance* deco = &mapRuntime.decorations[debugHighlightedDecoIndex];
//         if (deco->active && deco->type != DECO_NONE) {
//             DecoTypeRuntime* decoType = map_get_deco_type(&mapRuntime, deco->type);
//             if (decoType && decoType->model) {
//                 // Draw slightly larger, semi-transparent yellow outline
//                 int matIdx = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 2;  // Use dedicated slot
//                 float highlightScale = 1.15f;  // 15% larger for outline effect
//                 float renderYOffset = 0.2f;
//                 t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                     (float[3]){deco->scaleX * highlightScale, deco->scaleY * highlightScale, deco->scaleZ * highlightScale},
//                     (float[3]){deco->rotX, deco->rotY, deco->rotZ},
//                     (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
//                 );
//                 t3d_matrix_push(&decoMatFP[matIdx]);

//                 // Draw with bright yellow/orange pulsing color
//                 rdpq_set_mode_standard();
//                 rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//                 rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//                 // Pulse effect based on frame time
//                 int pulse = (frameIdx * 40) % 255;
//                 int pulseColor = 180 + (pulse > 127 ? 255 - pulse : pulse) / 2;
//                 rdpq_set_prim_color(RGBA32(255, pulseColor, 0, 160));  // Yellow-orange pulse

//                 if (deco->hasOwnSkeleton) {
//                     t3d_model_draw_skinned(decoType->model, &deco->skeleton);
//                 } else if (decoType->hasSkeleton) {
//                     t3d_model_draw_skinned(decoType->model, &decoType->skeleton);
//                 } else {
//                     t3d_model_draw(decoType->model);
//                 }
//                 t3d_matrix_pop(1);
//             }
//         }
//     }

//     // Draw placement preview in debug placement mode
//     if (debugFlyMode && debugPlacementMode) {
//         DecoTypeRuntime* previewType = map_get_deco_type(&mapRuntime, debugDecoType);
//         const DecoTypeDef* previewDef = &DECO_TYPES[debugDecoType];
//         if (previewType && previewType->model) {
//             // Use a dedicated matrix slot for preview (after all decorations)
//             int matIdx = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 1;
//             float renderYOffset = 0.2f;
//             t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                 (float[3]){debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ},
//                 (float[3]){0.0f, debugDecoRotY, 0.0f},
//                 (float[3]){debugDecoX, debugDecoY + renderYOffset, debugDecoZ}
//             );
//             t3d_matrix_push(&decoMatFP[matIdx]);

//             if (previewDef->vertexColorsOnly) {
//                 t3d_tri_sync();
//                 rdpq_sync_pipe();
//                 rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
//             }

//             if (previewType->hasSkeleton) {
//                 t3d_model_draw_skinned(previewType->model, &previewType->skeleton);
//             } else {
//                 t3d_model_draw(previewType->model);
//             }

//             if (previewDef->vertexColorsOnly) {
//                 t3d_tri_sync();
//                 rdpq_sync_pipe();
//                 rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
//             }
//             t3d_matrix_pop(1);
//         }
//     }

//     // Draw patrol point preview in patrol placement mode
//     if (debugFlyMode && patrolPlacementMode) {
//         DecoTypeRuntime* patrolType = map_get_deco_type(&mapRuntime, DECO_PATROLPOINT);
//         if (patrolType && patrolType->model) {
//             // Use a dedicated matrix slot for patrol preview
//             int matIdx = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS - 2;
//             t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
//                 (float[3]){0.1f, 0.1f, 0.1f},  // Small scale for patrol point
//                 (float[3]){0.0f, 0.0f, 0.0f},
//                 (float[3]){debugDecoX, debugDecoY + 0.2f, debugDecoZ}
//             );
//             t3d_matrix_push(&decoMatFP[matIdx]);
//             t3d_model_draw(patrolType->model);
//             t3d_matrix_pop(1);
//         }
//     }

//     // Update squash/stretch spring physics
//     // Two separate spring systems:
//     // 1. Landing squash (< 1.0) springs back faster
//     // 2. Jump stretch (> 1.0) springs back slower
//     if (!isJumping || isCharging) {
//         // Landing squash recovery (faster spring - original values * 2)
//         const float SQUASH_SPRING = 16.0f;    // 2x faster for landing recovery
//         const float SQUASH_DAMPING = 8.0f;    // 2x damping
//         float targetLandingSquash = 0.0f;
//         float springForce = (targetLandingSquash - landingSquash) * SQUASH_SPRING;
//         squashVelocity += springForce * DELTA_TIME;
//         squashVelocity *= (1.0f - SQUASH_DAMPING * DELTA_TIME);
//         landingSquash += squashVelocity * DELTA_TIME;
        
//         // Clamp landing squash to reasonable range
//         if (landingSquash < 0.0f) landingSquash = 0.0f;
//         if (landingSquash > 0.95f) landingSquash = 0.95f;
        
//         // Update total squash scale
//         squashScale = 1.0f - landingSquash - chargeSquash;
        
//         // Clamp final scale
//         if (squashScale < 0.05f) squashScale = 0.05f;
//         if (squashScale > 1.3f) squashScale = 1.3f;
//     } else {
//         // Jump stretch recovery (slower spring - original values)
//         const float STRETCH_SPRING = 8.0f;
//         const float STRETCH_DAMPING = 4.0f;
//         float targetScale = 1.0f;
//         float springForce = (targetScale - squashScale) * STRETCH_SPRING;
//         squashVelocity += springForce * DELTA_TIME;
//         squashVelocity *= (1.0f - STRETCH_DAMPING * DELTA_TIME);
//         squashScale += squashVelocity * DELTA_TIME;
        
//         // Clamp to reasonable range
//         if (squashScale < 0.05f) squashScale = 0.05f;
//         if (squashScale > 1.3f) squashScale = 1.3f;
//     }

//     g_renderDecoTicks += get_ticks() - decoRenderStart;

//     // Draw player model with skeleton based on current part
//     // Skip render on alternating frames during invincibility (flash effect)
//     uint32_t playerRenderStart = get_ticks();
//     bool shouldRenderPlayer = (invincibilityTimer <= 0.0f) ||
//                               ((invincibilityFlashFrame / INVINCIBILITY_FLASH_RATE) % 2 == 0);

//     if (shouldRenderPlayer && torsoModel) {
//         // Apply squash: Y compressed, X/Z expanded to preserve volume
//         float scaleY = 0.40f * squashScale;
//         float scaleXZ = 0.40f * (1.0f + (1.0f - squashScale) * 0.5f);  // Expand as Y squashes
//         t3d_mat4fp_from_srt_euler(&roboMatFP[frameIdx],
//             (float[3]){scaleXZ, scaleY, scaleXZ},
//             (float[3]){0.0f, playerState.playerAngle, 0.0f},
//             (float[3]){cubeX, cubeY, cubeZ}
//         );
//         t3d_matrix_push(&roboMatFP[frameIdx]);
//         t3d_model_draw_skinned(torsoModel, &torsoSkel);
//         t3d_matrix_pop(1);

//     }
//     g_renderPlayerTicks += get_ticks() - playerRenderStart;

//     // Draw player shadow on ground using rdpq_triangle (screen-space) - only when jumping
//     uint32_t shadowRenderStart = get_ticks();
//     if (shadowSprite && bestGroundY > -9000.0f && !playerState.isGrounded) {
//         // Get ground normal at player position
//         float groundNX, groundNY, groundNZ;
//         maploader_get_ground_height_normal(&mapLoader, cubeX, cubeY, cubeZ, &groundNX, &groundNY, &groundNZ);

//         // Shadow size based on height above ground (smaller when higher)
//         float heightAboveGround = cubeY - bestGroundY;
//         float shadowAlpha = fmaxf(0.0f, 1.0f - heightAboveGround / 150.0f);
//         if (shadowAlpha > 0.0f) {
//             float shadowScale = 6.0f * fmaxf(0.4f, 1.0f - heightAboveGround / 100.0f);
//             float shadowY = bestGroundY + 1.0f;  // Slightly above ground

//             // Calculate 4 corner positions projected onto the slope
//             // For each corner offset in XZ, calculate the Y based on slope
//             // Y offset = -(normalX * dx + normalZ * dz) / normalY
//             float sz = shadowScale;
//             T3DVec3 corners[4];
//             float offsets[4][2] = {{-sz, -sz}, {sz, -sz}, {-sz, sz}, {sz, sz}};

//             for (int c = 0; c < 4; c++) {
//                 float dx = offsets[c][0];
//                 float dz = offsets[c][1];
//                 float dy = 0.0f;
//                 if (fabsf(groundNY) > 0.01f) {
//                     // Project onto slope: solve for Y where point lies on plane
//                     dy = -(groundNX * dx + groundNZ * dz) / groundNY;
//                 }
//                 corners[c] = (T3DVec3){{cubeX + dx, shadowY + dy, cubeZ + dz}};
//             }

//             // Convert world positions to screen positions
//             T3DVec3 screenPos[4];
//             for (int i = 0; i < 4; i++) {
//                 t3d_viewport_calc_viewspace_pos(&viewport, &screenPos[i], &corners[i]);
//             }

//             // Only draw if at least one corner is in front of camera
//             if (!(screenPos[0].v[2] < 0 && screenPos[1].v[2] < 0 && screenPos[2].v[2] < 0 && screenPos[3].v[2] < 0)) {
//                 // Sync T3D before using rdpq directly
//                 t3d_tri_sync();
//                 rdpq_sync_pipe();

//                 // Set up render mode FIRST, then upload texture
//                 rdpq_set_mode_standard();
//                 rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
//                 rdpq_set_prim_color(RGBA32(0, 0, 0, (int)(shadowAlpha * 180)));
//                 rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

//                 // Upload shadow texture using sprite upload (handles format automatically)
//                 rdpq_sprite_upload(TILE0, shadowSprite, NULL);

//                 // Build vertex data for rdpq_triangle: X, Y, S, T, INV_W
//                 float v0[] = {screenPos[0].v[0], screenPos[0].v[1], 0.0f, 0.0f, 1.0f};
//                 float v1[] = {screenPos[1].v[0], screenPos[1].v[1], 8.0f, 0.0f, 1.0f};
//                 float v2[] = {screenPos[2].v[0], screenPos[2].v[1], 0.0f, 8.0f, 1.0f};
//                 float v3[] = {screenPos[3].v[0], screenPos[3].v[1], 8.0f, 8.0f, 1.0f};

//                 // Draw two triangles for the quad
//                 rdpq_triangle(&TRIFMT_TEX, v0, v1, v2);
//                 rdpq_triangle(&TRIFMT_TEX, v2, v1, v3);
//             }
//         }
//     }

//     // Draw slime oil trail decals as 3D quads (proper Z-buffer integration)
//     if (shadowSprite) {
//         // Set up static vertex data once (unit quad centered at origin)
//         int16_t *pos0 = t3d_vertbuffer_get_pos(decalVerts, 0);
//         int16_t *pos1 = t3d_vertbuffer_get_pos(decalVerts, 1);
//         int16_t *pos2 = t3d_vertbuffer_get_pos(decalVerts, 2);
//         int16_t *pos3 = t3d_vertbuffer_get_pos(decalVerts, 3);
//         pos0[0] = -1; pos0[1] = 0; pos0[2] = -1;
//         pos1[0] =  1; pos1[1] = 0; pos1[2] = -1;
//         pos2[0] = -1; pos2[1] = 0; pos2[2] =  1;
//         pos3[0] =  1; pos3[1] = 0; pos3[2] =  1;

//         // UVs: full 8x8 texture coverage
//         // Empirically determined: 8 * 32 * 2.5 = 640 works for 8x8 texture
//         // Vertex layout: 0=(-1,-1), 1=(+1,-1), 2=(-1,+1), 3=(+1,+1)
//         // UV origin is top-left, V increases downward
//         int16_t uvMax = 8 * 80;  // 640
//         int16_t *uv0 = t3d_vertbuffer_get_uv(decalVerts, 0);
//         int16_t *uv1 = t3d_vertbuffer_get_uv(decalVerts, 1);
//         int16_t *uv2 = t3d_vertbuffer_get_uv(decalVerts, 2);
//         int16_t *uv3 = t3d_vertbuffer_get_uv(decalVerts, 3);
//         // vert 0 (-1,-1) -> UV (0, uvMax) bottom-left
//         // vert 1 (+1,-1) -> UV (uvMax, uvMax) bottom-right
//         // vert 2 (-1,+1) -> UV (0, 0) top-left
//         // vert 3 (+1,+1) -> UV (uvMax, 0) top-right
//         uv0[0] = 0;     uv0[1] = uvMax;
//         uv1[0] = uvMax; uv1[1] = uvMax;
//         uv2[0] = 0;     uv2[1] = 0;
//         uv3[0] = uvMax; uv3[1] = 0;

//         // Set vertex colors (will be tinted by prim color)
//         *t3d_vertbuffer_get_color(decalVerts, 0) = 0xFFFFFFFF;
//         *t3d_vertbuffer_get_color(decalVerts, 1) = 0xFFFFFFFF;
//         *t3d_vertbuffer_get_color(decalVerts, 2) = 0xFFFFFFFF;
//         *t3d_vertbuffer_get_color(decalVerts, 3) = 0xFFFFFFFF;

//         // Sync and set up render mode once
//         t3d_tri_sync();
//         rdpq_sync_pipe();
//         rdpq_set_mode_standard();
//         rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
//         rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//         rdpq_mode_zbuf(true, false);  // Z compare but no write
//         rdpq_sprite_upload(TILE0, shadowSprite, NULL);
//         // No culling so both sides of quad are visible
//         t3d_state_set_drawflags(T3D_FLAG_DEPTH | T3D_FLAG_TEXTURED);

//         for (int i = 0; i < mapRuntime.decoCount; i++) {
//             DecoInstance* deco = &mapRuntime.decorations[i];
//             if (!deco->active || deco->type != DECO_SLIME) continue;

//             // Draw each active decal for this slime
//             for (int d = 0; d < 5; d++) {
//                 float alpha = deco->state.slime.decalAlpha[d];
//                 if (alpha <= 0.01f) continue;

//                 float decalX = deco->state.slime.decalX[d];
//                 float decalY = deco->state.slime.decalY[d];
//                 float decalZ = deco->state.slime.decalZ[d];
//                 float decalScale = deco->state.slime.decalScale;

//                 // Skip if line of sight from camera to decal is blocked
//                 if (maploader_raycast_blocked(&mapLoader,
//                     camPos.v[0], camPos.v[1], camPos.v[2],
//                     decalX, decalY + 1.0f, decalZ)) {
//                     continue;
//                 }

//                 // Set up transformation matrix for decal position (raised to avoid z-fighting)
//                 t3d_mat4fp_from_srt_euler(decalMatFP,
//                     (float[3]){decalScale, 1.0f, decalScale},
//                     (float[3]){0, 0, 0},
//                     (float[3]){decalX, decalY + 3.0f, decalZ}
//                 );

//                 // Set color with alpha for this decal
//                 rdpq_set_prim_color(RGBA32(20, 20, 20, (int)(alpha * 200)));

//                 // Re-upload texture before each decal (t3d may overwrite TMEM)
//                 rdpq_sprite_upload(TILE0, shadowSprite, NULL);

//                 // Draw quad as two triangles
//                 t3d_matrix_push(decalMatFP);
//                 t3d_vert_load(decalVerts, 0, 4);
//                 // Verts: 0=(-1,-1), 1=(1,-1), 2=(-1,1), 3=(1,1)
//                 // Triangle 1: 0-1-3 (bottom-left, bottom-right, top-right)
//                 // Triangle 2: 0-3-2 (bottom-left, top-right, top-left)
//                 t3d_tri_draw(0, 1, 3);
//                 t3d_tri_draw(0, 3, 2);
//                 t3d_matrix_pop(1);

//                 // Sync after each decal to ensure matrix/verts are consumed before reuse
//                 rspq_flush();
//             }
//         }
//     }

//     // Draw oil puddle decals as 3D quads (proper Z-buffer integration)
//     // Note: vertex data and render mode already set up by slime decals above
//     if (shadowSprite) {
//         // Re-sync and set up for oil puddles (different color)
//         t3d_tri_sync();
//         rdpq_sync_pipe();
//         rdpq_set_mode_standard();
//         rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
//         rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//         rdpq_mode_zbuf(true, false);
//         rdpq_sprite_upload(TILE0, shadowSprite, NULL);
//         t3d_state_set_drawflags(T3D_FLAG_DEPTH | T3D_FLAG_TEXTURED);
//         rdpq_set_prim_color(RGBA32(40, 20, 50, 180));

//         for (int i = 0; i < mapRuntime.decoCount; i++) {
//             DecoInstance* deco = &mapRuntime.decorations[i];
//             if (!deco->active || deco->type != DECO_OILPUDDLE) continue;

//             // Skip if line of sight from camera to puddle is blocked
//             if (maploader_raycast_blocked(&mapLoader,
//                 camPos.v[0], camPos.v[1], camPos.v[2],
//                 deco->posX, deco->posY + 1.0f, deco->posZ)) {
//                 continue;
//             }

//             float sz = deco->state.oilpuddle.radius;

//             // Set up transformation matrix for decal position (raised to avoid z-fighting)
//             t3d_mat4fp_from_srt_euler(decalMatFP,
//                 (float[3]){sz, 1.0f, sz},
//                 (float[3]){0, 0, 0},
//                 (float[3]){deco->posX, deco->posY + 3.0f, deco->posZ}
//             );

//             // Re-upload texture before each decal (t3d may overwrite TMEM)
//             rdpq_sprite_upload(TILE0, shadowSprite, NULL);

//             // Draw quad as two triangles
//             t3d_matrix_push(decalMatFP);
//             t3d_vert_load(decalVerts, 0, 4);
//             t3d_tri_draw(0, 1, 3);
//             t3d_tri_draw(0, 3, 2);
//             t3d_matrix_pop(1);

//             // Sync after each decal to ensure matrix/verts are consumed before reuse
//             rspq_flush();
//         }
//     }

//     // Draw death decals (slime death splats that fade over time)
//     if (shadowSprite) {
//         t3d_tri_sync();
//         rdpq_sync_pipe();
//         rdpq_set_mode_standard();
//         rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
//         rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//         rdpq_mode_zbuf(true, false);
//         rdpq_sprite_upload(TILE0, shadowSprite, NULL);
//         t3d_state_set_drawflags(T3D_FLAG_DEPTH | T3D_FLAG_TEXTURED);

//         for (int i = 0; i < MAX_DEATH_DECALS; i++) {
//             DeathDecal* decal = &g_deathDecals[i];
//             if (!decal->active) continue;

//             // Set color with fading alpha
//             uint8_t alpha = (uint8_t)(decal->alpha * 200.0f);
//             if (alpha > 200) alpha = 200;
//             rdpq_set_prim_color(RGBA32(20, 20, 20, alpha));

//             // Set up transformation matrix
//             t3d_mat4fp_from_srt_euler(decalMatFP,
//                 (float[3]){decal->scale, 1.0f, decal->scale},
//                 (float[3]){0, 0, 0},
//                 (float[3]){decal->x, decal->y + 3.0f, decal->z}
//             );

//             rdpq_sprite_upload(TILE0, shadowSprite, NULL);

//             t3d_matrix_push(decalMatFP);
//             t3d_vert_load(decalVerts, 0, 4);
//             t3d_tri_draw(0, 1, 3);
//             t3d_tri_draw(0, 3, 2);
//             t3d_matrix_pop(1);

//             rspq_flush();
//         }
//     }

//     // Draw particles (simple screen-space quads)
//     {
//         bool hasActiveParticles = false;
//         for (int i = 0; i < MAX_PARTICLES; i++) {
//             if (g_particles[i].active) {
//                 hasActiveParticles = true;
//                 break;
//             }
//         }

//         if (hasActiveParticles) {
//             rdpq_sync_pipe();
//             rdpq_set_mode_standard();
//             rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//             rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

//             for (int i = 0; i < MAX_PARTICLES; i++) {
//                 Particle* p = &g_particles[i];
//                 if (!p->active) continue;

//                 // Convert world position to screen position
//                 T3DVec3 worldPos = {{p->x, p->y, p->z}};
//                 T3DVec3 screenPos;
//                 t3d_viewport_calc_viewspace_pos(&viewport, &screenPos, &worldPos);

//                 // Skip if behind camera
//                 if (screenPos.v[2] <= 0) continue;

//                 // Calculate alpha based on remaining life
//                 float lifeRatio = p->life / p->maxLife;
//                 uint8_t alpha = (uint8_t)(180.0f * lifeRatio);

//                 // Draw as a small filled rectangle
//                 float halfSize = p->size;
//                 float sx = screenPos.v[0];
//                 float sy = screenPos.v[1];

//                 // Skip if off screen
//                 if (sx < -halfSize || sx > 320 + halfSize || sy < -halfSize || sy > 240 + halfSize) continue;

//                 rdpq_set_prim_color(RGBA32(p->r, p->g, p->b, alpha));

//                 // Draw as two triangles forming a quad
//                 float v0[] = {sx - halfSize, sy - halfSize};
//                 float v1[] = {sx + halfSize, sy - halfSize};
//                 float v2[] = {sx - halfSize, sy + halfSize};
//                 float v3[] = {sx + halfSize, sy + halfSize};

//                 rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
//                 rdpq_triangle(&TRIFMT_FILL, v2, v1, v3);
//             }
//         }
//     }

//     // Draw jump arc prediction IN 3D SPACE (while charging in torso mode, only after hop threshold)
//     if (isCharging && jumpChargeTime >= hopThreshold && currentPart == PART_TORSO && arcDotModel) {
//         // Calculate predicted jump velocity based on current charge and stick direction
//         float predictedVelY = chargeJumpEarlyBase + jumpChargeTime * chargeJumpEarlyMultiplier;

//         // Use stick direction for arc - matches actual jump behavior
//         float aimMag = sqrtf(jumpAimX * jumpAimX + jumpAimY * jumpAimY);
//         if (aimMag > 1.0f) aimMag = 1.0f;

//         // Horizontal scale must match the jump logic
//         const float HORIZONTAL_SCALE = 0.4f;
//         float predictedForward = (3.0f + 2.0f * jumpChargeTime) * FPS_SCALE * aimMag;
//         float predictedVelX, predictedVelZ;
//         if (aimMag > 0.1f) {
//             predictedVelX = (jumpAimX / aimMag) * predictedForward * HORIZONTAL_SCALE;
//             predictedVelZ = (jumpAimY / aimMag) * predictedForward;
//         } else {
//             predictedVelX = 0.0f;
//             predictedVelZ = 0.0f;
//         }

//         // Simulate trajectory - check ground only every 10 frames to save performance
//         float simVelX = predictedVelX;
//         float simVelY = predictedVelY;
//         float simVelZ = predictedVelZ;
//         float simX = cubeX;
//         float simY = cubeY;
//         float simZ = cubeZ;
//         float landX = cubeX, landY = groundLevel, landZ = cubeZ;

//         const int maxDots = 6;
//         int dotInterval = 4;
//         int dotsDrawn = 0;

//         for (int frame = 0; frame < 60; frame++) {
//             simX += simVelX;
//             simY += simVelY;
//             simZ += simVelZ;
//             simVelY -= GRAVITY;

//             // Check ground only every 10 frames (expensive operation)
//             if (frame % 10 == 0) {
//                 float groundY = maploader_get_ground_height(&mapLoader, simX, simY + 50.0f, simZ);
//                 if (groundY > INVALID_GROUND_Y + 10.0f) {
//                     landY = groundY;
//                 }
//             }

//             // Check if we've landed
//             if (simY <= landY + 2.0f) {
//                 landX = simX;
//                 landZ = simZ;
//                 break;
//             }

//             // Draw arc dots
//             if (frame > 2 && frame % dotInterval == 0 && dotsDrawn < maxDots) {
//                 float dotSize = 0.05f;
//                 t3d_mat4fp_from_srt_euler(&arcMatFP[dotsDrawn],
//                     (float[3]){dotSize, dotSize, dotSize},
//                     (float[3]){0, frame * 2.0f, 0},
//                     (float[3]){simX, simY, simZ}
//                 );
//                 t3d_matrix_push(&arcMatFP[dotsDrawn]);
//                 rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//                 rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//                 rdpq_set_prim_color(RGBA32(0, 255, 255, 255));
//                 t3d_model_draw(arcDotModel);
//                 t3d_matrix_pop(1);
//                 dotsDrawn++;
//             }

//             // Update landing position
//             landX = simX;
//             landZ = simZ;
//         }

//         // Draw landing marker
//         float dotSize = 0.1f;
//         t3d_mat4fp_from_srt_euler(&arcMatFP[dotsDrawn],
//             (float[3]){dotSize, dotSize, dotSize},
//             (float[3]){0, 0, 0},
//             (float[3]){landX, landY + 3.0f, landZ}
//         );
//         t3d_matrix_push(&arcMatFP[dotsDrawn]);
//         rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//         rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//         rdpq_set_prim_color(RGBA32(255, 255, 0, 255));
//         t3d_model_draw(arcDotModel);
//         t3d_matrix_pop(1);

//         // Store arc end X for camera lerping
//         jumpArcEndX = landX;
//     } else {
//         // Reset arc end to player position when not charging
//         jumpArcEndX = cubeX;
//     }

//     t3d_tri_sync();
//     rdpq_set_mode_standard();

//     // Debug collision visualization - draw all collision triangles as wireframe
//     if (debugFlyMode && debugShowCollision) {
//         rdpq_set_mode_standard();
//         rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//         rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

//         // Draw collision triangles for active map segments
//         for (int s = 0; s < mapLoader.count; s++) {
//             MapSegment* seg = &mapLoader.segments[s];
//             if (!seg->active || !seg->collision) continue;

//             CollisionMesh* mesh = seg->collision;
//             for (int i = 0; i < mesh->count; i++) {
//                 CollisionTriangle* t = &mesh->triangles[i];

//                 // Apply 90° Y rotation and scale (same as collision code)
//                 float x0 = -t->z0 * seg->scaleX + seg->posX;
//                 float y0 = t->y0 * seg->scaleY + seg->posY;
//                 float z0 = t->x0 * seg->scaleZ + seg->posZ;
//                 float x1 = -t->z1 * seg->scaleX + seg->posX;
//                 float y1 = t->y1 * seg->scaleY + seg->posY;
//                 float z1 = t->x1 * seg->scaleZ + seg->posZ;
//                 float x2 = -t->z2 * seg->scaleX + seg->posX;
//                 float y2 = t->y2 * seg->scaleY + seg->posY;
//                 float z2 = t->x2 * seg->scaleZ + seg->posZ;

//                 // Skip triangles far from camera
//                 float camX = debugFlyMode ? debugCamX : cubeX;
//                 float camZ = debugFlyMode ? debugCamZ : cubeZ;
//                 float centerX = (x0 + x1 + x2) / 3.0f;
//                 float centerZ = (z0 + z1 + z2) / 3.0f;
//                 float dist = sqrtf((centerX - camX) * (centerX - camX) + (centerZ - camZ) * (centerZ - camZ));
//                 if (dist > 300.0f) continue;

//                 // Calculate triangle normal to determine color
//                 float ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
//                 float vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
//                 float nx = uy * vz - uz * vy;
//                 float ny = uz * vx - ux * vz;
//                 float nz = ux * vy - uy * vx;
//                 float len = sqrtf(nx*nx + ny*ny + nz*nz);
//                 if (len > 0.001f) { nx /= len; ny /= len; nz /= len; }

//                 // Color based on normal: green=floor, red=wall, blue=ceiling
//                 color_t triColor;
//                 if (ny > 0.3f) {
//                     triColor = RGBA32(0, 255, 0, 100);  // Floor/slope - green
//                 } else if (ny < -0.7f) {
//                     triColor = RGBA32(0, 0, 255, 100);  // Ceiling - blue
//                 } else {
//                     triColor = RGBA32(255, 0, 0, 100);  // Wall - red
//                 }

//                 // Project vertices to screen using view-projection matrix
//                 T3DVec3 world0 = {{x0, y0, z0}};
//                 T3DVec3 world1 = {{x1, y1, z1}};
//                 T3DVec3 world2 = {{x2, y2, z2}};
//                 T3DVec4 clip0, clip1, clip2;

//                 // Transform to clip space using the combined view-projection matrix
//                 t3d_mat4_mul_vec3(&clip0, &viewport.matCamProj, &world0);
//                 t3d_mat4_mul_vec3(&clip1, &viewport.matCamProj, &world1);
//                 t3d_mat4_mul_vec3(&clip2, &viewport.matCamProj, &world2);

//                 // Skip if all behind camera (w < 0 or very small)
//                 if (clip0.v[3] < 0.1f && clip1.v[3] < 0.1f && clip2.v[3] < 0.1f) continue;

//                 // Perspective divide and convert to screen coordinates
//                 float screenW = 160.0f;  // Half screen width
//                 float screenH = 120.0f;  // Half screen height

//                 float sx0 = (clip0.v[3] > 0.1f) ? (screenW + clip0.v[0] / clip0.v[3] * screenW) : -1000;
//                 float sy0 = (clip0.v[3] > 0.1f) ? (screenH - clip0.v[1] / clip0.v[3] * screenH) : -1000;
//                 float sx1 = (clip1.v[3] > 0.1f) ? (screenW + clip1.v[0] / clip1.v[3] * screenW) : -1000;
//                 float sy1 = (clip1.v[3] > 0.1f) ? (screenH - clip1.v[1] / clip1.v[3] * screenH) : -1000;
//                 float sx2 = (clip2.v[3] > 0.1f) ? (screenW + clip2.v[0] / clip2.v[3] * screenW) : -1000;
//                 float sy2 = (clip2.v[3] > 0.1f) ? (screenH - clip2.v[1] / clip2.v[3] * screenH) : -1000;

//                 // Skip if any vertex is way off screen (simple clipping)
//                 if (sx0 < -500 || sx0 > 820 || sy0 < -500 || sy0 > 740) continue;
//                 if (sx1 < -500 || sx1 > 820 || sy1 < -500 || sy1 > 740) continue;
//                 if (sx2 < -500 || sx2 > 820 || sy2 < -500 || sy2 > 740) continue;

//                 // Draw filled triangle
//                 rdpq_set_prim_color(triColor);
//                 float v0[] = {sx0, sy0};
//                 float v1[] = {sx1, sy1};
//                 float v2[] = {sx2, sy2};
//                 rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
//             }
//         }

//         // Draw decoration collision meshes (transparent blue)
//         for (int d = 0; d < mapRuntime.decoCount; d++) {
//             DecoInstance* deco = &mapRuntime.decorations[d];
//             if (!deco->active || deco->type == DECO_NONE) continue;

//             DecoTypeRuntime* decoType = &mapRuntime.decoTypes[deco->type];
//             if (!decoType->loaded || !decoType->collision) continue;

//             CollisionMesh* mesh = decoType->collision;

//             // For SIGN: apply extra rotation to match visual (baseRotY + 180° and tilt on Z)
//             float signExtraRotY = 0.0f;
//             float signTiltZ = 0.0f;
//             if (deco->type == DECO_SIGN) {
//                 signExtraRotY = 3.14159265f;  // +180° to match visual
//                 signTiltZ = deco->state.sign.tilt;
//             }
//             float cosExtraY = cosf(signExtraRotY);
//             float sinExtraY = sinf(signExtraRotY);
//             float cosTiltZ = cosf(signTiltZ);
//             float sinTiltZ = sinf(signTiltZ);

//             for (int i = 0; i < mesh->count; i++) {
//                 CollisionTriangle* t = &mesh->triangles[i];

//                 // Apply 90° Y rotation and scale (same as collision code)
//                 float lx0 = -t->z0 * deco->scaleX;
//                 float ly0 = t->y0 * deco->scaleY;
//                 float lz0 = t->x0 * deco->scaleZ;
//                 float lx1 = -t->z1 * deco->scaleX;
//                 float ly1 = t->y1 * deco->scaleY;
//                 float lz1 = t->x1 * deco->scaleZ;
//                 float lx2 = -t->z2 * deco->scaleX;
//                 float ly2 = t->y2 * deco->scaleY;
//                 float lz2 = t->x2 * deco->scaleZ;

//                 // For SIGN: apply tilt (rotZ) then extra Y rotation
//                 if (deco->type == DECO_SIGN) {
//                     // Apply tilt around Z axis
//                     float tx, ty;
//                     tx = lx0 * cosTiltZ - ly0 * sinTiltZ; ty = lx0 * sinTiltZ + ly0 * cosTiltZ; lx0 = tx; ly0 = ty;
//                     tx = lx1 * cosTiltZ - ly1 * sinTiltZ; ty = lx1 * sinTiltZ + ly1 * cosTiltZ; lx1 = tx; ly1 = ty;
//                     tx = lx2 * cosTiltZ - ly2 * sinTiltZ; ty = lx2 * sinTiltZ + ly2 * cosTiltZ; lx2 = tx; ly2 = ty;

//                     // Apply extra Y rotation (+180°)
//                     float tz;
//                     tx = lx0 * cosExtraY + lz0 * sinExtraY; tz = -lx0 * sinExtraY + lz0 * cosExtraY; lx0 = tx; lz0 = tz;
//                     tx = lx1 * cosExtraY + lz1 * sinExtraY; tz = -lx1 * sinExtraY + lz1 * cosExtraY; lx1 = tx; lz1 = tz;
//                     tx = lx2 * cosExtraY + lz2 * sinExtraY; tz = -lx2 * sinExtraY + lz2 * cosExtraY; lx2 = tx; lz2 = tz;
//                 }

//                 float x0 = lx0 + deco->posX;
//                 float y0 = ly0 + deco->posY;
//                 float z0 = lz0 + deco->posZ;
//                 float x1 = lx1 + deco->posX;
//                 float y1 = ly1 + deco->posY;
//                 float z1 = lz1 + deco->posZ;
//                 float x2 = lx2 + deco->posX;
//                 float y2 = ly2 + deco->posY;
//                 float z2 = lz2 + deco->posZ;

//                 // Skip triangles far from camera
//                 float camX = debugFlyMode ? debugCamX : cubeX;
//                 float camZ = debugFlyMode ? debugCamZ : cubeZ;
//                 float centerX = (x0 + x1 + x2) / 3.0f;
//                 float centerZ = (z0 + z1 + z2) / 3.0f;
//                 float dist = sqrtf((centerX - camX) * (centerX - camX) + (centerZ - camZ) * (centerZ - camZ));
//                 if (dist > 300.0f) continue;

//                 // Project vertices to screen
//                 T3DVec3 world0 = {{x0, y0, z0}};
//                 T3DVec3 world1 = {{x1, y1, z1}};
//                 T3DVec3 world2 = {{x2, y2, z2}};
//                 T3DVec4 clip0, clip1, clip2;

//                 t3d_mat4_mul_vec3(&clip0, &viewport.matCamProj, &world0);
//                 t3d_mat4_mul_vec3(&clip1, &viewport.matCamProj, &world1);
//                 t3d_mat4_mul_vec3(&clip2, &viewport.matCamProj, &world2);

//                 if (clip0.v[3] < 0.1f && clip1.v[3] < 0.1f && clip2.v[3] < 0.1f) continue;

//                 float screenW = 160.0f;
//                 float screenH = 120.0f;

//                 float sx0 = (clip0.v[3] > 0.1f) ? (screenW + clip0.v[0] / clip0.v[3] * screenW) : -1000;
//                 float sy0 = (clip0.v[3] > 0.1f) ? (screenH - clip0.v[1] / clip0.v[3] * screenH) : -1000;
//                 float sx1 = (clip1.v[3] > 0.1f) ? (screenW + clip1.v[0] / clip1.v[3] * screenW) : -1000;
//                 float sy1 = (clip1.v[3] > 0.1f) ? (screenH - clip1.v[1] / clip1.v[3] * screenH) : -1000;
//                 float sx2 = (clip2.v[3] > 0.1f) ? (screenW + clip2.v[0] / clip2.v[3] * screenW) : -1000;
//                 float sy2 = (clip2.v[3] > 0.1f) ? (screenH - clip2.v[1] / clip2.v[3] * screenH) : -1000;

//                 if (sx0 < -500 || sx0 > 820 || sy0 < -500 || sy0 > 740) continue;
//                 if (sx1 < -500 || sx1 > 820 || sy1 < -500 || sy1 > 740) continue;
//                 if (sx2 < -500 || sx2 > 820 || sy2 < -500 || sy2 > 740) continue;

//                 // Draw transparent blue triangle for decoration collision
//                 rdpq_set_prim_color(RGBA32(0, 100, 255, 120));
//                 float v0[] = {sx0, sy0};
//                 float v1[] = {sx1, sy1};
//                 float v2[] = {sx2, sy2};
//                 rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
//             }
//         }
//     }

//     g_renderShadowTicks += get_ticks() - shadowRenderStart;

//     // Debug info / HUD
//     uint32_t hudStart = get_ticks();
//     int activeCount = 0;
//     for (int i = 0; i < mapLoader.count; i++) {
//         if (mapLoader.segments[i].active) activeCount++;
//     }

//     if (debugFlyMode) {
//         // Debug mode HUD
//         const char* modeStr = debugPlacementMode ? "[PLACE]" : "[CAM]";
//         rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 10, "DEBUG %s", modeStr);
//         rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 22, "Deco: %s", DECO_TYPES[debugDecoType].name);
//         rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 34, "Placed: %d", mapRuntime.decoCount);

//         if (debugPlacementMode) {
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 50, "Pos: %.1f %.1f %.1f", debugDecoX, debugDecoY, debugDecoZ);
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 62, "Rot: %.2f", debugDecoRotY);
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 74, "Scale: %.2f %.2f %.2f", debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ);
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 188, "Stick=Move C-U/D=Y C-L/R=Rot");
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 200, "Z+Stick=ScaleXZ Z+C=ScaleY");
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 212, "A=Place B=Camera L/R=Type");
//         } else {
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 50, "Cam: %.0f %.0f %.0f", debugCamX, debugCamY, debugCamZ);
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 62, "Collision: %s", debugShowCollision ? "ON" : "OFF");
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 188, "D-Left=Collision");
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 200, "Stick=Move C=Look A/Z=Up/Dn");
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 212, "B=Place D-Right=Del L/R=Type");

//             // Show highlighted decoration name in top-right corner
//             if (debugHighlightedDecoIndex >= 0) {
//                 DecoInstance* hlDeco = &mapRuntime.decorations[debugHighlightedDecoIndex];
//                 if (hlDeco->active && hlDeco->type != DECO_NONE && hlDeco->type < DECO_TYPE_COUNT) {
//                     const char* typeName = DECO_TYPES[hlDeco->type].name;
//                     // Draw on right side of screen (320 - text width estimate)
//                     rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 180, 10, "SELECT: %s", typeName);
//                     rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 180, 22, "Pos: %.0f,%.0f,%.0f", hlDeco->posX, hlDeco->posY, hlDeco->posZ);
//                     rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 180, 34, "[D-Right to delete]");
//                 }
//             }
//         }
//         rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 224, "D-Up=Exit Debug");
//     } else {
//         // Normal gameplay HUD - minimal for performance
//         // Health HUD (sprite-based, slides down from top)
//         if (healthHudY > HEALTH_HUD_HIDE_Y + 1.0f) {
//             // Determine sprite index: 0 = full health (3), 3 = dead (0)
//             int healthIdx = maxPlayerHealth - playerHealth;
//             if (healthIdx < 0) healthIdx = 0;
//             if (healthIdx > 3) healthIdx = 3;

//             sprite_t *healthSprite = healthSprites[healthIdx];
//             if (healthSprite) {
//                 int spriteX = 160 - 32;  // Center the 64x64 sprite (32x32 at 2x scale)
//                 int spriteY = (int)healthHudY;

//                 rdpq_set_mode_standard();
//                 rdpq_mode_alphacompare(1);

//                 // Flash effect: alternate visibility during flash timer
//                 bool showSprite = true;
//                 if (healthFlashTimer > 0.0f) {
//                     // Flash every 0.1 seconds
//                     int flashPhase = (int)(healthFlashTimer * 10.0f) % 2;
//                     showSprite = (flashPhase == 0);
//                 }

//                 if (showSprite) {
//                     rdpq_sprite_blit(healthSprite, spriteX, spriteY, &(rdpq_blitparms_t){
//                         .scale_x = 2.0f,
//                         .scale_y = 2.0f
//                     });
//                 }
//             }
//         }

//         // Bolt/Screw HUD (slides in from right)
//         // Only draw if visible or animating
//         if (screwHudX < SCREW_HUD_HIDE_X - 1.0f) {
//             sprite_t *screwSprite = screwSprites[screwAnimFrame];
//             if (screwSprite) {
//                 int spriteX = (int)screwHudX;
//                 int spriteY = 180;  // Lower right area

//                 rdpq_set_mode_standard();
//                 rdpq_mode_alphacompare(1);

//                 rdpq_sprite_blit(screwSprite, spriteX, spriteY, &(rdpq_blitparms_t){
//                     .scale_x = 1.5f,
//                     .scale_y = 1.5f
//                 });

//                 // Draw bolt count next to the screw sprite
//                 int collected = save_get_total_bolts_collected();
//                 int total = g_saveLevelInfo.totalBolts;
//                 rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, spriteX + 48, spriteY + 28, "%d/%d", collected, total);
//             }
//         }

//         // Show current part (top right)
//         const char* partNames[] = {"Head", "Torso", "Arms", "Legs"};
//         rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 250, 10, "%s", partNames[currentPart]);
//     }

//     // Draw hit flash overlay (red flash when damaged)
//     if (hitFlashTimer > 0.0f) {
//         rdpq_sync_pipe();
//         rdpq_set_mode_standard();
//         rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//         rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

//         uint8_t flashAlpha = (uint8_t)(hitFlashTimer / 0.15f * 100.0f);  // Max 100 alpha
//         rdpq_set_prim_color(RGBA32(255, 0, 0, flashAlpha));
//         rdpq_fill_rectangle(0, 0, 320, 240);
//     }

//     // Draw iris effect (death/respawn transition)
//     if (irisActive) {
//         rdpq_sync_pipe();
//         rdpq_set_mode_standard();
//         rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//         rdpq_mode_blender(0);  // No blending - solid black

//         rdpq_set_prim_color(RGBA32(0, 0, 0, 255));

//         // Draw iris as triangular segments forming a ring around the opening
//         // This creates a circular hole effect
//         float cx = irisCenterX;
//         float cy = irisCenterY;
//         float r = irisRadius;

//         // If iris is fully closed, just draw black screen
//         if (r <= 1.0f) {
//             rdpq_fill_rectangle(0, 0, 320, 240);
//         } else {
//             // Draw triangles from screen corners/edges to circle edge
//             // Use 16 segments for smooth circle
//             #define IRIS_SEGMENTS 16
//             float angleStep = (2.0f * 3.14159f) / IRIS_SEGMENTS;

//             for (int i = 0; i < IRIS_SEGMENTS; i++) {
//                 float a1 = i * angleStep;
//                 float a2 = (i + 1) * angleStep;

//                 // Points on the circle
//                 float x1 = cx + cosf(a1) * r;
//                 float y1 = cy + sinf(a1) * r;
//                 float x2 = cx + cosf(a2) * r;
//                 float y2 = cy + sinf(a2) * r;

//                 // Extended points far outside screen (to cover everything)
//                 float ext = 500.0f;  // Far enough to cover screen
//                 float ex1 = cx + cosf(a1) * ext;
//                 float ey1 = cy + sinf(a1) * ext;
//                 float ex2 = cx + cosf(a2) * ext;
//                 float ey2 = cy + sinf(a2) * ext;

//                 // Draw quad (two triangles) from circle edge to extended edge
//                 float v0[] = {x1, y1};
//                 float v1[] = {x2, y2};
//                 float v2[] = {ex1, ey1};
//                 float v3[] = {ex2, ey2};

//                 rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
//                 rdpq_triangle(&TRIFMT_FILL, v1, v3, v2);
//             }
//         }
//     }
//     // Regular fade overlay (for level transitions, not death)
//     else if (fadeAlpha > 0.0f) {
//         rdpq_sync_pipe();
//         rdpq_set_mode_standard();
//         rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//         rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

//         uint8_t alpha = (uint8_t)(fadeAlpha * 255.0f);
//         rdpq_set_prim_color(RGBA32(0, 0, 0, alpha));
//         rdpq_fill_rectangle(0, 0, 320, 240);
//     }

//     // Draw pause menu
//     if (isPaused) {
//         option_draw(&pauseMenu);
//     }

//     // Draw dialogue (for interact triggers)
//     if (scriptRunning) {
//         dialogue_draw(&dialogueBox);
//     }

//     // Draw A button prompt for nearby interact triggers
//     if (!isPaused && !scriptRunning && !isDead && !isHurt && !isRespawning && !isTransitioning && !debugFlyMode) {
//         for (int i = 0; i < mapRuntime.decoCount; i++) {
//             DecoInstance* deco = &mapRuntime.decorations[i];
//             if (deco->active && deco->type == DECO_INTERACTTRIGGER &&
//                 deco->state.interactTrigger.playerInRange && !deco->state.interactTrigger.interacting) {
//                 // Draw A button prompt in bottom right
//                 rdpq_textparms_t parms = {0};
//                 parms.align = ALIGN_RIGHT;
//                 parms.valign = VALIGN_BOTTOM;
//                 // Draw with button font (font ID 2)
//                 rdpq_text_printf(&parms, 2, 305, 220, "a");  // lowercase 'a' = A button icon
//                 break;
//             }
//         }
//     }

//     // Draw debug menu (shared module)
//     debug_menu_draw();

//     g_renderHUDTicks += get_ticks() - hudStart;
//     g_renderTotalTicks += get_ticks() - renderStart;

//     // Update performance graph with this frame's render time
//     {
//         uint32_t frameEndTicks = get_ticks();
//         int frameUs = (int)((frameEndTicks - renderStart) * 1000000ULL / TICKS_PER_SECOND);
//         perfGraphData[perfGraphHead] = frameUs;
//         perfGraphHead = (perfGraphHead + 1) % PERF_GRAPH_WIDTH;
//     }

//     // Performance stats (only in debug mode to save HUD rendering cost)
//     if (debugFlyMode) {
//         struct mallinfo mi = mallinfo();
//         int ramUsedKB = mi.uordblks / 1024;
//         int ramTotalKB = 4 * 1024;  // 4MB base RAM (8MB with expansion pak)
//         int ramFreeKB = ramTotalKB - ramUsedKB;
//         if (ramFreeKB < 0) ramFreeKB = 0;

//         // CPU usage based on actual frame time vs budget (33333us at 30fps)
//         float cpuUsePct = (float)g_lastFrameUs / 333.33f;  // 33333us = 100%
//         if (cpuUsePct > 999.0f) cpuUsePct = 999.0f;
//         float cpuFreePct = 100.0f - cpuUsePct;
//         if (cpuFreePct < 0.0f) cpuFreePct = 0.0f;

//         rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 205, 200, "RAM:%4dK/%4dK", ramUsedKB, ramTotalKB);
//         rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 205, 210, "Free:%4dK", ramFreeKB);
//         rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 205, 220, "CPU:%3.0f%% F:%3.0f%%", cpuUsePct, cpuFreePct);

//         // Draw performance graph (frame time history)
//         if (perfGraphEnabled) {
//             int graphX = 10;
//             int graphY = 180;

//             // Draw background
//             rdpq_set_mode_standard();
//             rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
//             rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
//             rdpq_set_prim_color(RGBA32(0, 0, 0, 180));
//             rdpq_fill_rectangle(graphX - 2, graphY - PERF_GRAPH_HEIGHT - 12,
//                                graphX + PERF_GRAPH_WIDTH + 2, graphY + 2);

//             // Draw target line (33.3ms = 30 FPS)
//             rdpq_set_prim_color(RGBA32(0, 255, 0, 128));
//             int targetY = graphY - (PERF_GRAPH_TARGET_US * PERF_GRAPH_HEIGHT / 66666);  // Scale: 66ms = full height
//             rdpq_fill_rectangle(graphX, targetY, graphX + PERF_GRAPH_WIDTH, targetY + 1);

//             // Draw bars
//             for (int i = 0; i < PERF_GRAPH_WIDTH; i++) {
//                 int idx = (perfGraphHead + i) % PERF_GRAPH_WIDTH;
//                 int frameUs = perfGraphData[idx];

//                 // Scale: 0-66666us maps to 0-PERF_GRAPH_HEIGHT
//                 int barHeight = (frameUs * PERF_GRAPH_HEIGHT) / 66666;
//                 if (barHeight > PERF_GRAPH_HEIGHT) barHeight = PERF_GRAPH_HEIGHT;
//                 if (barHeight < 1) barHeight = 1;

//                 // Color based on frame time (green = good, yellow = slow, red = very slow)
//                 uint8_t r, g, b;
//                 if (frameUs < PERF_GRAPH_TARGET_US) {
//                     r = 0; g = 200; b = 0;  // Green - under budget
//                 } else if (frameUs < 50000) {
//                     r = 255; g = 200; b = 0;  // Yellow - over budget
//                 } else {
//                     r = 255; g = 0; b = 0;  // Red - way over budget
//                 }

//                 rdpq_set_prim_color(RGBA32(r, g, b, 255));
//                 rdpq_fill_rectangle(graphX + i, graphY - barHeight, graphX + i + 1, graphY);
//             }

//             // Label
//             rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, graphX, graphY - PERF_GRAPH_HEIGHT - 8, "Frame ms");
//         }
//     }

//     rdpq_sync_full(NULL, NULL);  // Full sync before frame end
//     rdpq_detach_show();
// }
