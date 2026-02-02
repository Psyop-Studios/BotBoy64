// ============================================================
// COUNTDOWN MODULE IMPLEMENTATION
// 3-2-1-GO! countdown system for game start
// ============================================================

#include "countdown.h"
#include "constants.h"
#include <math.h>

// Set FPU to flush denormals to zero (prevents denormal exceptions on float-to-int)
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
// EASING FUNCTIONS
// ============================================================

// Elastic bounce for extra cartoon feel
static float countdown_ease_out_elastic(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    float p = 0.3f;
    return powf(2.0f, -10.0f * t) * sinf((t - p / 4.0f) * (2.0f * 3.14159f) / p) + 1.0f;
}

// ============================================================
// INITIALIZATION
// ============================================================

void countdown_init(Countdown* cd) {
    cd->state = COUNTDOWN_INACTIVE;
    cd->timer = 0.0f;
    cd->scale = 1.0f;
    cd->alpha = 1.0f;
    cd->pending = false;
    for (int i = 0; i < 4; i++) {
        cd->sprites[i] = NULL;
    }
    cd->screenCenterX = SCREEN_CENTER_X;
    cd->screenCenterY = SCREEN_CENTER_Y;
}

void countdown_reset(Countdown* cd) {
    cd->state = COUNTDOWN_INACTIVE;
    cd->timer = 0.0f;
    cd->scale = 1.0f;
    cd->alpha = 1.0f;
    cd->pending = false;
    // Don't free sprites here - let countdown_deinit handle that
}

void countdown_deinit(Countdown* cd) {
    for (int i = 0; i < 4; i++) {
        if (cd->sprites[i]) {
            sprite_free(cd->sprites[i]);
            cd->sprites[i] = NULL;
        }
    }
    cd->state = COUNTDOWN_INACTIVE;
}

// ============================================================
// STATE CONTROL
// ============================================================

void countdown_start(Countdown* cd) {
    cd->state = COUNTDOWN_3;
    cd->timer = 0.0f;
    cd->scale = 0.0f;
    cd->alpha = 1.0f;
    cd->pending = false;
}

void countdown_queue(Countdown* cd) {
    cd->pending = true;
    cd->state = COUNTDOWN_INACTIVE;
}

bool countdown_is_active(Countdown* cd) {
    return cd->pending || (cd->state != COUNTDOWN_INACTIVE && cd->state != COUNTDOWN_DONE);
}

// ============================================================
// UPDATE
// ============================================================

void countdown_update(Countdown* cd, float deltaTime, bool paused, bool respawning, bool irisOn) {
    // Don't update if paused
    if (paused) {
        return;
    }

    // If pending, wait for iris/respawn to finish before starting
    if (cd->pending) {
        if (!respawning && !irisOn) {
            // Iris finished, start the countdown now
            cd->pending = false;
            countdown_start(cd);
        }
        return;
    }

    if (cd->state == COUNTDOWN_INACTIVE || cd->state == COUNTDOWN_DONE) {
        return;
    }

    cd->timer += deltaTime;

    // Calculate bounce scale based on time in state
    float stateTime = (cd->state == COUNTDOWN_GO) ? COUNTDOWN_GO_TIME : COUNTDOWN_TIME_PER_NUMBER;
    float progress = cd->timer / stateTime;

    if (progress < 0.4f) {
        // Pop in with elastic bounce (first 40% of state time)
        float popProgress = progress / 0.4f;
        cd->scale = countdown_ease_out_elastic(popProgress) * 1.5f;
        cd->alpha = 1.0f;
    } else if (progress < 0.7f) {
        // Hold at full size
        cd->scale = 1.5f;
        cd->alpha = 1.0f;
    } else {
        // Shrink and fade out (last 30%)
        float fadeProgress = (progress - 0.7f) / 0.3f;
        cd->scale = 1.5f * (1.0f - fadeProgress * 0.5f);
        cd->alpha = 1.0f - fadeProgress;
    }

    // Transition to next state
    if (cd->timer >= stateTime) {
        cd->timer = 0.0f;
        switch (cd->state) {
            case COUNTDOWN_3:
                cd->state = COUNTDOWN_2;
                break;
            case COUNTDOWN_2:
                cd->state = COUNTDOWN_1;
                break;
            case COUNTDOWN_1:
                cd->state = COUNTDOWN_GO;
                break;
            case COUNTDOWN_GO:
                cd->state = COUNTDOWN_DONE;
                // Free sprites when done to save memory
                for (int i = 0; i < 4; i++) {
                    if (cd->sprites[i]) {
                        sprite_free(cd->sprites[i]);
                        cd->sprites[i] = NULL;
                    }
                }
                break;
            default:
                break;
        }
    }
}

// ============================================================
// DRAWING
// ============================================================

void countdown_draw(Countdown* cd, int centerX, int centerY) {
    if (cd->state == COUNTDOWN_INACTIVE || cd->state == COUNTDOWN_DONE) {
        return;
    }

    // Prevent denormal exceptions on scale/position conversions
    fpu_flush_denormals();

    // Lazy load countdown sprites on first use
    if (cd->sprites[0] == NULL) {
        // CRITICAL: Wait for RSP queue to flush before loading from ROM
        rspq_wait();
        cd->sprites[0] = sprite_load("rom:/Three.sprite");  // 3
        cd->sprites[1] = sprite_load("rom:/Two.sprite");    // 2
        cd->sprites[2] = sprite_load("rom:/One.sprite");    // 1
        cd->sprites[3] = sprite_load("rom:/Go.sprite");     // GO
    }

    // Get the sprite for current state
    sprite_t* sprite = NULL;
    float baseScale = 4.0f;  // Base scale for 16x16 sprites
    int spriteWidth = 16;
    int spriteHeight = 16;

    switch (cd->state) {
        case COUNTDOWN_3:
            sprite = cd->sprites[0];
            break;
        case COUNTDOWN_2:
            sprite = cd->sprites[1];
            break;
        case COUNTDOWN_1:
            sprite = cd->sprites[2];
            break;
        case COUNTDOWN_GO:
            sprite = cd->sprites[3];
            spriteWidth = 32;  // Go is 32x16
            baseScale = 2.0f;  // Scale Go at 2x so it appears same height as others
            break;
        default:
            return;
    }

    if (!sprite) return;

    // Use provided center or defaults
    int cx = (centerX > 0) ? centerX : SCREEN_CENTER_X;
    int cy = (centerY > 0) ? centerY : SCREEN_CENTER_Y;

    // Add slight wobble for extra cartoon feel
    float wobble = sinf(cd->timer * 20.0f) * 0.03f * cd->scale;
    float displayScale = (cd->scale + wobble) * baseScale;

    // Calculate sprite position (centered)
    float scaledWidth = spriteWidth * displayScale;
    float scaledHeight = spriteHeight * displayScale;
    int spriteX = cx - (int)(scaledWidth / 2.0f);
    int spriteY = cy - (int)(scaledHeight / 2.0f);

    // Sync TMEM before loading new texture (prevents SYNC_LOAD warnings)
    rdpq_sync_tile();
    rdpq_sync_pipe();  // Sync before 2D mode switch
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);

    rdpq_sprite_blit(sprite, spriteX, spriteY, &(rdpq_blitparms_t){
        .scale_x = displayScale,
        .scale_y = displayScale
    });
}
