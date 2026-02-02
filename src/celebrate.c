// ============================================================
// CELEBRATION MODULE IMPLEMENTATION
// Fireworks + Level Complete UI for level completion
// ============================================================

#include "celebrate.h"
#include "constants.h"
#include "ui.h"
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
// SOUND EFFECTS
// ============================================================

static wav64_t sfxFireworkSetoff;
static wav64_t sfxFireworkExplosion[3];
static bool g_celebrateSoundsLoaded = false;

// Find a free audio channel (channels 2-7, avoiding 0=music, 1=UI)
static int get_free_sfx_channel(void) {
    for (int ch = 2; ch < 8; ch++) {
        if (!mixer_ch_playing(ch)) {
            return ch;
        }
    }
    return 2;  // Fallback to channel 2 if all busy
}

// ============================================================
// COLORS
// ============================================================

static const color_t CELEBRATE_COLORS[] = {
    {0xFF, 0x44, 0x44, 0xFF},  // Red
    {0x44, 0xFF, 0x44, 0xFF},  // Green
    {0x44, 0x88, 0xFF, 0xFF},  // Blue
    {0xFF, 0xFF, 0x44, 0xFF},  // Yellow
    {0xFF, 0x44, 0xFF, 0xFF},  // Magenta
    {0x44, 0xFF, 0xFF, 0xFF},  // Cyan
    {0xFF, 0x88, 0x44, 0xFF},  // Orange
};

// Use UI_BLINK_RATE from ui.h (20 frames per blink cycle)

// ============================================================
// RNG
// ============================================================

static uint32_t celebrate_rand(CelebrateState* cs) {
    cs->rng = cs->rng * 1103515245 + 12345;
    return (cs->rng >> 16) & 0x7FFF;
}

static float celebrate_randf(CelebrateState* cs) {
    return (float)celebrate_rand(cs) / 32767.0f;
}

// ============================================================
// INITIALIZATION
// ============================================================

void celebrate_init(CelebrateState* cs) {
    cs->phase = CELEBRATE_PHASE_INACTIVE;
    cs->timer = 0.0f;
    cs->fireworkSpawnTimer = 0.0f;
    cs->worldX = 0.0f;
    cs->worldY = 0.0f;
    cs->worldZ = 0.0f;
    cs->blinkTimer = 0;
    cs->rng = 54321;
    cs->boltsCollected = 0;
    cs->totalBolts = 0;
    cs->deaths = 0;
    cs->completionTime = 0.0f;
    cs->rank = 'D';
    cs->rankSprite = NULL;
    cs->rankSpriteChar = 0;
    cs->targetLevel = 0;
    cs->targetSpawn = 0;
    cs->soundsLoaded = false;
    cs->setoffChannel = -1;

    for (int i = 0; i < CELEBRATE_MAX_FIREWORKS; i++) {
        cs->fireworks[i].active = false;
        cs->fireworks[i].exploded = false;
    }
    for (int i = 0; i < CELEBRATE_MAX_SPARKS; i++) {
        cs->sparks[i].active = false;
    }
}

void celebrate_reset(CelebrateState* cs) {
    celebrate_init(cs);
}

// ============================================================
// RANK CALCULATION
// ============================================================

char celebrate_calculate_rank(int deaths, int boltsCollected, int totalBolts, float time) {
    bool allBolts = (boltsCollected == totalBolts && totalBolts > 0);
    float boltRatio = totalBolts > 0 ? (float)boltsCollected / (float)totalBolts : 1.0f;

    // S Rank: No deaths, all bolts, under 5 minutes
    if (deaths == 0 && allBolts && time < 300.0f) {
        return 'S';
    }

    // A Rank: No deaths with most bolts OR all bolts with few deaths and good time
    if ((deaths == 0 && boltRatio >= 0.75f) ||
        (allBolts && deaths <= 1 && time < 420.0f)) {
        return 'A';
    }

    // B Rank: Low deaths with decent bolt collection
    if ((deaths <= 2 && boltRatio >= 0.5f) || (deaths == 0)) {
        return 'B';
    }

    // C Rank: Moderate deaths or low bolt collection
    if (deaths <= 5 && boltRatio >= 0.25f) {
        return 'C';
    }

    // D Rank: Just completed
    return 'D';
}

