#include <libdragon.h>
#include "splash.h"
#include "../scene.h"
#include "../save.h"

// Flag to tell menu scene to fade in from black
bool menuStartWithFadeIn = false;

// Splash screen state
typedef enum {
    SPLASH_INITIAL_DELAY,
    SPLASH_PSYOPS_FADE_IN,
    SPLASH_PSYOPS_HOLD,
    SPLASH_PSYOPS_FADE_OUT,
    SPLASH_POLLYGONE_FADE_IN,
    SPLASH_POLLYGONE_HOLD,
    SPLASH_POLLYGONE_FADE_OUT,
    SPLASH_STATICTYR_FADE_IN,
    SPLASH_STATICTYR_HOLD,
    SPLASH_STATICTYR_FADE_OUT,
    SPLASH_LIBS_FADE_IN,
    SPLASH_LIBS_HOLD,
    SPLASH_LIBS_FADE_OUT
} SplashState;

static SplashState splashState;
static float fadeAlpha;
static float holdTimer;

// Psyops logo sprites (4 quadrants of 128x128 each = 256x256 total)
static sprite_t* psyopsSprites[4] = {NULL, NULL, NULL, NULL};

// PollyGone logo (128x128)
static sprite_t* pollyGoneSprite = NULL;

// StatycTyr logo (128x128)
static sprite_t* statycTyrSprite = NULL;

// Library logos (LibDragon: 2x 128x128, Tiny3D: 2x 128x99)
static sprite_t* libdragonSprites[2] = {NULL, NULL};
static sprite_t* tiny3dSprites[2] = {NULL, NULL};

// Timing (in seconds)
#define INITIAL_DELAY_TIME 1.5f
#define FADE_IN_TIME 1.0f
#define HOLD_TIME 2.0f
#define FADE_OUT_TIME 1.0f

void init_splash_scene(void) {
    splashState = SPLASH_INITIAL_DELAY;
    fadeAlpha = 0.0f;
    holdTimer = 0.0f;

    // Load Psyops logo (4 quadrants)
    psyopsSprites[0] = sprite_load("rom:/Psyops1.sprite");  // Top-left
    psyopsSprites[1] = sprite_load("rom:/Psyops2.sprite");  // Top-right
    psyopsSprites[2] = sprite_load("rom:/Psyops3.sprite");  // Bottom-left
    psyopsSprites[3] = sprite_load("rom:/Psyops4.sprite");  // Bottom-right
}

// Helper to free Psyops sprites
static void free_psyops_sprites(void) {
    for (int i = 0; i < 4; i++) {
        if (psyopsSprites[i]) {
            sprite_free(psyopsSprites[i]);
            psyopsSprites[i] = NULL;
        }
    }
}

// Helper to load PollyGone sprite
static void load_pollygone_sprite(void) {
    pollyGoneSprite = sprite_load("rom:/PollyGone.sprite");
}

// Helper to free PollyGone sprite
static void free_pollygone_sprite(void) {
    if (pollyGoneSprite) {
        sprite_free(pollyGoneSprite);
        pollyGoneSprite = NULL;
    }
}

// Helper to load StatycTyr sprite
static void load_statictyr_sprite(void) {
    statycTyrSprite = sprite_load("rom:/StatycTyr.sprite");
}

// Helper to free StatycTyr sprite
static void free_statictyr_sprite(void) {
    if (statycTyrSprite) {
        sprite_free(statycTyrSprite);
        statycTyrSprite = NULL;
    }
}

// Helper to free library logo sprites
static void free_lib_sprites(void) {
    for (int i = 0; i < 2; i++) {
        if (libdragonSprites[i]) {
            sprite_free(libdragonSprites[i]);
            libdragonSprites[i] = NULL;
        }
        if (tiny3dSprites[i]) {
            sprite_free(tiny3dSprites[i]);
            tiny3dSprites[i] = NULL;
        }
    }
}

// Load library logo sprites
static void load_lib_sprites(void) {
    libdragonSprites[0] = sprite_load("rom:/LibDragon1.sprite");
    libdragonSprites[1] = sprite_load("rom:/LibDragon2.sprite");
    tiny3dSprites[0] = sprite_load("rom:/Tiny3D1.sprite");
    tiny3dSprites[1] = sprite_load("rom:/Tiny3D2.sprite");
}

void deinit_splash_scene(void) {
    free_psyops_sprites();
    free_pollygone_sprite();
    free_statictyr_sprite();
    free_lib_sprites();
}

