// ============================================================
// MULTIPLAYER SCENE - 2-Player Splitscreen Co-op
// ============================================================
// Top/bottom split, each player gets 320x120 viewport
// Player 2 has purple tint, individual respawns, shared bolts
// ============================================================

#include <libdragon.h>
#include <fgeom.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include "multiplayer.h"
#include "../scene.h"
#include "../controls.h"
#include "../mapLoader.h"
// Note: mapData.h removed - conflicts with player.h's player_is_dead
#include "../levels.h"
#include "../constants.h"
#include "../save.h"
#include "../collision.h"
#include "../ui.h"
#include "../player.h"
#include "../camera.h"
#include "../hud.h"
#include "../countdown.h"
#include "../celebrate.h"
#include "../effects.h"
#include "../particles.h"
#include "../perf_graph.h"
#include "level_select.h"
// Define DECO_RENDER_OWNER to enable UV scrolling implementations (lava, toxic, etc.)
#define DECO_RENDER_OWNER
#include "../deco_render.h"
// level3_special.h removed - using DECO_LEVEL3_STREAM decoration instead
#include <math.h>
#include <malloc.h>

// Define UI_IMPLEMENTATION before ui.h is used to create global storage
#define UI_IMPLEMENTATION

// Note: skeleton_is_valid is available from mapData.h via levels.h include chain

#define FB_COUNT 3
#define NUM_PLAYERS 2

// Multiplayer performance optimizations - reduced ranges since we render twice
#define MP_VISIBILITY_RANGE 700.0f        // Reduced from 1125 (chunks)
#define MP_DECO_VISIBILITY_RANGE 500.0f   // Reduced from 900 (decorations)
#define MP_MAX_POINT_LIGHTS 2             // Reduced from 8 (expensive per-viewport)
#define MP_SKIP_PARTICLES 1               // Skip particles entirely for perf
#define MP_SKIP_ARC_PREVIEW 0             // Keep arc preview (user requested)
#define MP_SKELETAL_DECO_RANGE 250.0f     // Skip animated decos beyond this (expensive)

// Selected level (set from menu before init) - default to level 2 (index 1)
int multiplayerLevelID = 1;

// Frame counter for culling
static int frameIdx = 0;

// Player struct is defined in player.h - we use that for per-player state
// Physics constants are in player.h (PLAYER_HOP_THRESHOLD, PLAYER_MAX_CHARGE_TIME, etc.)

// Players
static Player players[NUM_PLAYERS];

// Per-player camera state (using camera module)
static CameraState playerCameras[NUM_PLAYERS];

// Per-player HUD state (using HUD module)
static HealthHUD healthHUDs[NUM_PLAYERS];
static ScrewHUD screwHUD;  // Shared screw HUD (bolts are shared)

// Shared state
static MapLoader mapLoader;
static MapRuntime mapRuntime;
static LevelID currentLevel;
static T3DViewport viewports[NUM_PLAYERS];  // One viewport per player for splitscreen

// Player model (shared) and current body part
static T3DModel* playerModel = NULL;
static T3DMat4FP* playerMatFP = NULL;
static int mpBodyPart = 1;  // Current body part for multiplayer (1 = torso default)

// Jump arc prediction visualization
static T3DModel* arcDotModel = NULL;
static T3DMat4FP* arcMatFP = NULL;
static T3DVertPacked* arcDotVerts = NULL;  // Flat-shaded arc dots (avoid CI texture issues)
#define ARC_MAX_DOTS 8  // 6 dots + 1 landing marker + 1 buffer

// Decoration rendering (matrices for decoration models)
static T3DMat4FP* decoMatFP = NULL;

// Level 3 special decorations now use shared level3_special.h module

// Arc physics uses constants from player.h (PLAYER_CHARGE_JUMP_EARLY_BASE, etc.)

// Torso animations (shared between players, skeletons are per-player)
static T3DAnim torsoAnimIdle;
static T3DAnim torsoAnimWalk;
static T3DAnim torsoAnimJumpCharge;
static T3DAnim torsoAnimJumpLaunch;
static T3DAnim torsoAnimJumpLand;
static bool torsoHasAnims = false;

// Level transition state
static bool isTransitioning = false;
static float transitionTimer = 0.0f;
static int targetTransitionLevel = 0;
static int targetTransitionSpawn = 0;
static float fadeAlpha = 0.0f;

// Bolt/screw collection count (shared)
static int boltsCollected = 0;

// Pause menu state
static OptionPrompt pauseMenu;
static OptionPrompt optionsMenu;
static bool isPaused = false;
static bool pauseMenuInitialized = false;
static bool optionsMenuInitialized = false;
static bool isInOptionsMenu = false;
static int pausingPlayer = -1;  // Which player paused the game

// Previous button state for released detection (per player)
static joypad_buttons_t prevHeld[NUM_PLAYERS] = {0};

// HUD state now managed by HUD module (healthHUDs and screwHUD declared above)

// Level banner (shows level name on entry)
static LevelBanner levelBanner;

// Countdown (3-2-1-GO at game start)
static Countdown mpCountdown;

// Celebration (level complete fireworks + UI)
static CelebrateState mpCelebrate;

// Per-player iris state for death effect (effects module)
static IrisState playerIris[NUM_PLAYERS];

// Splitscreen height (each player gets half)
#define SPLIT_HEIGHT (SCREEN_HEIGHT / 2)

// Get buttons JUST RELEASED this frame (for proper charge jump)
static joypad_buttons_t mp_get_buttons_released(int playerIdx, joypad_buttons_t currentHeld) {
    joypad_buttons_t released;
    // Released = was held last frame, not held now
    released.raw = prevHeld[playerIdx].raw & ~currentHeld.raw;
    prevHeld[playerIdx] = currentHeld;
    return released;
}

// Helper to attach animation only if different (avoid expensive re-attach)
static inline void mp_attach_anim(Player* p, T3DAnim* anim) {
    if (p->currentAnim != anim) {
        t3d_anim_attach(anim, &p->skeleton);
        p->currentAnim = anim;
    }
}

// ============================================================
// MULTIPLAYER DAMAGE CALLBACKS
// ============================================================
// These functions are called by decoration callbacks in mapData.h
// when decorations deal damage. They use currentPlayerIndex from
// mapRuntime to know which player to damage.

// Check if current player is dead (called by decoration callbacks)
bool player_is_dead(void) {
    int idx = mapRuntime.currentPlayerIndex;
    if (idx < 0 || idx >= NUM_PLAYERS) return true;
    return players[idx].isDead;
}

// Forward declaration for single-player damage function
extern void singleplayer_take_damage(int damage);

// Deal damage to current player (called by decoration callbacks)
// Dispatches to single-player or multiplayer implementation based on current scene
void player_take_damage(int damage) {
    // Forward to single-player implementation when not in multiplayer mode
    if (get_current_scene() != MULTIPLAYER_SCENE) {
        singleplayer_take_damage(damage);
        return;
    }

    int idx = mapRuntime.currentPlayerIndex;
    if (idx < 0 || idx >= NUM_PLAYERS) return;

    Player* p = &players[idx];

    // Ignore damage while invincible, dead, or hurt
    if (p->isDead || p->invincibilityTimer > 0.0f || p->isHurt) return;

    // Apply damage
    p->health -= damage;
    debugf("MP: Player %d took %d damage (health: %d)\n", idx, damage, p->health);

    // Show health HUD with flash effect
    hud_health_show(&healthHUDs[idx], true);

    if (p->health <= 0) {
        // Player died
        p->health = 0;
        p->isDead = true;
        p->deathTimer = 0.0f;
        debugf("MP: Player %d died!\n", idx);

        // Start iris close effect for this player
        float screenY = (idx == 0) ? SPLIT_HEIGHT / 2 : SPLIT_HEIGHT + SPLIT_HEIGHT / 2;
        effects_iris_start(&playerIris[idx], SCREEN_WIDTH / 2, screenY);
    } else {
        // Hurt state with brief invincibility
        p->isHurt = true;
        p->hurtAnimTime = 0.5f;
        p->invincibilityTimer = 1.0f;

        // Screen shake feedback
        effects_screen_shake(5.0f);
    }
}

// Knockback current player away from a point
void player_knockback(float fromX, float fromZ, float strength) {
    int idx = mapRuntime.currentPlayerIndex;
    if (idx < 0 || idx >= NUM_PLAYERS) return;

    Player* p = &players[idx];
    float dx = p->x - fromX;
    float dz = p->z - fromZ;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist > 0.1f) {
        p->physics.velX = (dx / dist) * strength;
        p->physics.velZ = (dz / dist) * strength;
    }
}

// Squash current player (landing impact visual)
void player_squash(float amount) {
    int idx = mapRuntime.currentPlayerIndex;
    if (idx < 0 || idx >= NUM_PLAYERS) return;

    players[idx].landingSquash = amount;
}

