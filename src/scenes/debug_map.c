#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include "debug_map.h"
#include "../scene.h"
#include "../mapLoader.h"
#include "../mapData.h"
#include "../levels.h"
#include "../constants.h"
#include <stdbool.h>
#include <math.h>

#define FB_COUNT 3

// Debug modes
typedef enum {
    MODE_CAMERA,
    MODE_PLACEMENT,
} DebugMode;

static DebugMode currentMode = MODE_CAMERA;

// Map data
static MapLoader mapLoader;
static MapRuntime mapRuntime;

// Camera state
static float camX = 0.0f;
static float camY = 50.0f;
static float camZ = 0.0f;
static float camYaw = 0.0f;    // Horizontal rotation
static float camPitch = 0.0f;  // Vertical rotation

// Current decoration being placed
static DecoType currentDecoType = DECO_BARREL;
static float decoX = 0.0f;
static float decoY = 0.0f;
static float decoZ = 0.0f;
static float decoRotY = 0.0f;
static float decoScaleX = 1.0f;
static float decoScaleY = 1.0f;
static float decoScaleZ = 1.0f;

// Rendering
static T3DViewport viewport;
static T3DMat4FP* decoMatFP;
static int frameIdx = 0;
static bool sceneInitialized = false;

// Shadow/decal sprite for decal-based decorations preview
static sprite_t* shadowSprite = NULL;

// Input timing for triggers
static int triggerCooldown = 0;

void init_debug_map_scene(void) {
    // Reset state
    frameIdx = 0;
    currentMode = MODE_CAMERA;

    // Reset camera
    camX = 0.0f;
    camY = 50.0f;
    camZ = 0.0f;
    camYaw = 0.0f;
    camPitch = 0.0f;

    // Reset decoration placement
    currentDecoType = DECO_BARREL;
    decoX = 0.0f;
    decoY = 0.0f;
    decoZ = 0.0f;
    decoRotY = 0.0f;
    decoScaleX = 1.0f;
    decoScaleY = 1.0f;
    decoScaleZ = 1.0f;
    triggerCooldown = 0;

    // Initialize map loader for terrain
    maploader_init(&mapLoader, FB_COUNT, DEBUG_VISIBILITY_RANGE);
    map_runtime_init(&mapRuntime, FB_COUNT, DEBUG_VISIBILITY_RANGE);

    // Load level 1 terrain (but not decorations - we place those manually)
    const LevelData* level = ALL_LEVELS[LEVEL_1];
    maploader_load_simple(&mapLoader, level->segments, level->segmentCount);
    mapRuntime.mapLoader = &mapLoader;  // Set collision reference for rats

    // Allocate decoration matrices (one per decoration per frame buffer, +1 for preview)
    decoMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT * (MAX_DECORATIONS + 1));

    // Create viewport
    viewport = t3d_viewport_create_buffered(FB_COUNT);

    // Load shadow sprite for decal previews
    shadowSprite = sprite_load("rom:/shadow.sprite");

    sceneInitialized = true;
}

void deinit_debug_map_scene(void) {
    if (!sceneInitialized) return;

    // Print all placed decorations before exiting
    if (mapRuntime.decoCount > 0) {
        map_print_all_decorations(&mapRuntime);
    }

    // CRITICAL: Wait for BOTH RSP and RDP to finish before freeing GPU resources
    // rdpq_fence() does NOT block CPU - use rspq_wait() which actually waits
    rspq_wait();

    maploader_free(&mapLoader);
    map_runtime_free(&mapRuntime);
    collision_loader_free_all();  // Free collision meshes to prevent stale pointer reuse
    t3d_viewport_destroy(&viewport);

    if (decoMatFP) {
        free_uncached(decoMatFP);
        decoMatFP = NULL;
    }

    if (shadowSprite) {
        sprite_free(shadowSprite);
        shadowSprite = NULL;
    }

    sceneInitialized = false;
}