void update_splash_scene(void) {
    // Update audio mixer first (prevents crackling)
    if (audio_can_write()) {
        short *buf = audio_write_begin();
        rspq_wait();  // Sync RSP immediately before mixer_poll (VADPCM uses highpri queue)
        save_apply_volume_settings_safe();  // Apply pending volume changes in safe window
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }

    float dt = 1.0f / 30.0f;  // Assuming 30fps

    // Allow pressing A/B/Start to trigger early fade-out of current image
    joypad_poll();
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed.start || pressed.a || pressed.b) {
        // Skip to fade-out phase of current logo (don't skip entire scene)
        switch (splashState) {
            case SPLASH_INITIAL_DELAY:
                // Skip delay and go straight to fade in
                splashState = SPLASH_PSYOPS_FADE_IN;
                holdTimer = 0.0f;
                break;
            case SPLASH_PSYOPS_FADE_IN:
                // If still fading in, set alpha to current value and start fading out
                splashState = SPLASH_PSYOPS_FADE_OUT;
                if (fadeAlpha < 0.5f) fadeAlpha = 0.5f;  // Ensure visible before fade out
                break;
            case SPLASH_PSYOPS_HOLD:
                // Already fully visible, just start fade out
                splashState = SPLASH_PSYOPS_FADE_OUT;
                break;
            case SPLASH_POLLYGONE_FADE_IN:
                splashState = SPLASH_POLLYGONE_FADE_OUT;
                if (fadeAlpha < 0.5f) fadeAlpha = 0.5f;
                break;
            case SPLASH_POLLYGONE_HOLD:
                splashState = SPLASH_POLLYGONE_FADE_OUT;
                break;
            case SPLASH_STATICTYR_FADE_IN:
                splashState = SPLASH_STATICTYR_FADE_OUT;
                if (fadeAlpha < 0.5f) fadeAlpha = 0.5f;
                break;
            case SPLASH_STATICTYR_HOLD:
                splashState = SPLASH_STATICTYR_FADE_OUT;
                break;
            case SPLASH_LIBS_FADE_IN:
                // If still fading in, set alpha to current value and start fading out
                splashState = SPLASH_LIBS_FADE_OUT;
                if (fadeAlpha < 0.5f) fadeAlpha = 0.5f;  // Ensure visible before fade out
                break;
            case SPLASH_LIBS_HOLD:
                // Already fully visible, just start fade out
                splashState = SPLASH_LIBS_FADE_OUT;
                break;
            default:
                // Already fading out, do nothing
                break;
        }
    }

    switch (splashState) {
        // Initial delay (black screen)
        case SPLASH_INITIAL_DELAY:
            holdTimer += dt;
            if (holdTimer >= INITIAL_DELAY_TIME) {
                splashState = SPLASH_PSYOPS_FADE_IN;
                holdTimer = 0.0f;
            }
            break;

        // Psyops logo phase
        case SPLASH_PSYOPS_FADE_IN:
            fadeAlpha += dt / FADE_IN_TIME;
            if (fadeAlpha >= 1.0f) {
                fadeAlpha = 1.0f;
                splashState = SPLASH_PSYOPS_HOLD;
                holdTimer = 0.0f;
            }
            break;

        case SPLASH_PSYOPS_HOLD:
            holdTimer += dt;
            if (holdTimer >= HOLD_TIME) {
                splashState = SPLASH_PSYOPS_FADE_OUT;
            }
            break;

        case SPLASH_PSYOPS_FADE_OUT:
            fadeAlpha -= dt / FADE_OUT_TIME;
            if (fadeAlpha <= 0.0f) {
                fadeAlpha = 0.0f;
                // Free Psyops sprites and load PollyGone sprite
                free_psyops_sprites();
                load_pollygone_sprite();
                splashState = SPLASH_POLLYGONE_FADE_IN;
            }
            break;

        // PollyGone logo phase
        case SPLASH_POLLYGONE_FADE_IN:
            fadeAlpha += dt / FADE_IN_TIME;
            if (fadeAlpha >= 1.0f) {
                fadeAlpha = 1.0f;
                splashState = SPLASH_POLLYGONE_HOLD;
                holdTimer = 0.0f;
            }
            break;

        case SPLASH_POLLYGONE_HOLD:
            holdTimer += dt;
            if (holdTimer >= HOLD_TIME) {
                splashState = SPLASH_POLLYGONE_FADE_OUT;
            }
            break;

        case SPLASH_POLLYGONE_FADE_OUT:
            fadeAlpha -= dt / FADE_OUT_TIME;
            if (fadeAlpha <= 0.0f) {
                fadeAlpha = 0.0f;
                // Free PollyGone sprite and load StatycTyr sprite
                free_pollygone_sprite();
                load_statictyr_sprite();
                splashState = SPLASH_STATICTYR_FADE_IN;
            }
            break;

        // StatycTyr logo phase
        case SPLASH_STATICTYR_FADE_IN:
            fadeAlpha += dt / FADE_IN_TIME;
            if (fadeAlpha >= 1.0f) {
                fadeAlpha = 1.0f;
                splashState = SPLASH_STATICTYR_HOLD;
                holdTimer = 0.0f;
            }
            break;

        case SPLASH_STATICTYR_HOLD:
            holdTimer += dt;
            if (holdTimer >= HOLD_TIME) {
                splashState = SPLASH_STATICTYR_FADE_OUT;
            }
            break;

        case SPLASH_STATICTYR_FADE_OUT:
            fadeAlpha -= dt / FADE_OUT_TIME;
            if (fadeAlpha <= 0.0f) {
                fadeAlpha = 0.0f;
                // Free StatycTyr sprite and load library sprites
                free_statictyr_sprite();
                load_lib_sprites();
                splashState = SPLASH_LIBS_FADE_IN;
            }
            break;

        // Library logos phase (LibDragon + Tiny3D)
        case SPLASH_LIBS_FADE_IN:
            fadeAlpha += dt / FADE_IN_TIME;
            if (fadeAlpha >= 1.0f) {
                fadeAlpha = 1.0f;
                splashState = SPLASH_LIBS_HOLD;
                holdTimer = 0.0f;
            }
            break;

        case SPLASH_LIBS_HOLD:
            holdTimer += dt;
            if (holdTimer >= HOLD_TIME) {
                splashState = SPLASH_LIBS_FADE_OUT;
            }
            break;

        case SPLASH_LIBS_FADE_OUT:
            fadeAlpha -= dt / FADE_OUT_TIME;
            if (fadeAlpha <= 0.0f) {
                fadeAlpha = 0.0f;
                // Free library sprites and go to logo screen
                free_lib_sprites();
                change_scene(LOGO_SCENE);
            }
            break;
    }
}

