// ============================================================
// LOGO SCENE - Shows game logo over demo playback
// ============================================================
// This scene runs demo mode in the background with the game logo
// and "Press Start" overlaid. Press Start to go to menu.
// ============================================================

#include <libdragon.h>
#include <math.h>
#include "logo_scene.h"
#include "../save.h"
#include "game.h"
#include "level_select.h"
#include "../scene.h"
#include "../demo_data.h"
#include "../constants.h"
#include "../mapData.h"

// External flag to tell menu scene to fade in from black
extern bool menuStartWithFadeIn;

// External demo mode variables (defined in demo_scene.c)
extern bool g_demoMode;
extern DemoPlaybackState g_demoState;
extern const DemoData DEMO_LIST[];
extern DemoOverlayDrawFunc g_demoOverlayDraw;
extern DemoOverlayUpdateFunc g_demoOverlayUpdate;
extern DemoIrisDrawFunc g_demoIrisDraw;
extern bool g_demoGoalReached;
extern int g_requestedDemoIndex;
extern bool g_replayMode;

// Logo scene state
typedef enum {
    LOGO_INTRO,      // Playing intro sound, waiting for logo to appear
    LOGO_FADE_IN,
    LOGO_WAITING,
    LOGO_FADE_OUT
} LogoState;

static LogoState logoState;
static float fadeAlpha;
static float pressStartTimer;  // For flashing effect

// Intro sound timing
static wav64_t introSound;
static bool introSoundLoaded = false;
static float introTimer = 0.0f;
static bool introSoundStarted = false;
static bool logoShown = false;
#define INTRO_SOUND_DURATION 3.0f  // Approximate duration of BOTTUBOI5 sound
#define LOGO_APPEAR_TIME (INTRO_SOUND_DURATION * 0.5f)  // Logo appears halfway through

// Sprites
static sprite_t* logoSprite = NULL;
static sprite_t* pressStartSprite = NULL;

// Demo tracking
static int currentDemoIndex = 0;
static float demoTimer = 0.0f;
#define DEMO_MAX_TIME 20.0f

// Deferred level switch flags (same pattern as demo_scene.c)
static bool pendingLevelSwitch = false;
static bool justSwitchedLevel = false;
static bool skipNextDraw = false;
static int levelSwitchDelayFrames = 0;  // Delay to allow RDP to drain

// Number of frames to wait before level switch (allows RDP queue to drain)
#define LEVEL_SWITCH_DELAY_FRAMES 2

// Timing
#define FADE_IN_TIME 0.5f
#define FADE_OUT_TIME 0.5f
#define FLASH_SPEED 2.0f  // How fast "press start" flashes

// Forward declaration for overlay callback
static void logo_draw_overlay(void);

// Start a specific demo by index
static void logo_start_demo(int demoIndex) {
    if (demoIndex < 0 || demoIndex >= demo_get_count()) {
        demoIndex = 0;
    }
    currentDemoIndex = demoIndex;

    const DemoData* demo = demo_get(currentDemoIndex);
    if (demo) {
        selectedLevelID = demo->levelId;
        startWithIrisOpen = true;
        g_demoMode = true;
        g_replayMode = true;

        demo_start_playback(demo);
        demoTimer = 0.0f;

        debugf("Logo: Starting demo %d for level %d (%d frames)\n",
               currentDemoIndex, demo->levelId, demo->frameCount);
    }
}

// Start the next demo (randomly selected)
static void logo_start_next_demo(void) {
    g_demoMode = true;
    g_replayMode = true;

    // Pick a random demo
    if (demo_get_count() > 1) {
        int newIndex;
        do {
            newIndex = (int)(TICKS_READ() % demo_get_count());
        } while (newIndex == currentDemoIndex && demo_get_count() > 1);
        currentDemoIndex = newIndex;
    } else {
        currentDemoIndex = 0;
    }

    const DemoData* demo = demo_get(currentDemoIndex);
    if (demo) {
        selectedLevelID = demo->levelId;
        startWithIrisOpen = true;
        demo_start_playback(demo);
        demoTimer = 0.0f;

        debugf("Logo: Starting demo %d for level %d (%d frames)\n",
               currentDemoIndex, demo->levelId, demo->frameCount);
    }
}

