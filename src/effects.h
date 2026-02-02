#ifndef EFFECTS_H
#define EFFECTS_H

#include <libdragon.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================
// SCREEN EFFECTS MODULE
// Screen shake, hit flash, fade, iris wipe
// ============================================================

// ============================================================
// SCREEN SHAKE
// ============================================================

// Initialize effects system
void effects_init(void);

// Update all effects (call every frame)
void effects_update(float deltaTime);

// Trigger screen shake (stacks - uses max intensity)
void effects_screen_shake(float intensity);

// Get current shake offset (apply to camera or viewport)
void effects_get_shake_offset(float* outX, float* outY);

// ============================================================
// HIT FLASH / BOLT FLASH
// ============================================================

// Trigger red hit flash (damage taken)
void effects_hit_flash(float duration);

// Trigger white bolt flash (collection)
void effects_bolt_flash(void);

// Get current flash color (returns RGBA32, alpha 0 if no flash)
color_t effects_get_flash_color(void);

// Check if any flash is active
bool effects_flash_active(void);

// ============================================================
// HITSTOP (FREEZE FRAME)
// ============================================================

// Trigger hitstop freeze frame
void effects_hitstop(float duration);

// Check if hitstop is active (game should pause gameplay)
bool effects_hitstop_active(void);

// ============================================================
// FADE OVERLAY
// ============================================================

// Set fade alpha (0.0 = transparent, 1.0 = fully black)
void effects_set_fade(float alpha);

// Get current fade alpha
float effects_get_fade(void);

// Draw fade overlay (call during 2D rendering)
void effects_draw_fade(void);

// ============================================================
// IRIS WIPE EFFECT
// ============================================================

// Iris state struct for per-player or global iris
typedef struct {
    float radius;       // Current iris radius (starts large)
    float centerX;      // Iris center X (screen coords)
    float centerY;      // Iris center Y (screen coords)
    float targetX;      // Target center (player screen pos)
    float targetY;
    bool active;        // Is iris effect running?
    float pauseTimer;   // Dramatic pause timer
    bool paused;        // In pause phase?
} IrisState;

// Constants
#define IRIS_PAUSE_RADIUS 25.0f
#define IRIS_PAUSE_DURATION 0.33f
#define IRIS_SEGMENTS 16
#define IRIS_CLOSE_SPEED 5.0f
#define IRIS_OPEN_SPEED 8.0f

// Initialize iris state
void effects_iris_init(IrisState* iris);

// Start iris close (death/transition)
void effects_iris_start(IrisState* iris, float targetX, float targetY);

// Update iris effect
// Returns true when fully closed
bool effects_iris_update(IrisState* iris, float deltaTime, float newTargetX, float newTargetY);

// Open iris back up (after respawn)
// Returns true when fully open
bool effects_iris_open(IrisState* iris, float deltaTime);

// Draw iris effect
void effects_iris_draw(IrisState* iris);

// Check if iris is fully closed
bool effects_iris_closed(IrisState* iris);

// Check if iris is active
bool effects_iris_active(IrisState* iris);

#endif // EFFECTS_H
