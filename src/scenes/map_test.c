#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include "map_test.h"
#include "../scene.h"
#include "../mapLoader.h"
#include "../controls.h"
#include "../constants.h"
#include <stdbool.h>
#include <math.h>

#define FB_COUNT 3

// Map loader
static MapLoader mapLoader;

// Player state
static ControlConfig controlConfig;
static PlayerState playerState;
static float playerX = 0.0f;
static float playerY = 50.0f;
static float playerZ = 0.0f;
static float playerRadius = 8.0f;
static float playerHeight = 20.0f;
static float groundLevel = -100.0f;

// Camera
static T3DVec3 camPos = {{0.0f, 50.0f, -120.0f}};
static T3DVec3 camTarget = {{0.0f, 0.0f, 0.0f}};
static T3DViewport viewport;

// Debug fly camera
static float debugCamX = 0.0f;
static float debugCamY = 100.0f;
static float debugCamZ = -120.0f;
static float debugCamYaw = 0.0f;
static float debugCamPitch = 0.0f;

static int frameIdx = 0;
static bool sceneInitialized = false;

void init_map_test_scene(void) {
    // Reset all state
    frameIdx = 0;
    debugFlyMode = false;

    // Reset player position
    playerX = 0.0f;
    playerY = 50.0f;
    playerZ = 0.0f;

    // Reset camera
    camPos = (T3DVec3){{0.0f, 50.0f, -120.0f}};
    camTarget = (T3DVec3){{0.0f, 0.0f, 0.0f}};

    // Reset debug fly camera
    debugCamX = 0.0f;
    debugCamY = 100.0f;
    debugCamZ = -120.0f;
    debugCamYaw = 0.0f;
    debugCamPitch = 0.0f;

    // Initialize controls
    controls_init(&controlConfig);
    playerState.velX = 0.0f;
    playerState.velY = 0.0f;
    playerState.velZ = 0.0f;
    playerState.playerAngle = T3D_DEG_TO_RAD(-90.0f);
    playerState.isGrounded = false;
    playerState.currentJumps = 0;

    // Initialize map loader
    maploader_init(&mapLoader, FB_COUNT, VISIBILITY_RANGE);

    // Define maps in order - collision auto-looked up by name
    // Models are cached, so duplicates only load once
    const char* levelLayout[] = {
        "rom:/test_map.t3dm",
        "rom:/testLVL.t3dm",
        "rom:/test_map.t3dm",
        "rom:/test_map.t3dm",
        "rom:/test_map.t3dm",
        "rom:/test_map.t3dm",
    };

    maploader_load_simple(&mapLoader, levelLayout, 6);

    // Create viewport
    viewport = t3d_viewport_create_buffered(FB_COUNT);

    sceneInitialized = true;
}

void deinit_map_test_scene(void) {
    if (!sceneInitialized) return;

    // CRITICAL: Wait for BOTH RSP and RDP to finish before freeing GPU resources
    // rdpq_fence() does NOT block CPU - use rspq_wait() which actually waits
    rspq_wait();

    maploader_free(&mapLoader);
    collision_loader_free_all();  // Free collision meshes to prevent stale pointer reuse
    t3d_viewport_destroy(&viewport);

    sceneInitialized = false;
}

