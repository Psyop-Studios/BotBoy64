#include <libdragon.h>
#include <rdpq_tri.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include "menu_scene.h"
#include "splash.h"
#include "../scene.h"
#include "multiplayer.h"
#include "../controls.h"
#include "../constants.h"
#include "../collision.h"
#include "../mapLoader.h"
#include "../mapData.h"
#include "../levels_generated.h"
#include "../levels.h"
#include "../levels/menu.h"
#include "../ui.h"
#include "../debug_menu.h"
#include "../PsyopsCode.h"
#include "../save.h"
#include "../sounds_generated.h"
#include "../qr_display.h"
#include "level_select.h"
#include "game.h"
#include <stdbool.h>
#include <math.h>
#include <malloc.h>

#define FB_COUNT 3
#define MENU_VISIBILITY_RANGE 500.0f

static bool sceneInitialized = false;
static bool menuHasShownWelcome = false;  // Track if welcome dialogue has ever been shown

// Map loader and runtime (for decorations)
static MapLoader mapLoader;
static MapRuntime mapRuntime;
static T3DMat4FP* decoMatFP = NULL;

// Monitor screen UV scrolling (for MONITORTABLE)
static int16_t* g_menuMonitorBaseUVs = NULL;
static int g_menuMonitorVertCount = 0;
static T3DVertPacked* g_menuMonitorVerts = NULL;

static void menu_monitor_init_uvs(T3DModel* screenModel) {
    if (g_menuMonitorBaseUVs != NULL) return;

    T3DModelIter it = t3d_model_iter_create(screenModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&it)) {
        T3DObjectPart* part = it.object->parts;
        g_menuMonitorVerts = part->vert;
        g_menuMonitorVertCount = part->vertLoadCount;

        g_menuMonitorBaseUVs = malloc(g_menuMonitorVertCount * 2 * sizeof(int16_t));

        for (int i = 0; i < g_menuMonitorVertCount; i++) {
            int16_t* uv = t3d_vertbuffer_get_uv(g_menuMonitorVerts, i);
            g_menuMonitorBaseUVs[i * 2] = uv[0];
            g_menuMonitorBaseUVs[i * 2 + 1] = uv[1];
        }
        break;
    }
}