// Initialize a single player
static void init_player(int idx, float startX, float startY, float startZ) {
    Player* p = &players[idx];

    // Position
    p->x = startX;
    p->y = startY;
    p->z = startZ;
    p->groundLevel = startY;
    p->spawnX = startX;
    p->spawnY = startY;
    p->spawnZ = startZ;

    // Physics
    controls_init(&p->config);
    memset(&p->physics, 0, sizeof(PlayerState));
    p->physics.canMove = true;
    p->physics.canRotate = true;
    p->physics.canJump = true;

    // Direction
    p->angle = 0.0f;

    // Animation state
    p->currentAnim = NULL;
    p->isCharging = false;
    p->isJumping = false;
    p->isLanding = false;
    p->jumpAnimPaused = false;
    p->jumpChargeTime = 0.0f;
    p->jumpAimX = 0.0f;
    p->jumpAimY = 0.0f;
    p->jumpPeakY = startY;

    // Squash/stretch
    p->squashScale = 1.0f;
    p->squashVelocity = 0.0f;
    p->landingSquash = 0.0f;
    p->chargeSquash = 0.0f;

    // Coyote/buffer
    p->coyoteTimer = 0.0f;
    p->jumpBufferTimer = 0.0f;

    // Health
    p->health = 3;
    p->maxHealth = 3;
    p->isDead = false;
    p->isHurt = false;
    p->hurtAnimTime = 0.0f;
    p->invincibilityTimer = 0.0f;
    p->deathTimer = 0.0f;
    p->isRespawning = false;
    p->respawnDelayTimer = 0.0f;

    // Camera - position behind and above player
    p->camPos = (T3DVec3){{0, 100.0f, -200.0f}};
    p->camTarget = (T3DVec3){{startX, startY + 50.0f, startZ}};
    p->smoothCamX = startX;
    p->smoothCamY = startY + 50.0f;

    // Controller
    p->port = (idx == 0) ? JOYPAD_PORT_1 : JOYPAD_PORT_2;

    // Color tint (P1 = white/normal, P2 = purple)
    if (idx == 0) {
        p->tint = RGBA32(255, 255, 255, 255);
    } else {
        p->tint = RGBA32(200, 150, 255, 255);  // Purple tint
    }

    // Body part (default to Torso - will be set per-level later)
    p->currentPart = PLAYER_PART_TORSO;
    p->partSwitchCooldown = 0;

    // Create skeleton for this player
    if (playerModel) {
        p->skeleton = t3d_skeleton_create(playerModel);
        p->skeletonBlend = t3d_skeleton_clone(&p->skeleton, false);

        // Create per-player animations based on body part
        if (mpBodyPart == 2) {
            // Arms mode - load arms_* animations from Robo_arms.t3dm
            p->animArmsIdle = t3d_anim_create(playerModel, "arms_idle");
            p->animArmsWalk1 = t3d_anim_create(playerModel, "arms_walk_1");
            p->animArmsWalk2 = t3d_anim_create(playerModel, "arms_walk_2");
            p->animArmsJump = t3d_anim_create(playerModel, "arms_jump");
            p->animArmsJumpLand = t3d_anim_create(playerModel, "arms_jump_land");
            p->animArmsSpin = t3d_anim_create(playerModel, "arms_atk_spin");
            p->animArmsWhip = t3d_anim_create(playerModel, "arms_atk_whip");
            p->animArmsDeath = t3d_anim_create(playerModel, "arms_death");
            p->animArmsPain1 = t3d_anim_create(playerModel, "arms_pain_1");
            p->animArmsPain2 = t3d_anim_create(playerModel, "arms_pain_2");
            p->animArmsSlide = t3d_anim_create(playerModel, "arms_slide");

            // Set looping
            if (p->animArmsIdle.animRef) t3d_anim_set_looping(&p->animArmsIdle, true);
            if (p->animArmsWalk1.animRef) t3d_anim_set_looping(&p->animArmsWalk1, true);
            if (p->animArmsWalk2.animRef) t3d_anim_set_looping(&p->animArmsWalk2, true);
            if (p->animArmsJump.animRef) t3d_anim_set_looping(&p->animArmsJump, false);
            if (p->animArmsJumpLand.animRef) t3d_anim_set_looping(&p->animArmsJumpLand, false);
            if (p->animArmsSpin.animRef) t3d_anim_set_looping(&p->animArmsSpin, false);
            if (p->animArmsWhip.animRef) t3d_anim_set_looping(&p->animArmsWhip, false);
            if (p->animArmsDeath.animRef) t3d_anim_set_looping(&p->animArmsDeath, false);
            if (p->animArmsPain1.animRef) t3d_anim_set_looping(&p->animArmsPain1, false);
            if (p->animArmsPain2.animRef) t3d_anim_set_looping(&p->animArmsPain2, false);
            if (p->animArmsSlide.animRef) t3d_anim_set_looping(&p->animArmsSlide, true);

            p->armsHasAnims = (p->animArmsIdle.animRef != NULL);
            p->isArmsMode = true;
            p->currentPart = PLAYER_PART_ARMS;

            // Copy to generic slots for shared code paths
            p->animIdle = p->animArmsIdle;
            p->animWalk = p->animArmsWalk1;
            p->animJumpLand = p->animArmsJumpLand;

        } else if (mpBodyPart == 3) {
            // Fullbody/LEGS mode - load fb_* animations from Robo_fb.t3dm
            // Basic animations (shared slots)
            p->animIdle = t3d_anim_create(playerModel, "fb_idle");
            p->animWalk = t3d_anim_create(playerModel, "fb_walk");
            p->animJumpLaunch = t3d_anim_create(playerModel, "fb_jump");
            p->animWait = t3d_anim_create(playerModel, "fb_wait");
            p->animPain1 = t3d_anim_create(playerModel, "fb_pain_1");
            p->animPain2 = t3d_anim_create(playerModel, "fb_pain_2");
            p->animDeath = t3d_anim_create(playerModel, "fb_death");
            p->animSlideFront = t3d_anim_create(playerModel, "fb_slide");

            // Fullbody-specific animations
            p->fbAnimRun = t3d_anim_create(playerModel, "fb_run");
            p->fbAnimCrouch = t3d_anim_create(playerModel, "fb_crouch");
            p->fbAnimCrouchJump = t3d_anim_create(playerModel, "fb_crouch_jump");
            p->fbAnimCrouchJumpHover = t3d_anim_create(playerModel, "fb_crouch_jump_hover");
            p->fbAnimSpinAir = t3d_anim_create(playerModel, "fb_spin_air");
            p->fbAnimSpinAtk = t3d_anim_create(playerModel, "fb_spin_atk");
            p->fbAnimSpinCharge = t3d_anim_create(playerModel, "fb_spin_charge");
            p->fbAnimRunNinja = t3d_anim_create(playerModel, "fb_run_ninja");
            p->fbAnimCrouchAttack = t3d_anim_create(playerModel, "fb_crouch_attack");

            // Set looping for basic animations
            if (p->animIdle.animRef) t3d_anim_set_looping(&p->animIdle, true);
            if (p->animWalk.animRef) t3d_anim_set_looping(&p->animWalk, true);
            if (p->animJumpLaunch.animRef) t3d_anim_set_looping(&p->animJumpLaunch, false);
            if (p->animWait.animRef) t3d_anim_set_looping(&p->animWait, false);
            if (p->animPain1.animRef) t3d_anim_set_looping(&p->animPain1, false);
            if (p->animPain2.animRef) t3d_anim_set_looping(&p->animPain2, false);
            if (p->animDeath.animRef) t3d_anim_set_looping(&p->animDeath, false);
            if (p->animSlideFront.animRef) t3d_anim_set_looping(&p->animSlideFront, true);

            // Set looping for fullbody-specific animations
            if (p->fbAnimRun.animRef) t3d_anim_set_looping(&p->fbAnimRun, true);
            if (p->fbAnimCrouch.animRef) t3d_anim_set_looping(&p->fbAnimCrouch, false);
            if (p->fbAnimCrouchJump.animRef) t3d_anim_set_looping(&p->fbAnimCrouchJump, false);
            if (p->fbAnimCrouchJumpHover.animRef) t3d_anim_set_looping(&p->fbAnimCrouchJumpHover, true);
            if (p->fbAnimSpinAir.animRef) t3d_anim_set_looping(&p->fbAnimSpinAir, false);
            if (p->fbAnimSpinAtk.animRef) t3d_anim_set_looping(&p->fbAnimSpinAtk, false);
            if (p->fbAnimSpinCharge.animRef) t3d_anim_set_looping(&p->fbAnimSpinCharge, false);
            if (p->fbAnimRunNinja.animRef) t3d_anim_set_looping(&p->fbAnimRunNinja, true);
            if (p->fbAnimCrouchAttack.animRef) t3d_anim_set_looping(&p->fbAnimCrouchAttack, false);

            p->fbHasAnims = (p->animIdle.animRef != NULL);
            p->torsoHasAnims = p->fbHasAnims;
            p->currentPart = PLAYER_PART_LEGS;

        } else {
            // Torso mode (default, also used for HEAD) - load torso_* from Robo_torso.t3dm
            p->animIdle = t3d_anim_create(playerModel, "torso_idle");
            p->animWalk = t3d_anim_create(playerModel, "torso_walk_fast");
            p->animJumpCharge = t3d_anim_create(playerModel, "torso_jump_charge");
            p->animJumpLaunch = t3d_anim_create(playerModel, "torso_jump_launch");
            p->animJumpLand = t3d_anim_create(playerModel, "torso_jump_land");
            p->animWait = t3d_anim_create(playerModel, "torso_wait");
            p->animPain1 = t3d_anim_create(playerModel, "torso_pain_1");
            p->animPain2 = t3d_anim_create(playerModel, "torso_pain_2");
            p->animDeath = t3d_anim_create(playerModel, "torso_death");
            p->animSlideFront = t3d_anim_create(playerModel, "torso_slide_front");
            p->animSlideFrontRecover = t3d_anim_create(playerModel, "torso_slide_front_recover");
            p->animSlideBack = t3d_anim_create(playerModel, "torso_slide_back");
            p->animSlideBackRecover = t3d_anim_create(playerModel, "torso_slide_back_recover");

            // Set looping
            if (p->animIdle.animRef) t3d_anim_set_looping(&p->animIdle, true);
            if (p->animWalk.animRef) t3d_anim_set_looping(&p->animWalk, true);
            if (p->animJumpCharge.animRef) t3d_anim_set_looping(&p->animJumpCharge, false);
            if (p->animJumpLaunch.animRef) t3d_anim_set_looping(&p->animJumpLaunch, false);
            if (p->animJumpLand.animRef) t3d_anim_set_looping(&p->animJumpLand, false);
            if (p->animWait.animRef) t3d_anim_set_looping(&p->animWait, false);
            if (p->animPain1.animRef) t3d_anim_set_looping(&p->animPain1, false);
            if (p->animPain2.animRef) t3d_anim_set_looping(&p->animPain2, false);
            if (p->animDeath.animRef) t3d_anim_set_looping(&p->animDeath, false);
            if (p->animSlideFront.animRef) t3d_anim_set_looping(&p->animSlideFront, false);
            if (p->animSlideFrontRecover.animRef) t3d_anim_set_looping(&p->animSlideFrontRecover, false);
            if (p->animSlideBack.animRef) t3d_anim_set_looping(&p->animSlideBack, false);
            if (p->animSlideBackRecover.animRef) t3d_anim_set_looping(&p->animSlideBackRecover, false);

            p->torsoHasAnims = (p->animIdle.animRef != NULL);
            p->currentPart = (mpBodyPart == 0) ? PLAYER_PART_HEAD : PLAYER_PART_TORSO;
        }

        // Start with idle animation (use mode-specific idle)
        T3DAnim* startAnim = (mpBodyPart == 2) ? &p->animArmsIdle : &p->animIdle;
        if (startAnim->animRef != NULL) {
            t3d_anim_attach(startAnim, &p->skeleton);
            p->currentAnim = startAnim;
            p->attachedAnim = startAnim;
            t3d_anim_set_time(startAnim, 0.0f);
            t3d_anim_set_playing(startAnim, true);
            // Initialize bone poses to avoid denormal values
            t3d_anim_update(startAnim, 0.0f);
            t3d_skeleton_update(&p->skeleton);
            const char* modeNames[] = {"head", "torso", "arms", "fullbody"};
            debugf("MP: Player %d %s animations initialized\n", idx, modeNames[mpBodyPart]);
        } else {
            const char* modeNames[] = {"head", "torso", "arms", "fullbody"};
            debugf("MP: WARNING - Player %d %s_idle animation not found!\n", idx, modeNames[mpBodyPart]);
        }
    }
}