// ============================================================
// FIREWORK SPAWNING
// ============================================================

static void spawn_firework(CelebrateState* cs) {
    for (int i = 0; i < CELEBRATE_MAX_FIREWORKS; i++) {
        if (!cs->fireworks[i].active) {
            cs->fireworks[i].active = true;
            cs->fireworks[i].exploded = false;
            // Spawn across the screen width
            cs->fireworks[i].x = cs->worldX + (celebrate_randf(cs) - 0.5f) * 280.0f;
            cs->fireworks[i].y = cs->worldY;
            cs->fireworks[i].z = cs->worldZ + (celebrate_randf(cs) - 0.5f) * 60.0f;
            cs->fireworks[i].velY = 150.0f + celebrate_randf(cs) * 80.0f;
            cs->fireworks[i].colorIdx = celebrate_rand(cs) % CELEBRATE_NUM_COLORS;

            // Play firework setoff sound
            if (g_celebrateSoundsLoaded) {
                cs->setoffChannel = get_free_sfx_channel();
                wav64_play(&sfxFireworkSetoff, cs->setoffChannel);
            }
            break;
        }
    }
}

static void explode_firework(CelebrateState* cs, CelebrateFirework* fw) {
    fpu_flush_denormals();  // Prevent denormal exceptions from sinf/cosf
    fw->exploded = true;
    fw->active = false;

    // Stop setoff sound and play explosion
    if (g_celebrateSoundsLoaded) {
        // Stop the setoff sound if it's still playing
        if (cs->setoffChannel >= 0 && mixer_ch_playing(cs->setoffChannel)) {
            mixer_ch_stop(cs->setoffChannel);
        }
        // Play random explosion sound
        int explosionIdx = celebrate_rand(cs) % 3;
        int channel = get_free_sfx_channel();
        wav64_play(&sfxFireworkExplosion[explosionIdx], channel);
    }

    // Spawn sparks in a sphere
    int sparksToSpawn = 8 + (celebrate_rand(cs) % 6);
    for (int i = 0; i < sparksToSpawn; i++) {
        for (int j = 0; j < CELEBRATE_MAX_SPARKS; j++) {
            if (!cs->sparks[j].active) {
                float theta = celebrate_randf(cs) * 6.283f;
                float phi = celebrate_randf(cs) * 3.14159f;
                float speed = 40.0f + celebrate_randf(cs) * 40.0f;
                cs->sparks[j].active = true;
                cs->sparks[j].x = fw->x;
                cs->sparks[j].y = fw->y;
                cs->sparks[j].z = fw->z;
                cs->sparks[j].velX = sinf(phi) * cosf(theta) * speed;
                cs->sparks[j].velY = cosf(phi) * speed;
                cs->sparks[j].velZ = sinf(phi) * sinf(theta) * speed;
                cs->sparks[j].maxLife = 0.5f + celebrate_randf(cs) * 0.5f;
                cs->sparks[j].life = cs->sparks[j].maxLife;
                cs->sparks[j].colorIdx = fw->colorIdx;
                break;
            }
        }
    }
}

// ============================================================
// START CELEBRATION
// ============================================================

void celebrate_start(CelebrateState* cs, float worldX, float worldY, float worldZ,
                     int boltsCollected, int totalBolts, int deaths, float completionTime,
                     int targetLevel, int targetSpawn) {
    cs->phase = CELEBRATE_PHASE_FIREWORKS;
    cs->timer = 0.0f;
    cs->fireworkSpawnTimer = 0.0f;
    cs->worldX = worldX;
    cs->worldY = worldY;
    cs->worldZ = worldZ;
    cs->blinkTimer = 0;
    cs->rng = 54321 + (uint32_t)(completionTime * 1000);

    cs->boltsCollected = boltsCollected;
    cs->totalBolts = totalBolts;
    cs->deaths = deaths;
    cs->completionTime = completionTime;
    cs->rank = celebrate_calculate_rank(deaths, boltsCollected, totalBolts, completionTime);
    cs->targetLevel = targetLevel;
    cs->targetSpawn = targetSpawn;

    // Clear all fireworks and sparks
    for (int i = 0; i < CELEBRATE_MAX_FIREWORKS; i++) {
        cs->fireworks[i].active = false;
        cs->fireworks[i].exploded = false;
    }
    for (int i = 0; i < CELEBRATE_MAX_SPARKS; i++) {
        cs->sparks[i].active = false;
    }

    debugf("Celebrate: Started at (%.1f, %.1f, %.1f) target level %d\n",
           worldX, worldY, worldZ, targetLevel);
}

