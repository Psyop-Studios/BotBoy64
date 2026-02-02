#include <libdragon.h>
#include <math.h>
#include <stdlib.h>
#include "level_complete.h"
#include "game.h"
#include "level_select.h"
#include "../scene.h"
#include "../save.h"
#include "../levels.h"
#include "../constants.h"

// Note: fpu_flush_denormals() is now defined in mapData.h (included via levels.h)

// Menu options
typedef enum {
    COMPLETE_CONTINUE,
    COMPLETE_RESTART,
    COMPLETE_QUIT,
    COMPLETE_OPTION_COUNT
} CompleteOption;

// Firework particle system
#define MAX_FIREWORKS 8
#define MAX_SPARKS 64
#define FIREWORK_SPAWN_INTERVAL 0.4f

typedef struct {
    float x, y;           // Position (screen coords)
    float velY;           // Upward velocity
    bool active;
    bool exploded;
    color_t color;
} Firework;

typedef struct {
    float x, y;
    float velX, velY;
    float life;
    float maxLife;
    color_t color;
    bool active;
} Spark;

// Firework colors (bright, celebratory)
static const color_t FIREWORK_COLORS[] = {
    {0xFF, 0x44, 0x44, 0xFF},  // Red
    {0x44, 0xFF, 0x44, 0xFF},  // Green
    {0x44, 0x88, 0xFF, 0xFF},  // Blue
    {0xFF, 0xFF, 0x44, 0xFF},  // Yellow
    {0xFF, 0x44, 0xFF, 0xFF},  // Magenta
    {0x44, 0xFF, 0xFF, 0xFF},  // Cyan
    {0xFF, 0x88, 0x44, 0xFF},  // Orange
};
#define NUM_FIREWORK_COLORS 7

static Firework fireworks[MAX_FIREWORKS];
static Spark sparks[MAX_SPARKS];
static float fireworkSpawnTimer = 0.0f;
static uint32_t fireworkRng = 12345;

// Simple RNG for fireworks
static uint32_t firework_rand(void) {
    fireworkRng = fireworkRng * 1103515245 + 12345;
    return (fireworkRng >> 16) & 0x7FFF;
}

static float firework_randf(void) {
    return (float)firework_rand() / 32767.0f;
}

static LevelCompleteData completionData = {0};
static int selectedOption = 0;
static float cursorBob = 0.0f;
static float animTimer = 0.0f;

// Rank calculation: S/A/B/C/D based on performance
// S = Perfect (no deaths, all bolts, under time limit)
// A = Great (no deaths OR all bolts and fast)
// B = Good (few deaths, decent completion)
// C = Okay (completed with some struggle)
// D = Completed (just finished)
static char calculate_rank(void) {
    int deaths = completionData.deathCount;
    float time = completionData.levelTime;
    bool allBolts = (completionData.boltsCollected == completionData.totalBoltsInLevel);
    float boltRatio = completionData.totalBoltsInLevel > 0 ?
        (float)completionData.boltsCollected / (float)completionData.totalBoltsInLevel : 1.0f;

    // S Rank: No deaths, all bolts, under 5 minutes (lenient for longer levels)
    if (deaths == 0 && allBolts && time < 300.0f) {
        return 'S';
    }

    // A Rank: No deaths with most bolts OR all bolts with few deaths and good time
    if ((deaths == 0 && boltRatio >= 0.75f) ||
        (allBolts && deaths <= 1 && time < 420.0f)) {
        return 'A';
    }

    // B Rank: Low deaths with decent bolt collection
    if ((deaths <= 2 && boltRatio >= 0.5f) ||
        (deaths == 0)) {
        return 'B';
    }

    // C Rank: Moderate performance
    if (deaths <= 5 || boltRatio >= 0.25f) {
        return 'C';
    }

    // D Rank: Just completed
    return 'D';
}

