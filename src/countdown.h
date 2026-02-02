// ============================================================
// COUNTDOWN MODULE
// 3-2-1-GO! countdown system for game start
// Shared between game.c and multiplayer.c
// ============================================================

#ifndef COUNTDOWN_H
#define COUNTDOWN_H

#include <libdragon.h>
#include <stdbool.h>

// ============================================================
// CONSTANTS
// ============================================================

#define COUNTDOWN_TIME_PER_NUMBER 0.8f  // Time each number stays on screen
#define COUNTDOWN_GO_TIME 0.6f          // Time "GO!" stays on screen

// ============================================================
// TYPES
// ============================================================

typedef enum {
    COUNTDOWN_INACTIVE,
    COUNTDOWN_3,
    COUNTDOWN_2,
    COUNTDOWN_1,
    COUNTDOWN_GO,
    COUNTDOWN_DONE
} CountdownState;

// ============================================================
// STATE STRUCT
// ============================================================

typedef struct {
    CountdownState state;
    float timer;          // Time in current state
    float scale;          // Current scale for bounce effect
    float alpha;          // Fade out alpha
    bool pending;         // Waiting for iris to finish before starting
    sprite_t* sprites[4]; // Three, Two, One, Go (lazy loaded)
    int screenCenterX;    // Screen center X (for splitscreen support)
    int screenCenterY;    // Screen center Y
} Countdown;

// ============================================================
// FUNCTIONS
// ============================================================

// Initialize countdown state
void countdown_init(Countdown* cd);

// Reset countdown (free sprites, reset state)
void countdown_reset(Countdown* cd);

// Start the countdown immediately
void countdown_start(Countdown* cd);

// Queue countdown to start after iris/respawn finishes
void countdown_queue(Countdown* cd);

// Check if countdown is blocking player movement
bool countdown_is_active(Countdown* cd);

// Update countdown state
// deltaTime: frame delta time
// paused: is game paused?
// respawning: is player respawning?
// irisOn: is iris transition active?
void countdown_update(Countdown* cd, float deltaTime, bool paused, bool respawning, bool irisOn);

// Draw countdown at screen position
// If centerX/centerY are 0, uses default 160x120 (full screen center)
void countdown_draw(Countdown* cd, int centerX, int centerY);

// Free countdown resources
void countdown_deinit(Countdown* cd);

#endif // COUNTDOWN_H