// Reload player model and animations when body part changes during level transition
static void reload_player_model_for_body_part(int newBodyPart) {
    if (newBodyPart == mpBodyPart) return;  // No change needed

    debugf("MP: Body part changing from %d to %d, reloading model/anims\n", mpBodyPart, newBodyPart);

    // Wait for RSP/RDP to finish before freeing resources
    rspq_wait();

    // Destroy old animations for all players
    for (int i = 0; i < NUM_PLAYERS; i++) {
        Player* p = &players[i];

        // Destroy torso/generic mode animations
        if (p->animIdle.animRef) { t3d_anim_destroy(&p->animIdle); memset(&p->animIdle, 0, sizeof(T3DAnim)); }
        if (p->animWalk.animRef) { t3d_anim_destroy(&p->animWalk); memset(&p->animWalk, 0, sizeof(T3DAnim)); }
        if (p->animJumpCharge.animRef) { t3d_anim_destroy(&p->animJumpCharge); memset(&p->animJumpCharge, 0, sizeof(T3DAnim)); }
        if (p->animJumpLaunch.animRef) { t3d_anim_destroy(&p->animJumpLaunch); memset(&p->animJumpLaunch, 0, sizeof(T3DAnim)); }
        if (p->animJumpLand.animRef) { t3d_anim_destroy(&p->animJumpLand); memset(&p->animJumpLand, 0, sizeof(T3DAnim)); }
        if (p->animWait.animRef) { t3d_anim_destroy(&p->animWait); memset(&p->animWait, 0, sizeof(T3DAnim)); }
        if (p->animPain1.animRef) { t3d_anim_destroy(&p->animPain1); memset(&p->animPain1, 0, sizeof(T3DAnim)); }
        if (p->animPain2.animRef) { t3d_anim_destroy(&p->animPain2); memset(&p->animPain2, 0, sizeof(T3DAnim)); }
        if (p->animDeath.animRef) { t3d_anim_destroy(&p->animDeath); memset(&p->animDeath, 0, sizeof(T3DAnim)); }
        if (p->animSlideFront.animRef) { t3d_anim_destroy(&p->animSlideFront); memset(&p->animSlideFront, 0, sizeof(T3DAnim)); }
        if (p->animSlideFrontRecover.animRef) { t3d_anim_destroy(&p->animSlideFrontRecover); memset(&p->animSlideFrontRecover, 0, sizeof(T3DAnim)); }
        if (p->animSlideBack.animRef) { t3d_anim_destroy(&p->animSlideBack); memset(&p->animSlideBack, 0, sizeof(T3DAnim)); }
        if (p->animSlideBackRecover.animRef) { t3d_anim_destroy(&p->animSlideBackRecover); memset(&p->animSlideBackRecover, 0, sizeof(T3DAnim)); }

        // Destroy arms mode animations
        if (p->animArmsIdle.animRef) { t3d_anim_destroy(&p->animArmsIdle); memset(&p->animArmsIdle, 0, sizeof(T3DAnim)); }
        if (p->animArmsWalk1.animRef) { t3d_anim_destroy(&p->animArmsWalk1); memset(&p->animArmsWalk1, 0, sizeof(T3DAnim)); }
        if (p->animArmsWalk2.animRef) { t3d_anim_destroy(&p->animArmsWalk2); memset(&p->animArmsWalk2, 0, sizeof(T3DAnim)); }
        if (p->animArmsJump.animRef) { t3d_anim_destroy(&p->animArmsJump); memset(&p->animArmsJump, 0, sizeof(T3DAnim)); }
        if (p->animArmsJumpLand.animRef) { t3d_anim_destroy(&p->animArmsJumpLand); memset(&p->animArmsJumpLand, 0, sizeof(T3DAnim)); }
        if (p->animArmsSpin.animRef) { t3d_anim_destroy(&p->animArmsSpin); memset(&p->animArmsSpin, 0, sizeof(T3DAnim)); }
        if (p->animArmsWhip.animRef) { t3d_anim_destroy(&p->animArmsWhip); memset(&p->animArmsWhip, 0, sizeof(T3DAnim)); }
        if (p->animArmsDeath.animRef) { t3d_anim_destroy(&p->animArmsDeath); memset(&p->animArmsDeath, 0, sizeof(T3DAnim)); }
        if (p->animArmsPain1.animRef) { t3d_anim_destroy(&p->animArmsPain1); memset(&p->animArmsPain1, 0, sizeof(T3DAnim)); }
        if (p->animArmsPain2.animRef) { t3d_anim_destroy(&p->animArmsPain2); memset(&p->animArmsPain2, 0, sizeof(T3DAnim)); }
        if (p->animArmsSlide.animRef) { t3d_anim_destroy(&p->animArmsSlide); memset(&p->animArmsSlide, 0, sizeof(T3DAnim)); }

        // Destroy fullbody-specific animations
        if (p->fbAnimRun.animRef) { t3d_anim_destroy(&p->fbAnimRun); memset(&p->fbAnimRun, 0, sizeof(T3DAnim)); }
        if (p->fbAnimCrouch.animRef) { t3d_anim_destroy(&p->fbAnimCrouch); memset(&p->fbAnimCrouch, 0, sizeof(T3DAnim)); }
        if (p->fbAnimCrouchJump.animRef) { t3d_anim_destroy(&p->fbAnimCrouchJump); memset(&p->fbAnimCrouchJump, 0, sizeof(T3DAnim)); }
        if (p->fbAnimCrouchJumpHover.animRef) { t3d_anim_destroy(&p->fbAnimCrouchJumpHover); memset(&p->fbAnimCrouchJumpHover, 0, sizeof(T3DAnim)); }
        if (p->fbAnimSpinAir.animRef) { t3d_anim_destroy(&p->fbAnimSpinAir); memset(&p->fbAnimSpinAir, 0, sizeof(T3DAnim)); }
        if (p->fbAnimSpinAtk.animRef) { t3d_anim_destroy(&p->fbAnimSpinAtk); memset(&p->fbAnimSpinAtk, 0, sizeof(T3DAnim)); }
        if (p->fbAnimSpinCharge.animRef) { t3d_anim_destroy(&p->fbAnimSpinCharge); memset(&p->fbAnimSpinCharge, 0, sizeof(T3DAnim)); }
        if (p->fbAnimRunNinja.animRef) { t3d_anim_destroy(&p->fbAnimRunNinja); memset(&p->fbAnimRunNinja, 0, sizeof(T3DAnim)); }
        if (p->fbAnimCrouchAttack.animRef) { t3d_anim_destroy(&p->fbAnimCrouchAttack); memset(&p->fbAnimCrouchAttack, 0, sizeof(T3DAnim)); }

        // Destroy skeletons
        t3d_skeleton_destroy(&p->skeleton);
        t3d_skeleton_destroy(&p->skeletonBlend);
        p->currentAnim = NULL;
        p->attachedAnim = NULL;
    }

    // Free old player model
    if (playerModel) {
        t3d_model_free(playerModel);
        playerModel = NULL;
    }

    // Update body part
    mpBodyPart = newBodyPart;

    // Load new model
    const char* modelPath;
    if (mpBodyPart == 2) {
        modelPath = "rom:/Robo_arms.t3dm";
    } else if (mpBodyPart == 3) {
        modelPath = "rom:/Robo_fb.t3dm";
    } else {
        modelPath = "rom:/Robo_torso.t3dm";  // HEAD and TORSO use torso model
    }
    debugf("MP: Loading new model %s for body part %d\n", modelPath, mpBodyPart);
    playerModel = t3d_model_load(modelPath);
    torsoModel = playerModel;  // Update shared model pointer

    // Recreate skeletons and animations for all players
    for (int i = 0; i < NUM_PLAYERS; i++) {
        Player* p = &players[i];

        // Create new skeleton
        p->skeleton = t3d_skeleton_create(playerModel);
        p->skeletonBlend = t3d_skeleton_clone(&p->skeleton, false);

        // Create animations based on new body part (same logic as init_player)
        if (mpBodyPart == 2) {
            // Arms mode
            p->animArmsIdle = t3d_anim_create(playerModel, "arms_idle");
            p->animArmsWalk1 = t3d_anim_create(playerModel, "arms_walk_1");
            p->animArmsWalk2 = t3d_anim_create(playerModel, "arms_walk_2");
            p->animArmsJump = t3d_anim_create(playerModel, "arms_jump");
            p->animArmsJumpLand = t3d_anim_create(playerModel, "arms_jump_land");
            p->animArmsSpin = t3d_anim_create(playerModel, "arms_atk_spin");
            p->animArmsWhip = t3d_anim_create(playerModel, "arms_atk_whip");
            p->animArmsDeath = t3d_anim_create(playerModel, "arms_death");
            p->animArmsPain1 = t3d_anim_create(playerModel, "arms_pain_1");
            p->animArmsPain2 = t3d_anim_create(playerModel, "arms_pain_2");
            p->animArmsSlide = t3d_anim_create(playerModel, "arms_slide");

            if (p->animArmsIdle.animRef) t3d_anim_set_looping(&p->animArmsIdle, true);
            if (p->animArmsWalk1.animRef) t3d_anim_set_looping(&p->animArmsWalk1, true);
            if (p->animArmsWalk2.animRef) t3d_anim_set_looping(&p->animArmsWalk2, true);
            if (p->animArmsJump.animRef) t3d_anim_set_looping(&p->animArmsJump, false);
            if (p->animArmsJumpLand.animRef) t3d_anim_set_looping(&p->animArmsJumpLand, false);
            if (p->animArmsSpin.animRef) t3d_anim_set_looping(&p->animArmsSpin, false);
            if (p->animArmsWhip.animRef) t3d_anim_set_looping(&p->animArmsWhip, false);
            if (p->animArmsDeath.animRef) t3d_anim_set_looping(&p->animArmsDeath, false);
            if (p->animArmsPain1.animRef) t3d_anim_set_looping(&p->animArmsPain1, false);
            if (p->animArmsPain2.animRef) t3d_anim_set_looping(&p->animArmsPain2, false);
            if (p->animArmsSlide.animRef) t3d_anim_set_looping(&p->animArmsSlide, true);

            p->armsHasAnims = (p->animArmsIdle.animRef != NULL);
            p->isArmsMode = true;
            p->currentPart = PLAYER_PART_ARMS;
            p->animIdle = p->animArmsIdle;
            p->animWalk = p->animArmsWalk1;
            p->animJumpLand = p->animArmsJumpLand;

        } else if (mpBodyPart == 3) {
            // Fullbody/LEGS mode
            p->animIdle = t3d_anim_create(playerModel, "fb_idle");
            p->animWalk = t3d_anim_create(playerModel, "fb_walk");
            p->animJumpLaunch = t3d_anim_create(playerModel, "fb_jump");
            p->animWait = t3d_anim_create(playerModel, "fb_wait");
            p->animPain1 = t3d_anim_create(playerModel, "fb_pain_1");
            p->animPain2 = t3d_anim_create(playerModel, "fb_pain_2");
            p->animDeath = t3d_anim_create(playerModel, "fb_death");
            p->animSlideFront = t3d_anim_create(playerModel, "fb_slide");

            p->fbAnimRun = t3d_anim_create(playerModel, "fb_run");
            p->fbAnimCrouch = t3d_anim_create(playerModel, "fb_crouch");
            p->fbAnimCrouchJump = t3d_anim_create(playerModel, "fb_crouch_jump");
            p->fbAnimCrouchJumpHover = t3d_anim_create(playerModel, "fb_crouch_jump_hover");
            p->fbAnimSpinAir = t3d_anim_create(playerModel, "fb_spin_air");
            p->fbAnimSpinAtk = t3d_anim_create(playerModel, "fb_spin_atk");
            p->fbAnimSpinCharge = t3d_anim_create(playerModel, "fb_spin_charge");
            p->fbAnimRunNinja = t3d_anim_create(playerModel, "fb_run_ninja");
            p->fbAnimCrouchAttack = t3d_anim_create(playerModel, "fb_crouch_attack");

            if (p->animIdle.animRef) t3d_anim_set_looping(&p->animIdle, true);
            if (p->animWalk.animRef) t3d_anim_set_looping(&p->animWalk, true);
            if (p->animJumpLaunch.animRef) t3d_anim_set_looping(&p->animJumpLaunch, false);
            if (p->animWait.animRef) t3d_anim_set_looping(&p->animWait, false);
            if (p->animPain1.animRef) t3d_anim_set_looping(&p->animPain1, false);
            if (p->animPain2.animRef) t3d_anim_set_looping(&p->animPain2, false);
            if (p->animDeath.animRef) t3d_anim_set_looping(&p->animDeath, false);
            if (p->animSlideFront.animRef) t3d_anim_set_looping(&p->animSlideFront, true);

            if (p->fbAnimRun.animRef) t3d_anim_set_looping(&p->fbAnimRun, true);
            if (p->fbAnimCrouch.animRef) t3d_anim_set_looping(&p->fbAnimCrouch, false);
            if (p->fbAnimCrouchJump.animRef) t3d_anim_set_looping(&p->fbAnimCrouchJump, false);
            if (p->fbAnimCrouchJumpHover.animRef) t3d_anim_set_looping(&p->fbAnimCrouchJumpHover, true);
            if (p->fbAnimSpinAir.animRef) t3d_anim_set_looping(&p->fbAnimSpinAir, false);
            if (p->fbAnimSpinAtk.animRef) t3d_anim_set_looping(&p->fbAnimSpinAtk, false);
            if (p->fbAnimSpinCharge.animRef) t3d_anim_set_looping(&p->fbAnimSpinCharge, false);
            if (p->fbAnimRunNinja.animRef) t3d_anim_set_looping(&p->fbAnimRunNinja, true);
            if (p->fbAnimCrouchAttack.animRef) t3d_anim_set_looping(&p->fbAnimCrouchAttack, false);

            p->fbHasAnims = (p->animIdle.animRef != NULL);
            p->torsoHasAnims = p->fbHasAnims;
            p->currentPart = PLAYER_PART_LEGS;
            p->isArmsMode = false;

        } else {
            // Torso mode (default, also used for HEAD)
            p->animIdle = t3d_anim_create(playerModel, "torso_idle");
            p->animWalk = t3d_anim_create(playerModel, "torso_walk_fast");
            p->animJumpCharge = t3d_anim_create(playerModel, "torso_jump_charge");
            p->animJumpLaunch = t3d_anim_create(playerModel, "torso_jump_launch");
            p->animJumpLand = t3d_anim_create(playerModel, "torso_jump_land");
            p->animWait = t3d_anim_create(playerModel, "torso_wait");
            p->animPain1 = t3d_anim_create(playerModel, "torso_pain_1");
            p->animPain2 = t3d_anim_create(playerModel, "torso_pain_2");
            p->animDeath = t3d_anim_create(playerModel, "torso_death");
            p->animSlideFront = t3d_anim_create(playerModel, "torso_slide_front");
            p->animSlideFrontRecover = t3d_anim_create(playerModel, "torso_slide_front_recover");
            p->animSlideBack = t3d_anim_create(playerModel, "torso_slide_back");
            p->animSlideBackRecover = t3d_anim_create(playerModel, "torso_slide_back_recover");

            if (p->animIdle.animRef) t3d_anim_set_looping(&p->animIdle, true);
            if (p->animWalk.animRef) t3d_anim_set_looping(&p->animWalk, true);
            if (p->animJumpCharge.animRef) t3d_anim_set_looping(&p->animJumpCharge, false);
            if (p->animJumpLaunch.animRef) t3d_anim_set_looping(&p->animJumpLaunch, false);
            if (p->animJumpLand.animRef) t3d_anim_set_looping(&p->animJumpLand, false);
            if (p->animWait.animRef) t3d_anim_set_looping(&p->animWait, false);
            if (p->animPain1.animRef) t3d_anim_set_looping(&p->animPain1, false);
            if (p->animPain2.animRef) t3d_anim_set_looping(&p->animPain2, false);
            if (p->animDeath.animRef) t3d_anim_set_looping(&p->animDeath, false);
            if (p->animSlideFront.animRef) t3d_anim_set_looping(&p->animSlideFront, false);
            if (p->animSlideFrontRecover.animRef) t3d_anim_set_looping(&p->animSlideFrontRecover, false);
            if (p->animSlideBack.animRef) t3d_anim_set_looping(&p->animSlideBack, false);
            if (p->animSlideBackRecover.animRef) t3d_anim_set_looping(&p->animSlideBackRecover, false);

            p->torsoHasAnims = (p->animIdle.animRef != NULL);
            p->currentPart = (mpBodyPart == 0) ? PLAYER_PART_HEAD : PLAYER_PART_TORSO;
            p->isArmsMode = false;
        }

        // Attach idle animation
        T3DAnim* startAnim = (mpBodyPart == 2) ? &p->animArmsIdle : &p->animIdle;
        if (startAnim->animRef != NULL) {
            t3d_anim_attach(startAnim, &p->skeleton);
            p->currentAnim = startAnim;
            p->attachedAnim = startAnim;
            t3d_anim_set_time(startAnim, 0.0f);
            t3d_anim_set_playing(startAnim, true);
            t3d_anim_update(startAnim, 0.0f);
            t3d_skeleton_update(&p->skeleton);
        }
    }

    debugf("MP: Body part reload complete\n");
}