void init_logo_scene(void) {
    fpu_flush_denormals();

    debugf("Logo scene init\n");

    logoState = LOGO_INTRO;
    fadeAlpha = 0.0f;
    pressStartTimer = 0.0f;
    demoTimer = 0.0f;
    introTimer = 0.0f;
    introSoundStarted = false;
    logoShown = false;

    // Reset deferred flags
    pendingLevelSwitch = false;
    justSwitchedLevel = false;
    skipNextDraw = false;
    levelSwitchDelayFrames = 0;

    // Reset goal flag
    g_demoGoalReached = false;

    // Load sprites
    logoSprite = sprite_load("rom:/logo.sprite");
    pressStartSprite = sprite_load("rom:/PressStart.sprite");

    // Load and play intro sound
    wav64_open(&introSound, "rom:/BOTTUBOI5.wav64");
    introSoundLoaded = true;
    wav64_play(&introSound, 1);  // Play on channel 1
    introSoundStarted = true;
    debugf("Logo: Playing intro sound BOTTUBOI5\n");

    // Set up overlay callback so game.c draws our logo
    g_demoOverlayDraw = logo_draw_overlay;
    g_demoOverlayUpdate = NULL;
    g_demoIrisDraw = NULL;

    // Don't start demo yet - wait for intro sound to finish
}

void deinit_logo_scene(void) {
    debugf("Logo scene deinit\n");

    // Stop demo playback
    demo_stop_playback();
    g_demoMode = false;
    g_replayMode = false;

    // Clear overlay callbacks BEFORE freeing sprites to prevent use-after-free
    // if a draw happens during cleanup
    g_demoOverlayDraw = NULL;
    g_demoOverlayUpdate = NULL;
    g_demoIrisDraw = NULL;

    // CRITICAL: Wait for RSP/RDP to finish before freeing sprites
    // The overlay may have just drawn these sprites and RDP could still be using them
    rspq_wait();

    // Free sprites (now safe since RDP is done with them)
    if (logoSprite) {
        sprite_free(logoSprite);
        logoSprite = NULL;
    }
    if (pressStartSprite) {
        sprite_free(pressStartSprite);
        pressStartSprite = NULL;
    }

    // Close intro sound
    if (introSoundLoaded) {
        wav64_close(&introSound);
        introSoundLoaded = false;
    }

    // Clean up game scene (also calls rspq_wait internally)
    deinit_game_scene();
}

void update_logo_scene(void) {
    fpu_flush_denormals();

    // Process pending level switch at START of frame (same pattern as demo_scene.c)
    // Use a multi-frame delay to allow RDP queue to fully drain before freeing resources
    if (pendingLevelSwitch) {
        if (levelSwitchDelayFrames < LEVEL_SWITCH_DELAY_FRAMES) {
            // Wait frames to allow RDP to drain - don't update game scene during this time
            levelSwitchDelayFrames++;
            debugf("Logo: Level switch delay frame %d/%d\n", levelSwitchDelayFrames, LEVEL_SWITCH_DELAY_FRAMES);
            return;  // Skip rest of update during delay
        }

        pendingLevelSwitch = false;
        levelSwitchDelayFrames = 0;

        debugf("Logo: Processing pending level switch\n");
        fpu_flush_denormals();

        // Stop all audio
        for (int ch = 0; ch < 8; ch++) {
            mixer_ch_stop(ch);
        }
        rspq_highpri_sync();

        // CRITICAL: Wait for RSP/RDP to fully complete ALL queued commands before freeing
        // This prevents use-after-free when RDP is still reading texture/model data
        rspq_wait();

        deinit_game_scene();
        logo_start_next_demo();
        fpu_flush_denormals();
        init_game_scene();

        // Sync RDP after level load to ensure clean state before drawing
        rspq_wait();
        rdpq_sync_full(NULL, NULL);

        justSwitchedLevel = true;
        debugf("Logo: New demo loaded\n");
    }

    float dt = 1.0f / 30.0f;  // Assuming 30fps

    // Update audio mixer (prevents crackling)
    // Skip if game scene is initialized (it handles its own mixer_poll)
    // Skip on level switch frames to avoid rspq_wait when RDP may be unstable
    if (!is_game_scene_initialized() && audio_can_write() && !justSwitchedLevel) {
        short *buf = audio_write_begin();
        rspq_wait();  // Flush RSPQ to exit highpri mode before mixer_poll (needed when rdpq_debug is active)
        save_apply_volume_settings_safe();  // Apply pending volume changes in safe window
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }

    // Update flash timer
    pressStartTimer += dt * FLASH_SPEED;

    // Check for Start button press
    joypad_poll();
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    switch (logoState) {
        case LOGO_INTRO:
            // Playing intro sound, waiting for it to finish
            introTimer += dt;

            // Show logo halfway through
            if (!logoShown && introTimer >= LOGO_APPEAR_TIME) {
                logoShown = true;
                fadeAlpha = 1.0f;  // Logo appears instantly
                debugf("Logo: Showing logo at %.2f seconds\n", introTimer);
            }

            // Check if intro sound finished
            if (introTimer >= INTRO_SOUND_DURATION) {
                debugf("Logo: Intro sound finished, starting demo\n");
                logoState = LOGO_WAITING;

                // Now start the demo
                logo_start_next_demo();
                init_game_scene();
                demoTimer = 0.0f;
            }
            // No skipping allowed - must wait for sound to finish
            break;

        case LOGO_FADE_IN:
            fadeAlpha += dt / FADE_IN_TIME;
            if (fadeAlpha >= 1.0f) {
                fadeAlpha = 1.0f;
                logoState = LOGO_WAITING;
            }
            break;

        case LOGO_WAITING:
            // Update demo timer
            demoTimer += dt;

            // Wait for Start button
            if (pressed.start) {
                logoState = LOGO_FADE_OUT;
            }
            break;

        case LOGO_FADE_OUT:
            fadeAlpha -= dt / FADE_OUT_TIME;
            if (fadeAlpha <= 0.0f) {
                fadeAlpha = 0.0f;
                // Go to menu scene with fade in
                menuStartWithFadeIn = true;
                change_scene(MENU_SCENE);
                return;
            }
            break;
    }

    // Check if demo finished or hit timeout - switch to next demo (only when demo is running)
    if (logoState != LOGO_INTRO && (!demo_is_playing() || demoTimer >= DEMO_MAX_TIME || g_demoGoalReached)) {
        g_demoGoalReached = false;
        debugf("Logo: Demo finished, deferring switch to next frame\n");
        pendingLevelSwitch = true;
    }

    // Update game scene (it reads from demo data)
    if (is_game_scene_initialized()) {
        if (justSwitchedLevel) {
            justSwitchedLevel = false;
            skipNextDraw = true;
            debugf("Logo: Skipping update on level switch frame\n");
        } else {
            update_game_scene();
        }
    }
}