void update_map_test_scene(void) {
    frameIdx = (frameIdx + 1) % FB_COUNT;

    // Process controls
    controls_update(&playerState, &controlConfig, JOYPAD_PORT_1);

    if (debugFlyMode) {
        // Fly camera controls
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

        float stickX = inputs.stick_x / 128.0f;
        float stickY = inputs.stick_y / 128.0f;
        float flySpeed = 5.0f;
        float rotSpeed = 0.05f;

        if (held.c_left) debugCamYaw += rotSpeed;
        if (held.c_right) debugCamYaw -= rotSpeed;
        if (held.c_up) debugCamPitch += rotSpeed;
        if (held.c_down) debugCamPitch -= rotSpeed;

        if (debugCamPitch > 1.5f) debugCamPitch = 1.5f;
        if (debugCamPitch < -1.5f) debugCamPitch = -1.5f;

        float cosYaw = cosf(debugCamYaw);
        float sinYaw = sinf(debugCamYaw);

        debugCamX += (-stickX * cosYaw + stickY * sinYaw) * flySpeed;
        debugCamZ += (-stickX * sinYaw - stickY * cosYaw) * flySpeed;

        if (held.a) debugCamY += flySpeed;
        if (held.b) debugCamY -= flySpeed;
    } else {
        // Update player position
        playerX += playerState.velX;
        playerY += playerState.velY;
        playerZ += playerState.velZ;
    }

    // Update map visibility
    float checkX = debugFlyMode ? debugCamX : playerX;
    float checkZ = debugFlyMode ? debugCamZ : playerZ;
    maploader_update_visibility(&mapLoader, checkX, checkZ);

    if (!debugFlyMode) {
        // Wall collision
        float pushX = 0.0f, pushZ = 0.0f;
        if (maploader_check_walls(&mapLoader, playerX, playerY, playerZ,
            playerRadius, playerHeight, &pushX, &pushZ)) {
            playerX += pushX;
            playerZ += pushZ;
        }

        // Ground collision
        playerState.isGrounded = false;
        float groundY = maploader_get_ground_height(&mapLoader, playerX, playerY, playerZ);

        if (groundY > -9000.0f && playerState.velY <= 0 && playerY <= groundY + 1.0f) {
            playerY = groundY;
            playerState.velY = 0.0f;
            playerState.isGrounded = true;
            playerState.currentJumps = 0;
        }

        // Fallback ground
        if (!playerState.isGrounded && playerY <= groundLevel) {
            playerY = groundLevel;
            playerState.velY = 0.0f;
            playerState.isGrounded = true;
            playerState.currentJumps = 0;
        }
    }

    // Camera
    if (debugFlyMode) {
        camPos.v[0] = debugCamX;
        camPos.v[1] = debugCamY;
        camPos.v[2] = debugCamZ;
        float cosPitch = cosf(debugCamPitch);
        camTarget.v[0] = debugCamX + sinf(debugCamYaw) * cosPitch * 100.0f;
        camTarget.v[1] = debugCamY + sinf(debugCamPitch) * 100.0f;
        camTarget.v[2] = debugCamZ - cosf(debugCamYaw) * cosPitch * 100.0f;
    } else {
        camPos.v[0] = playerX;
        camPos.v[1] = playerY + 49.0f;
        camPos.v[2] = -120.0f;
        camTarget.v[0] = playerX;
        camTarget.v[1] = playerY;
        camTarget.v[2] = 0.0f;
    }

    // Exit to title
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed.start) {
        change_scene(TITLE);
    }
}

void draw_map_test_scene(void) {
    surface_t *disp = display_get();
    surface_t *zbuf = display_get_zbuf();
    rdpq_attach(disp, zbuf);
    rdpq_clear(RGBA32(0x20, 0x20, 0x40, 0xFF));
    rdpq_clear_z(0xFFFC);

    t3d_frame_start();
    t3d_viewport_attach(&viewport);

    // Camera setup
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(70.0f), 1.0f, 1000.0f);
    t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0, 1, 0}});

    // Lighting
    uint8_t ambientColor[4] = {80, 80, 80, 0xFF};
    uint8_t lightColor[4] = {255, 255, 255, 0xFF};
    T3DVec3 lightDir = {{1.0f, 1.0f, 1.0f}};
    t3d_vec3_norm(&lightDir);
    t3d_light_set_ambient(ambientColor);
    t3d_light_set_directional(0, lightColor, &lightDir);
    t3d_light_set_count(1);

    // Draw all active maps
    maploader_draw(&mapLoader, frameIdx);

    t3d_tri_sync();
    rdpq_sync_pipe();  // Wait for RDP to finish T3D triangles before UI
    rdpq_set_mode_standard();

    // Debug info
    int activeVerts = maploader_get_active_verts(&mapLoader);
    int activeCount = 0;
    for (int i = 0; i < mapLoader.count; i++) {
        if (mapLoader.segments[i].active) activeCount++;
    }

    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 10, "MAP LOADER TEST %s", debugFlyMode ? "[FLY]" : "");
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 20, "Pos: %.0f %.0f %.0f", playerX, playerY, playerZ);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 30, "Maps: %d/%d active", activeCount, mapLoader.count);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 40, "Verts: %d", activeVerts);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 50, "Grounded: %s", playerState.isGrounded ? "YES" : "NO");

    // Show map segment positions
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 70, "Segment X positions:");
    for (int i = 0; i < mapLoader.count && i < 6; i++) {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 80 + i * 10,
            " [%d] X=%.0f %s", i, mapLoader.segments[i].posX,
            mapLoader.segments[i].active ? "*" : "");
    }

    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 220, "START=exit Z=fly");

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}