static void menu_monitor_scroll_uvs(float offset) {
    if (!g_menuMonitorBaseUVs || !g_menuMonitorVerts) return;

    // Scroll texture DOWN (text falling from top to bottom)
    // T3D uses 10.5 fixed-point UV, so 32 = 1 texel
    // Use 32*4=128 for a slower, readable scroll speed
    int16_t scrollAmount = (int16_t)(-offset * 32.0f * 4.0f);

    for (int i = 0; i < g_menuMonitorVertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_menuMonitorVerts, i);
        uv[1] = g_menuMonitorBaseUVs[i * 2 + 1] + scrollAmount;
    }
    int packedCount = (g_menuMonitorVertCount + 1) / 2;
    data_cache_hit_writeback(g_menuMonitorVerts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

// UI elements
static DialogueBox dialogueBox;
static OptionPrompt optionPrompt = {0};  // Explicit zero-init to ensure active=false on startup
static DialogueScript activeScript;
static bool scriptRunning = false;

// InteractTrigger state
static DecoInstance* activeInteractTrigger = NULL;
static float savedPlayerAngle = 0.0f;
static float targetPlayerAngle = 0.0f;
static bool isLerpingAngle = false;

// Full debug fly mode state (like game.c)
static bool menuDebugFlyMode = false;
static float debugCamX = 0.0f, debugCamY = 100.0f, debugCamZ = 200.0f;
static float debugCamYaw = 3.14159f, debugCamPitch = 0.0f;
static bool debugPlacementMode = false;
static bool debugShowCollision = false;
static int debugHighlightedDecoIndex = -1;
static float debugDeleteCooldown = 0.0f;

// Decoration placement state
static DecoType debugDecoType = DECO_DIALOGUETRIGGER;
static float debugDecoX = 0.0f, debugDecoY = 0.0f, debugDecoZ = 0.0f;
static float debugDecoRotY = 0.0f;
static float debugDecoScaleX = 1.0f, debugDecoScaleY = 1.0f, debugDecoScaleZ = 1.0f;
static int debugTriggerCooldown = 0;

// Trigger-specific params
static int debugTriggerScriptId = 0;
static float debugTriggerRadius = 50.0f;

// Forward declarations for debug menu callbacks
static void debug_action_toggle_fly_mode(void);
static void debug_action_toggle_collision(void);
static void debug_action_toggle_placement(void);
static void debug_action_print_code(void);
static void debug_action_close_menu(void);

// Menu flow state
typedef enum {
    MENU_STATE_IDLE,
    MENU_STATE_MAIN_CHOICE,      // New Game vs Load Game (or Continue vs Level Select)
    MENU_STATE_SELECT_SAVE,      // Selecting save slot
    MENU_STATE_CONFIRM_NEW,      // Confirm overwrite for new game
    MENU_STATE_LEVEL_SELECT,     // Level select submenu
    MENU_STATE_STARTING_GAME,    // About to start game
    MENU_STATE_IRIS_OUT,         // Iris closing before scene change
    MENU_STATE_OPTIONS,          // Options menu (volume, delete saves, etc.)
    MENU_STATE_DELETE_SAVE,      // Selecting save slot to delete
    MENU_STATE_CONFIRM_DELETE,   // Confirm delete save
    MENU_STATE_JUKEBOX,          // Jukebox sound test menu
    MENU_STATE_SAVE_DETAILS,     // Showing save details before load/overwrite
    MENU_STATE_CONFIRM_LOAD,     // Confirm load save after seeing details
} MenuState;

static MenuState menuState = MENU_STATE_IDLE;
static bool menuIsNewGame = false;  // true = new game, false = load game
static int menuSelectedSlot = -1;   // Which save slot was selected
static bool gameSessionActive = false;  // true after load/new game, shows Continue/Level Select

// Jukebox state (track arrays are in sounds_generated.h)
static int jukeboxMusicIndex = 0;   // Currently selected music track
static int jukeboxSfxIndex = 0;     // Currently selected SFX
static wav64_t jukeboxSfx;          // Loaded SFX for playback
static bool jukeboxSfxLoaded = false;

// Party mode (colored lights around jukebox)
static bool partyModeActive = false;
static float partyLightAngle = 0.0f;    // Current rotation angle
static float partyJukeboxX = 0.0f;      // Jukebox position for lights
static float partyJukeboxY = 0.0f;
static float partyJukeboxZ = 0.0f;

// Party fog (color-cycling fog that creeps in during party mode)
static float partyFogIntensity = 0.0f;  // 0 = no fog, 1 = full fog
static float partyFogHue = 0.0f;        // Color cycling (0-360)

// Disco ball sparkles - white sparkles that grow/shrink around the ball
#define DISCO_SPARKLE_COUNT 8
#define DISCO_SPARKLE_RADIUS 25.0f      // Radius of sphere around disco ball
#define DISCO_SPARKLE_LIFETIME 0.8f     // How long each sparkle lives
#define DISCO_SPARKLE_SPAWN_RATE 0.15f  // Seconds between spawns

typedef struct {
    bool active;
    float x, y, z;          // World position offset from disco ball center
    float life;             // Current life (0 to DISCO_SPARKLE_LIFETIME)
    float maxLife;          // Total lifetime for this sparkle
} DiscoSparkle;

static DiscoSparkle g_discoSparkles[DISCO_SPARKLE_COUNT];
static float g_sparkleSpawnTimer = 0.0f;
static float g_discoBallX = 0.0f, g_discoBallY = 0.0f, g_discoBallZ = 0.0f;  // Cached disco ball position

// Simple pseudo-random for sparkle positions
static uint32_t g_sparkleRand = 12345;
static float sparkle_randf(void) {
    g_sparkleRand = g_sparkleRand * 1103515245 + 12345;
    return (float)(g_sparkleRand & 0x7FFF) / 32767.0f;
}

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

static void spawn_disco_sparkle(void) {
    // Find inactive sparkle slot
    for (int i = 0; i < DISCO_SPARKLE_COUNT; i++) {
        if (!g_discoSparkles[i].active) {
            // Random point on sphere surface
            float theta = sparkle_randf() * 6.28318f;  // 0 to 2*PI
            float phi = sparkle_randf() * 3.14159f;    // 0 to PI
            float r = DISCO_SPARKLE_RADIUS * (0.8f + sparkle_randf() * 0.4f);  // Slight radius variation

            g_discoSparkles[i].x = r * sinf(phi) * cosf(theta);
            g_discoSparkles[i].y = r * cosf(phi);
            g_discoSparkles[i].z = r * sinf(phi) * sinf(theta);
            g_discoSparkles[i].life = 0.0f;
            // Ensure maxLife has a minimum value to prevent division by zero
            g_discoSparkles[i].maxLife = fmaxf(0.1f, DISCO_SPARKLE_LIFETIME * (0.7f + sparkle_randf() * 0.6f));
            g_discoSparkles[i].active = true;
            break;
        }
    }
}

// Cheat code tracking for giant/tiny mode
// Giant code: C-Up, C-Up, C-Down, C-Down, A (big big small small confirm)
// Tiny code: C-Down, C-Down, C-Up, C-Up, B (small small big big cancel)
#define SIZE_CHEAT_LEN 5
static uint16_t sizeCheatBuffer[SIZE_CHEAT_LEN] = {0};
static const uint16_t GIANT_CODE[SIZE_CHEAT_LEN] = {0x0008, 0x0008, 0x0004, 0x0004, 0x8000};  // C-Up, C-Up, C-Down, C-Down, A
static const uint16_t TINY_CODE[SIZE_CHEAT_LEN] = {0x0004, 0x0004, 0x0008, 0x0008, 0x4000};   // C-Down, C-Down, C-Up, C-Up, B

// Iris transition effect
static float irisRadius = 400.0f;       // Current iris radius (starts large)
static float irisCenterX = 160.0f;      // Iris center X (screen coords)
static float irisCenterY = 120.0f;      // Iris center Y (screen coords)
static bool irisActive = false;         // Is iris effect running?
static bool irisOpening = false;        // true = opening, false = closing

// Fade-in from black effect (when coming from splash screen)
static bool fadeInActive = false;       // Is fade-in effect running?
static float fadeInAlpha = 1.0f;        // 1.0 = fully black, 0.0 = fully visible
#define MENU_FADE_IN_TIME 1.0f          // Time to fade in from black (seconds)

// Jukebox FX texture scrolling via vertex UV modification
// Support multiple parts in jukebox FX model
#define MAX_JUKEBOX_FX_PARTS 8
static int16_t* g_jukeboxFxBaseUVs[MAX_JUKEBOX_FX_PARTS] = {NULL};
static int g_jukeboxFxVertCount[MAX_JUKEBOX_FX_PARTS] = {0};
static T3DVertPacked* g_jukeboxFxVerts[MAX_JUKEBOX_FX_PARTS] = {NULL};
static int g_jukeboxFxPartCount = 0;

static void jukebox_fx_init_uvs(T3DModel* fxModel) {
    if (g_jukeboxFxPartCount > 0) return;  // Already initialized

    T3DModelIter iter = t3d_model_iter_create(fxModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        for (uint32_t p = 0; p < obj->numParts && g_jukeboxFxPartCount < MAX_JUKEBOX_FX_PARTS; p++) {
            T3DObjectPart* part = &obj->parts[p];
            int idx = g_jukeboxFxPartCount;

            g_jukeboxFxVerts[idx] = part->vert;
            g_jukeboxFxVertCount[idx] = part->vertLoadCount;
            g_jukeboxFxBaseUVs[idx] = malloc(g_jukeboxFxVertCount[idx] * 2 * sizeof(int16_t));

            for (int i = 0; i < g_jukeboxFxVertCount[idx]; i++) {
                int16_t* uv = t3d_vertbuffer_get_uv(g_jukeboxFxVerts[idx], i);
                g_jukeboxFxBaseUVs[idx][i * 2] = uv[0];
                g_jukeboxFxBaseUVs[idx][i * 2 + 1] = uv[1];
            }
            g_jukeboxFxPartCount++;
        }
    }
    debugf("Menu: Jukebox FX UVs initialized: %d parts\n", g_jukeboxFxPartCount);
}

static void jukebox_fx_scroll_uvs(float offset) {
    if (g_jukeboxFxPartCount == 0) return;

    // Scroll horizontally
    int16_t scrollAmountS = (int16_t)(offset * 32.0f * 32.0f);

    for (int p = 0; p < g_jukeboxFxPartCount; p++) {
        if (!g_jukeboxFxBaseUVs[p] || !g_jukeboxFxVerts[p]) continue;

        for (int i = 0; i < g_jukeboxFxVertCount[p]; i++) {
            int16_t* uv = t3d_vertbuffer_get_uv(g_jukeboxFxVerts[p], i);
            uv[0] = g_jukeboxFxBaseUVs[p][i * 2] + scrollAmountS;
            uv[1] = g_jukeboxFxBaseUVs[p][i * 2 + 1];
        }

        int packedCount = (g_jukeboxFxVertCount[p] + 1) / 2;
        data_cache_hit_writeback(g_jukeboxFxVerts[p], packedCount * sizeof(T3DVertPacked));
    }
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

// Disco floor model and UV scrolling
static T3DModel* discoFloorModel = NULL;
static T3DMat4FP* discoFloorMatFP = NULL;
static float discoFloorScrollOffset = 0.0f;
static float discoFloorCurrentY = -5.0f;  // Current Y position (starts hidden below floor)
#define DISCO_FLOOR_SCROLL_SPEED 0.5f  // Cycles per second
#define DISCO_FLOOR_HIDDEN_Y -5.0f     // Y position when hidden (below floor)
#define DISCO_FLOOR_VISIBLE_Y 2.0f     // Y position when visible (at floor level)
#define DISCO_FLOOR_RISE_SPEED 3.0f    // Units per second for rising animation

// Disco floor UV scrolling
static int16_t* g_discoFloorBaseUVs = NULL;
static int g_discoFloorVertCount = 0;
static T3DVertPacked* g_discoFloorVerts = NULL;

static void disco_floor_init_uvs(T3DModel* model) {
    if (g_discoFloorBaseUVs != NULL) return;  // Already initialized

    T3DModelIter iter = t3d_model_iter_create(model, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        if (!obj->numParts) continue;

        T3DObjectPart* part = &obj->parts[0];
        g_discoFloorVerts = part->vert;
        g_discoFloorVertCount = part->vertLoadCount;

        g_discoFloorBaseUVs = malloc(g_discoFloorVertCount * 2 * sizeof(int16_t));

        for (int i = 0; i < g_discoFloorVertCount; i++) {
            int16_t* uv = t3d_vertbuffer_get_uv(g_discoFloorVerts, i);
            g_discoFloorBaseUVs[i * 2] = uv[0];
            g_discoFloorBaseUVs[i * 2 + 1] = uv[1];
        }
        debugf("Menu: Disco floor UVs initialized: %d verts\n", g_discoFloorVertCount);
        break;
    }
}

static void disco_floor_scroll_uvs(float offset) {
    if (!g_discoFloorBaseUVs || !g_discoFloorVerts) return;

    // Scroll horizontally (S coordinate)
    int16_t scrollAmountS = (int16_t)(offset * 32.0f * 32.0f);

    for (int i = 0; i < g_discoFloorVertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_discoFloorVerts, i);
        uv[0] = g_discoFloorBaseUVs[i * 2] + scrollAmountS;
        uv[1] = g_discoFloorBaseUVs[i * 2 + 1];
    }

    int packedCount = (g_discoFloorVertCount + 1) / 2;
    data_cache_hit_writeback(g_discoFloorVerts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

// Player model and animations
static T3DModel* playerModel = NULL;
static T3DSkeleton playerSkel;
static T3DSkeleton playerSkelBlend;  // Keep for compatibility but not used
static T3DAnim playerAnimIdle;
static T3DAnim playerAnimWalk;
static T3DAnim* currentPlayerAnim = NULL;  // Track currently attached animation
static T3DMat4FP* playerMatFP = NULL;
static bool hasAnimations = false;
static bool isMoving = false;
static bool wasMoving = false;  // Track previous movement state

// Player state
static float playerX = 0.0f;
static float playerY = 0.0f;
static float playerZ = 0.0f;
static float playerAngle = 0.0f;
static float playerVelY = 0.0f;
static bool isGrounded = false;

// Controls
static ControlConfig controlConfig;

// Camera (static position)
static T3DVec3 camPos;
static T3DVec3 camTarget;
static T3DViewport viewport;

// Frame tracking
static int frameIdx = 0;

// Note: fpu_flush_denormals() is now defined in mapData.h

// Menu-specific level loader (doesn't use the save system)
static void menu_load_level(const LevelData* level, MapLoader* loader, MapRuntime* runtime) {
    debugf("Loading menu level: %s\n", level->name);

    // Build ROM paths for segments
    const char* romPaths[MAX_LEVEL_SEGMENTS];
    for (int i = 0; i < level->segmentCount; i++) {
        // level->segments already contains full ROM paths like "rom:/MenuScene.t3dm"
        romPaths[i] = level->segments[i];
    }

    // Load map segments
    maploader_load_simple(loader, romPaths, level->segmentCount);

    // Set collision reference
    runtime->mapLoader = loader;

    // Add decorations
    for (int i = 0; i < level->decorationCount; i++) {
        const DecoPlacement* d = &level->decorations[i];

        int idx = map_add_decoration(runtime, d->type,
            d->x, d->y, d->z, d->rotY,
            d->scaleX, d->scaleY, d->scaleZ);

        // Set transition target if this is a TransitionCollision
        if (idx >= 0 && d->type == DECO_TRANSITIONCOLLISION) {
            runtime->decorations[idx].state.transition.targetLevel = d->targetLevel;
            runtime->decorations[idx].state.transition.targetSpawn = d->targetSpawn;
            debugf("TransitionCollision [%d] configured: Level %d, Spawn %d\n",
                idx, d->targetLevel, d->targetSpawn);
        }

        // Copy activation ID for buttons and activatable objects
        if (idx >= 0 && d->activationId > 0) {
            runtime->decorations[idx].activationId = d->activationId;
            debugf("Decoration [%d] assigned activationId=%d\n", idx, d->activationId);
        }

        // Set script ID for dialogue triggers
        if (idx >= 0 && d->type == DECO_DIALOGUETRIGGER) {
            runtime->decorations[idx].state.dialogueTrigger.scriptId = d->scriptId;
            debugf("DialogueTrigger [%d] assigned scriptId=%d\n", idx, d->scriptId);
        }

        // Set script ID for interact triggers
        if (idx >= 0 && d->type == DECO_INTERACTTRIGGER) {
            runtime->decorations[idx].state.interactTrigger.scriptId = d->scriptId;
            debugf("InteractTrigger [%d] assigned scriptId=%d\n", idx, d->scriptId);
        }
    }

    // Start level music
    level_play_music(level->music);

    debugf("Menu level loaded: %d segments, %d decorations\n",
        level->segmentCount, level->decorationCount);
}

void init_menu_scene(void) {
    // Enable RDP validator to debug hardware crashes
    // rdpq_debug_start();  // DISABLED - validator itself may be causing freezes

    // Ensure FPU flushes denormals to zero
    fpu_flush_denormals();

    // Zero T3D skeleton and animation structures to prevent denormal floats
    memset(&playerSkel, 0, sizeof(T3DSkeleton));
    memset(&playerSkelBlend, 0, sizeof(T3DSkeleton));
    memset(&playerAnimIdle, 0, sizeof(T3DAnim));
    memset(&playerAnimWalk, 0, sizeof(T3DAnim));

    // Zero disco sparkle array to prevent garbage float values
    memset(g_discoSparkles, 0, sizeof(g_discoSparkles));

    frameIdx = 0;

    // Initialize save system if not already done
    if (!g_saveSystem.initialized) {
        save_system_init();
        save_set_level_info(level_get_total_bolts(), level_get_total_screwg(), level_get_real_count());
    }

    // Initialize QR code display (for reward URL if player has 100% completion)
    qr_display_init();

    // Check if we're returning from game with iris effect
    if (menuStartWithIrisOpen) {
        debugf("Menu: menuStartWithIrisOpen=true, enabling iris open\n");
        irisActive = true;
        irisOpening = true;
        irisRadius = 0.0f;  // Start fully closed, will open
        menuStartWithIrisOpen = false;
    } else {
        irisActive = false;
        irisOpening = false;
        irisRadius = (float)SCREEN_WIDTH + 80.0f;
    }

    // Check if we're coming from splash screen (fade in from black)
    if (menuStartWithFadeIn) {
        fadeInActive = true;
        fadeInAlpha = 1.0f;  // Start fully black
        menuStartWithFadeIn = false;
    } else {
        fadeInActive = false;
        fadeInAlpha = 0.0f;
    }

    // Reset menu state
    menuState = MENU_STATE_IDLE;
    menuIsNewGame = false;
    menuSelectedSlot = -1;

    // Initialize controls
    controls_init(&controlConfig);

    // Get player start position from menu level data
    playerX = MENU_LEVEL_DATA.playerStartX;
    playerY = MENU_LEVEL_DATA.playerStartY;
    playerZ = MENU_LEVEL_DATA.playerStartZ;
    playerAngle = 0.0f;
    playerVelY = 0.0f;
    isGrounded = false;
    isMoving = false;
    wasMoving = false;
    currentPlayerAnim = NULL;

    // Static camera position - looking at the menu area
    camPos = (T3DVec3){{0.0f, 100.0f, 200.0f}};  // Backed up, lowered
    camTarget = (T3DVec3){{0.0f, 0.0f, -117.0f}};  // Look at map center, lowered target

    // Initialize viewport
    viewport = t3d_viewport_create_buffered(FB_COUNT);

    // Initialize map loader and decoration runtime
    maploader_init(&mapLoader, FB_COUNT, MENU_VISIBILITY_RANGE);
    map_runtime_init(&mapRuntime, FB_COUNT, MENU_VISIBILITY_RANGE);

    // Load menu level (map segments + decorations)
    menu_load_level(&MENU_LEVEL_DATA, &mapLoader, &mapRuntime);

    // Rotate map 180° and shift right to face camera correctly
    // Use mapRotY for rotation (applies to both visual via seg->rotY and collision)
    mapLoader.mapRotY = T3D_DEG_TO_RAD(180.0f);
    for (int i = 0; i < mapLoader.count; i++) {
        mapLoader.segments[i].rotY = mapLoader.mapRotY;  // Visual rotation must match
        // WORKAROUND: GCC optimizes away += 0, which causes black screen on N64
        // Using tiny non-zero offset forces real write operation
        mapLoader.segments[i].posX += 0.001f;
        mapLoader.segments[i].collisionOffX += 0.001f;
    }
    maploader_rebuild_collision_grids(&mapLoader);

    // Snap player to ground immediately so they don't hover at menu start
    float groundY = maploader_get_ground_height(&mapLoader, playerX, playerY + 100.0f, playerZ);
    debugf("MENU SNAP: pos=(%.1f,%.1f,%.1f) groundY=%.1f\n", playerX, playerY, playerZ, groundY);
    if (groundY > -9000.0f) {
        playerY = groundY + 0.1f;
        debugf("  -> Snapped to Y=%.1f\n", playerY);
        isGrounded = true;
        playerVelY = 0.0f;
    } else {
        debugf("  -> NO GROUND, keeping Y=%.1f\n", playerY);
    }

    // Allocate decoration matrices (extra slots for multi-part decorations like turrets)
    decoMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT * (MAX_DECORATIONS + 60));

    // Allocate player matrices
    playerMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);

    // Load player model based on level's body part
    // 0 = head (uses torso model), 1 = torso, 2 = arms, 3 = legs (fullbody)
    int bodyPart = MENU_LEVEL_DATA.bodyPart;
    const char* modelPath;
    if (bodyPart == 2) {
        modelPath = "rom:/Robo_arms.t3dm";
    } else if (bodyPart == 3) {
        modelPath = "rom:/Robo_fb.t3dm";
    } else {
        modelPath = "rom:/Robo_torso.t3dm";  // HEAD and TORSO use torso model
    }
    debugf("Menu scene: Loading model %s for body part %d\n", modelPath, bodyPart);
    playerModel = t3d_model_load(modelPath);

    hasAnimations = false;
    if (playerModel) {
        playerSkel = t3d_skeleton_create(playerModel);
        playerSkelBlend = t3d_skeleton_clone(&playerSkel, false);

        // Load animations based on level's body part
        if (bodyPart == 2) {
            // Arms mode animations
            playerAnimIdle = t3d_anim_create(playerModel, "arms_idle");
            playerAnimWalk = t3d_anim_create(playerModel, "arms_walk_1");
            debugf("Menu scene: Using arms animations\n");
        } else if (bodyPart == 3) {
            // Fullbody mode animations
            playerAnimIdle = t3d_anim_create(playerModel, "fb_idle");
            playerAnimWalk = t3d_anim_create(playerModel, "fb_walk");
            debugf("Menu scene: Using fullbody animations\n");
        } else {
            // Torso mode animations (default)
            playerAnimIdle = t3d_anim_create(playerModel, "torso_idle");
            playerAnimWalk = t3d_anim_create(playerModel, "torso_walk_fast");
            debugf("Menu scene: Using torso animations\n");
        }

        // Only set up animations if they were successfully created
        if (playerAnimIdle.animRef && playerAnimWalk.animRef) {
            // Set up both animations as looping
            t3d_anim_set_looping(&playerAnimIdle, true);
            t3d_anim_set_looping(&playerAnimWalk, true);

            // Start with idle animation attached
            t3d_anim_attach(&playerAnimIdle, &playerSkel);
            t3d_anim_set_playing(&playerAnimIdle, true);
            currentPlayerAnim = &playerAnimIdle;
            wasMoving = false;

            // Initialize skeleton matrices immediately (prevents garbage data on first frame)
            t3d_anim_update(&playerAnimIdle, 0.0f);
            t3d_skeleton_update(&playerSkel);

            hasAnimations = true;
            debugf("Menu scene: Animations loaded successfully\n");
        } else {
            debugf("Menu scene: Failed to load animations (idle=%p, walk=%p)\n",
                   playerAnimIdle.animRef, playerAnimWalk.animRef);
        }
    }

    // Load disco floor model and matrix
    discoFloorModel = t3d_model_load("rom:/disco_floor.t3dm");
    discoFloorMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
    if (discoFloorModel) {
        disco_floor_init_uvs(discoFloorModel);
        debugf("Menu scene: Disco floor loaded\n");
    }

    // UI sprites and sounds are loaded globally in main.c

    // Initialize UI elements
    dialogue_init(&dialogueBox);
    option_init(&optionPrompt);
    debugf("Menu: optionPrompt after init: active=%d width=%d height=%d x=%d y=%d scale=%.2f\n",
           optionPrompt.active, optionPrompt.width, optionPrompt.height,
           optionPrompt.x, optionPrompt.y, optionPrompt.scale);
    script_init(&activeScript);
    scriptRunning = false;

    // Initialize debug menu with full debug features
    debug_menu_init("DEBUG MENU");
    debug_menu_add_action("Fly Mode", debug_action_toggle_fly_mode);
    debug_menu_add_action("Collision View", debug_action_toggle_collision);
    debug_menu_add_action("Place Mode", debug_action_toggle_placement);
    debug_menu_add_int("Script ID", &debugTriggerScriptId, 0, 99, 1);
    debug_menu_add_float("Trig Radius", &debugTriggerRadius, 10.0f, 200.0f, 10.0f);
    debug_menu_add_action("Print Decos", debug_action_print_code);
    debug_menu_add_action("Close Menu", debug_action_close_menu);

    // Show welcome dialogue only on first visit (never shown before)
    if (!menuHasShownWelcome) {
        dialogue_queue_add(&dialogueBox, "Welcome! Use the computer to change options.", "System");
        dialogue_queue_add(&dialogueBox, "Use the jukebox to play music.", NULL);
        dialogue_queue_add(&dialogueBox, "Use the door to start the game.", NULL);
        dialogue_queue_start(&dialogueBox);
        menuHasShownWelcome = true;
    }

    sceneInitialized = true;
    debugf("Menu scene initialized\n");
}

void deinit_menu_scene(void) {
    if (!sceneInitialized) return;

    // CRITICAL: Set flag to false FIRST to prevent draw/update from
    // accessing resources while they're being freed
    sceneInitialized = false;

    // CRITICAL: Wait for BOTH RSP and RDP to finish ALL operations before freeing resources.
    // rdpq_fence() does NOT block the CPU - it only schedules a fence command in the queue.
    // rspq_wait() actually blocks and waits for both RSP and RDP to complete all queued work.
    rspq_wait();

    // Free lazy-loaded UI sounds to reduce open wav64 file count
    ui_free_key_sounds();

    // Stop music before freeing resources
    level_stop_music();

    // Reset party mode and related states for next visit
    partyModeActive = false;
    partyLightAngle = 0.0f;
    partyFogIntensity = 0.0f;
    partyFogHue = 0.0f;

    // Reset jukebox and disco ball states
    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (deco->active) {
            if (deco->type == DECO_JUKEBOX) {
                deco->state.jukebox.isPlaying = false;
                deco->state.jukebox.blendIn = 0.0f;
                deco->state.jukebox.textureOffset = 0.0f;
            } else if (deco->type == DECO_DISCOBALL) {
                deco->state.discoBall.isActive = false;
                deco->state.discoBall.isSpinning = false;
                deco->state.discoBall.currentY = 0.0f;
                deco->state.discoBall.rotation = 0.0f;
            }
        }
    }

    // Free map loader and runtime
    maploader_free(&mapLoader);
    map_runtime_free(&mapRuntime);
    collision_loader_free_all();  // Free collision meshes to prevent stale pointer reuse

    if (decoMatFP) {
        free_uncached(decoMatFP);
        decoMatFP = NULL;
    }

    if (playerModel) {
        if (hasAnimations) {
            t3d_anim_destroy(&playerAnimIdle);
            t3d_anim_destroy(&playerAnimWalk);
        }
        t3d_skeleton_destroy(&playerSkel);
        t3d_skeleton_destroy(&playerSkelBlend);
        t3d_model_free(playerModel);
        playerModel = NULL;
        hasAnimations = false;
    }

    if (playerMatFP) {
        free_uncached(playerMatFP);
        playerMatFP = NULL;
    }

    // Clean up disco floor
    if (discoFloorModel) {
        t3d_model_free(discoFloorModel);
        discoFloorModel = NULL;
    }
    if (discoFloorMatFP) {
        free_uncached(discoFloorMatFP);
        discoFloorMatFP = NULL;
    }
    if (g_discoFloorBaseUVs) {
        free(g_discoFloorBaseUVs);
        g_discoFloorBaseUVs = NULL;
        g_discoFloorVerts = NULL;
        g_discoFloorVertCount = 0;
    }

    // Clean up monitor screen UV cache
    if (g_menuMonitorBaseUVs) {
        free(g_menuMonitorBaseUVs);
        g_menuMonitorBaseUVs = NULL;
        g_menuMonitorVerts = NULL;
        g_menuMonitorVertCount = 0;
    }

    // Clean up jukebox FX UV cache
    for (int p = 0; p < g_jukeboxFxPartCount; p++) {
        if (g_jukeboxFxBaseUVs[p]) {
            free(g_jukeboxFxBaseUVs[p]);
            g_jukeboxFxBaseUVs[p] = NULL;
        }
        g_jukeboxFxVertCount[p] = 0;
        g_jukeboxFxVerts[p] = NULL;
    }
    g_jukeboxFxPartCount = 0;

    // UI sprites and sounds are managed globally in main.c

    t3d_viewport_destroy(&viewport);

    debugf("Menu scene deinitialized\n");
}