// Level 3 special decorations now use shared level3_special.h module

// Note: fpu_flush_denormals() is now defined in mapData.h (included via levels.h)

void init_multiplayer_scene(void) {
    // Ensure FPU flushes denormals to zero
    fpu_flush_denormals();

    // Zero player structs to prevent denormal floats from uninitialized memory
    memset(players, 0, sizeof(players));

    // Zero T3D animation structures to prevent denormal floats
    memset(&torsoAnimIdle, 0, sizeof(T3DAnim));
    memset(&torsoAnimWalk, 0, sizeof(T3DAnim));
    memset(&torsoAnimJumpCharge, 0, sizeof(T3DAnim));
    memset(&torsoAnimJumpLaunch, 0, sizeof(T3DAnim));
    memset(&torsoAnimJumpLand, 0, sizeof(T3DAnim));

    debugf("Multiplayer scene init\n");

    // RDP debug disabled - was catching CI4 TLUT quirks that libdragon handles internally
    // rdpq_debug_start();

    // Set current level
    currentLevel = (LevelID)multiplayerLevelID;

    // T3D should already be initialized by main

    // Create viewports - one for each player half of the screen
    // Player 0 (top): y=0 to SPLIT_HEIGHT
    // Player 1 (bottom): y=SPLIT_HEIGHT to SCREEN_HEIGHT
    for (int i = 0; i < NUM_PLAYERS; i++) {
        viewports[i] = t3d_viewport_create();
        int y_offset = i * SPLIT_HEIGHT;
        t3d_viewport_set_area(&viewports[i], 0, y_offset, SCREEN_WIDTH, SPLIT_HEIGHT);
    }

    // Initialize map loader and runtime
    maploader_init(&mapLoader, FB_COUNT, MP_VISIBILITY_RANGE);
    map_runtime_init(&mapRuntime, FB_COUNT, MP_VISIBILITY_RANGE);

    // Load level
    level_load(currentLevel, &mapLoader, &mapRuntime);

    // Load level 3 special decorations (stream + water level at origin)
    // level3_special_load removed

    // Get player start position - find from DECO_PLAYERSPAWN decoration (like game.c)
    float startX, startY, startZ;
    level_get_player_start(currentLevel, &startX, &startY, &startZ);  // Fallback

    // Find first PlayerSpawn decoration and use it as actual spawn point
    bool foundSpawn = false;
    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (deco->type == DECO_PLAYERSPAWN && deco->active) {
            startX = deco->posX;
            startY = deco->posY;
            startZ = deco->posZ;
            foundSpawn = true;
            debugf("MP: PlayerSpawn found at: (%.1f, %.1f, %.1f)\n", startX, startY, startZ);
            break;
        }
    }
    if (!foundSpawn) {
        debugf("MP: No PlayerSpawn found, using level default position\n");
    }

    // Load player model based on level's body part
    // 0 = head (uses torso model), 1 = torso, 2 = arms, 3 = legs (fullbody)
    mpBodyPart = level_get_body_part(currentLevel);
    const char* modelPath;
    if (mpBodyPart == 2) {
        modelPath = "rom:/Robo_arms.t3dm";
    } else if (mpBodyPart == 3) {
        modelPath = "rom:/Robo_fb.t3dm";
    } else {
        modelPath = "rom:/Robo_torso.t3dm";  // HEAD and TORSO use torso model
    }
    debugf("MP: Loading model %s for body part %d\n", modelPath, mpBodyPart);
    playerModel = t3d_model_load(modelPath);
    torsoModel = playerModel;  // Share with mapData.h for DECO_TORSO_PICKUP
    playerMatFP = malloc_uncached(sizeof(T3DMat4FP) * NUM_PLAYERS);

    // Allocate arc visualization resources (BlueCube is simpler/safer than TransitionCollision)
    arcDotModel = t3d_model_load("rom:/BlueCube.t3dm");
    arcMatFP = malloc_uncached(sizeof(T3DMat4FP) * ARC_MAX_DOTS);

    // Allocate arc dot vertices for cube rendering (8 vertices for a cube, matching game.c)
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

    // Allocate decoration matrices (extra slots for multi-part decorations like turrets)
    decoMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT * (MAX_DECORATIONS + 60));

    // Per-player animations are created in init_player (no global animations needed)
    // This saves file handles for decoration loading
    torsoHasAnims = true;  // Will be verified by per-player init

    // Initialize players (P2 offset slightly to the side)
    init_player(0, startX - 30.0f, startY, startZ);
    init_player(1, startX + 30.0f, startY, startZ);

    // Force initial chunk loading at spawn position
    maploader_update_visibility(&mapLoader, startX, startZ);
    debugf("MP: Initial chunks loaded at spawn (%.0f, %.0f)\n", startX, startZ);

    // Reset transition state
    isTransitioning = false;
    transitionTimer = 0.0f;
    fadeAlpha = 0.0f;
    boltsCollected = 0;

    // Initialize HUD using HUD module
    for (int i = 0; i < NUM_PLAYERS; i++) {
        hud_health_init(&healthHUDs[i], 3);  // 3 max health
        // Force show health HUD for multiplayer (always visible)
        hud_health_show(&healthHUDs[i], false);
    }
    hud_screw_init(&screwHUD);

    // Initialize camera module for each player (with multiplayer zoom)
    for (int i = 0; i < NUM_PLAYERS; i++) {
        float offsetX = (i == 0) ? -30.0f : 30.0f;
        camera_init(&playerCameras[i], startX + offsetX, startY, startZ);
        camera_set_z_offset(&playerCameras[i], CAM_Z_OFFSET_MULTIPLAYER);
    }

    // Initialize level banner and show it
    level_banner_init(&levelBanner);
    level_banner_show(&levelBanner, get_level_name_with_number(currentLevel));

    // Load UI sprites for banner borders
    ui_load_sprites();

    // Initialize and start countdown (3-2-1-GO!)
    countdown_init(&mpCountdown);
    countdown_start(&mpCountdown);

    // Initialize celebration system
    celebrate_init(&mpCelebrate);
    celebrate_load_sounds();  // Firework sounds

    // Initialize effects system (screen shake, flash, etc.)
    effects_init();
    for (int i = 0; i < NUM_PLAYERS; i++) {
        effects_iris_init(&playerIris[i]);
    }

    // Initialize particles system
    particles_init();

    // Initialize performance graph (debug)
    perf_graph_init();

    debugf("Multiplayer: 2 players initialized\n");
}

