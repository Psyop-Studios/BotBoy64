#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include "../scene.h"
#include "../save.h"
#include "cutscene_demo.h"
#include <stdbool.h>
#include <math.h>
#include <string.h>

// Skeleton validity check - validates all critical pointers
static inline bool skeleton_is_valid(const T3DSkeleton* skel) {
    return skel != NULL &&
           skel->bones != NULL &&
           skel->boneMatricesFP != NULL &&
           skel->skeletonRef != NULL;
}

#define FB_COUNT 3

// Two models to look between
static T3DModel *model1 = NULL;
static T3DModel *ratModel = NULL;
static T3DSkeleton ratSkel;
static T3DAnim ratAnimBite;

// Model positions
static T3DVec3 model1Pos = {{-25.0f, 0.0f, 0.0f}};
static T3DVec3 ratPos = {{25.0f, 0.0f, 0.0f}};

// Matrices
static T3DMat4FP *model1MatFP = NULL;
static T3DMat4FP *ratMatFP = NULL;

// Camera (positive Z = behind the models looking at them)
static T3DVec3 camPos = {{0.0f, 10.0f, 60.0f}};
static T3DVec3 camTarget = {{0.0f, 0.0f, 0.0f}};
static T3DViewport viewport;

// Cutscene state
static int cutsceneStep = 0;
static bool cutsceneRunning = true;

static int frameIdx = 0;

// Helper: lerp float
static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

// Ease out (slow down at end)
static float easeOutQuad(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}

// Ease in-out (slow at start and end)
__attribute__((unused))
static float easeInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

// Helper: lerp vec3
static void lerpVec3(T3DVec3 *out, T3DVec3 *a, T3DVec3 *b, float t) {
    out->v[0] = lerpf(a->v[0], b->v[0], t);
    out->v[1] = lerpf(a->v[1], b->v[1], t);
    out->v[2] = lerpf(a->v[2], b->v[2], t);
}

// ============ CUTSCENE ACTIONS ============

// Camera target lerp (position stays fixed, target moves with easing)
static void CameraTargetLerp(int stepStart, int stepEnd, int *nextStep, int currentStep,
    T3DVec3 startTarget, T3DVec3 endTarget, int jumpToStep)
{
    if (currentStep < stepStart || currentStep > stepEnd) return;

    float t = 0.0f;
    if (stepEnd > stepStart) {
        t = (float)(currentStep - stepStart) / (float)(stepEnd - stepStart);
    }

    // Apply easing (slows down as it reaches target)
    float easedT = easeOutQuad(t);

    lerpVec3(&camTarget, &startTarget, &endTarget, easedT);

    if (currentStep >= stepEnd && jumpToStep >= 0) {
        *nextStep = jumpToStep;
    }
}

// Wait for frames
static void Wait(int stepStart, int stepEnd, int *nextStep, int currentStep, int jumpToStep)
{
    if (currentStep < stepStart || currentStep > stepEnd) return;

    if (currentStep >= stepEnd && jumpToStep >= 0) {
        *nextStep = jumpToStep;
    }
}

// ============ CUTSCENE SCRIPT ============

static void run_cutscene(void) {
    if (!cutsceneRunning) return;

    int nextStep = cutsceneStep;

    // Step 0-30: Wait at center
    Wait(0, 30, &nextStep, cutsceneStep, 31);

    // Step 31-90: Lerp target from cube to rat (60 frames = 1 sec)
    CameraTargetLerp(31, 90, &nextStep, cutsceneStep,
        model1Pos,   // start looking at cube
        ratPos,      // end looking at rat
        91);

    // Step 91-120: Hold on rat
    Wait(91, 120, &nextStep, cutsceneStep, 121);

    // Step 121-180: Lerp target back from rat to cube
    CameraTargetLerp(121, 180, &nextStep, cutsceneStep,
        ratPos,      // start looking at rat
        model1Pos,   // end looking at cube
        181);

    // Step 181-210: Hold on cube
    Wait(181, 210, &nextStep, cutsceneStep, 211);

    // Step 211: Loop back to start
    if (cutsceneStep >= 211) {
        nextStep = 31;  // Loop the lerp
    }

    // Advance step
    if (nextStep == cutsceneStep) {
        cutsceneStep++;
    } else {
        cutsceneStep = nextStep;
    }
}

