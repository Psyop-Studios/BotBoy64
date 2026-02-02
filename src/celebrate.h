// ============================================================
// CELEBRATION MODULE
// Fireworks + Level Complete UI for level completion
// Shared between game.c and multiplayer.c
// ============================================================

#ifndef CELEBRATE_H
#define CELEBRATE_H

#include <libdragon.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================
// CONSTANTS
// ============================================================

#define CELEBRATE_MAX_FIREWORKS 6
#define CELEBRATE_MAX_SPARKS 48
#define CELEBRATE_FIREWORK_INTERVAL 0.35f
#define CELEBRATE_FIREWORK_DURATION 2.5f
#define CELEBRATE_NUM_COLORS 7

// ============================================================
// TYPES
// ============================================================

typedef enum {
    CELEBRATE_PHASE_INACTIVE,
    CELEBRATE_PHASE_FIREWORKS,     // Fireworks playing, gameplay paused
    CELEBRATE_PHASE_UI_SHOWING,    // UI overlay visible, waiting for A press
    CELEBRATE_PHASE_DONE           // A was pressed, ready for iris transition
} CelebratePhase;

typedef struct {
    float x, y, z;           // World position
    float velY;              // Upward velocity
    bool active;
    bool exploded;
    uint8_t colorIdx;        // Color index (0-6)
} CelebrateFirework;

typedef struct {
    float x, y, z;
    float velX, velY, velZ;
    float life;
    float maxLife;
    uint8_t colorIdx;
    bool active;
} CelebrateSpark;

// Main celebration state struct
typedef struct {
    CelebratePhase phase;
    float timer;
    float fireworkSpawnTimer;
    float worldX, worldY, worldZ;  // World position where celebration happens
    int blinkTimer;
    uint32_t rng;

    CelebrateFirework fireworks[CELEBRATE_MAX_FIREWORKS];
    CelebrateSpark sparks[CELEBRATE_MAX_SPARKS];

    // Stats for UI display
    int boltsCollected;
    int totalBolts;
    int deaths;
    float completionTime;
    char rank;

    // Rank sprite (lazy-loaded)
    sprite_t* rankSprite;
    char rankSpriteChar;  // Which rank the sprite is for

    // Target transition info
    int targetLevel;
    int targetSpawn;

    // Sound state
    bool soundsLoaded;
    int setoffChannel;  // Track which channel setoff is playing on (-1 if none)
} CelebrateState;

// ============================================================
// FUNCTIONS
// ============================================================

// Initialize celebration state
void celebrate_init(CelebrateState* cs);

// Start the celebration at given world position
// bolts/totalBolts/deaths/time are for UI display, targetLevel/targetSpawn for transition
void celebrate_start(CelebrateState* cs, float worldX, float worldY, float worldZ,
                     int boltsCollected, int totalBolts, int deaths, float completionTime,
                     int targetLevel, int targetSpawn);

// Calculate rank based on performance
char celebrate_calculate_rank(int deaths, int boltsCollected, int totalBolts, float time);

// Update celebration state
// Returns true when player pressed A during UI phase (ready for transition)
bool celebrate_update(CelebrateState* cs, float deltaTime, bool aPressed);

// Draw celebration effects (fireworks + sparks + UI)
// screenCenterX/Y for splitscreen support (pass 160,120 for fullscreen)
void celebrate_draw(CelebrateState* cs, int screenCenterX, int screenCenterY);

// Check if celebration is active
bool celebrate_is_active(CelebrateState* cs);

// Check if celebration is done (player pressed A, ready for transition)
bool celebrate_is_done(CelebrateState* cs);

// Get target level for transition
int celebrate_get_target_level(CelebrateState* cs);

// Get target spawn for transition
int celebrate_get_target_spawn(CelebrateState* cs);

// Reset celebration state
void celebrate_reset(CelebrateState* cs);

// Get celebration colors (for external use)
color_t celebrate_get_color(int idx);

// Load/unload celebration sounds (call once at scene init/deinit)
void celebrate_load_sounds(void);
void celebrate_unload_sounds(void);

#endif // CELEBRATE_H