void deinit_multiplayer_scene(void) {
    debugf("Multiplayer scene deinit\n");

    // CRITICAL: Wait for BOTH RSP and RDP to finish ALL operations before freeing resources.
    // rdpq_fence() does NOT block the CPU - it only schedules a fence command in the queue.
    // rspq_wait() actually blocks and waits for both RSP and RDP to complete all queued work.
    rspq_wait();

    // Free lazy-loaded UI sounds to reduce open wav64 file count
    ui_free_key_sounds();

    // Destroy per-player skeletons and animations
    for (int i = 0; i < NUM_PLAYERS; i++) {
        // Destroy torso/fullbody mode animations
        if (players[i].animIdle.animRef) t3d_anim_destroy(&players[i].animIdle);
        if (players[i].animWalk.animRef) t3d_anim_destroy(&players[i].animWalk);
        if (players[i].animJumpCharge.animRef) t3d_anim_destroy(&players[i].animJumpCharge);
        if (players[i].animJumpLaunch.animRef) t3d_anim_destroy(&players[i].animJumpLaunch);
        if (players[i].animJumpLand.animRef) t3d_anim_destroy(&players[i].animJumpLand);
        if (players[i].animWait.animRef) t3d_anim_destroy(&players[i].animWait);
        if (players[i].animPain1.animRef) t3d_anim_destroy(&players[i].animPain1);
        if (players[i].animPain2.animRef) t3d_anim_destroy(&players[i].animPain2);
        if (players[i].animDeath.animRef) t3d_anim_destroy(&players[i].animDeath);
        if (players[i].animSlideFront.animRef) t3d_anim_destroy(&players[i].animSlideFront);
        if (players[i].animSlideFrontRecover.animRef) t3d_anim_destroy(&players[i].animSlideFrontRecover);
        if (players[i].animSlideBack.animRef) t3d_anim_destroy(&players[i].animSlideBack);
        if (players[i].animSlideBackRecover.animRef) t3d_anim_destroy(&players[i].animSlideBackRecover);

        // Destroy arms mode animations
        if (players[i].animArmsIdle.animRef) t3d_anim_destroy(&players[i].animArmsIdle);
        if (players[i].animArmsWalk1.animRef) t3d_anim_destroy(&players[i].animArmsWalk1);
        if (players[i].animArmsWalk2.animRef) t3d_anim_destroy(&players[i].animArmsWalk2);
        if (players[i].animArmsJump.animRef) t3d_anim_destroy(&players[i].animArmsJump);
        if (players[i].animArmsJumpLand.animRef) t3d_anim_destroy(&players[i].animArmsJumpLand);
        if (players[i].animArmsSpin.animRef) t3d_anim_destroy(&players[i].animArmsSpin);
        if (players[i].animArmsWhip.animRef) t3d_anim_destroy(&players[i].animArmsWhip);
        if (players[i].animArmsDeath.animRef) t3d_anim_destroy(&players[i].animArmsDeath);
        if (players[i].animArmsPain1.animRef) t3d_anim_destroy(&players[i].animArmsPain1);
        if (players[i].animArmsPain2.animRef) t3d_anim_destroy(&players[i].animArmsPain2);
        if (players[i].animArmsSlide.animRef) t3d_anim_destroy(&players[i].animArmsSlide);

        // Destroy fullbody-specific animations
        if (players[i].fbAnimRun.animRef) t3d_anim_destroy(&players[i].fbAnimRun);
        if (players[i].fbAnimCrouch.animRef) t3d_anim_destroy(&players[i].fbAnimCrouch);
        if (players[i].fbAnimCrouchJump.animRef) t3d_anim_destroy(&players[i].fbAnimCrouchJump);
        if (players[i].fbAnimCrouchJumpHover.animRef) t3d_anim_destroy(&players[i].fbAnimCrouchJumpHover);
        if (players[i].fbAnimSpinAir.animRef) t3d_anim_destroy(&players[i].fbAnimSpinAir);
        if (players[i].fbAnimSpinAtk.animRef) t3d_anim_destroy(&players[i].fbAnimSpinAtk);
        if (players[i].fbAnimSpinCharge.animRef) t3d_anim_destroy(&players[i].fbAnimSpinCharge);
        if (players[i].fbAnimRunNinja.animRef) t3d_anim_destroy(&players[i].fbAnimRunNinja);
        if (players[i].fbAnimCrouchAttack.animRef) t3d_anim_destroy(&players[i].fbAnimCrouchAttack);

        // Destroy skeletons
        t3d_skeleton_destroy(&players[i].skeleton);
        t3d_skeleton_destroy(&players[i].skeletonBlend);
    }

    // NOTE: Global torso animations removed - per-player animations are cleaned up above

    // Free player model
    if (playerModel) {
        t3d_model_free(playerModel);
        playerModel = NULL;
    }

    // Free matrices
    if (playerMatFP) {
        free_uncached(playerMatFP);
        playerMatFP = NULL;
    }

    // Free arc model and matrices
    if (arcDotModel) {
        t3d_model_free(arcDotModel);
        arcDotModel = NULL;
    }
    if (arcMatFP) {
        free_uncached(arcMatFP);
        arcMatFP = NULL;
    }
    if (arcDotVerts) {
        free_uncached(arcDotVerts);
        arcDotVerts = NULL;
    }
    if (decoMatFP) {
        free_uncached(decoMatFP);
        decoMatFP = NULL;
    }

    // Cleanup level 3 special decorations
    // level3_special_free removed

    // Clean up map
    maploader_free(&mapLoader);
    map_runtime_free(&mapRuntime);
    collision_loader_free_all();  // Free collision meshes to prevent stale pointer reuse

    // Free HUD resources using HUD module
    for (int i = 0; i < NUM_PLAYERS; i++) {
        hud_health_deinit(&healthHUDs[i]);
    }
    hud_screw_deinit(&screwHUD);

    // NOTE: Don't free UI sprites - they're shared globally and other scenes need them
    // ui_free_sprites();

    // Free countdown resources
    countdown_deinit(&mpCountdown);

    // Stop music and unload sounds
    level_stop_music();
    celebrate_unload_sounds();  // Firework sounds
}

// Check if player hit a transition trigger
static void check_transitions(int idx) {
    Player* p = &players[idx];

    // PERF: Single loop for both transitions and bolts (was 2 separate loops)
    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (!deco->active) continue;

        // Early distance check (both types need distance)
        float dx = p->x - deco->posX;
        float dy = p->y - deco->posY;
        float dz = p->z - deco->posZ;
        float distSq = dx*dx + dy*dy + dz*dz;

        // Check transitions (radius 80, distSq < 6400)
        if (deco->type == DECO_TRANSITIONCOLLISION && !deco->state.transition.triggered && distSq < 6400.0f) {
            int targetLevel = deco->state.transition.targetLevel;
            int targetSpawn = deco->state.transition.targetSpawn;
            deco->state.transition.triggered = true;

            if (!celebrate_is_active(&mpCelebrate)) {
                float celebX = (players[0].x + players[1].x) * 0.5f;
                float celebY = (players[0].y + players[1].y) * 0.5f;
                float celebZ = (players[0].z + players[1].z) * 0.5f;
                int totalBolts = get_level_bolt_count(currentLevel);
                celebrate_start(&mpCelebrate, celebX, celebY, celebZ,
                               boltsCollected, totalBolts, 0, 0.0f,
                               targetLevel, targetSpawn);
                debugf("MP: Player %d triggered celebration for level %d\n", idx, targetLevel);
            }
        }
        // Check bolts (radius 50, distSq < 2500)
        else if (deco->type == DECO_BOLT && distSq < 2500.0f) {
            deco->active = false;
            boltsCollected++;
            save_collect_bolt(currentLevel, deco->state.bolt.saveIndex);
            effects_bolt_flash();
            particles_spawn_sparks(deco->posX, deco->posY, deco->posZ, 8);
            hud_screw_show(&screwHUD, true);
            debugf("MP: Player %d collected bolt (total: %d)\n", idx, boltsCollected);
        }
    }
}

// Forward declarations for menu callbacks
static void mp_show_pause_menu(int playerIdx);
static void mp_show_options_menu(void);

// Helper to update volume option labels in-place
static void mp_update_options_volume_labels(void) {
    snprintf(optionsMenu.options[0], UI_MAX_OPTION_LENGTH, "Music Volume: %d", save_get_music_volume());
    snprintf(optionsMenu.options[1], UI_MAX_OPTION_LENGTH, "SFX Volume: %d", save_get_sfx_volume());
}

// Options menu - adjust volumes with left/right
static void mp_on_options_leftright(int value) {
    int direction;
    int index = option_decode_leftright(value, &direction);

    if (index == 0) {
        // Music volume
        int vol = save_get_music_volume() + direction;
        if (vol < 0) vol = 0;
        if (vol > 10) vol = 10;
        save_set_music_volume(vol);
        mp_update_options_volume_labels();
    } else if (index == 1) {
        // SFX volume
        int vol = save_get_sfx_volume() + direction;
        if (vol < 0) vol = 0;
        if (vol > 10) vol = 10;
        save_set_sfx_volume(vol);
        mp_update_options_volume_labels();
        ui_play_hover_sound();
    }
}

// Options menu select callback
static void mp_on_options_select(int choice) {
    if (choice == 2) {  // Back
        save_write_settings();
        isInOptionsMenu = false;
        mp_show_pause_menu(pausingPlayer);
    }
}

// Options menu cancel callback (B button)
static void mp_on_options_cancel(int choice) {
    (void)choice;
    save_write_settings();
    isInOptionsMenu = false;
    mp_show_pause_menu(pausingPlayer);
}

