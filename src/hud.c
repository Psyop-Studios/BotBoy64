// ============================================================
// HUD MODULE
// Sliding health bar, bolt counter, player info
// ============================================================

#include "hud.h"
#include "constants.h"
#include <stdio.h>

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
// HEALTH HUD
// ============================================================

void hud_health_init(HealthHUD* hud, int maxHealth) {
    for (int i = 0; i < HEALTH_HUD_SPRITE_COUNT; i++) {
        hud->sprites[i] = NULL;
    }
    hud->loaded = false;
    hud->y = HEALTH_HUD_HIDE_Y;
    hud->targetY = HEALTH_HUD_HIDE_Y;
    hud->visible = false;
    hud->hideTimer = 0.0f;
    hud->flashTimer = 0.0f;
    hud->maxHealth = maxHealth;
}

void hud_health_deinit(HealthHUD* hud) {
    for (int i = 0; i < HEALTH_HUD_SPRITE_COUNT; i++) {
        if (hud->sprites[i]) {
            sprite_free(hud->sprites[i]);
            hud->sprites[i] = NULL;
        }
    }
    hud->loaded = false;
}

void hud_health_update(HealthHUD* hud, float deltaTime) {
    // Animate Y position
    float dy = hud->targetY - hud->y;
    hud->y += dy * HEALTH_HUD_SPEED * deltaTime;

    // Decay flash timer
    if (hud->flashTimer > 0.0f) {
        hud->flashTimer -= deltaTime;
        if (hud->flashTimer < 0.0f) hud->flashTimer = 0.0f;
    }

    // Auto-hide timer
    if (hud->hideTimer > 0.0f) {
        hud->hideTimer -= deltaTime;
        if (hud->hideTimer <= 0.0f) {
            hud_health_hide(hud);
        }
    }
}

void hud_health_show(HealthHUD* hud, bool withFlash) {
    // Lazy load sprites on first use
    if (!hud->loaded) {
        debugf("Lazy loading health sprites\n");
        // CRITICAL: Wait for RSP queue to flush before loading from ROM
        rspq_wait();
        hud->sprites[0] = sprite_load("rom:/health1.sprite");
        hud->sprites[1] = sprite_load("rom:/health2.sprite");
        hud->sprites[2] = sprite_load("rom:/health3.sprite");
        hud->sprites[3] = sprite_load("rom:/health4.sprite");
        hud->loaded = true;
    }

    hud->visible = true;
    hud->targetY = HEALTH_HUD_SHOW_Y;
    if (withFlash) {
        hud->flashTimer = HEALTH_FLASH_DURATION;
        hud->hideTimer = HEALTH_DISPLAY_DURATION;
    }
}

void hud_health_hide(HealthHUD* hud) {
    hud->visible = false;
    hud->targetY = HEALTH_HUD_HIDE_Y;
    hud->hideTimer = 0.0f;
}

void hud_health_draw(HealthHUD* hud, int currentHealth, float offsetX, float offsetY) {
    hud_health_draw_scaled(hud, currentHealth, offsetX, offsetY, 2.0f);
}

void hud_health_draw_scaled(HealthHUD* hud, int currentHealth, float offsetX, float offsetY, float scale) {
    // Skip if hidden
    if (hud->y <= HEALTH_HUD_HIDE_Y + 1.0f) return;

    // Prevent denormal exceptions on float-to-int position conversions
    fpu_flush_denormals();

    // Determine sprite index: 0 = full health, 3 = dead
    int healthIdx = hud->maxHealth - currentHealth;
    if (healthIdx < 0) healthIdx = 0;
    if (healthIdx > 3) healthIdx = 3;

    sprite_t* sprite = hud->sprites[healthIdx];
    if (!sprite) return;

    // Center the sprite based on scale (base sprite is 32x32)
    int spriteWidth = (int)(32 * scale);
    int spriteX = (int)(SCREEN_CENTER_X - spriteWidth / 2 + offsetX);
    int spriteY = (int)(hud->y + offsetY);

    rdpq_sync_pipe();  // Sync before 2D mode switch
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);

    // Flash effect: alternate visibility during flash timer
    bool showSprite = true;
    if (hud->flashTimer > 0.0f) {
        int flashPhase = (int)(hud->flashTimer * 10.0f) % 2;
        showSprite = (flashPhase == 0);
    }

    if (showSprite) {
        rdpq_sprite_blit(sprite, spriteX, spriteY, &(rdpq_blitparms_t){
            .scale_x = scale,
            .scale_y = scale
        });
    }
}

// ============================================================
// SCREW/BOLT HUD
// ============================================================