void level_complete_set_data(int levelId, int boltsCollected, int totalBolts, int deaths, float time) {
    completionData.levelId = levelId;
    completionData.boltsCollected = boltsCollected;
    completionData.totalBoltsInLevel = totalBolts;
    completionData.deathCount = deaths;
    completionData.levelTime = time;
}

// Spawn a new firework rocket
static void spawn_firework(void) {
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (!fireworks[i].active) {
            fireworks[i].active = true;
            fireworks[i].exploded = false;
            fireworks[i].x = 40.0f + firework_randf() * 240.0f;  // Random X across screen
            fireworks[i].y = 240.0f;  // Start at bottom
            fireworks[i].velY = -180.0f - firework_randf() * 80.0f;  // Upward velocity
            fireworks[i].color = FIREWORK_COLORS[firework_rand() % NUM_FIREWORK_COLORS];
            break;
        }
    }
}

// Explode a firework into sparks
static void explode_firework(Firework* fw) {
    fw->exploded = true;
    fw->active = false;

    // Spawn sparks in a circle
    int sparksToSpawn = 12 + (firework_rand() % 8);  // 12-20 sparks
    for (int i = 0; i < sparksToSpawn; i++) {
        for (int j = 0; j < MAX_SPARKS; j++) {
            if (!sparks[j].active) {
                float angle = (float)i / (float)sparksToSpawn * 6.283f + firework_randf() * 0.3f;
                float speed = 60.0f + firework_randf() * 60.0f;
                sparks[j].active = true;
                sparks[j].x = fw->x;
                sparks[j].y = fw->y;
                sparks[j].velX = cosf(angle) * speed;
                sparks[j].velY = sinf(angle) * speed;
                sparks[j].maxLife = 0.6f + firework_randf() * 0.4f;
                sparks[j].life = sparks[j].maxLife;
                sparks[j].color = fw->color;
                break;
            }
        }
    }
}

void init_level_complete_scene(void) {
    selectedOption = 0;
    cursorBob = 0.0f;
    animTimer = 0.0f;
    fireworkSpawnTimer = 0.0f;
    fireworkRng = 12345 + (uint32_t)(completionData.levelTime * 1000);  // Seed based on level time

    // Clear all fireworks and sparks
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        fireworks[i].active = false;
        fireworks[i].exploded = false;
    }
    for (int i = 0; i < MAX_SPARKS; i++) {
        sparks[i].active = false;
    }

    // Mark level as completed in save system
    save_complete_level(completionData.levelId);

    // Calculate and save the rank
    char rank = calculate_rank();
    save_update_best_rank(completionData.levelId, rank);

    save_force_save();  // Level complete is a significant event - save immediately
}

void deinit_level_complete_scene(void) {
    // Cleanup
}