void draw_splash_scene(void) {
    rdpq_attach(display_get(), NULL);

    // Clear to black
    rdpq_clear(RGBA32(0, 0, 0, 255));

    int screenW = display_get_width();
    int screenH = display_get_height();

    if (fadeAlpha > 0.01f) {
        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        // Set alpha for fade effect
        uint8_t alpha = (uint8_t)(fadeAlpha * 255.0f);
        rdpq_set_prim_color(RGBA32(255, 255, 255, alpha));
        rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, PRIM, 0), (TEX0, 0, PRIM, 0)));

        if (splashState >= SPLASH_PSYOPS_FADE_IN && splashState <= SPLASH_PSYOPS_FADE_OUT) {
            // Draw Psyops logo (256x256, 4 quadrants)
            int logoX = (screenW - 256) / 2;
            int logoY = (screenH - 256) / 2;

            // Top-left (Psyops1)
            if (psyopsSprites[0]) {
                rdpq_sprite_blit(psyopsSprites[0], logoX, logoY + 1, NULL);
            }
            // Top-right (Psyops2)
            if (psyopsSprites[1]) {
                rdpq_sprite_blit(psyopsSprites[1], logoX + 128, logoY, NULL);
            }
            // Bottom-left (Psyops3)
            if (psyopsSprites[2]) {
                rdpq_sprite_blit(psyopsSprites[2], logoX, logoY + 128, NULL);
            }
            // Bottom-right (Psyops4)
            if (psyopsSprites[3]) {
                rdpq_sprite_blit(psyopsSprites[3], logoX + 128, logoY + 128, NULL);
            }
        } else if (splashState >= SPLASH_POLLYGONE_FADE_IN && splashState <= SPLASH_POLLYGONE_FADE_OUT) {
            // Draw PollyGone logo (128x128, centered)
            if (pollyGoneSprite) {
                int logoX = (screenW - 128) / 2;
                int logoY = (screenH - 128) / 2;
                rdpq_sprite_blit(pollyGoneSprite, logoX, logoY, NULL);
            }
        } else if (splashState >= SPLASH_STATICTYR_FADE_IN && splashState <= SPLASH_STATICTYR_FADE_OUT) {
            // Draw StatycTyr logo (128x128, centered)
            if (statycTyrSprite) {
                int logoX = (screenW - 128) / 2;
                int logoY = (screenH - 128) / 2;
                rdpq_sprite_blit(statycTyrSprite, logoX, logoY, NULL);
            }
        } else {
            // Draw LibDragon logo (256x128) on top, Tiny3D logo (256x99 scaled to 0.7) below
            float tinyScale = 0.7f;
            int tinyScaledW = (int)(256 * tinyScale);
            int tinyScaledH = (int)(99 * tinyScale);
            int gap = 8;
            int totalH = 128 + gap + tinyScaledH;
            int startY = (screenH - totalH) / 2;

            // LibDragon logo (256x128, 2 halves)
            int libX = (screenW - 256) / 2;
            int libY = startY;
            if (libdragonSprites[0]) {
                rdpq_sprite_blit(libdragonSprites[0], libX, libY, NULL);
            }
            if (libdragonSprites[1]) {
                rdpq_sprite_blit(libdragonSprites[1], libX + 128, libY, NULL);
            }

            // Tiny3D logo (256x99 scaled down, 2 halves) below LibDragon
            int tinyX = (screenW - tinyScaledW) / 2;
            int tinyY = startY + 128 + gap;
            rdpq_blitparms_t tinyParms = { .scale_x = tinyScale, .scale_y = tinyScale };
            if (tiny3dSprites[0]) {
                rdpq_sprite_blit(tiny3dSprites[0], tinyX, tinyY, &tinyParms);
            }
            if (tiny3dSprites[1]) {
                rdpq_sprite_blit(tiny3dSprites[1], tinyX + (int)(128 * tinyScale), tinyY, &tinyParms);
            }
        }
    }

    rdpq_sync_full(NULL, NULL);  // Full sync before frame end
    rdpq_detach_show();
}