// Screw HUD positions - computed dynamically based on screen width
// Show position: screen_width - right_margin - sprite_width (48 pixels at 1.5x scale = 32*1.5)
// Hide position: far off-screen right
static inline float screw_hud_show_x(void) {
    return (float)SCREEN_WIDTH - SCREW_HUD_RIGHT_MARGIN - 48.0f;
}
static inline float screw_hud_hide_x(void) {
    return (float)SCREEN_WIDTH + SCREW_HUD_HIDE_OFFSET;
}

void hud_screw_init(ScrewHUD* hud) {
    for (int i = 0; i < SCREW_HUD_FRAME_COUNT; i++) {
        hud->sprites[i] = NULL;
    }
    hud->loaded = false;
    hud->x = screw_hud_hide_x();
    hud->targetX = screw_hud_hide_x();
    hud->animFrame = 0;
    hud->animTimer = 0.0f;
    hud->visible = false;
    hud->hideTimer = 0.0f;
}

void hud_screw_deinit(ScrewHUD* hud) {
    for (int i = 0; i < SCREW_HUD_FRAME_COUNT; i++) {
        if (hud->sprites[i]) {
            sprite_free(hud->sprites[i]);
            hud->sprites[i] = NULL;
        }
    }
    hud->loaded = false;
}

void hud_screw_update(ScrewHUD* hud, float deltaTime) {
    // Animate X position (slide in/out)
    float dx = hud->targetX - hud->x;
    hud->x += dx * SCREW_HUD_SPEED * deltaTime;

    // Animate spinning
    if (hud->visible) {
        hud->animTimer += deltaTime;
        if (hud->animTimer >= 1.0f / SCREW_ANIM_FPS) {
            hud->animTimer = 0.0f;
            hud->animFrame = (hud->animFrame + 1) % SCREW_HUD_FRAME_COUNT;
        }
    }

    // Auto-hide timer
    if (hud->hideTimer > 0.0f) {
        hud->hideTimer -= deltaTime;
        if (hud->hideTimer <= 0.0f) {
            hud_screw_hide(hud);
        }
    }
}

void hud_screw_show(ScrewHUD* hud, bool autoHide) {
    // Lazy load sprites on first use (6 frames: 1,3,5,7,9,11)
    if (!hud->loaded) {
        debugf("Lazy loading screw sprites\n");
        // CRITICAL: Wait for RSP queue to flush before loading from ROM
        // ROM DMA conflicts with RDP DMA can cause RSP queue timeout
        rspq_wait();
        const int frameNumbers[SCREW_HUD_FRAME_COUNT] = {1, 3, 5, 7, 9, 11};
        for (int i = 0; i < SCREW_HUD_FRAME_COUNT; i++) {
            char path[32];
            sprintf(path, "rom:/ScrewUI%d.sprite", frameNumbers[i]);
            hud->sprites[i] = sprite_load(path);
        }
        hud->loaded = true;
    }

    hud->visible = true;
    hud->targetX = screw_hud_show_x();
    if (autoHide) {
        hud->hideTimer = SCREW_DISPLAY_DURATION;
    }
}

void hud_screw_hide(ScrewHUD* hud) {
    hud->visible = false;
    hud->targetX = screw_hud_hide_x();
    hud->hideTimer = 0.0f;
}

void hud_screw_draw(ScrewHUD* hud, int collected, int total, float offsetX, float offsetY) {
    // Skip if hidden (off-screen right)
    if (hud->x >= screw_hud_hide_x() - 1.0f) return;

    // Prevent denormal exceptions on float-to-int position conversions
    fpu_flush_denormals();

    sprite_t* sprite = hud->sprites[hud->animFrame];
    if (!sprite) return;

    int spriteX = (int)(hud->x + offsetX);
    int spriteY = (int)(8 + offsetY);

    rdpq_sync_pipe();  // Sync before 2D mode switch
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);

    rdpq_sprite_blit(sprite, spriteX, spriteY, &(rdpq_blitparms_t){
        .scale_x = 1.5f,
        .scale_y = 1.5f
    });

    // Draw bolt count next to the screw sprite
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO,
                     spriteX + 48, spriteY + 28, "%d/%d", collected, total);
}

// ============================================================
// PART DISPLAY
// ============================================================

void hud_draw_part_name(int part, float x, float y) {
    const char* partNames[] = {"Head", "Torso", "Arms", "Legs"};
    if (part >= 0 && part < 4) {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, (int)x, (int)y, "%s", partNames[part]);
    }
}

// ============================================================
// DEBUG HUD
// ============================================================

void hud_draw_debug(int fps, int memUsed, int memFree) {
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 4, 12, "FPS: %d", fps);
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 4, 22, "Mem: %dK / %dK",
                     memUsed / 1024, (memUsed + memFree) / 1024);
}