void update_level_complete_scene(void) {
    // Update audio mixer first (prevents crackling)
    if (audio_can_write()) {
        short *buf = audio_write_begin();
        rspq_wait();  // Flush RSPQ to exit highpri mode before mixer_poll (needed when rdpq_debug is active)
        save_apply_volume_settings_safe();  // Apply pending volume changes in safe window
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }

    joypad_poll();
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

    float dt = 0.033f;  // ~30fps
    animTimer += dt;
    // Wrap to prevent FPU overflow after long runtime
    if (animTimer > 1000.0f) {
        animTimer -= 1000.0f;
    }

    // Update firework spawning
    fireworkSpawnTimer += dt;
    if (fireworkSpawnTimer >= FIREWORK_SPAWN_INTERVAL) {
        fireworkSpawnTimer = 0.0f;
        spawn_firework();
    }

    // Update firework rockets
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (fireworks[i].active && !fireworks[i].exploded) {
            fireworks[i].y += fireworks[i].velY * dt;
            fireworks[i].velY += 80.0f * dt;  // Gravity slowing it down

            // Explode when velocity reverses or reaches peak
            if (fireworks[i].velY >= -20.0f || fireworks[i].y < 60.0f) {
                explode_firework(&fireworks[i]);
            }
        }
    }

    // Update sparks
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].active) {
            sparks[i].x += sparks[i].velX * dt;
            sparks[i].y += sparks[i].velY * dt;
            sparks[i].velY += 120.0f * dt;  // Gravity
            sparks[i].life -= dt;

            if (sparks[i].life <= 0.0f || sparks[i].y > 250.0f) {
                sparks[i].active = false;
            }
        }
    }

    // Navigate menu with D-pad or stick
    static bool stickHeldUp = false;
    static bool stickHeldDown = false;
    bool stickUp = inputs.stick_y > 50;
    bool stickDown = inputs.stick_y < -50;

    if (pressed.d_up || (stickUp && !stickHeldUp)) {
        selectedOption--;
        if (selectedOption < 0) selectedOption = COMPLETE_OPTION_COUNT - 1;
    }
    stickHeldUp = stickUp;

    if (pressed.d_down || (stickDown && !stickHeldDown)) {
        selectedOption++;
        if (selectedOption >= COMPLETE_OPTION_COUNT) selectedOption = 0;
    }
    stickHeldDown = stickDown;

    // Confirm selection with A or Start
    if (pressed.a || pressed.start) {
        switch (selectedOption) {
            case COMPLETE_CONTINUE:
                // Go to next level or level select if last level
                if (completionData.levelId + 1 < get_real_level_count()) {
                    // Set next level and go to game scene
                    selectedLevelID = completionData.levelId + 1;
                    change_scene(GAME);
                } else {
                    // Last level - go to level select
                    change_scene(LEVEL_SELECT);
                }
                break;
            case COMPLETE_RESTART:
                // Restart same level
                selectedLevelID = completionData.levelId;
                change_scene(GAME);
                break;
            case COMPLETE_QUIT:
                change_scene(LEVEL_SELECT);
                break;
        }
    }

    // Cursor animation
    cursorBob += 0.15f;
}

