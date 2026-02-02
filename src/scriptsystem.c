#include "scriptsystem.h"
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/tpx.h>
#include <stdbool.h>
#include <math.h>

// External camera references (from game.c)
extern T3DVec3 camPos;
extern T3DVec3 camTarget;

// Cutscene state
static bool cutsceneActive = false;
static int cutsceneStep = 0;

// Helper: lerp between two floats
static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

// Helper: lerp between two T3DVec3
static void lerpVec3(T3DVec3 *out, T3DVec3 *a, T3DVec3 *b, float t) {
    out->v[0] = lerpf(a->v[0], b->v[0], t);
    out->v[1] = lerpf(a->v[1], b->v[1], t);
    out->v[2] = lerpf(a->v[2], b->v[2], t);
}

/**
 * CameraCut - Instant camera jump
 * Runs for 1 step, immediately sets camera position/target
 *
 * @param step         Which step this action runs on
 * @param nextStep     Pointer to next step (modified to advance)
 * @param currentStep  Current cutscene step
 * @param camPosition  Where to put camera
 * @param camLookAt    Where camera looks
 * @param jumpToStep   Step to jump to after (-1 = stay)
 */
static void CameraCut(int step, int *nextStep, int currentStep,
    T3DVec3 camPosition, T3DVec3 camLookAt, int jumpToStep)
{
    if (currentStep != step) return;

    // Set camera instantly
    camPos = camPosition;
    camTarget = camLookAt;

    // Advance to next step
    if (jumpToStep >= 0) {
        *nextStep = jumpToStep;
    }
}

/**
 * CameraLerp - Smooth camera movement over multiple steps
 *
 * @param stepStart    First step of this action
 * @param stepEnd      Last step of this action
 * @param nextStep     Pointer to next step
 * @param currentStep  Current cutscene step
 * @param startPos     Starting camera position
 * @param endPos       Ending camera position
 * @param startLookAt  Starting look target
 * @param endLookAt    Ending look target
 * @param jumpToStep   Step to jump to when done (-1 = stay)
 */
static void CameraLerp(int stepStart, int stepEnd, int *nextStep, int currentStep,
    T3DVec3 startPos, T3DVec3 endPos, T3DVec3 startLookAt, T3DVec3 endLookAt,
    int jumpToStep)
{
    if (currentStep < stepStart || currentStep > stepEnd) return;

    // Calculate interpolation factor (0.0 to 1.0)
    float t = 0.0f;
    if (stepEnd > stepStart) {
        t = (float)(currentStep - stepStart) / (float)(stepEnd - stepStart);
    }

    // Lerp camera position and target
    lerpVec3(&camPos, &startPos, &endPos, t);
    lerpVec3(&camTarget, &startLookAt, &endLookAt, t);

    // Advance when we reach the end
    if (currentStep >= stepEnd && jumpToStep >= 0) {
        *nextStep = jumpToStep;
    }
}

/**
 * WaitForButton - Wait for A button press before continuing
 */
static void WaitForButton(int step, int *nextStep, int currentStep, int jumpToStep)
{
    if (currentStep != step) return;

    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed.a) {
        *nextStep = jumpToStep;
    }
}

/**
 * Run a cutscene script
 * Call this every frame during cutscene
 * Returns: true if cutscene still running, false when done
 */
bool cutscene_update(void) {
    if (!cutsceneActive) return false;

    int nextStep = cutsceneStep;

    // ========== DEFINE YOUR CUTSCENE HERE ==========
    // Example cutscene:

    // Step 0: Cut to starting position
    CameraCut(0, &nextStep, cutsceneStep,
        (T3DVec3){{0, 100, -200}},    // camera position
        (T3DVec3){{0, 50, 0}},        // look at
        1);                            // go to step 1

    // Steps 1-30: Lerp camera forward (30 frames)
    CameraLerp(1, 30, &nextStep, cutsceneStep,
        (T3DVec3){{0, 100, -200}},    // start pos
        (T3DVec3){{0, 50, -50}},      // end pos
        (T3DVec3){{0, 50, 0}},        // start look
        (T3DVec3){{0, 0, 100}},       // end look
        31);                           // go to step 31

    // Step 31: Wait for button
    WaitForButton(31, &nextStep, cutsceneStep, 32);

    // Step 32: End cutscene
    if (cutsceneStep == 32) {
        cutsceneActive = false;
        return false;
    }

    // ========== END CUTSCENE DEFINITION ==========

    // Advance step (automatically increment if not jumping)
    if (nextStep == cutsceneStep) {
        cutsceneStep++;
    } else {
        cutsceneStep = nextStep;
    }

    return true;
}

/**
 * Start a cutscene
 */
void cutscene_start(void) {
    cutsceneActive = true;
    cutsceneStep = 0;
}

/**
 * Check if cutscene is running
 */
bool cutscene_is_active(void) {
    return cutsceneActive;
}

/**
 * Skip/cancel cutscene
 */
void cutscene_skip(void) {
    cutsceneActive = false;
}