// ============================================================
// MENU FLOW HANDLERS
// ============================================================

// Build save slot label with stats
static void build_save_label(int slot, char* buffer, size_t bufSize) {
    if (!save_slot_has_data(slot)) {
        snprintf(buffer, bufSize, "Slot %d: Empty", slot + 1);
        return;
    }

    SaveFile* save = &g_saveSystem.saves[slot];
    int percent = save_calc_percentage(save);

    snprintf(buffer, bufSize, "Slot %d: %d%%", slot + 1, percent);
}

// Called when main menu choice (New Game / Load Game) is made
static void on_main_menu_choice(int choice);
// Called when active game menu choice (Continue / Level Select) is made
static void on_active_game_menu_choice(int choice);
// Called when save slot is selected
static void on_save_slot_selected(int slot);
// Called when confirm overwrite choice is made
static void on_confirm_overwrite(int choice);
// Called when confirm load choice is made (after seeing details)
static void on_confirm_load(int choice);
// Shows save details before loading/overwriting
static void show_save_details(int slot);
// Called when level is selected from level select menu
static void on_level_selected(int choice);

static void show_main_menu(void) {
    menuState = MENU_STATE_MAIN_CHOICE;
    option_set_title(&optionPrompt, "");

    if (gameSessionActive) {
        // After loading/starting a game, show Continue, Level Select, 2 Player
        option_add(&optionPrompt, "Continue");
        option_add(&optionPrompt, "Level Select");
        option_add(&optionPrompt, "2 Player");
        option_show(&optionPrompt, on_active_game_menu_choice, NULL);
    } else {
        // Initial menu - Load Game, New Game, 2 Player
        option_add(&optionPrompt, "Load Game");
        option_add(&optionPrompt, "New Game");
        option_add(&optionPrompt, "2 Player");
        option_show(&optionPrompt, on_main_menu_choice, NULL);
    }
}

static void show_save_selection(bool isNewGame) {
    menuIsNewGame = isNewGame;
    menuState = MENU_STATE_SELECT_SAVE;

    char label0[32], label1[32], label2[32];
    build_save_label(0, label0, sizeof(label0));
    build_save_label(1, label1, sizeof(label1));
    build_save_label(2, label2, sizeof(label2));

    option_set_title(&optionPrompt, isNewGame ? "Select Slot" : "Load Game");
    option_add(&optionPrompt, label0);
    option_add(&optionPrompt, label1);
    option_add(&optionPrompt, label2);
    option_show(&optionPrompt, on_save_slot_selected, NULL);
}

static void show_confirm_overwrite(int slot) {
    menuSelectedSlot = slot;
    menuState = MENU_STATE_CONFIRM_NEW;

    SaveFile* save = &g_saveSystem.saves[slot];
    int percent = save_calc_percentage(save);

    char msg[64];
    snprintf(msg, sizeof(msg), "Overwrite Slot %d (%d%%)?", slot + 1, percent);

    option_set_title(&optionPrompt, msg);
    option_add(&optionPrompt, "Yes, start new");
    option_add(&optionPrompt, "No, go back");
    option_show(&optionPrompt, on_confirm_overwrite, NULL);
}

static void start_game_with_save(int slot, bool isNewGame) {
    menuState = MENU_STATE_STARTING_GAME;

    if (isNewGame) {
        // Create new save file and start at level 1
        save_create_new(slot);
        selectedLevelID = 0;  // LEVEL_1 = 0
        debugf("Starting new game in slot %d, level 1\n", slot);
    } else {
        // Load existing save
        save_load(slot);

        // Determine which level to start at based on progress
        SaveFile* save = save_get_active();
        if (save) {
            // Find the first uncompleted level, or use currentLevel
            int targetLevel = save->currentLevel;

            // If current level is completed, advance to next uncompleted level
            for (int i = 0; i < SAVE_MAX_LEVELS; i++) {
                if (!save->levels[i].completed) {
                    targetLevel = i;
                    break;
                }
            }

            selectedLevelID = targetLevel;
            debugf("Loading save slot %d, going to level %d\n", slot, targetLevel + 1);
        } else {
            selectedLevelID = 0;
        }
    }

    // Mark game session as active (for Continue/Level Select menu)
    gameSessionActive = true;

    // Start iris transition (will change scene when fully closed)
    menuState = MENU_STATE_IRIS_OUT;
    irisActive = true;
    irisRadius = (float)SCREEN_WIDTH + 80.0f;
    irisCenterX = (float)SCREEN_CENTER_X;
    irisCenterY = (float)SCREEN_CENTER_Y;

    // Tell game scene to open with iris effect
    startWithIrisOpen = true;
}

// Start a specific level (used by Continue and Level Select)
static void start_level(int levelId) {
    selectedLevelID = levelId;

    // Start iris transition
    menuState = MENU_STATE_IRIS_OUT;
    irisActive = true;
    irisRadius = (float)SCREEN_WIDTH + 80.0f;
    irisCenterX = (float)SCREEN_CENTER_X;
    irisCenterY = (float)SCREEN_CENTER_Y;
    startWithIrisOpen = true;

    debugf("Starting level %d\n", levelId + 1);
}

static void on_main_menu_choice(int choice) {
    if (choice == 0) {
        // Load Game - check if any saves exist
        bool hasAnySave = false;
        for (int i = 0; i < SAVE_FILE_COUNT; i++) {
            if (save_slot_has_data(i)) {
                hasAnySave = true;
                break;
            }
        }

        if (hasAnySave) {
            show_save_selection(false);
        } else {
            dialogue_show(&dialogueBox, "No saved games found. Start a New Game first!", "System");
            menuState = MENU_STATE_IDLE;
        }
    } else if (choice == 1) {
        // New Game - show save selection
        show_save_selection(true);
    } else if (choice == 2) {
        // 2 Player - start multiplayer mode (starts on Level 2 = torso stage)
        multiplayerLevelID = 1;
        change_scene(MULTIPLAYER_SCENE);
    }
}

static void on_save_slot_selected(int slot) {
    menuSelectedSlot = slot;

    if (menuIsNewGame) {
        // New game - check if slot has data
        if (save_slot_has_data(slot)) {
            // Show details first, then ask for confirmation to overwrite
            show_save_details(slot);
        } else {
            // Empty slot, start immediately
            start_game_with_save(slot, true);
        }
    } else {
        // Load game - check if slot has data
        if (save_slot_has_data(slot)) {
            // Show details first, then ask for confirmation to load
            show_save_details(slot);
        } else {
            dialogue_show(&dialogueBox, "This slot is empty!", "System");
            // Show save selection again
            show_save_selection(false);
        }
    }
}

static void on_confirm_overwrite(int choice) {
    if (choice == 0) {
        // Yes, overwrite
        start_game_with_save(menuSelectedSlot, true);
    } else {
        // No, go back to save selection
        show_save_selection(true);
    }
}

// Show save details with Yes/No confirmation using option prompt
// Stats shown in title, options are Yes/No
static void show_save_details(int slot) {
    menuSelectedSlot = slot;
    SaveFile* save = &g_saveSystem.saves[slot];

    int percent = save_calc_percentage(save);

    static char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "Slot %d: %d%%", slot + 1, percent);

    menuState = MENU_STATE_SAVE_DETAILS;
    option_set_title(&optionPrompt, titleBuf);

    if (menuIsNewGame) {
        option_add(&optionPrompt, "Yes, overwrite");
        option_add(&optionPrompt, "No, go back");
    } else {
        option_add(&optionPrompt, "Yes, load");
        option_add(&optionPrompt, "No, go back");
    }
    option_show(&optionPrompt, on_confirm_load, NULL);
}