void draw_level_complete_scene(void) {
    surface_t *disp = display_get();
    rdpq_attach(disp, NULL);

    // Dark background with slight blue tint (victory feel)
    rdpq_clear(RGBA32(0x10, 0x18, 0x28, 0xFF));

    // Draw fireworks (behind text)
    rdpq_set_mode_standard();

    // Prevent denormal exceptions on firework/spark position conversions
    fpu_flush_denormals();

    // Draw rising firework rockets (small bright pixels)
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (fireworks[i].active && !fireworks[i].exploded) {
            int x = (int)fireworks[i].x;
            int y = (int)fireworks[i].y;
            if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
                // Draw rocket as a small vertical line (trail effect)
                rdpq_set_prim_color(fireworks[i].color);
                rdpq_fill_rectangle(x - 1, y, x + 1, y + 4);
            }
        }
    }

    // Draw explosion sparks
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].active) {
            int x = (int)sparks[i].x;
            int y = (int)sparks[i].y;
            if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
                // Fade alpha based on remaining life
                float lifeRatio = sparks[i].life / sparks[i].maxLife;
                uint8_t alpha = (uint8_t)(lifeRatio * 255.0f);
                color_t fadeColor = sparks[i].color;
                fadeColor.a = alpha;
                rdpq_set_prim_color(fadeColor);
                rdpq_fill_rectangle(x - 1, y - 1, x + 1, y + 1);
            }
        }
    }

    // Title with animation
    float titleBounce = sinf(animTimer * 3.0f) * 3.0f;
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, 30 + (int)titleBounce, "LEVEL COMPLETE!");

    // Level name
    const char* levelName = get_level_name(completionData.levelId);
    if (levelName) {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 110, 50, "%s", levelName);
    }

    // Stats box
    int statsY = 75;
    int statsX = 70;

    // Bolts collected
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, statsX, statsY,
        "Bolts: %d / %d", completionData.boltsCollected, completionData.totalBoltsInLevel);

    // Time taken
    int mins = (int)(completionData.levelTime / 60.0f);
    int secs = (int)completionData.levelTime % 60;
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, statsX, statsY + 18,
        "Time:  %d:%02d", mins, secs);

    // Deaths
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, statsX, statsY + 36,
        "Deaths: %d", completionData.deathCount);

    // Perfect run bonus message
    if (completionData.deathCount == 0 &&
        completionData.boltsCollected == completionData.totalBoltsInLevel) {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 85, statsY + 58, "PERFECT RUN!");
    } else if (completionData.deathCount == 0) {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 90, statsY + 58, "Deathless!");
    } else if (completionData.boltsCollected == completionData.totalBoltsInLevel) {
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 80, statsY + 58, "All Bolts Found!");
    }

    // Draw rank display (right side with decorative box)
    {
        char rank = calculate_rank();

        // Rank box position and size - moved left to be more visible
        int rankX = 230;
        int rankY = 70;
        int boxSize = 50;

        // Choose color based on rank
        color_t rankColor;
        switch (rank) {
            case 'S': rankColor = RGBA32(0xFF, 0xD7, 0x00, 0xFF); break;  // Gold
            case 'A': rankColor = RGBA32(0x44, 0xFF, 0x44, 0xFF); break;  // Green
            case 'B': rankColor = RGBA32(0x44, 0x88, 0xFF, 0xFF); break;  // Blue
            case 'C': rankColor = RGBA32(0xFF, 0x88, 0x44, 0xFF); break;  // Orange
            default:  rankColor = RGBA32(0x88, 0x88, 0x88, 0xFF); break;  // Gray
        }

        // Set fill mode for rectangles
        rdpq_sync_pipe();  // Sync before switching to fill mode
        rdpq_set_mode_fill(RGBA32(0x80, 0x80, 0x80, 0xFF));
        rdpq_fill_rectangle(rankX - 4, rankY - 4, rankX + boxSize + 4, rankY + boxSize + 4);

        // Inner border
        rdpq_set_mode_fill(rankColor);
        rdpq_fill_rectangle(rankX - 2, rankY - 2, rankX + boxSize + 2, rankY + boxSize + 2);

        // Background
        rdpq_set_mode_fill(RGBA32(0x20, 0x20, 0x30, 0xFF));
        rdpq_fill_rectangle(rankX, rankY, rankX + boxSize, rankY + boxSize);

        // Reset to standard mode for text
        rdpq_set_mode_standard();

        // Draw rank letter (large, centered)
        char rankStr[2] = {rank, '\0'};

        // Center the letter in the box
        int textX = rankX + 17;
        int textY = rankY + 18;

        // Draw shadow first (offset down-right, darker)
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX + 2, textY + 2, "%s", rankStr);

        // Draw multiple copies to make it appear larger and bolder
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX, textY, "%s", rankStr);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX + 1, textY, "%s", rankStr);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX, textY + 1, "%s", rankStr);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX + 1, textY + 1, "%s", rankStr);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX - 1, textY, "%s", rankStr);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX, textY - 1, "%s", rankStr);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX + 2, textY, "%s", rankStr);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX, textY + 2, "%s", rankStr);

        // Label below the box
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, rankX + 10, rankY + boxSize + 10, "RANK");

        // Show best rank if different
        uint8_t bestRankVal = save_get_best_rank(completionData.levelId);
        char bestRank = save_rank_to_char(bestRankVal);
        if (bestRankVal > 0 && bestRank != rank) {
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, rankX + 2, rankY + boxSize + 22, "Best: %c", bestRank);
        }
    }

    // Menu options
    const char* options[] = {
        "Continue",
        "Restart Level",
        "Level Select"
    };

    int menuY = 160;
    int spacing = 22;

    for (int i = 0; i < COMPLETE_OPTION_COUNT; i++) {
        int y = menuY + i * spacing;

        if (i == selectedOption) {
            // Selected option - draw cursor
            int bobOffset = (int)(sinf(cursorBob) * 2.0f);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 85 + bobOffset, y, ">");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, y, "%s", options[i]);
        } else {
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, y, "%s", options[i]);
        }
    }

    // Controls hint
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 85, 228, "A/Start: Select");

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}