static void update_camera_mode(joypad_inputs_t* inputs, joypad_buttons_t* held, joypad_buttons_t* pressed) {
    float moveSpeed = 3.0f;
    float rotSpeed = 0.03f;

    // Look around with C-buttons
    if (held->c_left) camYaw -= rotSpeed;
    if (held->c_right) camYaw += rotSpeed;
    if (held->c_up) camPitch += rotSpeed;
    if (held->c_down) camPitch -= rotSpeed;

    // Clamp pitch
    if (camPitch > 1.4f) camPitch = 1.4f;
    if (camPitch < -1.4f) camPitch = -1.4f;

    // Calculate forward and right vectors (horizontal plane)
    float cosYaw = cosf(camYaw);
    float sinYaw = sinf(camYaw);

    // Forward/back relative to look direction
    float stickY = inputs->stick_y / 128.0f;
    float stickX = inputs->stick_x / 128.0f;

    // Move forward/back in look direction (on XZ plane)
    camX += sinYaw * stickY * moveSpeed;
    camZ -= cosYaw * stickY * moveSpeed;

    // Strafe left/right
    camX += cosYaw * stickX * moveSpeed;
    camZ += sinYaw * stickX * moveSpeed;

    // Up/down with A/B in camera mode - wait, B switches modes
    // Use Z for down, A for up
    if (held->a) camY += moveSpeed;
    if (held->z) camY -= moveSpeed;

    // Switch to placement mode
    if (pressed->b) {
        currentMode = MODE_PLACEMENT;
        // Set decoration position to in front of camera
        decoX = camX + sinf(camYaw) * 50.0f;
        decoY = camY - 20.0f;
        decoZ = camZ - cosf(camYaw) * 50.0f;
    }
}

static void update_placement_mode(joypad_inputs_t* inputs, joypad_buttons_t* held, joypad_buttons_t* pressed) {
    float moveSpeed = 2.0f;
    float rotSpeed = 0.05f;
    float scaleSpeed = 0.02f;

    float stickY = inputs->stick_y / 128.0f;
    float stickX = inputs->stick_x / 128.0f;

    float cosYaw = cosf(camYaw);
    float sinYaw = sinf(camYaw);

    if (held->z) {
        // Z held: scale mode
        // Stick X = scale X, Stick Y = scale Z
        decoScaleX += stickX * scaleSpeed;
        decoScaleZ += stickY * scaleSpeed;

        // C-up/down = scale Y
        if (held->c_up) decoScaleY += scaleSpeed;
        if (held->c_down) decoScaleY -= scaleSpeed;

        // Clamp scales to positive values
        if (decoScaleX < 0.1f) decoScaleX = 0.1f;
        if (decoScaleY < 0.1f) decoScaleY = 0.1f;
        if (decoScaleZ < 0.1f) decoScaleZ = 0.1f;
    } else {
        // Normal mode: move and rotate
        // Move decoration XZ with stick (relative to camera direction)
        decoX += sinYaw * stickY * moveSpeed;
        decoZ -= cosYaw * stickY * moveSpeed;
        decoX += cosYaw * stickX * moveSpeed;
        decoZ += sinYaw * stickX * moveSpeed;

        // Move Y with C-up/down
        if (held->c_up) decoY += moveSpeed;
        if (held->c_down) decoY -= moveSpeed;

        // Rotate with C-left/right
        if (held->c_left) decoRotY -= rotSpeed;
        if (held->c_right) decoRotY += rotSpeed;
    }

    // Confirm placement with A
    if (pressed->a) {
        int idx = map_add_decoration(&mapRuntime, currentDecoType,
            decoX, decoY, decoZ, decoRotY,
            decoScaleX, decoScaleY, decoScaleZ);
        if (idx >= 0) {
            // Print all decorations as copyable C code
            map_print_all_decorations(&mapRuntime);
        }
    }

    // Switch back to camera mode
    if (pressed->b) {
        currentMode = MODE_CAMERA;
    }
}

