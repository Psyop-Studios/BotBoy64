// ============================================================
// EFFECTS MODULE
// Screen shake, hit flash, fade, iris wipe
// ============================================================

#include "effects.h"
#include "constants.h"
#include <stdlib.h>
#include <math.h>

// Set FPU to flush denormals to zero and disable FPU exceptions
static void fpu_flush_denormals(void) {
    uint32_t fcr31;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcr31));
    fcr31 |= (1 << 24);  // Set FS bit (Flush denormalized results to zero)
    fcr31 &= ~(0x1F << 7);  // Clear all exception enable bits (bits 7-11)
    fcr31 &= ~(0x3F << 2);  // Clear all cause bits (bits 2-6)
    fcr31 &= ~(1 << 17);    // Clear cause bit for unimplemented operation
    fcr31 &= ~(0x1F << 12); // Clear sticky FLAG bits (bits 12-16)
    __asm__ volatile("ctc1 %0, $31" : : "r"(fcr31));
}

// ============================================================
// STATIC DATA
// ============================================================

// Screen shake
static float g_shakeIntensity = 0.0f;
static float g_shakeOffsetX = 0.0f;
static float g_shakeOffsetY = 0.0f;

// Flash effects
static float g_hitFlashTimer = 0.0f;
static float g_boltFlashTimer = 0.0f;

// Hitstop
static float g_hitstopTimer = 0.0f;

// Fade
static float g_fadeAlpha = 0.0f;

// ============================================================
// INIT / UPDATE
// ============================================================

void effects_init(void) {
    g_shakeIntensity = 0.0f;
    g_shakeOffsetX = 0.0f;
    g_shakeOffsetY = 0.0f;
    g_hitFlashTimer = 0.0f;
    g_boltFlashTimer = 0.0f;
    g_hitstopTimer = 0.0f;
    g_fadeAlpha = 0.0f;
}

void effects_update(float deltaTime) {
    fpu_flush_denormals();  // Prevent denormal exceptions from float division
    // Update screen shake
    if (g_shakeIntensity > 0.1f) {
        g_shakeOffsetX = ((rand() % 200) - 100) / 100.0f * g_shakeIntensity;
        g_shakeOffsetY = ((rand() % 200) - 100) / 100.0f * g_shakeIntensity;
        g_shakeIntensity *= 0.85f;
    } else {
        g_shakeIntensity = 0.0f;
        g_shakeOffsetX = 0.0f;
        g_shakeOffsetY = 0.0f;
    }

    // Decay hit flash
    if (g_hitFlashTimer > 0.0f) {
        g_hitFlashTimer -= deltaTime;
        if (g_hitFlashTimer < 0.0f) g_hitFlashTimer = 0.0f;
    }

    // Decay bolt flash
    if (g_boltFlashTimer > 0.0f) {
        g_boltFlashTimer -= deltaTime;
        if (g_boltFlashTimer < 0.0f) g_boltFlashTimer = 0.0f;
    }

    // Decay hitstop
    if (g_hitstopTimer > 0.0f) {
        g_hitstopTimer -= deltaTime;
        if (g_hitstopTimer < 0.0f) g_hitstopTimer = 0.0f;
    }
}

// ============================================================
// SCREEN SHAKE
// ============================================================

void effects_screen_shake(float intensity) {
    if (intensity > g_shakeIntensity) {
        g_shakeIntensity = intensity;
    }
}

void effects_get_shake_offset(float* outX, float* outY) {
    if (outX) *outX = g_shakeOffsetX;
    if (outY) *outY = g_shakeOffsetY;
}

// ============================================================
// HIT FLASH / BOLT FLASH
// ============================================================

void effects_hit_flash(float duration) {
    g_hitFlashTimer = duration;
}

void effects_bolt_flash(void) {
    g_boltFlashTimer = 0.1f;
}

color_t effects_get_flash_color(void) {
    if (g_boltFlashTimer > 0.0f) {
        // White flash for bolt collection
        uint8_t alpha = (uint8_t)(g_boltFlashTimer * 10.0f * 100.0f);  // Quick fade
        if (alpha > 100) alpha = 100;
        return RGBA32(255, 255, 255, alpha);
    }
    if (g_hitFlashTimer > 0.0f) {
        // Red flash for damage
        uint8_t alpha = (uint8_t)(g_hitFlashTimer * 200.0f);
        if (alpha > 100) alpha = 100;
        return RGBA32(255, 0, 0, alpha);
    }
    return RGBA32(0, 0, 0, 0);
}