static void on_confirm_load(int choice) {
    if (choice == 0) {
        // Yes - load or start new game depending on mode
        start_game_with_save(menuSelectedSlot, menuIsNewGame);
    } else {
        // No, go back to save selection
        show_save_selection(menuIsNewGame);
    }
}

// Get the next level the player should play (first uncompleted, or level 1)
static int get_continue_level(void) {
    SaveFile* save = save_get_active();
    if (!save) return 0;

    // Find first uncompleted level
    for (int i = 0; i < REAL_LEVEL_COUNT && i < SAVE_MAX_LEVELS; i++) {
        if (!save->levels[i].completed) {
            return i;
        }
    }

    // All levels completed - return last level
    return REAL_LEVEL_COUNT > 0 ? REAL_LEVEL_COUNT - 1 : 0;
}

// Show level select menu with completed levels
static void show_level_select(void) {
    menuState = MENU_STATE_LEVEL_SELECT;

    SaveFile* save = save_get_active();
    int completedCount = save ? save_count_completed_levels(save) : 0;

    if (completedCount == 0) {
        // No levels completed - show message and return to main menu
        dialogue_show(&dialogueBox, "No levels completed!", "System");
        menuState = MENU_STATE_IDLE;
        return;
    }

    option_set_title(&optionPrompt, "Level Select");

    // Add completed levels as options using actual level names
    for (int i = 0; i < REAL_LEVEL_COUNT && i < SAVE_MAX_LEVELS; i++) {
        if (save && save->levels[i].completed) {
            // Use the actual level name from the level data
            const char* levelName = ALL_LEVELS[i]->name;
            option_add(&optionPrompt, levelName);
        }
    }

    option_show(&optionPrompt, on_level_selected, NULL);
}

static void on_active_game_menu_choice(int choice) {
    if (choice == 0) {
        // Continue - go to next uncompleted level
        int targetLevel = get_continue_level();
        start_level(targetLevel);
    } else if (choice == 1) {
        // Level Select
        show_level_select();
    } else if (choice == 2) {
        // 2 Player - start multiplayer mode (starts on Level 2 = torso stage)
        multiplayerLevelID = 1;
        change_scene(MULTIPLAYER_SCENE);
    }
}

static void on_level_selected(int choice) {
    // Map the choice index to actual level ID
    // Choice is 0-based index into the list of completed levels
    SaveFile* save = save_get_active();
    if (!save) {
        menuState = MENU_STATE_IDLE;
        return;
    }

    int completedIdx = 0;
    for (int i = 0; i < REAL_LEVEL_COUNT && i < SAVE_MAX_LEVELS; i++) {
        if (save->levels[i].completed) {
            if (completedIdx == choice) {
                // Found the level - start it
                start_level(i);
                return;
            }
            completedIdx++;
        }
    }

    // Shouldn't happen, but fallback to idle
    menuState = MENU_STATE_IDLE;
}

// ============================================================
// OPTIONS MENU (Computer interaction)
// ============================================================

static void on_options_choice(int choice);
static void on_options_leftright(int encoded);
static void on_options_cancel(int choice);
static void on_delete_save_slot(int slot);
static void on_confirm_delete(int choice);

static void show_options_menu(void) {
    menuState = MENU_STATE_OPTIONS;
    option_set_title(&optionPrompt, "Options");

    // Build volume labels with current values
    char musicLabel[32], sfxLabel[32];
    snprintf(musicLabel, sizeof(musicLabel), "Music Volume: %d", save_get_music_volume());
    snprintf(sfxLabel, sizeof(sfxLabel), "SFX Volume: %d", save_get_sfx_volume());

    option_add(&optionPrompt, musicLabel);
    option_add(&optionPrompt, sfxLabel);
    option_add(&optionPrompt, "Delete Save");
    option_add(&optionPrompt, "Credits");
    option_add(&optionPrompt, "Back");

    // Set up left/right callback for volume adjustment
    option_set_leftright(&optionPrompt, on_options_leftright);
    option_show(&optionPrompt, on_options_choice, on_options_cancel);
}

static void on_options_choice(int choice) {
    switch (choice) {
        case 0: {
            // Music Volume - cycle through 0-10
            int vol = save_get_music_volume();
            vol = (vol + 1) % 11;  // Cycle 0-10
            save_set_music_volume(vol);
            // Settings saved when leaving options menu to reduce EEPROM wear
            // Refresh menu to show new value
            show_options_menu();
            break;
        }
        case 1: {
            // SFX Volume - cycle through 0-10
            int vol = save_get_sfx_volume();
            vol = (vol + 1) % 11;  // Cycle 0-10
            save_set_sfx_volume(vol);
            // Settings saved when leaving options menu to reduce EEPROM wear
            // Play a test sound so user can hear the volume
            ui_play_hover_sound();
            // Refresh menu to show new value
            show_options_menu();
            break;
        }
        case 2: {
            // Delete Save - show save slot selection
            menuState = MENU_STATE_DELETE_SAVE;

            char label0[32], label1[32], label2[32];
            build_save_label(0, label0, sizeof(label0));
            build_save_label(1, label1, sizeof(label1));
            build_save_label(2, label2, sizeof(label2));

            option_set_title(&optionPrompt, "Delete Save");
            option_add(&optionPrompt, label0);
            option_add(&optionPrompt, label1);
            option_add(&optionPrompt, label2);
            option_add(&optionPrompt, "Cancel");
            option_show(&optionPrompt, on_delete_save_slot, NULL);
            break;
        }
        case 3: {
            // Credits
            dialogue_queue_add(&dialogueBox, "BotBot!64", "Credits");
            dialogue_queue_add(&dialogueBox, "Programmers: Cypress, Nupi", NULL);
            dialogue_queue_add(&dialogueBox, "3D Modelers: DC.all, StatycTyr", NULL);
            dialogue_queue_add(&dialogueBox, "Composer/Sound: DakodaComposer", NULL);
            dialogue_queue_add(&dialogueBox, "Created for N64brew Game Jam 2025", NULL);
            dialogue_queue_add(&dialogueBox, "Thanks for playing!", NULL);
            dialogue_queue_start(&dialogueBox);
            menuState = MENU_STATE_IDLE;
            break;
        }
        case 4:
        default:
            // Back / Cancel - save settings before closing
            save_write_settings();
            menuState = MENU_STATE_IDLE;
            break;
    }
}

// Helper to update volume option labels in-place
static void update_volume_labels(void) {
    // Update option labels directly (option 0 = music, option 1 = sfx)
    snprintf(optionPrompt.options[0], UI_MAX_OPTION_LENGTH, "Music Volume: %d", save_get_music_volume());
    snprintf(optionPrompt.options[1], UI_MAX_OPTION_LENGTH, "SFX Volume: %d", save_get_sfx_volume());
}

// Left/Right callback for volume adjustment
static void on_options_leftright(int encoded) {
    int direction;
    int index = option_decode_leftright(encoded, &direction);

    switch (index) {
        case 0: {
            // Music Volume
            int vol = save_get_music_volume();
            vol += direction;
            if (vol < 0) vol = 0;
            if (vol > 10) vol = 10;
            save_set_music_volume(vol);
            update_volume_labels();
            break;
        }
        case 1: {
            // SFX Volume
            int vol = save_get_sfx_volume();
            vol += direction;
            if (vol < 0) vol = 0;
            if (vol > 10) vol = 10;
            save_set_sfx_volume(vol);
            update_volume_labels();
            // Play test sound so user can hear the new volume
            ui_play_hover_sound();
            break;
        }
        default:
            // Other options don't use left/right
            break;
    }
}

// Cancel callback for options menu (B button)
static void on_options_cancel(int choice) {
    (void)choice;  // Unused
    // Save settings before closing
    save_write_settings();
    // Note: option_close() is already called by ui.h before this callback
    // Just reset menu state
    menuState = MENU_STATE_IDLE;
}

// ============================================================
// JUKEBOX (Sound Test)
// ============================================================

static void on_jukebox_choice(int choice);
static void on_jukebox_leftright(int encoded);
static void on_jukebox_cancel(int choice);

// Helper to update jukebox option labels
static void update_jukebox_labels(void) {
    char musicLabel[UI_MAX_OPTION_LENGTH];
    char sfxLabel[UI_MAX_OPTION_LENGTH];
    snprintf(musicLabel, UI_MAX_OPTION_LENGTH, "Music: %s", JUKEBOX_MUSIC_NAMES[jukeboxMusicIndex]);
    snprintf(sfxLabel, UI_MAX_OPTION_LENGTH, "SFX: %s", JUKEBOX_SFX_NAMES[jukeboxSfxIndex]);

    // Copy to option labels (option 0 = music, option 1 = sfx)
    snprintf(optionPrompt.options[0], UI_MAX_OPTION_LENGTH, "%s", musicLabel);
    snprintf(optionPrompt.options[1], UI_MAX_OPTION_LENGTH, "%s", sfxLabel);
}

// Exit jukebox - let the music keep playing so player can chill
static void jukebox_exit(void) {
    // Stop any playing SFX (but let music continue)
    if (jukeboxSfxLoaded) {
        mixer_ch_stop(1);  // Stop SFX channel
        rspq_wait();       // Wait for RSP before closing wav64
        wav64_close(&jukeboxSfx);
        jukeboxSfxLoaded = false;
    }

    // Stop SFX channels only (1-7), leave music channel (0) alone
    for (int i = 1; i < 8; i++) {
        mixer_ch_stop(i);
    }

    // Reset menu size back to defaults
    optionPrompt.width = 160;
    optionPrompt.x = 80;
    optionPrompt.y = 80;
    optionPrompt.itemSpacing = 20;

    // Reset jukebox-specific behavior flags (these were set in show_jukebox_menu)
    optionPrompt.stayOpenOnSelect = false;
    optionPrompt.suppressSelectSound = false;

    // Reset animation state to ensure clean transition
    optionPrompt.animState = UI_ANIM_NONE;
    optionPrompt.scale = 1.0f;

    // Don't restart menu music - let the selected track keep playing
    menuState = MENU_STATE_IDLE;
}

static void show_jukebox_menu(void) {
    menuState = MENU_STATE_JUKEBOX;

    // CRITICAL: Clear animation state BEFORE changing geometry to prevent denormal float crash
    // Jukebox is the only menu that modifies width before option_show(), so stale scale values
    // from previous menu animation can cause trunc.w.s exception when computing scaledW
    optionPrompt.animState = UI_ANIM_NONE;
    optionPrompt.animTime = 0.0f;
    optionPrompt.scale = 1.0f;

    // Don't stop music - let whatever is playing continue
    // Player can select a new track if they want

    option_set_title(&optionPrompt, "Jukebox");

    // Make jukebox menu wider to fit long track names (but fit screen width)
    int jukeboxWidth = (SCREEN_WIDTH >= 280) ? 260 : (SCREEN_WIDTH - 20);
    optionPrompt.width = jukeboxWidth;
    optionPrompt.x = (SCREEN_WIDTH - jukeboxWidth) / 2;  // Center on screen
    optionPrompt.y = 60;
    optionPrompt.itemSpacing = 25;

    // Build initial labels
    char musicLabel[UI_MAX_OPTION_LENGTH];
    char sfxLabel[UI_MAX_OPTION_LENGTH];
    snprintf(musicLabel, UI_MAX_OPTION_LENGTH, "Music: %s", JUKEBOX_MUSIC_NAMES[jukeboxMusicIndex]);
    snprintf(sfxLabel, UI_MAX_OPTION_LENGTH, "SFX: %s", JUKEBOX_SFX_NAMES[jukeboxSfxIndex]);

    option_add(&optionPrompt, musicLabel);
    option_add(&optionPrompt, sfxLabel);
    option_add(&optionPrompt, "Back");

    // Set up left/right callback for track selection
    option_set_leftright(&optionPrompt, on_jukebox_leftright);

    // Jukebox stays open on A press and doesn't play the default select sound
    optionPrompt.stayOpenOnSelect = true;
    optionPrompt.suppressSelectSound = true;

    option_show(&optionPrompt, on_jukebox_choice, on_jukebox_cancel);
}

// Left/Right callback for jukebox track selection
static void on_jukebox_leftright(int encoded) {
    int direction;
    int index = option_decode_leftright(encoded, &direction);

    switch (index) {
        case 0: {
            // Music track selection
            jukeboxMusicIndex += direction;
            if (jukeboxMusicIndex < 0) jukeboxMusicIndex = JUKEBOX_MUSIC_COUNT - 1;
            if (jukeboxMusicIndex >= JUKEBOX_MUSIC_COUNT) jukeboxMusicIndex = 0;
            update_jukebox_labels();
            break;
        }
        case 1: {
            // SFX selection
            jukeboxSfxIndex += direction;
            if (jukeboxSfxIndex < 0) jukeboxSfxIndex = JUKEBOX_SFX_COUNT - 1;
            if (jukeboxSfxIndex >= JUKEBOX_SFX_COUNT) jukeboxSfxIndex = 0;
            update_jukebox_labels();
            break;
        }
        default:
            break;
    }
}