// ============================================================
// UPDATE
// ============================================================

bool celebrate_update(CelebrateState* cs, float deltaTime, bool aPressed) {
    if (cs->phase == CELEBRATE_PHASE_INACTIVE) return false;

    cs->timer += deltaTime;
    cs->blinkTimer++;
    if (cs->blinkTimer >= UI_BLINK_RATE * 2) {
        cs->blinkTimer = 0;
    }

    if (cs->phase == CELEBRATE_PHASE_FIREWORKS) {
        // Spawn fireworks periodically
        cs->fireworkSpawnTimer += deltaTime;
        if (cs->fireworkSpawnTimer >= CELEBRATE_FIREWORK_INTERVAL) {
            cs->fireworkSpawnTimer = 0.0f;
            spawn_firework(cs);
        }

        // Update firework rockets
        for (int i = 0; i < CELEBRATE_MAX_FIREWORKS; i++) {
            if (cs->fireworks[i].active && !cs->fireworks[i].exploded) {
                cs->fireworks[i].y += cs->fireworks[i].velY * deltaTime;
                cs->fireworks[i].velY -= 120.0f * deltaTime;

                // Explode when velocity gets low or reaches peak height
                if (cs->fireworks[i].velY <= 20.0f ||
                    cs->fireworks[i].y > cs->worldY + 150.0f) {
                    explode_firework(cs, &cs->fireworks[i]);
                }
            }
        }

        // Update sparks
        for (int i = 0; i < CELEBRATE_MAX_SPARKS; i++) {
            if (cs->sparks[i].active) {
                cs->sparks[i].x += cs->sparks[i].velX * deltaTime;
                cs->sparks[i].y += cs->sparks[i].velY * deltaTime;
                cs->sparks[i].z += cs->sparks[i].velZ * deltaTime;
                cs->sparks[i].velY -= 60.0f * deltaTime;  // Gravity
                cs->sparks[i].life -= deltaTime;
                if (cs->sparks[i].life <= 0) {
                    cs->sparks[i].active = false;
                }
            }
        }

        // After firework duration, show the UI
        if (cs->timer >= CELEBRATE_FIREWORK_DURATION) {
            cs->phase = CELEBRATE_PHASE_UI_SHOWING;
            debugf("Celebrate: Fireworks done, showing UI\n");
        }
    }
    else if (cs->phase == CELEBRATE_PHASE_UI_SHOWING) {
        // Continue spawning fireworks during UI
        cs->fireworkSpawnTimer += deltaTime;
        if (cs->fireworkSpawnTimer >= CELEBRATE_FIREWORK_INTERVAL) {
            cs->fireworkSpawnTimer = 0.0f;
            spawn_firework(cs);
        }

        // Update rising fireworks
        for (int i = 0; i < CELEBRATE_MAX_FIREWORKS; i++) {
            if (cs->fireworks[i].active && !cs->fireworks[i].exploded) {
                cs->fireworks[i].y += cs->fireworks[i].velY * deltaTime;
                cs->fireworks[i].velY -= 120.0f * deltaTime;

                if (cs->fireworks[i].velY <= 20.0f ||
                    cs->fireworks[i].y > cs->worldY + 150.0f) {
                    explode_firework(cs, &cs->fireworks[i]);
                }
            }
        }

        // Update sparks
        for (int i = 0; i < CELEBRATE_MAX_SPARKS; i++) {
            if (cs->sparks[i].active) {
                cs->sparks[i].x += cs->sparks[i].velX * deltaTime;
                cs->sparks[i].y += cs->sparks[i].velY * deltaTime;
                cs->sparks[i].z += cs->sparks[i].velZ * deltaTime;
                cs->sparks[i].velY -= 60.0f * deltaTime;
                cs->sparks[i].life -= deltaTime;
                if (cs->sparks[i].life <= 0) {
                    cs->sparks[i].active = false;
                }
            }
        }

        // Check for A press to continue
        if (aPressed) {
            cs->phase = CELEBRATE_PHASE_DONE;
            debugf("Celebrate: A pressed, ready for transition\n");
            return true;
        }
    }

    return false;
}