// Bouncy elastic ease-out function for playful animations
static float logo_ease_out_elastic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float p = 0.3f;
    return powf(2.0f, -10.0f * t) * fm_sinf((t - p / 4.0f) * (2.0f * 3.14159f) / p) + 1.0f;
}

// Draw overlay callback - called by game.c during draw phase
static void logo_draw_overlay(void) {
    if (!logoSprite || fadeAlpha < 0.01f) return;

    // Flush FPU denormals before sprite math
    fpu_flush_denormals();

    int screenW = display_get_width();
    int screenH = display_get_height();

    // Sync T3D triangles before 2D operations
    t3d_tri_sync();
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    // Calculate alpha for main content
    uint8_t alpha = (uint8_t)(fadeAlpha * 255.0f);

    // Draw logo with playful breathing and floating animation
    if (logoSprite) {
        int logoW = logoSprite->width;
        int logoH = logoSprite->height;

        // Gentle breathing scale (oscillates between 0.98 and 1.02)
        float breathePhase = fm_sinf(pressStartTimer * 1.5f);  // Slow breath
        float logoScale = 1.0f + breathePhase * 0.02f;

        // Clamp scale for safety
        if (logoScale < 0.5f) logoScale = 0.5f;
        if (logoScale > 1.5f) logoScale = 1.5f;

        // Center position
        int logoCenterX = screenW / 2;
        int logoCenterY = screenH / 2 - 20;

        rdpq_set_prim_color(RGBA32(255, 255, 255, alpha));
        rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, PRIM, 0), (TEX0, 0, PRIM, 0)));

        rdpq_blitparms_t logoParms = {
            .scale_x = logoScale,
            .scale_y = logoScale,
            .cx = logoW / 2,
            .cy = logoH / 2
        };
        rdpq_sprite_blit(logoSprite, logoCenterX, logoCenterY, &logoParms);
    }

    // Draw "Press Start" with bouncy cartoon animation
    if (pressStartSprite && logoState == LOGO_WAITING) {
        int pressW = pressStartSprite->width;
        int pressH = pressStartSprite->height;

        // Continuous bounce animation using a looping pattern
        float bouncePhase = fmodf(pressStartTimer, 1.8f);
        float pressScale = 1.0f;

        if (bouncePhase < 0.3f) {
            // Squash down (anticipation)
            float squashProgress = bouncePhase / 0.3f;
            pressScale = 1.0f - 0.15f * fm_sinf(squashProgress * 3.14159f * 0.5f);
        } else if (bouncePhase < 0.7f) {
            // Pop up with elastic bounce
            float popProgress = (bouncePhase - 0.3f) / 0.4f;
            pressScale = logo_ease_out_elastic(popProgress) * 1.15f;
        } else if (bouncePhase < 1.0f) {
            // Settle back with slight overshoot
            float settleProgress = (bouncePhase - 0.7f) / 0.3f;
            pressScale = 1.15f - 0.15f * settleProgress + 0.05f * fm_sinf(settleProgress * 3.14159f);
        } else {
            // Hold at normal with tiny wobble
            float wobbleProgress = (bouncePhase - 1.0f) / 0.8f;
            pressScale = 1.0f + 0.02f * fm_sinf(wobbleProgress * 3.14159f * 4.0f);
        }

        // Add constant subtle squish wobble
        float squishX = 1.0f + fm_sinf(pressStartTimer * 8.0f) * 0.015f;
        float squishY = 1.0f - fm_sinf(pressStartTimer * 8.0f) * 0.015f;

        // Vertical bounce offset (synced with scale animation)
        float bounceY = 0.0f;
        if (bouncePhase < 0.3f) {
            bounceY = 3.0f * fm_sinf((bouncePhase / 0.3f) * 3.14159f * 0.5f);  // Squash down
        } else if (bouncePhase < 0.7f) {
            float popProgress = (bouncePhase - 0.3f) / 0.4f;
            bounceY = -8.0f * logo_ease_out_elastic(popProgress) + 8.0f * popProgress;  // Pop up then settle
        }

        // Flash alpha (brighter during bounce peak)
        float flash = 0.7f + 0.3f * fm_sinf(pressStartTimer * 2.5f);
        if (bouncePhase > 0.3f && bouncePhase < 0.7f) {
            flash = 1.0f;  // Full brightness during bounce
        }
        uint8_t pressAlpha = (uint8_t)(fadeAlpha * flash * 255.0f);

        // Clamp scales for safety
        float finalScaleX = pressScale * squishX;
        float finalScaleY = pressScale * squishY;
        if (finalScaleX < 0.1f) finalScaleX = 0.1f;
        if (finalScaleX > 2.0f) finalScaleX = 2.0f;
        if (finalScaleY < 0.1f) finalScaleY = 0.1f;
        if (finalScaleY > 2.0f) finalScaleY = 2.0f;

        int pressCenterX = screenW / 2;
        int pressCenterY = screenH / 2 + 55 + (int)bounceY;

        rdpq_set_prim_color(RGBA32(255, 255, 255, pressAlpha));

        rdpq_blitparms_t pressParms = {
            .scale_x = finalScaleX,
            .scale_y = finalScaleY,
            .cx = pressW / 2,
            .cy = pressH / 2
        };
        rdpq_sprite_blit(pressStartSprite, pressCenterX, pressCenterY, &pressParms);
    }

    // CRITICAL: Sync pipe after 2D sprite drawing before any T3D operations resume
    // This ensures TMEM and RDP state are consistent before T3D takes over again
    rdpq_sync_pipe();
}