// A button pressed on jukebox option - play the selected track
static void on_jukebox_choice(int choice) {
    switch (choice) {
        case 0: {
            // Play selected music (menu stays open due to stayOpenOnSelect)
            level_play_music(JUKEBOX_MUSIC[jukeboxMusicIndex]);
            // Activate jukebox animation and party mode
            for (int i = 0; i < mapRuntime.decoCount; i++) {
                DecoInstance* deco = &mapRuntime.decorations[i];
                if (deco->active && deco->type == DECO_JUKEBOX) {
                    // Start jukebox animation (reset blend if first time)
                    if (!deco->state.jukebox.isPlaying) {
                        deco->state.jukebox.blendIn = 0.0f;
                        // Reset animation to start
                        DecoTypeRuntime* decoType = &mapRuntime.decoTypes[DECO_JUKEBOX];
                        if (decoType->loaded && decoType->animCount > 0) {
                            t3d_anim_set_time(&decoType->anims[decoType->currentAnim], 0.0f);
                        }
                    }
                    deco->state.jukebox.isPlaying = true;
                    // Activate party mode lights
                    if (!partyModeActive) {
                        partyJukeboxX = deco->posX;
                        partyJukeboxY = deco->posY;
                        partyJukeboxZ = deco->posZ;
                        partyModeActive = true;
                    }
                    break;
                }
            }
            break;
        }
        case 1: {
            // Play selected SFX (menu stays open due to stayOpenOnSelect)
            // Stop previous SFX if loaded
            if (jukeboxSfxLoaded) {
                mixer_ch_stop(1);
                rspq_wait();  // Wait for RSP before closing wav64
                wav64_close(&jukeboxSfx);
                jukeboxSfxLoaded = false;
            }
            // Load and play new SFX
            wav64_open(&jukeboxSfx, JUKEBOX_SFX[jukeboxSfxIndex]);
            jukeboxSfxLoaded = true;
            wav64_play(&jukeboxSfx, 1);  // Play on channel 1
            break;
        }
        case 2:
        default:
            // Back - manually close menu and exit jukebox
            optionPrompt.stayOpenOnSelect = false;  // Allow close
            option_close(&optionPrompt);
            // jukebox_exit will be called when animation finishes via pendingCallback
            optionPrompt.pendingCallback = on_jukebox_cancel;
            optionPrompt.pendingSelection = 0;
            break;
    }
}

// Cancel callback for jukebox (B button)
static void on_jukebox_cancel(int choice) {
    (void)choice;
    jukebox_exit();
}

static void on_delete_save_slot(int slot) {
    if (slot == 3) {
        // Cancel
        menuState = MENU_STATE_IDLE;
        return;
    }

    if (!save_slot_has_data(slot)) {
        dialogue_show(&dialogueBox, "This slot is already empty!", "System");
        menuState = MENU_STATE_IDLE;
        return;
    }

    // Confirm deletion
    menuSelectedSlot = slot;
    menuState = MENU_STATE_CONFIRM_DELETE;

    SaveFile* save = &g_saveSystem.saves[slot];
    int percent = save_calc_percentage(save);

    char msg[64];
    snprintf(msg, sizeof(msg), "Delete Slot %d (%d%%)?", slot + 1, percent);

    option_set_title(&optionPrompt, msg);
    option_add(&optionPrompt, "Yes, delete");
    option_add(&optionPrompt, "No, keep it");
    option_show(&optionPrompt, on_confirm_delete, NULL);
}

static void on_confirm_delete(int choice) {
    if (choice == 0) {
        // Yes, delete
        save_delete(menuSelectedSlot);
        dialogue_show(&dialogueBox, "Save data deleted.", "System");
    }
    // Either way, return to idle
    menuState = MENU_STATE_IDLE;
}

// Build and start a script by ID
// Scripts are defined here - in a real game you might load these from data files
static void start_script(int scriptId) {
    script_init(&activeScript);

    switch (scriptId) {
        case 0: {
            // Script 0: Main Menu - use the proper menu flow
            show_main_menu();
            return;  // Don't use script system for this
        }
        case 1: {
            // Script 1: NPC greeting with branching
            // Node 0: greeting, leads to node 1 (options)
            script_add_dialogue(&activeScript, "Hello there, traveler!", "Robot", 1);
            // Node 1: options menu
            int askNode = script_add_options(&activeScript, "What would you like to know?");
            // Node 2: about response
            int aboutNode = script_add_dialogue(&activeScript, "I'm a helper robot. I give hints!", "Robot", -1);
            // Node 3: hint response
            int hintNode = script_add_dialogue(&activeScript, "Try exploring all the areas!", "Robot", -1);
            // Node 4: goodbye response
            int byeNode = script_add_dialogue(&activeScript, "See you later!", "Robot", -1);

            script_node_add_option(&activeScript, askNode, "Who are you?", aboutNode);
            script_node_add_option(&activeScript, askNode, "Any hints?", hintNode);
            script_node_add_option(&activeScript, askNode, "Goodbye", byeNode);
            break;
        }
        case 2: {
            // Script 2: Simple sign/info
            script_add_dialogue(&activeScript, "Welcome to the Scrapyard!", NULL, 1);
            script_add_dialogue(&activeScript, "Collect bolts and avoid hazards.", NULL, 2);
            script_add_dialogue(&activeScript, "Good luck!", NULL, -1);
            break;
        }
        case 3: {
            // Script 3: Options Menu (computer interaction)
            show_options_menu();
            return;  // Don't use script system for this
        }
        case 4: {
            // Script 4: Jukebox (sound test)
            show_jukebox_menu();
            return;  // Don't use script system for this
        }
        default: {
            // Unknown script - show error
            script_add_dialogue(&activeScript, "Script not found!", "Error", -1);
            break;
        }
    }

    script_start(&activeScript, &dialogueBox, &optionPrompt);
    scriptRunning = true;
}


// ============================================================
// DEBUG MENU FUNCTIONS
// ============================================================

// Action callbacks for debug menu
static void debug_action_toggle_fly_mode(void) {
    menuDebugFlyMode = !menuDebugFlyMode;
    if (menuDebugFlyMode) {
        // Initialize debug camera at current view
        debugCamX = camPos.v[0];
        debugCamY = camPos.v[1];
        debugCamZ = camPos.v[2];
        debugCamYaw = 3.14159f;
        debugCamPitch = 0.0f;
        debugPlacementMode = false;
    }
    debugf("Fly mode: %s\n", menuDebugFlyMode ? "ON" : "OFF");
}

static void debug_action_toggle_collision(void) {
    debugShowCollision = !debugShowCollision;
    debugf("Collision view: %s\n", debugShowCollision ? "ON" : "OFF");
}

static void debug_action_toggle_placement(void) {
    if (!menuDebugFlyMode) {
        debugf("Enter Fly Mode first!\n");
        return;
    }
    debugPlacementMode = !debugPlacementMode;
    if (debugPlacementMode) {
        float sinYaw = sinf(debugCamYaw);
        float cosYaw = cosf(debugCamYaw);
        debugDecoX = debugCamX + sinYaw * 50.0f;
        debugDecoY = debugCamY - 20.0f;
        debugDecoZ = debugCamZ - cosYaw * 50.0f;
        debugDecoRotY = 0.0f;
        debugDecoScaleX = 1.0f;
        debugDecoScaleY = 1.0f;
        debugDecoScaleZ = 1.0f;
    }
    debugf("Placement mode: %s\n", debugPlacementMode ? "ON" : "OFF");
}

static void debug_action_print_code(void) {
    // Print all decorations
    map_print_all_decorations(&mapRuntime);
}

static void debug_action_close_menu(void) {
    debug_menu_close();
}

// Custom draw for menu scene debug info
static void menu_debug_draw_extra(void) {
    if (!psyops_is_unlocked()) return;

    // Show debug fly mode info
    if (menuDebugFlyMode && !debug_menu_is_open()) {
        rdpq_set_prim_color(RGBA32(0xFF, 0xFF, 0x00, 0xFF));
        const char* modeStr = debugPlacementMode ? "[PLACE]" : "[CAM]";
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 22,
            "FLY %s  Pos: %.0f, %.0f, %.0f", modeStr, debugCamX, debugCamY, debugCamZ);

        if (debugPlacementMode) {
            // Show decoration type and placement info
            const DecoTypeDef* typeDef = &DECO_TYPES[debugDecoType];
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 34,
                "Type: %s", typeDef->name);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 46,
                "Deco: %.0f, %.0f, %.0f  Rot:%.1f",
                debugDecoX, debugDecoY, debugDecoZ, debugDecoRotY);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 58,
                "Scale: %.1f, %.1f, %.1f",
                debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ);

            // Control hints
            rdpq_set_prim_color(RGBA32(0x80, 0xFF, 0x80, 0xFF));
            rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, 10, 72, "l");
            rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, 26, 72, "r");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 42, 72, "  Type");
            rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, 90, 72, "a");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 106, 72, "  Place");
            rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, 160, 72, "b");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 176, 72, "  Exit");
        } else {
            // Camera mode control hints
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 34,
                "Collision: %s", debugShowCollision ? "ON" : "OFF");

            rdpq_set_prim_color(RGBA32(0x80, 0xFF, 0x80, 0xFF));
            rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, 10, 50, "b");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 26, 50, "  Place Mode");
            rdpq_text_printf(NULL, DEBUG_BUTTON_FONT, 110, 50, "fh");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 142, 50, "  Col/Del");

            // Show highlighted decoration info
            if (debugHighlightedDecoIndex >= 0) {
                DecoInstance* hlDeco = &mapRuntime.decorations[debugHighlightedDecoIndex];
                const DecoTypeDef* hlDef = &DECO_TYPES[hlDeco->type];
                rdpq_set_prim_color(RGBA32(0xFF, 0x80, 0x80, 0xFF));
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 62,
                    "Select: [%d] %s", debugHighlightedDecoIndex, hlDef->name);
            }
        }
    }

    // Show player position when menu is closed and not in fly mode
    if (!debug_menu_is_open() && !menuDebugFlyMode) {
        rdpq_set_prim_color(RGBA32(0x80, 0x80, 0x80, 0xFF));
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 5, 230,
            "Pos: %.0f, %.0f, %.0f", playerX, playerY, playerZ);
    }
}