void update_debug_map_scene(void) {
    frameIdx = (frameIdx + 1) % FB_COUNT;

    joypad_poll();
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    // Cycle decoration type with L/R triggers (with cooldown)
    if (triggerCooldown > 0) triggerCooldown--;

    if (triggerCooldown == 0) {
        if (held.l) {
            currentDecoType = (currentDecoType + DECO_TYPE_COUNT - 1) % DECO_TYPE_COUNT;
            // Lazy load the model
            map_get_deco_model(&mapRuntime, currentDecoType);
            triggerCooldown = 15;
        }
        if (held.r) {
            currentDecoType = (currentDecoType + 1) % DECO_TYPE_COUNT;
            // Lazy load the model
            map_get_deco_model(&mapRuntime, currentDecoType);
            triggerCooldown = 15;
        }
    }

    // Mode-specific updates
    if (currentMode == MODE_CAMERA) {
        update_camera_mode(&inputs, &held, &pressed);
    } else {
        update_placement_mode(&inputs, &held, &pressed);
    }

    // Update map visibility based on camera
    maploader_update_visibility(&mapLoader, camX, camZ);

    // Update decoration animations
    float deltaTime = 1.0f / 60.0f;
    map_update_decorations(&mapRuntime, deltaTime);

    // Exit to title
    if (pressed.start) {
        change_scene(TITLE);
    }
}