bool effects_flash_active(void) {
    return g_hitFlashTimer > 0.0f || g_boltFlashTimer > 0.0f;
}

// ============================================================
// HITSTOP
// ============================================================

void effects_hitstop(float duration) {
    g_hitstopTimer = duration;
}

bool effects_hitstop_active(void) {
    return g_hitstopTimer > 0.0f;
}

// ============================================================
// FADE OVERLAY
// ============================================================

void effects_set_fade(float alpha) {
    g_fadeAlpha = alpha;
    if (g_fadeAlpha < 0.0f) g_fadeAlpha = 0.0f;
    if (g_fadeAlpha > 1.0f) g_fadeAlpha = 1.0f;
}

float effects_get_fade(void) {
    return g_fadeAlpha;
}

void effects_draw_fade(void) {
    if (g_fadeAlpha <= 0.0f) return;

    rdpq_sync_pipe();  // Sync before 2D mode switch
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    uint8_t alpha = (uint8_t)(g_fadeAlpha * 255.0f);
    rdpq_set_prim_color(RGBA32(0, 0, 0, alpha));
    rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============================================================
// IRIS WIPE EFFECT
// ============================================================

void effects_iris_init(IrisState* iris) {
    float maxRadius = (float)SCREEN_WIDTH + 80.0f;
    iris->radius = maxRadius;
    iris->centerX = (float)SCREEN_CENTER_X;
    iris->centerY = (float)SCREEN_CENTER_Y;
    iris->targetX = (float)SCREEN_CENTER_X;
    iris->targetY = (float)SCREEN_CENTER_Y;
    iris->active = false;
    iris->pauseTimer = 0.0f;
    iris->paused = false;
}

void effects_iris_start(IrisState* iris, float targetX, float targetY) {
    iris->active = true;
    iris->paused = false;
    iris->pauseTimer = 0.0f;
    iris->targetX = targetX;
    iris->targetY = targetY;
    iris->centerX = targetX;
    iris->centerY = targetY;
    iris->radius = (float)SCREEN_WIDTH + 80.0f;  // Start fully open
}

bool effects_iris_update(IrisState* iris, float deltaTime, float newTargetX, float newTargetY) {
    if (!iris->active) return false;

    // Update target position (follow player)
    iris->targetX = newTargetX;
    iris->targetY = newTargetY;

    // Smoothly move center toward target
    float dx = iris->targetX - iris->centerX;
    float dy = iris->targetY - iris->centerY;
    iris->centerX += dx * 0.15f;
    iris->centerY += dy * 0.15f;

    // Handle pause at small radius
    if (iris->paused) {
        iris->pauseTimer -= deltaTime;
        if (iris->pauseTimer <= 0.0f) {
            iris->paused = false;
        }
        return false;  // Still paused
    }

    // Shrink iris
    iris->radius -= IRIS_CLOSE_SPEED;

    // Check for pause point
    if (iris->radius <= IRIS_PAUSE_RADIUS && iris->radius > 0.0f && iris->pauseTimer <= 0.0f) {
        iris->paused = true;
        iris->pauseTimer = IRIS_PAUSE_DURATION;
        iris->radius = IRIS_PAUSE_RADIUS;
    }

    // Check if fully closed
    if (iris->radius <= 0.0f && !iris->paused) {
        iris->radius = 0.0f;
        return true;  // Fully closed
    }

    return false;
}

bool effects_iris_open(IrisState* iris, float deltaTime) {
    if (!iris->active) return true;

    // Expand iris
    iris->radius += IRIS_OPEN_SPEED;

    // Check if fully open
    float maxRadius = (float)SCREEN_WIDTH + 80.0f;
    if (iris->radius >= maxRadius) {
        iris->radius = maxRadius;
        iris->active = false;
        return true;  // Fully open
    }

    return false;
}

void effects_iris_draw(IrisState* iris) {
    if (!iris->active) return;

    // IRIS EFFECT DISABLED - rdpq_triangle causes RDP hardware crashes on real N64
    // Using simple alpha fade instead for stability
    float r = iris->radius;

    // Validate radius
    if (isnan(r) || isinf(r) || r < 0.0f) {
        r = 0.0f;  // Treat as fully closed
    }

    // Iris max radius is typically SCREEN_WIDTH + some margin
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

bool effects_iris_closed(IrisState* iris) {
    return iris->active && iris->radius <= 0.0f && !iris->paused;
}

bool effects_iris_active(IrisState* iris) {
    return iris->active;
}
