#ifndef HUD_H
#define HUD_H

#include <libdragon.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================
// HUD MODULE
// Sliding health bar, bolt counter, player info
// ============================================================

// ============================================================
// HEALTH HUD
// ============================================================

#define HEALTH_HUD_SPRITE_COUNT 4
#define HEALTH_HUD_SHOW_Y 8.0f
#define HEALTH_HUD_HIDE_Y -70.0f
#define HEALTH_HUD_SPEED 8.0f
#define HEALTH_FLASH_DURATION 0.8f
#define HEALTH_DISPLAY_DURATION 3.0f

typedef struct {
    sprite_t* sprites[HEALTH_HUD_SPRITE_COUNT];
    bool loaded;
    float y;
    float targetY;
    bool visible;
    float hideTimer;
    float flashTimer;
    int maxHealth;
} HealthHUD;

// Initialize health HUD
void hud_health_init(HealthHUD* hud, int maxHealth);

// Free health HUD resources
void hud_health_deinit(HealthHUD* hud);

// Update health HUD animation
void hud_health_update(HealthHUD* hud, float deltaTime);

// Show health HUD with optional flash effect
void hud_health_show(HealthHUD* hud, bool withFlash);

// Hide health HUD
void hud_health_hide(HealthHUD* hud);

// Draw health HUD at specified offset
void hud_health_draw(HealthHUD* hud, int currentHealth, float offsetX, float offsetY);

// Draw health HUD with custom scale (for splitscreen)
void hud_health_draw_scaled(HealthHUD* hud, int currentHealth, float offsetX, float offsetY, float scale);

// ============================================================
// BOLT/SCREW HUD
// ============================================================

#define SCREW_HUD_FRAME_COUNT 6
// Screw HUD X positions relative to screen width (computed at runtime in hud.c)
#define SCREW_HUD_RIGHT_MARGIN 26.0f   // Distance from right edge when shown
#define SCREW_HUD_HIDE_OFFSET 74.0f    // How far off-screen when hidden
#define SCREW_HUD_SPEED 8.0f
#define SCREW_ANIM_FPS 8.0f
#define SCREW_DISPLAY_DURATION 2.5f

typedef struct {
    sprite_t* sprites[SCREW_HUD_FRAME_COUNT];
    bool loaded;
    float x;
    float targetX;
    int animFrame;
    float animTimer;
    bool visible;
    float hideTimer;
} ScrewHUD;

// Initialize screw HUD
void hud_screw_init(ScrewHUD* hud);

// Free screw HUD resources
void hud_screw_deinit(ScrewHUD* hud);

// Update screw HUD animation
void hud_screw_update(ScrewHUD* hud, float deltaTime);

// Show screw HUD (called on bolt collection)
void hud_screw_show(ScrewHUD* hud, bool autoHide);

// Hide screw HUD
void hud_screw_hide(ScrewHUD* hud);

// Draw screw HUD at specified offset
void hud_screw_draw(ScrewHUD* hud, int collected, int total, float offsetX, float offsetY);

// ============================================================
// PART DISPLAY
// ============================================================

// Draw current body part name
void hud_draw_part_name(int part, float x, float y);

// ============================================================
// DEBUG / PERFORMANCE HUD
// ============================================================

// Draw debug HUD with frame time, memory, etc.
void hud_draw_debug(int fps, int memUsed, int memFree);

#endif // HUD_H