// Show options submenu
static void mp_show_options_menu(void) {
    if (!optionsMenuInitialized) {
        option_init(&optionsMenu);
        optionsMenuInitialized = true;
    }

    char musicLabel[32], sfxLabel[32];
    snprintf(musicLabel, sizeof(musicLabel), "Music Volume: %d", save_get_music_volume());
    snprintf(sfxLabel, sizeof(sfxLabel), "SFX Volume: %d", save_get_sfx_volume());

    option_set_title(&optionsMenu, "Options");
    option_add(&optionsMenu, musicLabel);
    option_add(&optionsMenu, sfxLabel);
    option_add(&optionsMenu, "Back");
    option_set_leftright(&optionsMenu, mp_on_options_leftright);
    option_show(&optionsMenu, mp_on_options_select, mp_on_options_cancel);
    isInOptionsMenu = true;
}

// Pause menu callback
static void mp_on_pause_menu_select(int choice) {
    switch (choice) {
        case 0:  // Resume
            isPaused = false;
            break;
        case 1:  // Options
            mp_show_options_menu();
            break;
        case 2:  // Restart Level
            isPaused = false;
            // Reset both players to spawn
            for (int i = 0; i < NUM_PLAYERS; i++) {
                players[i].x = players[i].spawnX;
                players[i].y = players[i].spawnY;
                players[i].z = players[i].spawnZ;
                players[i].physics.velX = 0;
                players[i].physics.velY = 0;
                players[i].physics.velZ = 0;
                players[i].health = players[i].maxHealth;
                players[i].isDead = false;
                players[i].isRespawning = false;
                players[i].isCharging = false;
                players[i].isJumping = false;
                players[i].isLanding = false;
                players[i].squashScale = 1.0f;
            }
            break;
        case 3:  // Quit to Menu
            isPaused = false;
            level_stop_music();
            change_scene(MENU_SCENE);
            break;
    }
}

// Show pause menu
static void mp_show_pause_menu(int playerIdx) {
    if (!pauseMenuInitialized) {
        option_init(&pauseMenu);
        pauseMenuInitialized = true;
    }

    option_set_title(&pauseMenu, "PAUSED");
    option_add(&pauseMenu, "Resume");
    option_add(&pauseMenu, "Options");
    option_add(&pauseMenu, "Restart Level");
    option_add(&pauseMenu, "Quit to Menu");
    option_show(&pauseMenu, mp_on_pause_menu_select, NULL);
    isPaused = true;
    isInOptionsMenu = false;
    pausingPlayer = playerIdx;
}

void update_multiplayer_scene(void) {
    fpu_flush_denormals();  // Prevent denormal exceptions from sqrtf/float ops

    // Update audio mixer first (prevents crackling)
    if (audio_can_write()) {
        short *buf = audio_write_begin();
        rspq_wait();  // Flush RSPQ to exit highpri mode before mixer_poll (needed when rdpq_debug is active)
        save_apply_volume_settings_safe();  // Apply pending volume changes in safe window
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }

    // Track frame time for performance graph
    static uint32_t lastFrameTicks = 0;
    uint32_t nowTicks = get_ticks();
    uint32_t frameUs = TICKS_TO_US(nowTicks - lastFrameTicks);
    lastFrameTicks = nowTicks;

    // Increment frame index (bounded for double/triple buffering)
    frameIdx = (frameIdx + 1) % FB_COUNT;

    float dt = 1.0f / 30.0f;  // Assuming 30fps

    // Check for perf graph toggle (C-Left on either controller)
    joypad_buttons_t p1held = joypad_get_buttons_held(JOYPAD_PORT_1);
    joypad_buttons_t p2held = joypad_get_buttons_held(JOYPAD_PORT_2);
    bool cLeftHeld = p1held.c_left || p2held.c_left;
    perf_graph_update(cLeftHeld, frameUs);

    // Handle transition
    if (isTransitioning) {
        transitionTimer += dt;

        // Phase 1: Fade out
        if (transitionTimer < 0.5f) {
            fadeAlpha = transitionTimer / 0.5f;
        }
        // Phase 2: Load new level
        else if (transitionTimer < 1.0f) {
            fadeAlpha = 1.0f;

            // Load new level at transition midpoint
            if (transitionTimer >= 0.5f && transitionTimer < 0.55f) {
                // Unload level 3 special decos if leaving level 3
                // level3_special_unload removed

                currentLevel = (LevelID)targetTransitionLevel;

                // Reinit map
                maploader_free(&mapLoader);
                map_runtime_free(&mapRuntime);
                collision_loader_free_all();  // Free collision meshes to prevent stale pointer reuse
                maploader_init(&mapLoader, FB_COUNT, MP_VISIBILITY_RANGE);
                map_runtime_init(&mapRuntime, FB_COUNT, MP_VISIBILITY_RANGE);
                level_load(currentLevel, &mapLoader, &mapRuntime);

                // Load level 3 special decos if entering level 3
                // level3_special_load removed

                // Handle body part change (reloads model and animations if different)
                int newBodyPart = level_get_body_part(currentLevel);
                reload_player_model_for_body_part(newBodyPart);

                // Reset all TransitionCollision triggered flags (allow re-triggering in new level)
                for (int i = 0; i < mapRuntime.decoCount; i++) {
                    DecoInstance* deco = &mapRuntime.decorations[i];
                    if (deco->type == DECO_TRANSITIONCOLLISION && deco->active) {
                        deco->state.transition.triggered = false;
                    }
                }

                // Find the Nth PlayerSpawn decoration (targetTransitionSpawn is 0-indexed)
                float spawnX, spawnY, spawnZ;
                int spawnIndex = 0;
                bool foundSpawn = false;
                for (int i = 0; i < mapRuntime.decoCount; i++) {
                    DecoInstance* deco = &mapRuntime.decorations[i];
                    if (deco->type == DECO_PLAYERSPAWN && deco->active) {
                        if (spawnIndex == targetTransitionSpawn) {
                            spawnX = deco->posX;
                            spawnY = deco->posY;
                            spawnZ = deco->posZ;
                            foundSpawn = true;
                            debugf("MP: Found spawn %d at (%.1f, %.1f, %.1f)\n",
                                   targetTransitionSpawn, spawnX, spawnY, spawnZ);
                            break;
                        }
                        spawnIndex++;
                    }
                }
                if (!foundSpawn) {
                    // Fallback to level default position
                    level_get_player_start(currentLevel, &spawnX, &spawnY, &spawnZ);
                    debugf("MP: Spawn %d not found, using level default (%.1f, %.1f, %.1f)\n",
                           targetTransitionSpawn, spawnX, spawnY, spawnZ);
                }

                // Reset and teleport both players
                for (int i = 0; i < NUM_PLAYERS; i++) {
                    float offsetX = (i == 0) ? -30.0f : 30.0f;

                    // Reset position
                    players[i].x = spawnX + offsetX;
                    players[i].y = spawnY;
                    players[i].z = spawnZ;
                    players[i].spawnX = spawnX + offsetX;
                    players[i].spawnY = spawnY;
                    players[i].spawnZ = spawnZ;

                    // Reset physics state
                    players[i].physics.velX = 0;
                    players[i].physics.velY = 0;
                    players[i].physics.velZ = 0;
                    players[i].physics.isGrounded = false;
                    players[i].physics.isSliding = false;  // Critical: reset sliding state
                    players[i].physics.canMove = true;
                    players[i].physics.canRotate = true;
                    players[i].physics.canJump = true;

                    // Reset animation states
                    players[i].isCharging = false;
                    players[i].isJumping = false;
                    players[i].isLanding = false;
                    players[i].jumpAnimPaused = false;
                    players[i].jumpChargeTime = 0.0f;
                    players[i].squashScale = 1.0f;
                    players[i].squashVelocity = 0.0f;
                    players[i].landingSquash = 0.0f;
                    players[i].chargeSquash = 0.0f;
                    players[i].isSlidingFront = true;  // Reset slide direction

                    // Reset to idle animation
                    if (players[i].animIdle.animRef != NULL) {
                        t3d_anim_attach(&players[i].animIdle, &players[i].skeleton);
                        t3d_anim_set_time(&players[i].animIdle, 0.0f);
                        t3d_anim_set_playing(&players[i].animIdle, true);
                        t3d_anim_update(&players[i].animIdle, 0.0f);
                        t3d_skeleton_update(&players[i].skeleton);
                        players[i].currentAnim = &players[i].animIdle;
                    }

                    // Reset camera (with multiplayer zoom)
                    camera_init(&playerCameras[i], spawnX + offsetX, spawnY, spawnZ);
                    camera_set_z_offset(&playerCameras[i], CAM_Z_OFFSET_MULTIPLAYER);
                }

                // Show level banner for new level
                level_banner_show(&levelBanner, get_level_name_with_number(currentLevel));

                // Block controls until countdown starts
                for (int i = 0; i < NUM_PLAYERS; i++) {
                    players[i].physics.canMove = false;
                    players[i].physics.canJump = false;
                }

                debugf("MP: Both players teleported to level %d\n", currentLevel);
            }
        }
        // Phase 3: Fade in
        else if (transitionTimer < 1.5f) {
            fadeAlpha = 1.0f - (transitionTimer - 1.0f) / 0.5f;
        }
        // Done
        else {
            isTransitioning = false;
            fadeAlpha = 0.0f;
            // Start countdown now that fade is complete
            countdown_start(&mpCountdown);
            // Reset bolts for new level
            boltsCollected = 0;
        }

        return;  // Don't update players during transition
    }

    // Check for pause (Start on either controller)
    joypad_poll();
    joypad_buttons_t p1_pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t p2_pressed = joypad_get_buttons_pressed(JOYPAD_PORT_2);

    if (!isPaused) {
        if (p1_pressed.start) {
            mp_show_pause_menu(0);
        } else if (p2_pressed.start) {
            mp_show_pause_menu(1);
        }
    }

    // Handle pause menu
    if (isPaused) {
        // Only the player who paused can navigate the menu
        joypad_port_t pausePort = (pausingPlayer == 0) ? JOYPAD_PORT_1 : JOYPAD_PORT_2;
        option_update(&pauseMenu, pausePort);
        // Check if menu was closed
        if (!option_is_active(&pauseMenu)) {
            isPaused = false;
        }
        return;  // Skip game updates while paused
    }

    // Update countdown (3-2-1-GO!)
    countdown_update(&mpCountdown, dt, isPaused, false, isTransitioning);

    // Update celebration (fireworks + level complete UI)
    bool aPressed = p1_pressed.a || p2_pressed.a;
    if (celebrate_update(&mpCelebrate, dt, aPressed)) {
        // Player pressed A, start the fade transition
        isTransitioning = true;
        transitionTimer = 0.0f;
        targetTransitionLevel = celebrate_get_target_level(&mpCelebrate);
        targetTransitionSpawn = celebrate_get_target_spawn(&mpCelebrate);
        celebrate_reset(&mpCelebrate);
    }

    // Update both players using shared player_update() function
    // Block ALL controls during countdown OR celebration
    bool controlsActive = !countdown_is_active(&mpCountdown) && !celebrate_is_active(&mpCelebrate);
    for (int i = 0; i < NUM_PLAYERS; i++) {
        // Use shared player module physics (matches game.c behavior exactly)
        // Pass controlsActive as inputEnabled to block input during countdown/celebration
        // player_update() handles goto physics internally when inputEnabled is false
        player_update(&players[i], &mapLoader, &mapRuntime, dt, controlsActive);

        // Check decoration collisions for damage (skip if dead)
        // Set currentPlayerIndex so decoration callbacks know which player to damage
        if (!players[i].isDead) {
            mapRuntime.currentPlayerIndex = i;
            map_check_deco_collisions(&mapRuntime, players[i].x, players[i].y, players[i].z, PLAYER_RADIUS);
        }

        // Update camera after physics (player_update handles its own internal camera)
        // We still need to update our splitscreen camera module
        float chargeRatio = (players[i].jumpChargeTime - PLAYER_HOP_THRESHOLD) / (PLAYER_MAX_CHARGE_TIME - PLAYER_HOP_THRESHOLD);
        if (chargeRatio < 0.0f) chargeRatio = 0.0f;
        if (chargeRatio > 1.0f) chargeRatio = 1.0f;
        float arcEndX = players[i].x + players[i].jumpAimX * 100.0f;
        // Use camera with collision detection for wall avoidance
        float deathZoom = players[i].isDead ? 1.0f : 0.0f;
        camera_update_with_collision(&playerCameras[i], players[i].x, players[i].y, players[i].z,
                      players[i].physics.velX, players[i].physics.velZ,
                      players[i].isCharging, chargeRatio, arcEndX,
                      &mapLoader, deathZoom);

        // Apply screen shake to camera
        float shakeX, shakeY;
        effects_get_shake_offset(&shakeX, &shakeY);
        camera_apply_shake(&playerCameras[i], shakeX, shakeY);

        check_transitions(i);
    }

    // Teleport lagging player if too far apart (prevents one player getting left behind)
    // "Furthest to the left" = higher X value (levels progress toward negative X)
    // Teleport the one with higher X to the one with lower X (the leader)
    #define MP_MAX_PLAYER_SEPARATION 400.0f
    if (!players[0].isDead && !players[1].isDead && controlsActive) {
        float xDiff = players[0].x - players[1].x;
        float absXDiff = (xDiff > 0) ? xDiff : -xDiff;

        if (absXDiff > MP_MAX_PLAYER_SEPARATION) {
            // Determine who is behind (higher X = left/behind, lower X = ahead/right)
            int behindIdx = (players[0].x > players[1].x) ? 0 : 1;
            int aheadIdx = 1 - behindIdx;

            debugf("MP: Player %d too far behind (%.1f), teleporting to player %d\n",
                   behindIdx, absXDiff, aheadIdx);

            // Teleport behind player to slightly behind the leader
            players[behindIdx].x = players[aheadIdx].x + 30.0f;  // Offset so they don't overlap
            players[behindIdx].y = players[aheadIdx].y + 20.0f;  // Slight height offset to fall to ground
            players[behindIdx].z = players[aheadIdx].z;

            // Reset velocity so they don't fly off
            players[behindIdx].physics.velX = 0;
            players[behindIdx].physics.velY = 0;
            players[behindIdx].physics.velZ = 0;
            players[behindIdx].physics.isGrounded = false;

            // Update camera immediately to avoid jarring snap
            camera_init(&playerCameras[behindIdx], players[behindIdx].x, players[behindIdx].y, players[behindIdx].z);
            camera_set_z_offset(&playerCameras[behindIdx], CAM_Z_OFFSET_MULTIPLAYER);
        }
    }

    // Update map visibility based on BOTH player positions
    // Chunks stay active if ANY player is within range
    float playerXs[NUM_PLAYERS] = { players[0].x, players[1].x };
    float playerZs[NUM_PLAYERS] = { players[0].z, players[1].z };
    float playerYs[NUM_PLAYERS] = { players[0].y, players[1].y };
    maploader_update_visibility_multi(&mapLoader, playerXs, playerZs, NUM_PLAYERS);

    // Set player position for decorations/AI
    // Use midpoint as reference to find closest player (fair for both)
    float midX = (players[0].x + players[1].x) * 0.5f;
    float midZ = (players[0].z + players[1].z) * 0.5f;
    map_set_closest_player_pos(&mapRuntime, playerXs, playerYs, playerZs, NUM_PLAYERS, midX, midZ);

    // Update decorations
    map_update_decorations(&mapRuntime, dt);

    // Update level 3 special decoration UV offsets
    // level3_special_update removed

    // Update HUDs using HUD module
    for (int i = 0; i < NUM_PLAYERS; i++) {
        hud_health_update(&healthHUDs[i], dt);
    }
    hud_screw_update(&screwHUD, dt);

    // Update level banner
    level_banner_update(&levelBanner, dt);

    // Update effects (screen shake, flash timers)
    effects_update(dt);

    // Update particles
    particles_update(dt);

    // Update per-player iris effects (for death)
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (effects_iris_active(&playerIris[i])) {
            // Calculate player's screen position for iris center
            float screenY = (i == 0) ? SPLIT_HEIGHT / 2 : SPLIT_HEIGHT + SPLIT_HEIGHT / 2;
            effects_iris_update(&playerIris[i], dt, SCREEN_WIDTH / 2, screenY);
        }
    }
}