void draw_debug_map_scene(void) {
    surface_t *disp = display_get();
    surface_t *zbuf = display_get_zbuf();
    rdpq_attach(disp, zbuf);
    rdpq_clear(RGBA32(0x10, 0x10, 0x30, 0xFF));
    rdpq_clear_z(0xFFFC);

    t3d_frame_start();
    t3d_viewport_attach(&viewport);

    // Camera setup
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(70.0f), 1.0f, 1500.0f);

    // Calculate camera target based on yaw/pitch
    float cosPitch = cosf(camPitch);
    T3DVec3 camPos = {{camX, camY, camZ}};
    T3DVec3 camTarget = {{
        camX + sinf(camYaw) * cosPitch * 100.0f,
        camY + sinf(camPitch) * 100.0f,
        camZ - cosf(camYaw) * cosPitch * 100.0f
    }};
    t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0, 1, 0}});

    // Lighting
    uint8_t ambientColor[4] = {80, 80, 80, 0xFF};
    uint8_t lightColor[4] = {255, 255, 255, 0xFF};
    T3DVec3 lightDir = {{1.0f, 1.0f, 1.0f}};
    t3d_vec3_norm(&lightDir);
    t3d_light_set_ambient(ambientColor);
    t3d_light_set_directional(0, lightColor, &lightDir);
    t3d_light_set_count(1);

    // Draw terrain
    maploader_draw(&mapLoader, frameIdx);

    // Draw placed decorations - each gets its own matrix slot
    int matrixStride = MAX_DECORATIONS + 1;  // +1 for preview
    for (int i = 0; i < mapRuntime.decoCount; i++) {
        DecoInstance* deco = &mapRuntime.decorations[i];
        if (!deco->active || deco->type == DECO_NONE) continue;
        DecoTypeRuntime* decoType = map_get_deco_type(&mapRuntime, deco->type);
        const DecoTypeDef* decoDef = &DECO_TYPES[deco->type];
        if (decoType && decoType->model) {
            int matIdx = frameIdx * matrixStride + i;
            t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                (float[3]){deco->posX, deco->posY, deco->posZ}
            );
            t3d_matrix_push(&decoMatFP[matIdx]);

            // For models with only vertex colors (no textures), set shade-only mode
            if (decoDef->vertexColorsOnly) {
                t3d_tri_sync();
                rdpq_sync_pipe();
                rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
            }

            // Use per-instance skeleton if available, otherwise shared skeleton
            // Validate skeleton before use to prevent crash on corrupted/freed data
            if (deco->hasOwnSkeleton && skeleton_is_valid(&deco->skeleton)) {
                t3d_model_draw_skinned(decoType->model, &deco->skeleton);
            } else if (decoType->hasSkeleton && skeleton_is_valid(&decoType->skeleton)) {
                t3d_model_draw_skinned(decoType->model, &decoType->skeleton);
            } else {
                t3d_model_draw(decoType->model);
            }

            // Reset combiner after vertex-colors-only model
            if (decoDef->vertexColorsOnly) {
                t3d_tri_sync();
                rdpq_sync_pipe();
                rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
            }
            t3d_matrix_pop(1);
        }
    }

    // Draw current decoration preview (in placement mode) - uses last matrix slot
    if (currentMode == MODE_PLACEMENT) {
        DecoTypeRuntime* previewType = map_get_deco_type(&mapRuntime, currentDecoType);
        const DecoTypeDef* previewDef = &DECO_TYPES[currentDecoType];
        (void)previewDef;  // Suppress unused warning for decal types
        if (previewType && previewType->model) {
            int matIdx = frameIdx * matrixStride + MAX_DECORATIONS;
            t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
                (float[3]){decoScaleX, decoScaleY, decoScaleZ},
                (float[3]){0.0f, decoRotY, 0.0f},
                (float[3]){decoX, decoY, decoZ}
            );
            t3d_matrix_push(&decoMatFP[matIdx]);

            // For models with only vertex colors (no textures), set shade-only mode
            if (previewDef->vertexColorsOnly) {
                t3d_tri_sync();
                rdpq_sync_pipe();
                rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
            }

            // Validate skeleton before use to prevent crash on corrupted/freed data
            if (previewType->hasSkeleton && skeleton_is_valid(&previewType->skeleton)) {
                t3d_model_draw_skinned(previewType->model, &previewType->skeleton);
            } else {
                t3d_model_draw(previewType->model);
            }

            // Reset combiner after vertex-colors-only model
            if (previewDef->vertexColorsOnly) {
                t3d_tri_sync();
                rdpq_sync_pipe();
                rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
            }
            t3d_matrix_pop(1);
        } else if (currentDecoType == DECO_OILPUDDLE) {
            // Decal-based decoration preview (oil puddle)
            // End T3D rendering temporarily for 2D decal
            t3d_tri_sync();
            rdpq_sync_pipe();  // Wait for RDP to finish T3D triangles

            float sz = decoScaleX * 20.0f;  // Same radius calc as oilpuddle_init
            T3DVec3 corners[4] = {
                {{decoX - sz, decoY + 0.5f, decoZ - sz}},
                {{decoX + sz, decoY + 0.5f, decoZ - sz}},
                {{decoX - sz, decoY + 0.5f, decoZ + sz}},
                {{decoX + sz, decoY + 0.5f, decoZ + sz}}
            };

            T3DVec3 screenPos[4];
            for (int c = 0; c < 4; c++) {
                t3d_viewport_calc_viewspace_pos(&viewport, &screenPos[c], &corners[c]);
            }

            if (screenPos[0].v[2] > 0 && screenPos[1].v[2] > 0 &&
                screenPos[2].v[2] > 0 && screenPos[3].v[2] > 0) {

                rdpq_sync_pipe();
                rdpq_set_mode_standard();
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                // Bright purple so it's very visible
                rdpq_set_prim_color(RGBA32(180, 60, 220, 200));

                float v0[] = {screenPos[0].v[0], screenPos[0].v[1]};
                float v1[] = {screenPos[1].v[0], screenPos[1].v[1]};
                float v2[] = {screenPos[2].v[0], screenPos[2].v[1]};
                float v3[] = {screenPos[3].v[0], screenPos[3].v[1]};

                rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
                rdpq_triangle(&TRIFMT_FILL, v2, v1, v3);
            }
        }
    }

    t3d_tri_sync();
    rdpq_sync_pipe();  // Wait for RDP to finish T3D triangles before UI
    rdpq_set_mode_standard();

    // UI
    const char* modeStr = (currentMode == MODE_CAMERA) ? "CAMERA" : "PLACEMENT";
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 10, "DEBUG MAP - %s", modeStr);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 22, "Deco: %s", DECO_TYPES[currentDecoType].name);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 34, "Placed: %d", mapRuntime.decoCount);

    if (currentMode == MODE_CAMERA) {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 50, "Cam: %.0f %.0f %.0f", camX, camY, camZ);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 200, "Stick=Move C=Look A/Z=Up/Down");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 212, "L/R=Deco Type B=Place Mode");
    } else {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 50, "Pos: %.1f %.1f %.1f", decoX, decoY, decoZ);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 62, "Rot: %.2f", decoRotY);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 74, "Scale: %.2f %.2f %.2f", decoScaleX, decoScaleY, decoScaleZ);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 188, "Stick=Move C-U/D=Y C-L/R=Rot");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 200, "Z+Stick=ScaleXZ Z+C=ScaleY");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 212, "A=Place B=Camera L/R=Type");
    }

    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 224, "START=Exit");

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}