// ============================================================
// DRAWING
// ============================================================

void celebrate_draw(CelebrateState* cs, int screenCenterX, int screenCenterY) {
    if (cs->phase == CELEBRATE_PHASE_INACTIVE) return;

    // Prevent denormal exceptions on particle position conversions
    fpu_flush_denormals();

    // Draw sparks as 2D screen-space particles
    for (int i = 0; i < CELEBRATE_MAX_SPARKS; i++) {
        if (cs->sparks[i].active) {
            // Simple 2D projection (X relative to world center, Y is height)
            int screenX = screenCenterX + (int)(cs->sparks[i].x - cs->worldX);
            int screenY = screenCenterY + 80 - (int)(cs->sparks[i].y - cs->worldY);

            if (screenX >= 0 && screenX < SCREEN_WIDTH && screenY >= 0 && screenY < SCREEN_HEIGHT) {
                float alpha = cs->sparks[i].life / cs->sparks[i].maxLife;
                color_t c = CELEBRATE_COLORS[cs->sparks[i].colorIdx];
                c.a = (uint8_t)(alpha * 255.0f);

                rdpq_sync_pipe();  // Sync before 2D mode switch
                rdpq_set_mode_standard();
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(c);
                rdpq_fill_rectangle(screenX - 1, screenY - 1, screenX + 2, screenY + 2);
            }
        }
    }

    // Draw rising firework rockets as 2D
    for (int i = 0; i < CELEBRATE_MAX_FIREWORKS; i++) {
        if (cs->fireworks[i].active && !cs->fireworks[i].exploded) {
            int screenX = screenCenterX + (int)(cs->fireworks[i].x - cs->worldX);
            int screenY = screenCenterY + 80 - (int)(cs->fireworks[i].y - cs->worldY);

            if (screenX >= 0 && screenX < SCREEN_WIDTH && screenY >= 0 && screenY < SCREEN_HEIGHT) {
                color_t c = CELEBRATE_COLORS[cs->fireworks[i].colorIdx];
                rdpq_sync_pipe();  // Sync before 2D mode switch
                rdpq_set_mode_standard();
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(c);
                rdpq_fill_rectangle(screenX - 2, screenY - 2, screenX + 3, screenY + 3);
            }
        }
    }

    // Draw level complete UI overlay during UI phase
    if (cs->phase == CELEBRATE_PHASE_UI_SHOWING) {
        // Calculate box position centered on screen center
        int boxW = 180, boxH = 130;
        int boxX = screenCenterX - boxW / 2;
        int boxY = screenCenterY - boxH / 2;

        // Load UI sprites if needed
        ui_load_sprites();

        // Draw main UI box with sprite borders
        ui_draw_box(boxX, boxY, boxW, boxH, UI_COLOR_BG, UI_COLOR_BORDER);

        // Title - centered
        rdpq_sync_pipe();  // Sync before 2D mode switch
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_set_prim_color(UI_COLOR_TEXT);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO,
                        boxX + 32, boxY + 18, "LEVEL COMPLETE!");

        // Stats
        int minutes = (int)(cs->completionTime / 60.0f);
        int seconds = (int)cs->completionTime % 60;
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO,
                        boxX + 20, boxY + 38,
                        "Time: %d:%02d", minutes, seconds);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO,
                        boxX + 20, boxY + 53,
                        "Bolts: %d/%d", cs->boltsCollected, cs->totalBolts);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO,
                        boxX + 20, boxY + 68,
                        "Deaths: %d", cs->deaths);

        // Draw rank box (positioned inside the main UI box)
        int rankBoxW = 48;
        int rankBoxH = 48;
        int rankX = boxX + boxW - rankBoxW - 12;
        int rankY = boxY + 35;

        // Use transparent background so parent box shows through
        ui_draw_box(rankX, rankY, rankBoxW, rankBoxH, RGBA32(0, 0, 0, 0), UI_COLOR_BORDER);

        // Load rank sprite if needed (lazy load)
        if (cs->rankSprite == NULL || cs->rankSpriteChar != cs->rank) {
            // Free old sprite if switching ranks
            if (cs->rankSprite != NULL) {
                sprite_free(cs->rankSprite);
                cs->rankSprite = NULL;
            }
            // Load the appropriate rank sprite
            switch (cs->rank) {
                case 'S': cs->rankSprite = sprite_load("rom:/S_Score.sprite"); break;
                case 'A': cs->rankSprite = sprite_load("rom:/A_Score.sprite"); break;
                case 'B': cs->rankSprite = sprite_load("rom:/B_Score.sprite"); break;
                case 'C': cs->rankSprite = sprite_load("rom:/C_Score.sprite"); break;
                default:  cs->rankSprite = sprite_load("rom:/D_Score.sprite"); break;
            }
            cs->rankSpriteChar = cs->rank;
        }

        // Draw the rank sprite centered in the box (16x16 scaled to 32x32)
        if (cs->rankSprite) {
            rdpq_sync_pipe();  // Sync before 2D mode switch
            rdpq_set_mode_standard();
            rdpq_mode_alphacompare(1);
            float rankScale = 2.0f;  // 16x16 * 2 = 32x32
            int spriteX = rankX + (rankBoxW - 32) / 2;
            int spriteY = rankY + (rankBoxH - 32) / 2;
            rdpq_sprite_blit(cs->rankSprite, spriteX, spriteY, &(rdpq_blitparms_t){
                .scale_x = rankScale,
                .scale_y = rankScale
            });
        }

        // Blinking "Press A" with button icon
        if (cs->blinkTimer < UI_BLINK_RATE) {
            rdpq_text_printf(NULL, 2, boxX + boxW / 2 - 4, boxY + boxH - 16, "a");
        }
    }
}