// Draw a single player's viewport
static void draw_player_viewport(int viewportIdx) {
    Player* viewPlayer = &players[viewportIdx];
    T3DViewport* vp = &viewports[viewportIdx];

    // Set scissor for this player's half of screen
    int y_start = viewportIdx * SPLIT_HEIGHT;
    int y_end = (viewportIdx + 1) * SPLIT_HEIGHT;
    rdpq_set_scissor(0, y_start, SCREEN_WIDTH, y_end);

    // Set up 3D rendering - attach this player's viewport
    t3d_viewport_attach(vp);

    // Adjust FOV for half-height viewport (wider to compensate)
    t3d_viewport_set_projection(vp, T3D_DEG_TO_RAD(80.0f), 10.0f, 1000.0f);

    // Camera for this player - get from camera module
    T3DVec3 camPos, camTarget;
    camera_get_vectors(&playerCameras[viewportIdx], &camPos, &camTarget);
    T3DVec3 up = {{0, 1, 0}};
    t3d_viewport_look_at(vp, &camPos, &camTarget, &up);

    // Lighting setup - use per-level settings or defaults
    uint8_t ambientR, ambientG, ambientB;
    level_get_ambient_color(currentLevel, &ambientR, &ambientG, &ambientB);
    uint8_t ambientColor[4] = {ambientR, ambientG, ambientB, 0xFF};

    uint8_t directionalR, directionalG, directionalB;
    level_get_directional_color(currentLevel, &directionalR, &directionalG, &directionalB);
    uint8_t lightColor[4] = {directionalR, directionalG, directionalB, 0xFF};

    float lightDirX, lightDirY, lightDirZ;
    level_get_light_direction(currentLevel, &lightDirX, &lightDirY, &lightDirZ);
    T3DVec3 lightDir = {{lightDirX, lightDirY, lightDirZ}};
    t3d_vec3_norm(&lightDir);

    t3d_light_set_ambient(ambientColor);
    t3d_light_set_directional(0, lightColor, &lightDir);

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
    for (int i = 0; i < mapRuntime.decoCount && lightCount < MP_MAX_POINT_LIGHTS; i++) {
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

    // Set light count for map rendering (excludes DECO_LIGHT_NOMAP)
    t3d_light_set_count(lightCount);

    // Render map (uses module-level frameIdx)
    rdpq_sync_pipe();
    maploader_draw_culled(&mapLoader, frameIdx, vp);
    t3d_tri_sync();
    rdpq_sync_pipe();

    // Add DECO_LIGHT_NOMAP lights (only affects decorations and player, not map)
    for (int i = 0; i < mapRuntime.decoCount && lightCount < MP_MAX_POINT_LIGHTS; i++) {
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

    // Update light count for decoration/player rendering
    t3d_light_set_count(lightCount);

    // Render decorations using shared deco_render module
    // PERF: Use frustum culling - only render decorations in front of THIS player's camera
    // Calculate camera forward direction for frustum culling
    float camFwdX = camTarget.v[0] - camPos.v[0];
    float camFwdZ = camTarget.v[2] - camPos.v[2];
    float camFwdLen = sqrtf(camFwdX * camFwdX + camFwdZ * camFwdZ);
    if (camFwdLen > 0.01f) {
        camFwdX /= camFwdLen;
        camFwdZ /= camFwdLen;
    }

    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (!deco->active || deco->type == DECO_NONE) continue;

        // Distance culling - use THIS viewport's player for tighter culling
        float dx = deco->posX - viewPlayer->x;
        float dz = deco->posZ - viewPlayer->z;
        float distSq = dx * dx + dz * dz;
        if (distSq > MP_DECO_VISIBILITY_RANGE * MP_DECO_VISIBILITY_RANGE) continue;

        // PERF: Frustum culling - skip decorations behind the camera
        // Dot product of (deco - cam) with camera forward; negative = behind
        float toCamX = deco->posX - camPos.v[0];
        float toCamZ = deco->posZ - camPos.v[2];
        float dotFwd = toCamX * camFwdX + toCamZ * camFwdZ;
        if (dotFwd < -50.0f) continue;  // Behind camera with small margin

        // PERF: Skip animated/skeletal decorations when distant (expensive to draw)
        bool isAnimated = (deco->type == DECO_RAT || deco->type == DECO_SLIME ||
                          deco->type == DECO_SLIME_LAVA || deco->type == DECO_BULLDOZER ||
                          deco->type == DECO_DROID_SEC || deco->type == DECO_TURRET ||
                          deco->type == DECO_TURRET_PULSE || deco->type == DECO_FAN2 ||
                          deco->type == DECO_JUKEBOX || deco->type == DECO_DISCOBALL);
        if (isAnimated && distSq > MP_SKELETAL_DECO_RANGE * MP_SKELETAL_DECO_RANGE) continue;

        // Skip invisible triggers
        if (deco->type == DECO_PLAYERSPAWN) continue;
        if (deco->type == DECO_DAMAGECOLLISION) continue;
        if (deco->type == DECO_TRANSITIONCOLLISION) continue;
        if (deco->type == DECO_DIALOGUETRIGGER) continue;
        if (deco->type == DECO_INTERACTTRIGGER) continue;
        if (deco->type == DECO_PATROLPOINT) continue;

        DecoTypeRuntime* decoType = map_get_deco_type(&mapRuntime, deco->type);
        int matIdx = frameIdx * MAX_DECORATIONS + i;

        // Use shared decoration rendering (full parity with game.c)
        deco_render_single(deco, decoType, &mapRuntime, decoMatFP, frameIdx, matIdx);
    }

    // Sync after decorations, before player rendering
    t3d_tri_sync();
    rdpq_sync_pipe();

    // Render BOTH players (so each can see the other)
    for (int p = 0; p < NUM_PLAYERS; p++) {
        Player* player = &players[p];

        // Skip dead players or respawning
        if (player->isDead && !player->isRespawning) continue;

        // Skip if invincible and on "invisible" frame (flash effect)
        if (player->invincibilityTimer > 0.0f) {
            int flashFrame = (int)(player->invincibilityTimer * 10.0f);
            if (flashFrame % 2 == 1) continue;
        }

        // Set up player matrix (0.40f base scale like game.c)
        // Apply squash: Y compressed, X/Z expanded to preserve volume
        float baseScale = 0.40f;
        float scaleY = baseScale * player->squashScale;
        float scaleXZ = baseScale * (1.0f + (1.0f - player->squashScale) * 0.5f);
        t3d_mat4fp_from_srt_euler(&playerMatFP[p],
            (float[3]){scaleXZ, scaleY, scaleXZ},
            (float[3]){0, player->angle, 0},
            (float[3]){player->x, player->y, player->z}
        );

        t3d_matrix_push(&playerMatFP[p]);

        // Apply player tint
        rdpq_set_prim_color(player->tint);

        // Draw model with skinned animation
        // Validate skeleton before use to prevent crash on corrupted/freed data
        if (playerModel && skeleton_is_valid(&player->skeleton)) {
            t3d_model_draw_skinned(playerModel, &player->skeleton);
        } else if (playerModel) {
            t3d_model_draw(playerModel);
        }

        t3d_matrix_pop(1);
    }

    // Draw jump arc prediction for players who are charging (torso mode, past hop threshold)
    // Only draw arc for the player whose viewport we're rendering
    // PERF: Skip arc preview in multiplayer - expensive ground collision checks
#if !MP_SKIP_ARC_PREVIEW
    Player* vp_player = &players[viewportIdx];
    if (vp_player->isCharging && vp_player->jumpChargeTime >= PLAYER_HOP_THRESHOLD &&
        vp_player->currentPart == PLAYER_PART_TORSO && arcDotVerts) {

        // CRITICAL: Sync RDP pipeline before switching to flat-shaded 3D mode
        t3d_tri_sync();
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);  // Flat color, avoid CI texture issues
        t3d_state_set_drawflags(T3D_FLAG_DEPTH | T3D_FLAG_CULL_BACK);

        // Calculate predicted jump velocity based on current charge and stick direction
        // Include triple jump combo power multiplier for accurate arc preview
        float comboPowerMult = (vp_player->jumpComboCount == 0) ? PLAYER_JUMP_COMBO_POWER_MULT_1 :
                               (vp_player->jumpComboCount == 1) ? PLAYER_JUMP_COMBO_POWER_MULT_2 :
                                                                  PLAYER_JUMP_COMBO_POWER_MULT_3;
        float predictedVelY = (PLAYER_CHARGE_JUMP_EARLY_BASE + vp_player->jumpChargeTime * PLAYER_CHARGE_JUMP_EARLY_MULT) * comboPowerMult;

        // Use stick direction for arc - matches actual jump behavior
        float aimMag = sqrtf(vp_player->jumpAimX * vp_player->jumpAimX +
                            vp_player->jumpAimY * vp_player->jumpAimY);
        if (aimMag > 1.0f) aimMag = 1.0f;

        // Horizontal scale must match the jump logic (includes triple jump multiplier)
        float predictedForward = (3.0f + 2.0f * vp_player->jumpChargeTime) * FPS_SCALE * aimMag * comboPowerMult;
        float predictedVelX, predictedVelZ;
        if (aimMag > 0.1f) {
            // Apply horizontal scale uniformly to both axes to match actual jump
            predictedVelX = (vp_player->jumpAimX / aimMag) * predictedForward * PLAYER_HORIZONTAL_SCALE;
            predictedVelZ = (vp_player->jumpAimY / aimMag) * predictedForward * PLAYER_HORIZONTAL_SCALE;
        } else {
            predictedVelX = 0.0f;
            predictedVelZ = 0.0f;
        }

        // Simulate trajectory
        float simVelX = predictedVelX;
        float simVelY = predictedVelY;
        float simVelZ = predictedVelZ;
        float simX = vp_player->x;
        float simY = vp_player->y;
        float simZ = vp_player->z;
        float landX = vp_player->x, landY = vp_player->groundLevel, landZ = vp_player->z;

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

            // Draw arc dots as cubes (color based on jump combo) - matching game.c
            if (frame > 2 && frame % dotInterval == 0 && dotsDrawn < maxDots) {
                float dotSize = 3.0f;  // World units
                t3d_mat4fp_from_srt_euler(&arcMatFP[dotsDrawn],
                    (float[3]){dotSize, dotSize, dotSize},
                    (float[3]){0, frame * 2.0f, 0},
                    (float[3]){simX, simY, simZ}
                );
                t3d_matrix_push(&arcMatFP[dotsDrawn]);
                // Arc color based on jump combo: blue (1st), orange (2nd), red (3rd)
                color_t arcColor = (vp_player->jumpComboCount == 0) ? RGBA32(100, 200, 255, 220) :  // Blue
                                   (vp_player->jumpComboCount == 1) ? RGBA32(255, 165, 50, 220) :   // Orange
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

        // Draw landing marker as cube (yellow, bright) - matching game.c
        float dotSize = 5.0f;  // Larger for landing marker
        t3d_mat4fp_from_srt_euler(&arcMatFP[dotsDrawn],
            (float[3]){dotSize, dotSize, dotSize},
            (float[3]){0, 0, 0},
            (float[3]){landX, landY + 3.0f, landZ}
        );
        t3d_matrix_push(&arcMatFP[dotsDrawn]);
        rdpq_set_prim_color(RGBA32(255, 255, 100, 240));  // Yellow (matching game.c)
        t3d_vert_load(arcDotVerts, 0, 8);
        // Draw 6 faces (12 triangles) - CCW winding for front faces
        t3d_tri_draw(3, 2, 6); t3d_tri_draw(3, 6, 7);  // Front
        t3d_tri_draw(1, 0, 4); t3d_tri_draw(1, 4, 5);  // Back
        t3d_tri_draw(7, 6, 5); t3d_tri_draw(7, 5, 4);  // Top
        t3d_tri_draw(0, 1, 2); t3d_tri_draw(0, 2, 3);  // Bottom
        t3d_tri_draw(2, 1, 5); t3d_tri_draw(2, 5, 6);  // Right
        t3d_tri_draw(0, 3, 7); t3d_tri_draw(0, 7, 4);  // Left
        t3d_matrix_pop(1);

        // Sync T3D triangles after all arc dots are drawn (matching game.c)
        t3d_tri_sync();

        // CRITICAL: Reset combiner back to textured mode after flat-shaded arc dots
        // Without this, player 2's viewport has corrupted textures
        rdpq_sync_pipe();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    }
#endif  // !MP_SKIP_ARC_PREVIEW

    // Draw particles in this viewport (after 3D, before HUD)
    // PERF: Skip particles in multiplayer - too expensive to draw twice
#if !MP_SKIP_PARTICLES
    particles_draw(vp);
    particles_draw_impact_stars(vp, viewPlayer->x, viewPlayer->y, viewPlayer->z);
#endif

    // Draw per-player iris effect (death wipe)
    if (effects_iris_active(&playerIris[viewportIdx])) {
        effects_iris_draw(&playerIris[viewportIdx]);
    }

    // Sync at end of viewport to ensure all rendering completes before next viewport
    rdpq_sync_pipe();
}

void draw_multiplayer_scene(void) {
    // Get display surface and z-buffer (like game.c does)
    surface_t* disp = display_get();
    surface_t* zbuf = display_get_zbuf();
    rdpq_attach(disp, zbuf);
    rdpq_clear(RGBA32(0x28, 0x28, 0x3C, 0xFF));  // Dark blue-grey background
    rdpq_clear_z(0xFFFC);

    // Initialize T3D frame
    t3d_frame_start();

    // Draw player 1 viewport (top half)
    draw_player_viewport(0);

    // Sync between viewports to prevent TMEM state leakage
    t3d_tri_sync();
    rdpq_sync_pipe();

    // Draw player 2 viewport (bottom half)
    draw_player_viewport(1);

    // Reset scissor for UI
    rdpq_set_scissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Draw splitscreen divider line
    rdpq_sync_pipe();  // Sync before switching to fill mode
    rdpq_set_mode_fill(RGBA32(100, 100, 100, 255));
    rdpq_fill_rectangle(0, SPLIT_HEIGHT - 1, SCREEN_WIDTH, SPLIT_HEIGHT + 1);

    // Draw player indicators and HUD using HUD module
    // Player 1 health (top viewport, right side) - smaller for splitscreen
    hud_health_draw_scaled(&healthHUDs[0], players[0].health, 110.0f, 0.0f, 1.0f);

    // Player 2 health (bottom viewport, right side) - offset for bottom half of screen
    hud_health_draw_scaled(&healthHUDs[1], players[1].health, 110.0f, SPLIT_HEIGHT, 1.0f);

    // Bolt/Screw HUD - ONLY show during pause menu, hide during gameplay
    // (screw HUD drawn in pause menu overlay below)

    // Draw level banner (slides in from bottom)
    level_banner_draw(&levelBanner);

    // Draw countdown (3-2-1-GO!) centered on screen
    countdown_draw(&mpCountdown, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

    // Draw celebration (fireworks + level complete UI)
    celebrate_draw(&mpCelebrate, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);

    // Draw hit/bolt flash overlay (screen-wide effect)
    if (effects_flash_active()) {
        color_t flashColor = effects_get_flash_color();
        rdpq_sync_pipe();  // Sync before 2D mode switch
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(flashColor);
        rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    // Draw fade overlay (for effects module)
    effects_draw_fade();

    // Draw pause menu overlay and menu
    if (isPaused) {
        // Dim background
        rdpq_sync_pipe();  // Sync before 2D mode switch
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0, 0, 0, 128));
        rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        // Draw bolt/screw count on pause screen using HUD module (only during main pause, not options)
        if (!isInOptionsMenu) {
            hud_screw_show(&screwHUD, false);
            hud_screw_draw(&screwHUD, boltsCollected, 100, -110.0f, 200.0f);
        }

        // Draw appropriate menu
        if (isInOptionsMenu) {
            option_draw(&optionsMenu);
        } else {
            option_draw(&pauseMenu);
        }
    }

    // Draw transition fade
    if (fadeAlpha > 0.0f) {
        int alpha = (int)(fadeAlpha * 255.0f);
        rdpq_sync_pipe();  // Sync before 2D mode switch
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0, 0, 0, alpha));
        rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    // Draw performance graph (debug - toggle with C-Left)
    perf_graph_draw(10, SCREEN_HEIGHT - 6);

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}