void update_menu_scene(void) {
    if (!sceneInitialized) return;

    joypad_poll();
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    float deltaTime = 1.0f / 30.0f;

    // Handle fade-in from black (coming from splash screen)
    if (fadeInActive) {
        fadeInAlpha -= deltaTime / MENU_FADE_IN_TIME;
        if (fadeInAlpha <= 0.0f) {
            fadeInAlpha = 0.0f;
            fadeInActive = false;
        }
    }

    // Handle iris opening (returning from game)
    if (irisActive && irisOpening) {
        // Expand iris from center
        float maxIrisRadius = (float)SCREEN_WIDTH + 80.0f;
        float expandSpeed = (maxIrisRadius - irisRadius) * 0.08f;
        if (expandSpeed < 8.0f) expandSpeed = 8.0f;
        irisRadius += expandSpeed;

        if (irisRadius >= maxIrisRadius) {
            irisRadius = maxIrisRadius;
            irisActive = false;
            irisOpening = false;
        }
        // Continue with update - just blocks input via irisOpening check below
    }

    // Cheat code detection for giant/tiny mode
    // Track C-Up, C-Down, A, B presses and check against codes
    {
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        uint16_t cheatInput = 0;
        if (pressed.c_up) cheatInput = 0x0008;
        else if (pressed.c_down) cheatInput = 0x0004;
        else if (pressed.a) cheatInput = 0x8000;
        else if (pressed.b) cheatInput = 0x4000;

        if (cheatInput != 0) {
            // Shift buffer and add new input
            for (int i = 0; i < SIZE_CHEAT_LEN - 1; i++) {
                sizeCheatBuffer[i] = sizeCheatBuffer[i + 1];
            }
            sizeCheatBuffer[SIZE_CHEAT_LEN - 1] = cheatInput;

            // Check for giant code match
            bool giantMatch = true;
            for (int i = 0; i < SIZE_CHEAT_LEN; i++) {
                if (sizeCheatBuffer[i] != GIANT_CODE[i]) { giantMatch = false; break; }
            }
            if (giantMatch) {
                g_playerScaleCheat = 2.0f;
                debugf("GIANT MODE ACTIVATED!\n");
                // Clear buffer to prevent re-triggering
                for (int i = 0; i < SIZE_CHEAT_LEN; i++) sizeCheatBuffer[i] = 0;
            }

            // Check for tiny code match
            bool tinyMatch = true;
            for (int i = 0; i < SIZE_CHEAT_LEN; i++) {
                if (sizeCheatBuffer[i] != TINY_CODE[i]) { tinyMatch = false; break; }
            }
            if (tinyMatch) {
                g_playerScaleCheat = 0.5f;
                debugf("TINY MODE ACTIVATED!\n");
                // Clear buffer to prevent re-triggering
                for (int i = 0; i < SIZE_CHEAT_LEN; i++) sizeCheatBuffer[i] = 0;
            }
        }
    }

    // Handle iris transition (going to game)
    if (menuState == MENU_STATE_IRIS_OUT) {
        // Shrink iris toward center
        float shrinkSpeed = irisRadius * 0.08f;
        if (shrinkSpeed < 8.0f) shrinkSpeed = 8.0f;
        irisRadius -= shrinkSpeed;

        if (irisRadius <= 0.0f) {
            irisRadius = 0.0f;
            // Iris fully closed - transition to game scene
            change_scene(GAME);
        }

        // Skip all other updates during transition
        goto update_audio;
    }

    // Check debug menu first (shared module)
    if (debug_menu_update(JOYPAD_PORT_1, deltaTime)) {
        // Debug menu consumed input, skip normal input but still update physics
        goto update_physics;
    }

    // === DEBUG FLY MODE ===
    if (menuDebugFlyMode && !debug_menu_is_open()) {
        joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

        float stickX = inputs.stick_x / 128.0f;
        float stickY = inputs.stick_y / 128.0f;
        // Deadzone
        if (fabsf(stickX) < 0.15f) stickX = 0.0f;
        if (fabsf(stickY) < 0.15f) stickY = 0.0f;

        float cosYaw = cosf(debugCamYaw);
        float sinYaw = sinf(debugCamYaw);

        // L/R triggers cycle decoration type
        if (debugTriggerCooldown > 0) debugTriggerCooldown--;
        if (debugTriggerCooldown == 0) {
            if (held.l) {
                debugDecoType = (debugDecoType + DECO_TYPE_COUNT - 1) % DECO_TYPE_COUNT;
                map_get_deco_model(&mapRuntime, debugDecoType);
                debugTriggerCooldown = 15;
            }
            if (held.r) {
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
            float cosPitch = cosf(debugCamPitch);
            float sinPitch = sinf(debugCamPitch);
            float lookDirX = sinYaw * cosPitch;
            float lookDirY = sinPitch;
            float lookDirZ = -cosYaw * cosPitch;

            float bestScore = 999999.0f;
            int bestIndex = -1;
            float maxSelectDist = 500.0f;
            float selectRadius = 30.0f;

            for (int i = 0; i < mapRuntime.decoCount; i++) {
                DecoInstance* deco = &mapRuntime.decorations[i];
                if (!deco->active || deco->type == DECO_NONE) continue;

                float toCamX = deco->posX - debugCamX;
                float toCamY = deco->posY - debugCamY;
                float toCamZ = deco->posZ - debugCamZ;

                float alongRay = toCamX * lookDirX + toCamY * lookDirY + toCamZ * lookDirZ;
                if (alongRay < 10.0f || alongRay > maxSelectDist) continue;

                float perpX = toCamY * lookDirZ - toCamZ * lookDirY;
                float perpY = toCamZ * lookDirX - toCamX * lookDirZ;
                float perpZ = toCamX * lookDirY - toCamY * lookDirX;
                float perpDist = sqrtf(perpX * perpX + perpY * perpY + perpZ * perpZ);

                if (perpDist < selectRadius) {
                    float score = perpDist + alongRay * 0.1f;
                    if (score < bestScore) {
                        bestScore = score;
                        bestIndex = i;
                    }
                }
            }

            debugHighlightedDecoIndex = bestIndex;

            if (debugDeleteCooldown > 0.0f) {
                debugDeleteCooldown -= deltaTime;
            }

            // D-pad right deletes highlighted decoration
            if (pressed.d_right && debugHighlightedDecoIndex >= 0 && debugDeleteCooldown <= 0.0f) {
                DecoInstance* deco = &mapRuntime.decorations[debugHighlightedDecoIndex];
                debugf("Deleted decoration: type=%d at (%.1f, %.1f, %.1f)\n",
                    deco->type, deco->posX, deco->posY, deco->posZ);
                map_remove_decoration(&mapRuntime, debugHighlightedDecoIndex);
                debugHighlightedDecoIndex = -1;
                debugDeleteCooldown = 0.3f;
            }

            // D-pad left toggles collision debug
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
        } else {
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
                int idx = map_add_decoration(&mapRuntime, debugDecoType,
                    debugDecoX, debugDecoY, debugDecoZ, debugDecoRotY,
                    debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ);

                // Set special properties for specific types
                if (idx >= 0 && debugDecoType == DECO_DIALOGUETRIGGER) {
                    mapRuntime.decorations[idx].state.dialogueTrigger.scriptId = debugTriggerScriptId;
                    mapRuntime.decorations[idx].state.dialogueTrigger.triggerRadius = debugTriggerRadius;
                    mapRuntime.decorations[idx].state.dialogueTrigger.triggered = false;
                    mapRuntime.decorations[idx].state.dialogueTrigger.onceOnly = false;
                    mapRuntime.decorations[idx].state.dialogueTrigger.hasTriggered = false;
                }
                if (idx >= 0 && debugDecoType == DECO_INTERACTTRIGGER) {
                    mapRuntime.decorations[idx].state.interactTrigger.scriptId = debugTriggerScriptId;
                    mapRuntime.decorations[idx].state.interactTrigger.triggerRadius = debugTriggerRadius;
                    mapRuntime.decorations[idx].state.interactTrigger.playerInRange = false;
                    mapRuntime.decorations[idx].state.interactTrigger.interacting = false;
                    mapRuntime.decorations[idx].state.interactTrigger.lookAtAngle = 0.0f;
                    mapRuntime.decorations[idx].state.interactTrigger.savedPlayerAngle = 0.0f;
                    mapRuntime.decorations[idx].state.interactTrigger.onceOnly = false;
                    mapRuntime.decorations[idx].state.interactTrigger.hasTriggered = false;
                }

                if (idx >= 0) {
                    debugf("Placed %s at (%.1f, %.1f, %.1f)\n",
                        DECO_TYPES[debugDecoType].name, debugDecoX, debugDecoY, debugDecoZ);
                    map_print_all_decorations(&mapRuntime);
                }
            }

            // B returns to camera mode
            if (pressed.b) {
                debugPlacementMode = false;
            }
        }

        // Skip normal player input when in fly mode
        goto update_physics;
    }

    // Check for triggered dialogue triggers (before UI update)
    if (!scriptRunning && !dialogue_is_active(&dialogueBox) && !option_is_active(&optionPrompt)) {
        for (int i = 0; i < mapRuntime.decoCount; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            if (deco->type == DECO_DIALOGUETRIGGER && deco->state.dialogueTrigger.triggered) {
                // Start the script for this trigger
                start_script(deco->state.dialogueTrigger.scriptId);
                // Reset triggered flag so it doesn't re-trigger immediately
                deco->state.dialogueTrigger.triggered = false;
                break;  // Only process one trigger per frame
            }
        }
    }

    // Handle active InteractTrigger dialogue completion
    if (activeInteractTrigger != NULL) {
        // Check if dialogue/options just closed (but not during scene transition)
        if (!dialogue_is_active(&dialogueBox) && !option_is_active(&optionPrompt) && !scriptRunning &&
            menuState != MENU_STATE_IRIS_OUT) {
            // Start lerping back to saved angle
            targetPlayerAngle = savedPlayerAngle;
            isLerpingAngle = true;
            // Clear interact state
            activeInteractTrigger->state.interactTrigger.interacting = false;
            if (activeInteractTrigger->state.interactTrigger.onceOnly) {
                activeInteractTrigger->state.interactTrigger.hasTriggered = true;
            }
            activeInteractTrigger = NULL;
        }
    }

    // Check for InteractTrigger A button press
    if (!scriptRunning && !dialogue_is_active(&dialogueBox) && !option_is_active(&optionPrompt)) {
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        for (int i = 0; i < mapRuntime.decoCount; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            if (deco->active && deco->type == DECO_INTERACTTRIGGER &&
                deco->state.interactTrigger.playerInRange && !deco->state.interactTrigger.interacting) {
                if (pressed.a) {
                    // Save current angle and start lerping toward look angle
                    savedPlayerAngle = playerAngle;
                    targetPlayerAngle = deco->state.interactTrigger.lookAtAngle;
                    isLerpingAngle = true;
                    // Mark as interacting
                    deco->state.interactTrigger.interacting = true;
                    activeInteractTrigger = deco;
                    // Start the script for this trigger
                    start_script(deco->state.interactTrigger.scriptId);
                    ui_play_ui_open_sound();
                    break;
                }
            }
        }
    }

    // Update UI elements - they consume input when active
    if (dialogue_update(&dialogueBox, JOYPAD_PORT_1)) {
        // Check if dialogue just closed and we're running a script
        if (scriptRunning && !dialogue_is_active(&dialogueBox) && !script_is_waiting_for_option(&activeScript)) {
            // Advance script to next node
            script_advance(&activeScript, &dialogueBox, &optionPrompt, -1);
            if (!script_is_active(&activeScript)) {
                scriptRunning = false;
            }
        }
        // Dialogue consumed input, skip player movement
        goto update_physics;
    }

    if (option_update(&optionPrompt, JOYPAD_PORT_1)) {
        // Check if option was selected and we're running a script
        if (scriptRunning && !option_is_active(&optionPrompt)) {
            // Advance script with selected option
            script_advance(&activeScript, &dialogueBox, &optionPrompt, option_get_selected(&optionPrompt));
            if (!script_is_active(&activeScript)) {
                scriptRunning = false;
            }
        }
        // Option prompt consumed input
        goto update_physics;
    }

    // Get stick input
    float stickX = inputs.stick_x / 85.0f;
    float stickY = inputs.stick_y / 85.0f;
    float stickMag = sqrtf(stickX * stickX + stickY * stickY);
    if (stickMag > 1.0f) {
        stickX /= stickMag;
        stickY /= stickMag;
        stickMag = 1.0f;
    }

    // Deadzone
    if (stickMag < 0.2f) {
        stickMag = 0.0f;
        stickX = 0.0f;
        stickY = 0.0f;
    }

    // Movement (camera-relative)
    isMoving = stickMag > 0.0f;
    if (isMoving) {
        // Calculate camera forward direction (from camera to target, ignoring Y)
        float camDirX = camTarget.v[0] - camPos.v[0];
        float camDirZ = camTarget.v[2] - camPos.v[2];
        float camLen = sqrtf(camDirX * camDirX + camDirZ * camDirZ);
        if (camLen > 0.001f) {
            camDirX /= camLen;
            camDirZ /= camLen;
        }

        // Camera right direction (perpendicular to forward)
        float camRightX = -camDirZ;
        float camRightZ = camDirX;

        // Convert stick input to world movement (forward = stick up, right = stick right)
        float moveX = (camDirX * stickY + camRightX * stickX);
        float moveZ = (camDirZ * stickY + camRightZ * stickX);

        // Calculate target angle from movement direction
        float targetAngle = atan2f(-moveX, moveZ);
        playerAngle = targetAngle;

        // Move player
        float moveSpeed = 3.0f * stickMag;
        playerX += moveX * moveSpeed;
        playerZ += moveZ * moveSpeed;

        // Invisible barrier in front of camera - prevent walking towards/under camera
        float cameraBarrierZ = camPos.v[2] - 80.0f;  // Barrier slightly in front of camera
        if (playerZ > cameraBarrierZ) {
            playerZ = cameraBarrierZ;
        }
    }

update_physics:
    // Gravity and ground collision
    playerVelY -= GRAVITY;
    playerY += playerVelY;

    // Ground collision - use standard maploader function (same as game.c)
    float groundY = maploader_get_ground_height(&mapLoader, playerX, playerY + 50.0f, playerZ);

    if (playerY < groundY + 0.1f) {
        playerY = groundY + 0.1f;
        playerVelY = 0.0f;
        isGrounded = true;
    } else {
        isGrounded = false;
    }

    // Wall collision - use standard maploader function (same as game.c)
    float pushX = 0.0f, pushZ = 0.0f;
    bool wallCollided = maploader_check_walls(&mapLoader, playerX, playerY, playerZ,
        PLAYER_RADIUS, PLAYER_HEIGHT, &pushX, &pushZ);
    if (wallCollided) {
        playerX += pushX;
        playerZ += pushZ;
    }

    // Decoration wall collision (jukebox, table, etc)
    float decoPushX = 0.0f, decoPushZ = 0.0f;
    map_check_deco_walls(&mapRuntime, playerX, playerY, playerZ,
        PLAYER_RADIUS, PLAYER_HEIGHT, &decoPushX, &decoPushZ);
    playerX += decoPushX + 0.001f;
    playerZ += decoPushZ + 0.001f;

    // Check decoration behavior collisions (pickups, damage, etc)
    map_check_deco_collisions(&mapRuntime, playerX, playerY, playerZ, PLAYER_RADIUS);

    // Update player position in mapRuntime for decoration interactions
    map_set_player_pos(&mapRuntime, playerX, playerY, playerZ);

    // Update decorations
    map_update_decorations(&mapRuntime, deltaTime);

    // Update map visibility
    maploader_update_visibility(&mapLoader, playerX, playerZ);

    // Lerp player angle toward target (for interact trigger rotation)
    if (isLerpingAngle) {
        // Calculate shortest angle difference (handle wrap-around)
        float diff = targetPlayerAngle - playerAngle;
        while (diff > 3.14159f) diff -= 6.28318f;
        while (diff < -3.14159f) diff += 6.28318f;

        // Lerp toward target
        float lerpSpeed = 8.0f * deltaTime;
        if (fabsf(diff) < 0.05f) {
            playerAngle = targetPlayerAngle;
            isLerpingAngle = false;
        } else {
            playerAngle += diff * lerpSpeed;
        }
    }

    // Update animations (with skeleton validity checks to prevent accessing freed memory)
    if (hasAnimations && skeleton_is_valid(&playerSkel)) {
        // Switch animation when movement state changes
        if (isMoving != wasMoving) {
            if (isMoving) {
                // Started moving - switch to walk
                t3d_anim_attach(&playerAnimWalk, &playerSkel);
                t3d_anim_set_playing(&playerAnimWalk, true);
                currentPlayerAnim = &playerAnimWalk;
            } else {
                // Stopped moving - switch to idle
                t3d_anim_attach(&playerAnimIdle, &playerSkel);
                t3d_anim_set_playing(&playerAnimIdle, true);
                currentPlayerAnim = &playerAnimIdle;
            }
            wasMoving = isMoving;
        }

        // Update the current animation
        if (currentPlayerAnim) {
            t3d_anim_update(currentPlayerAnim, deltaTime);
        }

        // Update skeleton
        t3d_skeleton_update(&playerSkel);
    }

    // Update party mode lights (orbit around jukebox)
    if (partyModeActive) {
        partyLightAngle += deltaTime * 2.0f;  // Rotate speed
        if (partyLightAngle > 6.28318f) {
            partyLightAngle -= 6.28318f;
        }

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

    // Update disco floor texture scroll
    discoFloorScrollOffset += deltaTime * DISCO_FLOOR_SCROLL_SPEED;
    if (discoFloorScrollOffset > 1.0f) {
        discoFloorScrollOffset -= 1.0f;
    }

update_audio:
    // Update audio mixer
    if (audio_can_write()) {
        short *buf = audio_write_begin();
        rspq_wait();  // Flush RSPQ to exit highpri mode before mixer_poll (needed when rdpq_debug is active)
        save_apply_volume_settings_safe();  // Apply pending volume changes in safe window
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }
}

void draw_menu_scene(void) {
    if (!sceneInitialized) return;

    // Update frame index
    frameIdx = (frameIdx + 1) % FB_COUNT;

    surface_t *disp = display_get();
    surface_t *zbuf = display_get_zbuf();
    rdpq_attach(disp, zbuf);
    rdpq_clear(RGBA32(0x20, 0x20, 0x40, 0xFF));
    rdpq_clear_z(0xFFFC);

    t3d_frame_start();
    t3d_viewport_attach(&viewport);

    // Party mode fog - color cycling fog in the background
    // Note: rdpq_mode_fog() is REQUIRED to configure the RDP blender for fog!
    if (partyFogIntensity > 0.01f) {
        uint8_t fogR, fogG, fogB;
        hsv_to_rgb(partyFogHue, 0.5f, 0.6f, &fogR, &fogG, &fogB);  // Subtle colors
        rdpq_mode_fog(RDPQ_FOG_STANDARD);
        rdpq_set_fog_color(RGBA32(fogR, fogG, fogB, 255));

        // Fog starts close but fades slowly (visible haze, not obscuring)
        float fogNear = 60.0f - (partyFogIntensity * 30.0f);    // 60 -> 30 (close to camera)
        float fogFar = 450.0f - (partyFogIntensity * 100.0f);   // 450 -> 350 (far enough to see scene)
        t3d_fog_set_range(fogNear, fogFar);
        t3d_fog_set_enabled(true);
    } else {
        rdpq_mode_fog(0);
        t3d_fog_set_enabled(false);
    }

    // Setup camera (debug fly mode or fixed)
    T3DVec3 up = {{0, 1, 0}};
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(70.0f), 10.0f, 1000.0f);

    if (menuDebugFlyMode) {
        // Debug fly camera
        float cosPitch = cosf(debugCamPitch);
        float sinPitch = sinf(debugCamPitch);
        float cosYaw = cosf(debugCamYaw);
        float sinYaw = sinf(debugCamYaw);

        T3DVec3 debugCamPosVec = {{debugCamX, debugCamY, debugCamZ}};
        T3DVec3 debugCamTargetVec = {{
            debugCamX + sinYaw * cosPitch,
            debugCamY + sinPitch,
            debugCamZ - cosYaw * cosPitch
        }};
        t3d_viewport_look_at(&viewport, &debugCamPosVec, &debugCamTargetVec, &up);
    } else {
        // Fixed menu camera
        t3d_viewport_look_at(&viewport, &camPos, &camTarget, &up);
    }

    // Lighting
    uint8_t ambientColor[4] = {80, 80, 80, 0xFF};
    t3d_light_set_ambient(ambientColor);

    // Check if disco ball is spinning (enables party lights)
    bool discoBallSpinning = false;
    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (deco->active && deco->type == DECO_DISCOBALL && deco->state.discoBall.isSpinning) {
            discoBallSpinning = true;
            break;
        }
    }

    // Party mode: 4 colored lights rotating (only when disco ball is spinning)
    // Directional lights point in a direction - we rotate the direction vectors
    if (discoBallSpinning) {
        // Red light - direction rotates around Y axis
        uint8_t redColor[4] = {255, 50, 50, 0xFF};
        T3DVec3 redDir = {{
            cosf(partyLightAngle),
            -0.3f,
            sinf(partyLightAngle)
        }};
        t3d_vec3_norm(&redDir);
        t3d_light_set_directional(0, redColor, &redDir);

        // Green light (90 degrees offset)
        uint8_t greenColor[4] = {50, 255, 50, 0xFF};
        T3DVec3 greenDir = {{
            cosf(partyLightAngle + 1.5708f),
            -0.3f,
            sinf(partyLightAngle + 1.5708f)
        }};
        t3d_vec3_norm(&greenDir);
        t3d_light_set_directional(1, greenColor, &greenDir);

        // Blue light (180 degrees offset)
        uint8_t blueColor[4] = {50, 50, 255, 0xFF};
        T3DVec3 blueDir = {{
            cosf(partyLightAngle + 3.1416f),
            -0.3f,
            sinf(partyLightAngle + 3.1416f)
        }};
        t3d_vec3_norm(&blueDir);
        t3d_light_set_directional(2, blueColor, &blueDir);

        // Yellow light (270 degrees offset)
        uint8_t yellowColor[4] = {255, 255, 50, 0xFF};
        T3DVec3 yellowDir = {{
            cosf(partyLightAngle + 4.7124f),
            -0.3f,
            sinf(partyLightAngle + 4.7124f)
        }};
        t3d_vec3_norm(&yellowDir);
        t3d_light_set_directional(3, yellowColor, &yellowDir);

        t3d_light_set_count(4);
    } else {
        // Normal white light when party mode is off
        uint8_t lightColor[4] = {255, 255, 255, 0xFF};
        T3DVec3 lightDir = {{1.0f, 1.0f, 1.0f}};
        t3d_vec3_norm(&lightDir);
        t3d_light_set_directional(0, lightColor, &lightDir);
        t3d_light_set_count(1);
    }

    // Draw map using standard maploader (same as game.c)
    maploader_draw(&mapLoader, frameIdx);

    // Draw disco floor with scrolling texture (positioned just above the floor)
    if (discoFloorModel) {
        // Scroll the UVs
        disco_floor_scroll_uvs(discoFloorScrollOffset);

        // Animate disco floor Y position - rises when disco ball is spinning
        float targetY = discoBallSpinning ? DISCO_FLOOR_VISIBLE_Y : DISCO_FLOOR_HIDDEN_Y;
        float dt = 1.0f / 30.0f;  // Assume 30fps
        if (discoFloorCurrentY < targetY) {
            discoFloorCurrentY += DISCO_FLOOR_RISE_SPEED * dt;
            if (discoFloorCurrentY > targetY) discoFloorCurrentY = targetY;
        } else if (discoFloorCurrentY > targetY) {
            discoFloorCurrentY -= DISCO_FLOOR_RISE_SPEED * dt;
            if (discoFloorCurrentY < targetY) discoFloorCurrentY = targetY;
        }

        // Position the disco floor close to camera, scaled up 5x horizontally only
        t3d_mat4fp_from_srt_euler(&discoFloorMatFP[frameIdx],
            (float[3]){5.0f, 1.0f, 5.0f},           // Scale 5x horizontally, keep Y at 1
            (float[3]){0.0f, 0.0f, 0.0f},           // Rotation
            (float[3]){0.0f, discoFloorCurrentY, 100.0f}  // Position with animated Y
        );
        t3d_matrix_push(&discoFloorMatFP[frameIdx]);
        t3d_model_draw(discoFloorModel);
        t3d_matrix_pop(1);
    }

    // Draw decorations
    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (!deco->active || deco->type == DECO_NONE) continue;

        // Skip invisible triggers in normal gameplay (only show in debug mode)
        if (deco->type == DECO_DIALOGUETRIGGER && !menuDebugFlyMode) continue;
        if (deco->type == DECO_INTERACTTRIGGER && !menuDebugFlyMode) continue;

        // Draw DialogueTrigger in debug mode (yellow)
        if (deco->type == DECO_DIALOGUETRIGGER && menuDebugFlyMode) {
            DecoTypeRuntime* cubeType = map_get_deco_type(&mapRuntime, DECO_PLAYERSPAWN);
            if (cubeType && cubeType->model) {
                int matIdx = frameIdx * MAX_DECORATIONS + i;
                // BlueCube is 128 units wide (-64 to +64), scale to match trigger radius
                float radius = deco->state.dialogueTrigger.triggerRadius;
                float scale = radius / 64.0f;
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){scale, scale * 0.3f, scale},
                    (float[3]){0.0f, 0.0f, 0.0f},
                    (float[3]){deco->posX, deco->posY + 5.0f, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);
                rdpq_sync_pipe();  // Sync before 2D mode switch
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(RGBA32(255, 255, 0, 100));
                t3d_model_draw(cubeType->model);
                t3d_matrix_pop(1);
            }
            continue;
        }

        // Draw InteractTrigger in debug mode (orange)
        if (deco->type == DECO_INTERACTTRIGGER && menuDebugFlyMode) {
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
                rdpq_sync_pipe();  // Sync before 2D mode switch
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(RGBA32(255, 165, 0, 100));
                t3d_model_draw(cubeType->model);
                t3d_matrix_pop(1);
            }
            continue;
        }

        DecoTypeRuntime* decoType = map_get_deco_type(&mapRuntime, deco->type);
        const DecoTypeDef* decoDef = &DECO_TYPES[deco->type];
        if (decoType && decoType->model) {
            int matIdx = frameIdx * MAX_DECORATIONS + i;

            // MONITORTABLE: Draw table and monitor screen with texture scroll
            if (deco->type == DECO_MONITORTABLE) {
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                    (float[3]){deco->posX, deco->posY, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);

                // Draw table (no scroll)
                t3d_model_draw(decoType->model);

                // Draw monitor screen with texture scroll
                if (mapRuntime.monitorScreenModel) {
                    menu_monitor_init_uvs(mapRuntime.monitorScreenModel);
                    menu_monitor_scroll_uvs(deco->state.monitorTable.textureOffset);
                    t3d_model_draw(mapRuntime.monitorScreenModel);
                }

                t3d_matrix_pop(1);
                continue;
            }

            // DISCOBALL: Draw with Y offset (descent) and rotation
            if (deco->type == DECO_DISCOBALL) {
                float ballY = deco->posY + deco->state.discoBall.currentY;
                t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                    (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                    (float[3]){deco->rotX, deco->state.discoBall.rotation, deco->rotZ},
                    (float[3]){deco->posX, ballY, deco->posZ}
                );
                t3d_matrix_push(&decoMatFP[matIdx]);
                t3d_model_draw(decoType->model);
                t3d_matrix_pop(1);

                // Cache disco ball position for sparkle drawing
                if (deco->state.discoBall.isSpinning) {
                    g_discoBallX = deco->posX;
                    g_discoBallY = ballY;
                    g_discoBallZ = deco->posZ;
                }
                continue;
            }

            t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                (float[3]){deco->posX, deco->posY, deco->posZ}
            );
            t3d_matrix_push(&decoMatFP[matIdx]);

            // Use per-instance skeleton if available, otherwise shared skeleton
            // Note: T3D handles combiner setup based on model materials - don't override
            // Validate skeleton before use to prevent crash on corrupted/freed data
            if (deco->hasOwnSkeleton && skeleton_is_valid(&deco->skeleton)) {
                t3d_model_draw_skinned(decoType->model, &deco->skeleton);
            } else if (decoType->hasSkeleton && skeleton_is_valid(&decoType->skeleton)) {
                t3d_model_draw_skinned(decoType->model, &decoType->skeleton);
            } else {
                t3d_model_draw(decoType->model);
            }

            // Draw jukebox FX overlay (always draw, only scroll UVs when playing)
            if (deco->type == DECO_JUKEBOX && mapRuntime.jukeboxFxModel) {
                if (deco->state.jukebox.isPlaying) {
                    jukebox_fx_init_uvs(mapRuntime.jukeboxFxModel);
                    jukebox_fx_scroll_uvs(deco->state.jukebox.textureOffset);
                }
                if (deco->hasOwnSkeleton && skeleton_is_valid(&deco->skeleton)) {
                    t3d_model_draw_skinned(mapRuntime.jukeboxFxModel, &deco->skeleton);
                } else if (decoType->hasSkeleton && skeleton_is_valid(&decoType->skeleton)) {
                    t3d_model_draw_skinned(mapRuntime.jukeboxFxModel, &decoType->skeleton);
                } else {
                    t3d_model_draw(mapRuntime.jukeboxFxModel);
                }
            }

            t3d_matrix_pop(1);
        }
    }

    // Draw player (scaled down for menu scene, with cheat scale applied)
    if (playerModel && playerMatFP) {
        float menuPlayerScale = 0.65f * g_playerScaleCheat;
        t3d_mat4fp_from_srt_euler(&playerMatFP[frameIdx],
            (float[3]){menuPlayerScale, menuPlayerScale, menuPlayerScale},
            (float[3]){0.0f, playerAngle, 0.0f},
            (float[3]){playerX, playerY, playerZ}
        );
        t3d_matrix_push(&playerMatFP[frameIdx]);
        // Validate skeleton before use to prevent crash on corrupted/freed data
        if (skeleton_is_valid(&playerSkel)) {
            t3d_model_draw_skinned(playerModel, &playerSkel);
        } else {
            t3d_model_draw(playerModel);
        }
        t3d_matrix_pop(1);
    }

    // Debug: Draw placement preview
    if (menuDebugFlyMode && debugPlacementMode) {
        static T3DMat4FP previewMat __attribute__((aligned(16)));

        // Special preview for trigger types (show cube with appropriate color)
        if (debugDecoType == DECO_DIALOGUETRIGGER || debugDecoType == DECO_INTERACTTRIGGER) {
            DecoTypeRuntime* cubeType = map_get_deco_type(&mapRuntime, DECO_PLAYERSPAWN);
            if (cubeType && cubeType->model) {
                // BlueCube is 128 units wide, scale to match trigger radius
                float scale = debugTriggerRadius / 64.0f;
                t3d_mat4fp_from_srt_euler(&previewMat,
                    (float[3]){scale, scale * 0.3f, scale},
                    (float[3]){0.0f, 0.0f, 0.0f},
                    (float[3]){debugDecoX, debugDecoY + 5.0f, debugDecoZ}
                );
                data_cache_hit_writeback(&previewMat, sizeof(T3DMat4FP));
                t3d_matrix_push(&previewMat);
                rdpq_sync_pipe();  // Sync before 2D mode switch
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                if (debugDecoType == DECO_DIALOGUETRIGGER) {
                    rdpq_set_prim_color(RGBA32(255, 255, 0, 150));  // Yellow
                } else {
                    rdpq_set_prim_color(RGBA32(255, 165, 0, 150));  // Orange
                }
                t3d_model_draw(cubeType->model);
                t3d_matrix_pop(1);
            }
        } else {
            // Normal decoration preview
            DecoTypeRuntime* previewType = map_get_deco_type(&mapRuntime, debugDecoType);
            if (previewType && previewType->model) {
                t3d_mat4fp_from_srt_euler(&previewMat,
                    (float[3]){debugDecoScaleX, debugDecoScaleY, debugDecoScaleZ},
                    (float[3]){0.0f, debugDecoRotY, 0.0f},
                    (float[3]){debugDecoX, debugDecoY, debugDecoZ}
                );
                data_cache_hit_writeback(&previewMat, sizeof(T3DMat4FP));
                t3d_matrix_push(&previewMat);
                t3d_model_draw(previewType->model);
                t3d_matrix_pop(1);
            }
        }
    }

    // Debug: Highlight selected decoration
    if (menuDebugFlyMode && !debugPlacementMode && debugHighlightedDecoIndex >= 0) {
        DecoInstance* hlDeco = &mapRuntime.decorations[debugHighlightedDecoIndex];
        static T3DMat4FP hlMat __attribute__((aligned(16)));

        // Special highlight for trigger types
        if (hlDeco->type == DECO_DIALOGUETRIGGER || hlDeco->type == DECO_INTERACTTRIGGER) {
            DecoTypeRuntime* cubeType = map_get_deco_type(&mapRuntime, DECO_PLAYERSPAWN);
            if (cubeType && cubeType->model) {
                float radius = (hlDeco->type == DECO_DIALOGUETRIGGER)
                    ? hlDeco->state.dialogueTrigger.triggerRadius
                    : hlDeco->state.interactTrigger.triggerRadius;
                // BlueCube is 128 units wide, scale to match radius * 1.1 for highlight
                float scale = (radius / 64.0f) * 1.1f;
                t3d_mat4fp_from_srt_euler(&hlMat,
                    (float[3]){scale, scale * 0.3f, scale},
                    (float[3]){0.0f, 0.0f, 0.0f},
                    (float[3]){hlDeco->posX, hlDeco->posY + 5.0f, hlDeco->posZ}
                );
                data_cache_hit_writeback(&hlMat, sizeof(T3DMat4FP));
                t3d_matrix_push(&hlMat);
                rdpq_sync_pipe();  // Sync before 2D mode switch
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(RGBA32(255, 50, 50, 180));  // Red highlight
                t3d_model_draw(cubeType->model);
                t3d_matrix_pop(1);
            }
        } else {
            DecoTypeRuntime* hlType = map_get_deco_type(&mapRuntime, hlDeco->type);
            if (hlType && hlType->model) {
                float hlScale = 1.05f;
                t3d_mat4fp_from_srt_euler(&hlMat,
                    (float[3]){hlDeco->scaleX * hlScale, hlDeco->scaleY * hlScale, hlDeco->scaleZ * hlScale},
                    (float[3]){hlDeco->rotX, hlDeco->rotY, hlDeco->rotZ},
                    (float[3]){hlDeco->posX, hlDeco->posY, hlDeco->posZ}
                );
                data_cache_hit_writeback(&hlMat, sizeof(T3DMat4FP));
                uint8_t hlColor[4] = {255, 100, 100, 0xFF};
                t3d_light_set_ambient(hlColor);
                t3d_matrix_push(&hlMat);
                t3d_model_draw(hlType->model);
                t3d_matrix_pop(1);
                uint8_t ambientNorm[4] = {80, 80, 80, 0xFF};
                t3d_light_set_ambient(ambientNorm);
            }
        }
    }

    // Draw UI elements (2D overlay)
    rdpq_sync_pipe();
    rdpq_set_mode_standard();

    // Flush any denormal floats from 3D rendering before UI draws
    fpu_flush_denormals();

    dialogue_draw(&dialogueBox);
    option_draw(&optionPrompt);

    // Draw A button prompt for nearby interact triggers
    if (!scriptRunning && !dialogue_is_active(&dialogueBox) && !option_is_active(&optionPrompt) && !menuDebugFlyMode) {
        for (int i = 0; i < mapRuntime.decoCount; i++) {
            DecoInstance* deco = &mapRuntime.decorations[i];
            if (deco->active && deco->type == DECO_INTERACTTRIGGER &&
                deco->state.interactTrigger.playerInRange && !deco->state.interactTrigger.interacting) {
                // Draw A button prompt in bottom right (screen is 256x240)
                rdpq_set_prim_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
                rdpq_text_printf(NULL, 2, 230, 220, "a");
                break;
            }
        }
    }

    // Draw debug menu on top of everything (shared module + menu-specific extras)
    debug_menu_draw();
    menu_debug_draw_extra();

    // Draw 100% completion badge if player has all S ranks
    if (g_saveSystem.activeSaveSlot >= 0 && save_has_all_s_ranks()) {
        // Animated gold color that pulses (wrap to prevent FPU overflow)
        static float badgeTime = 0.0f;
        badgeTime += 0.05f;
        if (badgeTime > 1000.0f) {
            badgeTime -= 1000.0f;
        }
        float pulse = (sinf(badgeTime * 2.0f) + 1.0f) * 0.5f; // 0.0 to 1.0

        uint8_t r = 255;
        uint8_t g = (uint8_t)(200 + pulse * 55);  // 200-255 gold pulse
        uint8_t b = (uint8_t)(50 + pulse * 100);  // slight orange variation

        rdpq_set_prim_color(RGBA32(r, g, b, 255));
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 10, "S-RANK MASTER!");

        // Show S rank count
        rdpq_set_prim_color(RGBA32(255, 255, 255, 200));
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 22,
            "%d/%d S Ranks", save_count_s_ranks(), g_saveLevelInfo.realLevelCount);
    }

    // Draw QR code for reward if player has collected ALL bolts and golden screws
    if (g_saveSystem.activeSaveSlot >= 0 && qr_display_is_valid()) {
        int totalBolts = save_get_total_bolts_collected();
        int totalScrewg = save_get_total_screwg_collected();
        bool allBoltsCollected = (totalBolts >= TOTAL_BOLTS_IN_GAME);
        bool allScrewgCollected = (TOTAL_SCREWG_IN_GAME == 0 || totalScrewg >= TOTAL_SCREWG_IN_GAME);

        if (allBoltsCollected && allScrewgCollected) {
            // Draw QR code in bottom left corner
            // QR code with pixel size 2 and quiet zone border
            int qrSize = qr_display_get_size();
            int pixelSize = 2;
            int totalQrSize = (qrSize + 2) * pixelSize;
            int qrX = 8;  // Left margin
            int qrY = SCREEN_HEIGHT - totalQrSize - 8;  // Bottom margin

            qr_display_draw(qrX, qrY, pixelSize);

            // Draw "Congrats!" text centered above QR code
            rdpq_set_prim_color(RGBA32(255, 255, 100, 255));
            const char* label = "Congrats!";
            int labelLen = 9;  // "Congrats!" is 9 chars
            int labelWidth = labelLen * 8;  // 8px per char
            int labelX = qrX + (totalQrSize - labelWidth) / 2;  // Center above QR
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, labelX, qrY - 12, "%s", label);
        }
    }

    // Draw disco ball sparkles (when disco ball is spinning)
    if (discoBallSpinning) {
        float dt = 1.0f / 30.0f;

        // Spawn new sparkles periodically
        g_sparkleSpawnTimer += dt;
        if (g_sparkleSpawnTimer >= DISCO_SPARKLE_SPAWN_RATE) {
            g_sparkleSpawnTimer = 0.0f;
            spawn_disco_sparkle();
        }

        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        // Update and draw sparkles
        for (int i = 0; i < DISCO_SPARKLE_COUNT; i++) {
            if (!g_discoSparkles[i].active) continue;

            // Update life
            g_discoSparkles[i].life += dt;
            if (g_discoSparkles[i].life >= g_discoSparkles[i].maxLife) {
                g_discoSparkles[i].active = false;
                continue;
            }

            // Calculate scale based on life (grow then shrink)
            // Safety check: prevent division by zero or denormal values
            float lifeRatio = 0.0f;
            if (g_discoSparkles[i].maxLife > 0.001f) {
                lifeRatio = g_discoSparkles[i].life / g_discoSparkles[i].maxLife;
            }
            float scale;
            if (lifeRatio < 0.3f) {
                // Growing phase
                scale = lifeRatio / 0.3f;
            } else {
                // Shrinking phase
                scale = 1.0f - ((lifeRatio - 0.3f) / 0.7f);
            }

            // World position
            T3DVec3 sparkleWorld = {{
                g_discoBallX + g_discoSparkles[i].x,
                g_discoBallY + g_discoSparkles[i].y,
                g_discoBallZ + g_discoSparkles[i].z
            }};

            // Project to screen
            T3DVec3 sparkleScreen;
            t3d_viewport_calc_viewspace_pos(&viewport, &sparkleScreen, &sparkleWorld);

            // Skip if behind camera
            if (sparkleScreen.v[2] <= 0) continue;

            float sx = sparkleScreen.v[0];
            float sy = sparkleScreen.v[1];

            // Skip if off screen
            if (sx < -10 || sx > 330 || sy < -10 || sy > 250) continue;

            // Alpha based on scale
            uint8_t alpha = (uint8_t)(255.0f * scale);
            rdpq_set_prim_color(RGBA32(255, 255, 255, alpha));

            // Draw 4-pointed star sparkle
            float starSize = 3.0f * scale;
            float innerSize = 1.0f * scale;

            if (starSize < 0.5f) continue;  // Too small to draw

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

    // Draw iris effect (scene transition)
    // IRIS EFFECT DISABLED - rdpq_triangle causes RDP hardware crashes on real N64
    // Using simple alpha fade instead for stability
    if (irisActive) {
        float r = irisRadius;

        // Validate radius
        if (isnan(r) || isinf(r) || r < 0.0f) {
            r = 0.0f;  // Treat as fully closed
        }

        float maxRadius = (float)SCREEN_WIDTH + 80.0f;
        if (r > maxRadius) {
            r = maxRadius;
        }

        // Convert radius to alpha (inverted - smaller radius = more black)
        int alpha = (int)((1.0f - (r / maxRadius)) * 255.0f);
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;

        // Only draw if there's something to show
        if (alpha > 0) {
            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(0, 0, 0, (uint8_t)alpha));
            rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        }
    }

    // Draw fade-in from black overlay (coming from splash screen)
    if (fadeInActive && fadeInAlpha > 0.01f) {
        rdpq_sync_pipe();  // Sync before 2D mode switch
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        uint8_t alpha = (uint8_t)(fadeInAlpha * 255.0f);
        rdpq_set_prim_color(RGBA32(0, 0, 0, alpha));
        rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}