void init_cutscene_demo_scene(void) {
    // Zero T3D skeleton and animation structures to prevent denormal floats
    memset(&ratSkel, 0, sizeof(T3DSkeleton));
    memset(&ratAnimBite, 0, sizeof(T3DAnim));

    // Load cube model
    model1 = t3d_model_load("rom:/le_epic_cube.t3dm");

    // Load rat model with skeleton and animation
    ratModel = t3d_model_load("rom:/rat.t3dm");
    ratSkel = t3d_skeleton_create(ratModel);
    t3d_skeleton_reset(&ratSkel);
    ratAnimBite = t3d_anim_create(ratModel, "rat_attack");
    t3d_anim_attach(&ratAnimBite, &ratSkel);
    t3d_anim_set_looping(&ratAnimBite, true);
    t3d_anim_set_playing(&ratAnimBite, true);
    t3d_anim_set_speed(&ratAnimBite, 1.0f);

    // Allocate matrices
    model1MatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
    ratMatFP = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);

    // Create viewport
    viewport = t3d_viewport_create_buffered(FB_COUNT);

    // Set initial camera target to model1
    camTarget = model1Pos;

    // Reset cutscene
    cutsceneStep = 0;
    cutsceneRunning = true;
}

void deinit_cutscene_demo_scene(void) {
    // Destroy T3D animation and skeleton BEFORE freeing models
    // This prevents stale pointers from being read as garbage floats
    t3d_anim_destroy(&ratAnimBite);
    t3d_skeleton_destroy(&ratSkel);
    memset(&ratAnimBite, 0, sizeof(T3DAnim));
    memset(&ratSkel, 0, sizeof(T3DSkeleton));

    if (model1) {
        t3d_model_free(model1);
        model1 = NULL;
    }
    if (ratModel) {
        t3d_model_free(ratModel);
        ratModel = NULL;
    }
    if (model1MatFP) {
        free_uncached(model1MatFP);
        model1MatFP = NULL;
    }
    if (ratMatFP) {
        free_uncached(ratMatFP);
        ratMatFP = NULL;
    }
}

void update_cutscene_demo_scene(void) {
    // Audio poll at start of update
    if (audio_can_write()) {
        short *buf = audio_write_begin();
        rspq_wait();  // Flush RSPQ to exit highpri mode before mixer_poll (needed when rdpq_debug is active)
        save_apply_volume_settings_safe();  // Apply pending volume changes in safe window
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }

    frameIdx = (frameIdx + 1) % FB_COUNT;

    // Update rat animation
    t3d_anim_update(&ratAnimBite, 0.02f);

    // Run cutscene
    run_cutscene();

    // Check for exit
    joypad_poll();
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed.start) {
        change_scene(TITLE);
    }
}

void draw_cutscene_demo_scene(void) {
    surface_t *disp = display_get();
    surface_t *zbuf = display_get_zbuf();
    rdpq_attach(disp, zbuf);
    rdpq_clear(RGBA32(0x10, 0x10, 0x30, 0xFF));
    rdpq_clear_z(0xFFFC);

    t3d_frame_start();
    t3d_viewport_attach(&viewport);

    // Camera
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(70.0f), 1.0f, 500.0f);
    t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0, 1, 0}});

    // Lighting
    uint8_t ambientColor[4] = {80, 80, 80, 0xFF};
    uint8_t lightColor[4] = {255, 255, 255, 0xFF};
    T3DVec3 lightDir = {{1.0f, 1.0f, 1.0f}};
    t3d_vec3_norm(&lightDir);
    t3d_light_set_ambient(ambientColor);
    t3d_light_set_directional(0, lightColor, &lightDir);
    t3d_light_set_count(1);

    // Draw model 1 (left) - smaller scale
    t3d_mat4fp_from_srt_euler(&model1MatFP[frameIdx],
        (float[3]){0.3f, 0.3f, 0.3f},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){model1Pos.v[0], model1Pos.v[1], model1Pos.v[2]}
    );
    t3d_matrix_push(&model1MatFP[frameIdx]);
    t3d_model_draw(model1);
    t3d_matrix_pop(1);

    // Draw rat (right) with skeleton animation
    t3d_skeleton_update(&ratSkel);
    t3d_mat4fp_from_srt_euler(&ratMatFP[frameIdx],
        (float[3]){0.5f, 0.5f, 0.5f},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){ratPos.v[0], ratPos.v[1], ratPos.v[2]}
    );
    t3d_matrix_push(&ratMatFP[frameIdx]);
    // Validate skeleton before use to prevent crash on corrupted/freed data
    if (skeleton_is_valid(&ratSkel)) {
        t3d_model_draw_skinned(ratModel, &ratSkel);
    } else {
        t3d_model_draw(ratModel);
    }
    t3d_matrix_pop(1);

    t3d_tri_sync();
    rdpq_sync_pipe();  // Wait for RDP to finish T3D triangles before UI

    // Debug text
    rdpq_set_mode_standard();
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 10, "CUTSCENE DEMO");
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 20, "Step: %d", cutsceneStep);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 30, "Target: %.1f %.1f %.1f",
        camTarget.v[0], camTarget.v[1], camTarget.v[2]);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 10, 220, "START to exit");

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}