// ============================================================
// QUERIES
// ============================================================

bool celebrate_is_active(CelebrateState* cs) {
    return cs->phase != CELEBRATE_PHASE_INACTIVE;
}

bool celebrate_is_done(CelebrateState* cs) {
    return cs->phase == CELEBRATE_PHASE_DONE;
}

int celebrate_get_target_level(CelebrateState* cs) {
    return cs->targetLevel;
}

int celebrate_get_target_spawn(CelebrateState* cs) {
    return cs->targetSpawn;
}

color_t celebrate_get_color(int idx) {
    if (idx < 0 || idx >= CELEBRATE_NUM_COLORS) idx = 0;
    return CELEBRATE_COLORS[idx];
}

// ============================================================
// SOUND LOADING
// ============================================================

void celebrate_load_sounds(void) {
    if (g_celebrateSoundsLoaded) return;

    wav64_open(&sfxFireworkSetoff, "rom:/FireworkSetOff2.wav64");
    wav64_open(&sfxFireworkExplosion[0], "rom:/FireworkExplosion1.wav64");
    wav64_open(&sfxFireworkExplosion[1], "rom:/FireworkExplosion2.wav64");
    wav64_open(&sfxFireworkExplosion[2], "rom:/FireworkExplosion3.wav64");

    g_celebrateSoundsLoaded = true;
}

void celebrate_unload_sounds(void) {
    if (!g_celebrateSoundsLoaded) return;

    // Stop all SFX channels before closing wav64 files
    // This prevents use-after-free if sounds are still playing
    for (int ch = 2; ch < 8; ch++) {
        mixer_ch_stop(ch);
    }
    rspq_wait();  // Wait for RSP to finish before closing

    wav64_close(&sfxFireworkSetoff);
    wav64_close(&sfxFireworkExplosion[0]);
    wav64_close(&sfxFireworkExplosion[1]);
    wav64_close(&sfxFireworkExplosion[2]);

    g_celebrateSoundsLoaded = false;
}