void draw_logo_scene(void) {
    // Skip drawing when level just switched or switch is pending
    // This prevents drawing with stale/corrupted model data
    if (skipNextDraw || pendingLevelSwitch) {
        if (skipNextDraw) skipNextDraw = false;
        debugf("Logo: Skipping draw on level switch frame\n");
        return;
    }

    // During intro (before demo starts), draw black screen with logo
    if (logoState == LOGO_INTRO) {
        surface_t *disp = display_get();
        rdpq_attach(disp, NULL);
        rdpq_clear(RGBA32(0, 0, 0, 255));

        // Draw logo if it's time to show it
        if (logoShown && logoSprite) {
            int screenW = display_get_width();
            int screenH = display_get_height();
            int logoW = logoSprite->width;
            int logoH = logoSprite->height;

            // Breathing animation (same as overlay) - starts immediately
            float breathePhase = fm_sinf(pressStartTimer * 1.5f);
            float logoScale = 1.0f + breathePhase * 0.02f;
            if (logoScale < 0.5f) logoScale = 0.5f;
            if (logoScale > 1.5f) logoScale = 1.5f;

            rdpq_set_mode_standard();
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, PRIM, 0), (TEX0, 0, PRIM, 0)));

            rdpq_blitparms_t logoParms = {
                .scale_x = logoScale,
                .scale_y = logoScale,
                .cx = logoW / 2,
                .cy = logoH / 2
            };
            rdpq_sprite_blit(logoSprite, screenW / 2, screenH / 2 - 20, &logoParms);
        }

        rdpq_detach_show();
        return;
    }

    // Draw game scene (this will call g_demoOverlayDraw callback which draws our logo)
    if (is_game_scene_initialized()) {
        draw_game_scene();
    }
}
