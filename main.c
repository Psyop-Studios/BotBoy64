#include <libdragon.h>
#include <t3d/t3d.h>
#include "src/scene.h"
#include "src/constants.h"
#include "src/save.h"
#include "src/levels_generated.h"
#define UI_IMPLEMENTATION
#include "src/ui.h"

// Check if Expansion Pak is installed (8MB RAM)
static bool hasExpansionPak = false;

static void show_expansion_pak_error(void) {
    // Minimal init for error screen
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();

    // Register font for error text
    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

    while (1) {
        surface_t *disp = display_get();
        rdpq_attach_clear(disp, NULL);

        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_set_prim_color(RGBA32(0xFF, 0x40, 0x40, 0xFF));

        // Draw error message
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 60, 100,
            "EXPANSION PAK REQUIRED");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 130,
            "This game requires the N64");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 145,
            "Expansion Pak to run.");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 175,
            "Please insert an Expansion");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 40, 190,
            "Pak and restart the game.");

        rdpq_detach_show();
    }
}

static void init_game(void) {
    // Set FPU to flush denormals to zero (MUST be before any float operations)
    // (fpu_flush_denormals is defined in mapData.h, included via levels_generated.h)
    fpu_flush_denormals();

    // Initialize debug first for logging
    debug_init_isviewer();  // For emulators
    debug_init_usblog();    // For real hardware (SummerCart64)

    // Check for Expansion Pak (before heavy allocations)
    hasExpansionPak = is_memory_expanded();
    debugf("Expansion Pak: %s\n", hasExpansionPak ? "detected" : "not detected");
    if (!hasExpansionPak) {
        show_expansion_pak_error();
        return;  // Never returns
    }

    // Initialize libdragon subsystems
    dfs_init(DFS_DEFAULT_LOCATION);
    asset_init_compression(2);
    // TESTING: Lower resolution (256x240) for FPS testing - requires a filter for widths < 320
    // Original: display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
    display_init(RESOLUTION_256x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
    // rdpq_debug_start();  // Disabled - causes memory pressure and timer corruption
    joypad_init();

    // Set display to 30 FPS
    display_set_fps_limit(TARGET_FPS);

    // Register debug font for text rendering
    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

    // Load button icon font (f3dforever - maps lowercase letters to N64 buttons)
    rdpq_text_register_font(2, rdpq_font_load("rom:/f3dforever.font64"));

    // Initialize audio - manual polling with larger buffer for headroom
    // (Interrupt-driven audio conflicts with RSP/RDP pipeline)
    audio_init(48000, 12);  // 48kHz, 12 buffers (increased from 8 for level transitions)
    mixer_init(8);  // 8 channels: 0 for music, 1-7 for SFX
    wav64_init_compression(1);  // Enable VADPCM decompression (RSP-accelerated, supports looping)

    // Initialize Tiny3D
    t3d_init((T3DInitParams){});

    // Load global UI resources (sprites only - sounds are lazy-loaded when needed)
    ui_load_sprites();
    // NOTE: UI sounds are now lazy-loaded to reduce open wav64 file count
    // See ui.h for the lazy loading pattern

    // Initialize save system early so volume settings are available for demo mode
    // (Demo starts before menu scene, which normally initializes saves)
    save_system_init();
    save_set_level_info(TOTAL_BOLTS_IN_GAME, TOTAL_SCREWG_IN_GAME, REAL_LEVEL_COUNT);

    // Start with splash screen
    change_scene_(SPLASH);
}

static void update_game(void) {
    update_current_scene();

    if (should_change_scene) {
        change_scene_(next_scene);
    }
}

static void draw_game(void) {
    draw_current_scene();
}

int main(void) {
    init_game();

    while (1) {
        update_game();
        draw_game();
    }

    return 0;
}
