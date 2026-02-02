#ifndef UI_H
#define UI_H

#include <libdragon.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "constants.h"

// ============================================================
// UI LIBRARY - Dialogue and Option Selection System
// ============================================================

// Animation speed constant
#define UI_ANIM_SPEED 0.12f

// Set FPU to flush denormals to zero (prevents FPU exceptions)
static inline void ui_fpu_flush_denormals(void) {
    uint32_t fcr31;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcr31));
    fcr31 |= (1 << 24);     // FS bit: flush denormals to zero
    fcr31 &= ~(0x1F << 7);  // Clear exception enable bits
    fcr31 &= ~(0x3F << 2);  // Clear cause bits
    fcr31 &= ~(1 << 17);    // Clear unimplemented operation cause
    fcr31 &= ~(0x1F << 12); // Clear sticky FLAG bits
    __asm__ volatile("ctc1 %0, $31" : : "r"(fcr31));
}

// Helper: Sanitize float to prevent denormal values (flush to zero if very small)
static inline float ui_sanitize_float(float f) {
    // If absolute value is smaller than smallest normal float, clamp to 0
    // This prevents denormal exceptions on N64's MIPS FPU
    if (f > -1e-30f && f < 1e-30f) return 0.0f;
    return f;
}

// Bouncy easing function (overshoot then settle)
static inline float ui_ease_out_back(float t) {
    ui_fpu_flush_denormals();  // Prevent FPU exceptions from powf
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float result = 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
    // Clamp to prevent denormal values - easing overshoots to ~1.07 max
    if (result < 0.001f) result = 0.0f;
    if (result > 1.5f) result = 1.5f;
    return result;
}

// Reverse bouncy for closing
static inline float ui_ease_in_back(float t) {
    ui_fpu_flush_denormals();  // Prevent FPU exceptions
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float result = c3 * t * t * t - c1 * t * t;
    // Clamp to prevent denormal values - easing undershoots to ~-0.07 min
    if (result < -0.5f) result = -0.5f;
    if (result > 1.001f) result = 1.0f;
    if (result > -0.001f && result < 0.001f) result = 0.0f;
    return result;
}

// Configuration
#define UI_MAX_DIALOGUE_LENGTH 256
#define UI_MAX_OPTIONS 8
#define UI_MAX_OPTION_LENGTH 32
#define UI_TYPEWRITER_SPEED 2       // Characters per frame
#define UI_BLINK_RATE 20            // Frames per blink cycle
#define UI_MAX_QUEUE 16             // Max queued dialogue messages
#define UI_MAX_SCRIPT_NODES 32      // Max nodes in a dialogue script

// Screen dimensions (runtime detection for resolution independence)
#define UI_SCREEN_WIDTH SCREEN_WIDTH
#define UI_SCREEN_HEIGHT SCREEN_HEIGHT

// Default colors
#define UI_COLOR_BG        RGBA32(0x05, 0x1A, 0x05, 0xE0)  // Terminal green
#define UI_COLOR_BORDER    RGBA32(0x80, 0x80, 0xA0, 0xFF)
#define UI_COLOR_TEXT      RGBA32(0xFF, 0xFF, 0xFF, 0xFF)
#define UI_COLOR_HIGHLIGHT RGBA32(0x40, 0x60, 0xA0, 0xFF)
#define UI_COLOR_SHADOW    RGBA32(0x00, 0x00, 0x00, 0x80)

// UI border sprite size
#define UI_BORDER_SIZE 16

// ============================================================
// UI SPRITE SYSTEM
// ============================================================

typedef struct {
    sprite_t* topLeft;
    sprite_t* topCenter;
    sprite_t* topRight;
    sprite_t* leftCenter;
    sprite_t* rightCenter;
    sprite_t* bottomLeft;
    sprite_t* bottomCenter;
    sprite_t* bottomRight;
    bool loaded;
} UISprites;

// Global UI sprites (define storage in one .c file with UI_IMPLEMENTATION)
#ifdef UI_IMPLEMENTATION
UISprites g_uiSprites = {0};
#else
extern UISprites g_uiSprites;
#endif

// Load UI border sprites
static inline void ui_load_sprites(void) {
    if (g_uiSprites.loaded) return;

    g_uiSprites.topLeft = sprite_load("rom:/UI_Top_Left.sprite");
    g_uiSprites.topCenter = sprite_load("rom:/UI_Top_Center.sprite");
    g_uiSprites.topRight = sprite_load("rom:/UI_Top_Right.sprite");
    g_uiSprites.leftCenter = sprite_load("rom:/UI_Left_Center.sprite");
    g_uiSprites.rightCenter = sprite_load("rom:/UI_Right_Center.sprite");
    g_uiSprites.bottomLeft = sprite_load("rom:/UI_Bottom_Left.sprite");
    g_uiSprites.bottomCenter = sprite_load("rom:/UI_Bottom_Center.sprite");
    g_uiSprites.bottomRight = sprite_load("rom:/UI_Bottom_Right.sprite");
    g_uiSprites.loaded = true;

    // Debug: verify ALL sprites loaded correctly
    debugf("UI sprites loaded:\n");
    debugf("  topLeft=%p (%dx%d hsl=%d vsl=%d)\n",
           (void*)g_uiSprites.topLeft,
           g_uiSprites.topLeft ? g_uiSprites.topLeft->width : 0,
           g_uiSprites.topLeft ? g_uiSprites.topLeft->height : 0,
           g_uiSprites.topLeft ? g_uiSprites.topLeft->hslices : 0,
           g_uiSprites.topLeft ? g_uiSprites.topLeft->vslices : 0);
    debugf("  topCenter=%p topRight=%p\n",
           (void*)g_uiSprites.topCenter, (void*)g_uiSprites.topRight);
    debugf("  leftCenter=%p rightCenter=%p\n",
           (void*)g_uiSprites.leftCenter, (void*)g_uiSprites.rightCenter);
    debugf("  bottomLeft=%p bottomCenter=%p bottomRight=%p\n",
           (void*)g_uiSprites.bottomLeft, (void*)g_uiSprites.bottomCenter, (void*)g_uiSprites.bottomRight);
}

// Free UI sprites
static inline void ui_free_sprites(void) {
    if (!g_uiSprites.loaded) return;

    if (g_uiSprites.topLeft) sprite_free(g_uiSprites.topLeft);
    if (g_uiSprites.topCenter) sprite_free(g_uiSprites.topCenter);
    if (g_uiSprites.topRight) sprite_free(g_uiSprites.topRight);
    if (g_uiSprites.leftCenter) sprite_free(g_uiSprites.leftCenter);
    if (g_uiSprites.rightCenter) sprite_free(g_uiSprites.rightCenter);
    if (g_uiSprites.bottomLeft) sprite_free(g_uiSprites.bottomLeft);
    if (g_uiSprites.bottomCenter) sprite_free(g_uiSprites.bottomCenter);
    if (g_uiSprites.bottomRight) sprite_free(g_uiSprites.bottomRight);

    memset(&g_uiSprites, 0, sizeof(UISprites));
}

// ============================================================
// UI KEY SOUNDS (typewriter effect)
// ============================================================
// NOTE: UI sounds are LAZY-LOADED to reduce simultaneous open wav64 files.
// The N64 has limited resources - too many open wav64 files causes audio crashes.
// Pattern: Play functions auto-load if needed, explicit unload when done.
// Call ui_free_key_sounds() when exiting menus/dialogues to free resources.
// ============================================================

#define UI_KEY_SOUND_COUNT 5
#define UI_KEY_SOUND_CHANNEL_START 2  // First mixer channel for key sounds
#define UI_KEY_SOUND_CHANNEL_COUNT 4  // Use channels 2-5 for overlapping sounds

typedef struct {
    wav64_t sounds[UI_KEY_SOUND_COUNT];
    wav64_t pressA;          // Sound for pressing A / closing UI
    wav64_t pressAReversed;  // Reversed sound for opening UI
    wav64_t hover;           // Sound for moving between options
    int nextChannel;         // Rotating channel index
    bool loaded;
    bool pressALoaded;
    bool hoverLoaded;
} UIKeySounds;

// Global UI key sounds
#ifdef UI_IMPLEMENTATION
UIKeySounds g_uiKeySounds = {0};
#else
extern UIKeySounds g_uiKeySounds;
#endif

// Load UI key sounds
static inline void ui_load_key_sounds(void) {
    if (g_uiKeySounds.loaded) return;

    wav64_open(&g_uiKeySounds.sounds[0], "rom:/N64SingleKey1.wav64");
    wav64_open(&g_uiKeySounds.sounds[1], "rom:/N64SingleKey2.wav64");
    wav64_open(&g_uiKeySounds.sounds[2], "rom:/N64SingleKey3.wav64");
    wav64_open(&g_uiKeySounds.sounds[3], "rom:/N64SingleKey4.wav64");
    wav64_open(&g_uiKeySounds.sounds[4], "rom:/N64SingleKey5.wav64");
    g_uiKeySounds.nextChannel = 0;
    g_uiKeySounds.loaded = true;

    // Load press A sounds (forward for close/confirm, reversed for open)
    wav64_open(&g_uiKeySounds.pressA, "rom:/N64_Press_A_5.wav64");
    wav64_open(&g_uiKeySounds.pressAReversed, "rom:/N64_Press_A_5_reversed.wav64");
    g_uiKeySounds.pressALoaded = true;

    // Load hover sound for option navigation
    wav64_open(&g_uiKeySounds.hover, "rom:/N64UIHover4.wav64");
    g_uiKeySounds.hoverLoaded = true;
}

// Free UI key sounds
static inline void ui_free_key_sounds(void) {
    if (!g_uiKeySounds.loaded) return;

    // Stop all SFX channels that UI sounds might be playing on
    // UI uses channels 2-5 for key sounds, 6 for pressA/reversed, 7 for hover
    for (int ch = 2; ch < 8; ch++) {
        mixer_ch_stop(ch);
    }
    rspq_wait();  // Wait for RSP to finish before closing wav64 files

    for (int i = 0; i < UI_KEY_SOUND_COUNT; i++) {
        wav64_close(&g_uiKeySounds.sounds[i]);
    }
    if (g_uiKeySounds.pressALoaded) {
        wav64_close(&g_uiKeySounds.pressA);
        wav64_close(&g_uiKeySounds.pressAReversed);
        g_uiKeySounds.pressALoaded = false;
    }
    if (g_uiKeySounds.hoverLoaded) {
        wav64_close(&g_uiKeySounds.hover);
        g_uiKeySounds.hoverLoaded = false;
    }
    g_uiKeySounds.loaded = false;
}

// Play a random key sound on a rotating channel (lazy-loads if needed)
static inline void ui_play_key_sound(void) {
    // Lazy-load key sounds if not already loaded
    if (!g_uiKeySounds.loaded) {
        ui_load_key_sounds();
    }

    int soundIdx = rand() % UI_KEY_SOUND_COUNT;
    int channel = UI_KEY_SOUND_CHANNEL_START + g_uiKeySounds.nextChannel;

    wav64_play(&g_uiKeySounds.sounds[soundIdx], channel);

    // Rotate to next channel
    g_uiKeySounds.nextChannel = (g_uiKeySounds.nextChannel + 1) % UI_KEY_SOUND_CHANNEL_COUNT;
}

// Play the "press A" confirmation sound (for closing UI / confirming)
static inline void ui_play_press_a_sound(void) {
    // Lazy-load if needed
    if (!g_uiKeySounds.pressALoaded) {
        ui_load_key_sounds();
    }

    wav64_play(&g_uiKeySounds.pressAReversed, 6);  // Reversed for close/confirm
}

// Play the forward "press A" sound (for opening UI)
static inline void ui_play_ui_open_sound(void) {
    // Lazy-load if needed
    if (!g_uiKeySounds.pressALoaded) {
        ui_load_key_sounds();
    }

    wav64_play(&g_uiKeySounds.pressA, 6);  // Forward for open
}

// Play hover sound (for moving between options)
static inline void ui_play_hover_sound(void) {
    // Lazy-load if needed
    if (!g_uiKeySounds.hoverLoaded) {
        ui_load_key_sounds();
    }

    wav64_play(&g_uiKeySounds.hover, 7);  // Use channel 7 for hover
}

// ============================================================
// DIALOGUE BOX
// ============================================================

// Animation state for UI elements
typedef enum {
    UI_ANIM_NONE,
    UI_ANIM_OPENING,
    UI_ANIM_IDLE,
    UI_ANIM_CLOSING,
} UIAnimState;

// Sound callback type
typedef void (*UISoundCallback)(void);

// Queued message entry
typedef struct {
    char text[UI_MAX_DIALOGUE_LENGTH];
    char speaker[32];
} DialogueQueueEntry;

typedef struct {
    // Content
    char text[UI_MAX_DIALOGUE_LENGTH];
    char speaker[32];               // Optional speaker name

    // State
    bool active;
    int charIndex;                  // Current character for typewriter effect
    int textLength;                 // Total length of text
    bool complete;                  // All text revealed
    int blinkTimer;                 // For "press A" indicator

    // Layout
    int x, y;                       // Box position
    int width, height;              // Box size
    int padding;                    // Text padding inside box
    int lineHeight;                 // Pixels between lines

    // Settings
    bool useTypewriter;             // Enable typewriter effect
    int typewriterSpeed;            // Characters per frame

    // Animation
    UIAnimState animState;
    float animTime;                 // 0.0 to 1.0 progress
    float scale;                    // Current scale (0.0 to 1.0+)

    // Sound callbacks
    UISoundCallback onOpenSound;
    UISoundCallback onCloseSound;

    // Queue system
    DialogueQueueEntry queue[UI_MAX_QUEUE];
    int queueHead;                  // Next slot to write
    int queueTail;                  // Next slot to read
    int queueCount;                 // Number of queued messages
} DialogueBox;

// Initialize dialogue box with default settings
static inline void dialogue_init(DialogueBox* dlg) {
    memset(dlg, 0, sizeof(DialogueBox));
    dlg->x = 20;
    dlg->y = UI_SCREEN_HEIGHT - 80;
    dlg->width = UI_SCREEN_WIDTH - 40;
    dlg->height = 70;
    dlg->padding = 10;
    dlg->lineHeight = 12;
    dlg->useTypewriter = true;
    dlg->typewriterSpeed = UI_TYPEWRITER_SPEED;
    dlg->animState = UI_ANIM_NONE;
    dlg->animTime = 0.0f;
    dlg->scale = 1.0f;
}

// Set sound callbacks for dialogue
static inline void dialogue_set_sounds(DialogueBox* dlg, UISoundCallback onOpen, UISoundCallback onClose) {
    dlg->onOpenSound = onOpen;
    dlg->onCloseSound = onClose;
}

// Show dialogue with text
static inline void dialogue_show(DialogueBox* dlg, const char* text, const char* speaker) {
    // Don't restart if closing (wait for animation)
    if (dlg->animState == UI_ANIM_CLOSING) {
        return;
    }

    // Check if this is a fresh open (not advancing to next message)
    bool freshOpen = !dlg->active;

    strncpy(dlg->text, text, UI_MAX_DIALOGUE_LENGTH - 1);
    dlg->text[UI_MAX_DIALOGUE_LENGTH - 1] = '\0';
    dlg->textLength = strlen(dlg->text);

    if (speaker) {
        strncpy(dlg->speaker, speaker, 31);
        dlg->speaker[31] = '\0';
    } else {
        dlg->speaker[0] = '\0';
    }

    dlg->active = true;
    dlg->charIndex = dlg->useTypewriter ? 0 : dlg->textLength;
    dlg->complete = !dlg->useTypewriter;
    dlg->blinkTimer = 0;

    // Start opening animation
    dlg->animState = UI_ANIM_OPENING;
    dlg->animTime = 0.0f;
    dlg->scale = 0.0f;

    // Play open sound only on fresh open, not when advancing to next message
    if (freshOpen) {
        ui_play_ui_open_sound();
    }
    if (dlg->onOpenSound) dlg->onOpenSound();
}

// Show dialogue without speaker name
static inline void dialogue_show_text(DialogueBox* dlg, const char* text) {
    dialogue_show(dlg, text, NULL);
}

// Close dialogue (starts closing animation)
static inline void dialogue_close(DialogueBox* dlg) {
    if (dlg->animState == UI_ANIM_CLOSING || !dlg->active) return;

    dlg->animState = UI_ANIM_CLOSING;
    dlg->animTime = 0.0f;
    dlg->scale = 1.0f;

    // Play close sound
    if (dlg->onCloseSound) dlg->onCloseSound();
}

static inline bool dialogue_is_active(DialogueBox* dlg) {
    return dlg->active;
}

static inline bool dialogue_is_complete(DialogueBox* dlg) {
    return dlg->complete;
}

// Check if queue has pending messages
static inline bool dialogue_has_queued(DialogueBox* dlg) {
    return dlg->queueCount > 0;
}

// Add message to queue (doesn't show immediately)
static inline void dialogue_queue_add(DialogueBox* dlg, const char* text, const char* speaker) {
    if (dlg->queueCount >= UI_MAX_QUEUE) return;

    DialogueQueueEntry* entry = &dlg->queue[dlg->queueHead];

    int textLen = strlen(text);
    for (int i = 0; i < textLen && i < UI_MAX_DIALOGUE_LENGTH - 1; i++) {
        entry->text[i] = text[i];
    }
    entry->text[textLen < UI_MAX_DIALOGUE_LENGTH ? textLen : UI_MAX_DIALOGUE_LENGTH - 1] = '\0';

    if (speaker) {
        int speakerLen = strlen(speaker);
        for (int i = 0; i < speakerLen && i < 31; i++) {
            entry->speaker[i] = speaker[i];
        }
        entry->speaker[speakerLen < 31 ? speakerLen : 31] = '\0';
    } else {
        entry->speaker[0] = '\0';
    }

    dlg->queueHead = (dlg->queueHead + 1) % UI_MAX_QUEUE;
    dlg->queueCount++;
}

// Show next queued message (call after adding to queue, or automatically called on dismiss)
static inline bool dialogue_queue_next(DialogueBox* dlg) {
    if (dlg->queueCount <= 0) return false;

    DialogueQueueEntry* entry = &dlg->queue[dlg->queueTail];
    dialogue_show(dlg, entry->text, entry->speaker[0] ? entry->speaker : NULL);

    dlg->queueTail = (dlg->queueTail + 1) % UI_MAX_QUEUE;
    dlg->queueCount--;
    return true;
}

// Clear all queued messages
static inline void dialogue_queue_clear(DialogueBox* dlg) {
    dlg->queueHead = 0;
    dlg->queueTail = 0;
    dlg->queueCount = 0;
}

// Start showing queued messages (shows first one)
static inline void dialogue_queue_start(DialogueBox* dlg) {
    dialogue_queue_next(dlg);
}

// Update dialogue - returns true if dialogue consumed input
static inline bool dialogue_update(DialogueBox* dlg, joypad_port_t port) {
    if (!dlg->active) return false;

    // Update animation
    if (dlg->animState == UI_ANIM_OPENING) {
        dlg->animTime += UI_ANIM_SPEED;
        if (dlg->animTime >= 1.0f) {
            dlg->animTime = 1.0f;
            dlg->animState = UI_ANIM_IDLE;
        }
        dlg->scale = ui_ease_out_back(dlg->animTime);
    } else if (dlg->animState == UI_ANIM_CLOSING) {
        dlg->animTime += UI_ANIM_SPEED * 1.5f;  // Close slightly faster
        if (dlg->animTime >= 1.0f) {
            dlg->animTime = 1.0f;
            dlg->animState = UI_ANIM_NONE;
            dlg->active = false;  // Actually close now
            return false;
        }
        dlg->scale = 1.0f - ui_ease_in_back(dlg->animTime);
        if (dlg->scale < 0.0f) dlg->scale = 0.0f;
    }

    // Don't process input while animating
    if (dlg->animState != UI_ANIM_IDLE) {
        return true;
    }

    // Typewriter effect
    if (!dlg->complete) {
        int oldCharIndex = dlg->charIndex;
        dlg->charIndex += dlg->typewriterSpeed;
        if (dlg->charIndex >= dlg->textLength) {
            dlg->charIndex = dlg->textLength;
            dlg->complete = true;
        }
        // Play key sound every 3 characters
        if (dlg->charIndex > oldCharIndex && (dlg->charIndex / 3) != (oldCharIndex / 3)) {
            ui_play_key_sound();
        }
    }

    // Blink timer for indicator
    dlg->blinkTimer++;
    if (dlg->blinkTimer >= UI_BLINK_RATE * 2) {
        dlg->blinkTimer = 0;
    }

    // Input handling
    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);

    if (pressed.a) {
        ui_play_press_a_sound();  // Always play reversed sound on A press
        if (!dlg->complete) {
            // Skip to end of text
            dlg->charIndex = dlg->textLength;
            dlg->complete = true;
        } else {
            // Try to show next queued message, or close if none
            if (!dialogue_queue_next(dlg)) {
                dialogue_close(dlg);
            }
        }
        return true;
    }

    return true; // Dialogue is active, consume input focus
}

// Cached sprite pointers from initial load (for corruption detection)
static sprite_t* g_uiSpriteInitPtrs[8] = {NULL};
static bool g_uiSpritePtrsCached = false;

// Cache sprite pointers after initial load
static inline void ui_cache_sprite_ptrs(void) {
    if (g_uiSpritePtrsCached) return;
    g_uiSpriteInitPtrs[0] = g_uiSprites.topLeft;
    g_uiSpriteInitPtrs[1] = g_uiSprites.topCenter;
    g_uiSpriteInitPtrs[2] = g_uiSprites.topRight;
    g_uiSpriteInitPtrs[3] = g_uiSprites.leftCenter;
    g_uiSpriteInitPtrs[4] = g_uiSprites.rightCenter;
    g_uiSpriteInitPtrs[5] = g_uiSprites.bottomLeft;
    g_uiSpriteInitPtrs[6] = g_uiSprites.bottomCenter;
    g_uiSpriteInitPtrs[7] = g_uiSprites.bottomRight;
    g_uiSpritePtrsCached = true;
}

// Helper: Validate a sprite is not corrupted
// Returns true if sprite appears valid, false if corrupted
static inline bool ui_validate_sprite(sprite_t* sp, const char* name) {
    if (!sp) return true;  // NULL is OK (just won't be drawn)

    // Check basic dimensions
    if (sp->width == 0 || sp->height == 0 || sp->width > 256 || sp->height > 256) {
        debugf("ERROR: UI sprite '%s' corrupted! ptr=%p w=%d h=%d\n",
               name, (void*)sp, sp->width, sp->height);
        return false;
    }

    // Check hslices/vslices are reasonable (should be 1 for our 16x16 sprites)
    if (sp->hslices == 0 || sp->vslices == 0 || sp->hslices > 16 || sp->vslices > 16) {
        debugf("ERROR: UI sprite '%s' slices corrupted! hsl=%d vsl=%d\n",
               name, sp->hslices, sp->vslices);
        return false;
    }

    return true;
}

// Debug: dump sprite structure as hex
static inline void ui_dump_sprite(sprite_t* sp, const char* name) {
    if (!sp) return;
    uint32_t* raw = (uint32_t*)sp;
    debugf("Sprite '%s' at %p:\n", name, (void*)sp);
    debugf("  [0-3]: %08lx %08lx %08lx %08lx\n", raw[0], raw[1], raw[2], raw[3]);
    debugf("  [4-7]: %08lx %08lx %08lx %08lx\n", raw[4], raw[5], raw[6], raw[7]);
    debugf("  width=%d height=%d hsl=%d vsl=%d flags=%d format=%d\n",
           sp->width, sp->height, sp->hslices, sp->vslices, sp->flags, sprite_get_format(sp));
}

// Helper: Draw a filled rectangle with sprite border
static inline void ui_draw_box(int x, int y, int w, int h, color_t bg, color_t border) {
    (void)border;  // Not used when sprites are loaded

    // DEBUG: Skip ALL UI box drawing to isolate crash
    #if 0  // Re-enabled - the real bug is garbage optionPrompt values
    (void)x; (void)y; (void)w; (void)h; (void)bg;
    return;
    #endif

    // Sync RDP pipeline before mode changes
    rdpq_sync_pipe();

    // Draw background fill slightly inset so it doesn't poke past corners
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(bg);
    rdpq_fill_rectangle(x + 2, y + 2, x + w - 4, y + h - 4);

    // If sprites not loaded, fall back to simple border
    if (!g_uiSprites.loaded) {
        rdpq_set_prim_color(RGBA32(0x80, 0x80, 0xA0, 0xFF));
        rdpq_fill_rectangle(x, y, x + w, y + 2);
        rdpq_fill_rectangle(x, y + h - 2, x + w, y + h);
        rdpq_fill_rectangle(x, y, x + 2, y + h);
        rdpq_fill_rectangle(x + w - 2, y, x + w, y + h);
        return;
    }

    // Cache sprite pointers on first use (for corruption detection)
    ui_cache_sprite_ptrs();

    // On first draw, dump sprite structure for debugging
    static bool firstDraw = true;
    if (firstDraw) {
        debugf("=== FIRST UI DRAW - Sprite dump ===\n");
        ui_dump_sprite(g_uiSprites.topLeft, "topLeft");
        // Check if pointers changed since load
        if (g_uiSprites.topLeft != g_uiSpriteInitPtrs[0]) {
            debugf("WARNING: topLeft pointer changed! was=%p now=%p\n",
                   (void*)g_uiSpriteInitPtrs[0], (void*)g_uiSprites.topLeft);
        }
        firstDraw = false;
    }

    // Validate ALL sprites before drawing any
    bool anyCorrupted = false;
    if (!ui_validate_sprite(g_uiSprites.topLeft, "topLeft")) anyCorrupted = true;
    if (!ui_validate_sprite(g_uiSprites.topRight, "topRight")) anyCorrupted = true;
    if (!ui_validate_sprite(g_uiSprites.bottomLeft, "bottomLeft")) anyCorrupted = true;
    if (!ui_validate_sprite(g_uiSprites.bottomRight, "bottomRight")) anyCorrupted = true;
    if (!ui_validate_sprite(g_uiSprites.topCenter, "topCenter")) anyCorrupted = true;
    if (!ui_validate_sprite(g_uiSprites.bottomCenter, "bottomCenter")) anyCorrupted = true;
    if (!ui_validate_sprite(g_uiSprites.leftCenter, "leftCenter")) anyCorrupted = true;
    if (!ui_validate_sprite(g_uiSprites.rightCenter, "rightCenter")) anyCorrupted = true;

    if (anyCorrupted) {
        debugf("ERROR: UI sprites corrupted, aborting draw\n");
        debugf("  topLeft=%p topRight=%p\n", (void*)g_uiSprites.topLeft, (void*)g_uiSprites.topRight);
        debugf("  bottomLeft=%p bottomRight=%p\n", (void*)g_uiSprites.bottomLeft, (void*)g_uiSprites.bottomRight);
        return;  // Abort drawing to prevent crash
    }

    // Draw sprite borders with proper alpha blending
    // This blends the sprite over the background, preserving bg where sprite is transparent
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    // Draw corners first (except bottom-right which is drawn last)
    if (g_uiSprites.topLeft) rdpq_sprite_blit(g_uiSprites.topLeft, x, y, NULL);
    if (g_uiSprites.topRight) rdpq_sprite_blit(g_uiSprites.topRight, x + w - UI_BORDER_SIZE, y, NULL);
    if (g_uiSprites.bottomLeft) rdpq_sprite_blit(g_uiSprites.bottomLeft, x, y + h - UI_BORDER_SIZE, NULL);

    // Draw tiled edges
    // Top edge
    if (g_uiSprites.topCenter) {
        for (int tx = x + UI_BORDER_SIZE; tx < x + w - UI_BORDER_SIZE; tx += UI_BORDER_SIZE) {
            rdpq_sprite_blit(g_uiSprites.topCenter, tx, y, NULL);
        }
    }

    // Bottom edge
    if (g_uiSprites.bottomCenter) {
        for (int tx = x + UI_BORDER_SIZE; tx < x + w - UI_BORDER_SIZE; tx += UI_BORDER_SIZE) {
            rdpq_sprite_blit(g_uiSprites.bottomCenter, tx, y + h - UI_BORDER_SIZE, NULL);
        }
    }

    // Left edge
    if (g_uiSprites.leftCenter) {
        for (int ty = y + UI_BORDER_SIZE; ty < y + h - UI_BORDER_SIZE; ty += UI_BORDER_SIZE) {
            rdpq_sprite_blit(g_uiSprites.leftCenter, x, ty, NULL);
        }
    }

    // Right edge
    if (g_uiSprites.rightCenter) {
        for (int ty = y + UI_BORDER_SIZE; ty < y + h - UI_BORDER_SIZE; ty += UI_BORDER_SIZE) {
            rdpq_sprite_blit(g_uiSprites.rightCenter, x + w - UI_BORDER_SIZE, ty, NULL);
        }
    }

    // Draw bottom-right corner LAST so it covers any overlapping edge tiles
    if (g_uiSprites.bottomRight) rdpq_sprite_blit(g_uiSprites.bottomRight, x + w - UI_BORDER_SIZE, y + h - UI_BORDER_SIZE, NULL);
}

// Helper: Simple word wrap - returns number of lines drawn
static inline int ui_draw_text_wrapped(const char* text, int maxChars, int x, int y, int maxWidth, int lineHeight) {
    char buffer[UI_MAX_DIALOGUE_LENGTH];
    int textLen = (int)strlen(text);
    int len = maxChars < textLen ? maxChars : textLen;

    // Copy visible portion
    for (int i = 0; i < len && i < UI_MAX_DIALOGUE_LENGTH - 1; i++) {
        buffer[i] = text[i];
    }
    buffer[len] = '\0';

    // Simple word wrap: find characters per line based on width
    // Debug font is ~8 pixels per character
    int charsPerLine = maxWidth / 8;
    if (charsPerLine < 1) charsPerLine = 1;

    int line = 0;
    int pos = 0;
    char lineBuffer[128];

    while (pos < len) {
        // Find how many chars fit on this line
        int lineLen = 0;
        int lastSpace = -1;

        while (pos + lineLen < len && lineLen < charsPerLine) {
            if (buffer[pos + lineLen] == ' ') {
                lastSpace = lineLen;
            }
            if (buffer[pos + lineLen] == '\n') {
                break;
            }
            lineLen++;
        }

        // If we're not at end and didn't hit newline, try to break at space
        if (pos + lineLen < len && buffer[pos + lineLen] != '\n' && lastSpace > 0) {
            lineLen = lastSpace;
        }

        // Copy line to buffer and draw
        strncpy(lineBuffer, buffer + pos, lineLen);
        lineBuffer[lineLen] = '\0';

        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y + line * lineHeight, "%s", lineBuffer);

        pos += lineLen;
        // Skip space or newline
        if (pos < len && (buffer[pos] == ' ' || buffer[pos] == '\n')) {
            pos++;
        }
        line++;
    }

    return line;
}

// Draw dialogue box
static inline void dialogue_draw(DialogueBox* dlg) {
    if (!dlg->active) return;
    if (dlg->scale <= 0.01f) return;  // Don't draw if too small

    // Sanitize scale to prevent denormal float exceptions
    float scale = ui_sanitize_float(dlg->scale);

    // Calculate scaled main box dimensions (scale from center)
    int scaledW = (int)(dlg->width * scale);
    int scaledH = (int)(dlg->height * scale);
    int centerX = dlg->x + dlg->width / 2;
    int centerY = dlg->y + dlg->height / 2;
    int drawX = centerX - scaledW / 2;
    int drawY = centerY - scaledH / 2;

    // Speaker name box dimensions (also scaled)
    int speakerBoxHeight = (int)(48 * scale);
    int speakerBoxWidth = 120;
    int speakerGap = (int)(4 * scale);

    // Draw speaker name box if present
    if (dlg->speaker[0] != '\0') {
        // Calculate width based on speaker name length (~8px per char + tight padding)
        int nameLen = strlen(dlg->speaker);
        speakerBoxWidth = (int)(((nameLen * 8) + UI_BORDER_SIZE * 2) * scale);
        int minWidth = (int)(64 * scale);
        if (speakerBoxWidth < minWidth) speakerBoxWidth = minWidth;

        // Position speaker box above the scaled dialogue box
        int speakerBoxX = drawX;
        int speakerBoxY = drawY - speakerBoxHeight - speakerGap;

        ui_draw_box(speakerBoxX, speakerBoxY, speakerBoxWidth, speakerBoxHeight, UI_COLOR_BG, UI_COLOR_BORDER);

        // Only draw text if scale is large enough to be readable
        if (scale >= 0.5f) {
            // Draw speaker name centered in the box
            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_set_prim_color(UI_COLOR_TEXT);

            int scaledBorder = (int)(UI_BORDER_SIZE * scale);
            int textAreaX = speakerBoxX + scaledBorder;
            int textAreaW = speakerBoxWidth - scaledBorder * 2;
            int nameX = textAreaX + (textAreaW - nameLen * 8) / 2;
            int nameY = speakerBoxY + (speakerBoxHeight / 2) + 2;
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, nameX, nameY, "%s", dlg->speaker);
        }
    }

    // Draw main dialogue box
    ui_draw_box(drawX, drawY, scaledW, scaledH, UI_COLOR_BG, UI_COLOR_BORDER);

    // Only draw text content if scale is large enough to be readable
    if (scale < 0.5f) return;

    int scaledPadding = (int)(dlg->padding * scale);
    int textX = drawX + scaledPadding + (int)(9 * scale);
    int textY = drawY + scaledPadding + (int)(13 * scale);
    int textWidth = scaledW - scaledPadding * 2 - (int)(4 * scale);

    // Draw text with word wrap
    rdpq_set_prim_color(UI_COLOR_TEXT);
    ui_draw_text_wrapped(dlg->text, dlg->charIndex, textX, textY, textWidth, dlg->lineHeight);

    // Draw "press A" indicator when complete (using button font)
    if (dlg->complete && dlg->blinkTimer < UI_BLINK_RATE) {
        int indicatorX = drawX + scaledW - (int)(27 * scale);
        int indicatorY = drawY + scaledH - (int)(18 * scale);
        rdpq_text_printf(NULL, 2, indicatorX, indicatorY, "a");  // lowercase 'a' = A button icon
    }
}

// ============================================================
// OPTION PROMPT
// ============================================================

typedef void (*OptionCallback)(int selectedIndex);

typedef struct {
    // Content
    char title[64];
    char options[UI_MAX_OPTIONS][UI_MAX_OPTION_LENGTH];
    int optionCount;

    // State
    bool active;
    int selectedIndex;
    int blinkTimer;

    // Layout
    int x, y;
    int width, height;
    int padding;
    int itemSpacing;
    bool centered;                  // Center on screen

    // Animation (uses shared UIAnimState enum)
    UIAnimState animState;
    float animTime;                 // 0.0 to 1.0 progress
    float scale;                    // Current scale (0.0 to 1.0+)

    // Sound callbacks
    UISoundCallback onOpenSound;
    UISoundCallback onCloseSound;
    UISoundCallback onSelectSound;
    UISoundCallback onMoveSound;

    // Callback
    OptionCallback onSelect;
    OptionCallback onCancel;        // Called when B is pressed (optional)

    // Left/Right callback for value adjustment (direction: -1 = left, +1 = right)
    // Receives: (selectedIndex * 100) + (direction > 0 ? 1 : 0)
    // Use option_decode_leftright() to extract index and direction
    OptionCallback onLeftRight;

    // Behavior flags
    bool stayOpenOnSelect;          // If true, A button calls onSelect but doesn't close menu
    bool suppressSelectSound;       // If true, don't play the default select sound on A

    // Pending callback (called after close animation finishes)
    OptionCallback pendingCallback;
    int pendingSelection;

    // Stick input state (for throttling)
    bool stickHeldUp;
    bool stickHeldDown;
    bool stickHeldLeft;
    bool stickHeldRight;
} OptionPrompt;

// Helper to decode left/right callback value
// Returns the selected index, sets direction to -1 (left) or +1 (right)
static inline int option_decode_leftright(int value, int* direction) {
    int index = value / 100;
    *direction = (value % 100) ? 1 : -1;
    return index;
}

// Initialize option prompt with defaults
static inline void option_init(OptionPrompt* opt) {
    memset(opt, 0, sizeof(OptionPrompt));
    opt->x = 80;
    opt->y = 80;
    opt->width = 160;
    opt->padding = 15;
    opt->itemSpacing = 20;
    opt->centered = true;
    opt->animState = UI_ANIM_NONE;
    opt->animTime = 0.0f;
    opt->scale = 1.0f;
    opt->onMoveSound = ui_play_hover_sound;  // Default hover sound
}

// Set sound callbacks for UI feedback
static inline void option_set_sounds(OptionPrompt* opt,
    UISoundCallback onOpen, UISoundCallback onClose,
    UISoundCallback onSelect, UISoundCallback onMove) {
    opt->onOpenSound = onOpen;
    opt->onCloseSound = onClose;
    opt->onSelectSound = onSelect;
    opt->onMoveSound = onMove;
}

// Clear and set title
static inline void option_set_title(OptionPrompt* opt, const char* title) {
    // Reset animation state to prevent stale denormal values from previous menu
    opt->animState = UI_ANIM_NONE;
    opt->animTime = 0.0f;
    opt->scale = 1.0f;

    int titleLen = strlen(title);
    for (int i = 0; i < titleLen && i < 63; i++) {
        opt->title[i] = title[i];
    }
    opt->title[titleLen < 63 ? titleLen : 63] = '\0';
    opt->optionCount = 0;
}

// Add an option
static inline void option_add(OptionPrompt* opt, const char* label) {
    if (opt->optionCount >= UI_MAX_OPTIONS) return;
    int labelLen = strlen(label);
    for (int i = 0; i < labelLen && i < UI_MAX_OPTION_LENGTH - 1; i++) {
        opt->options[opt->optionCount][i] = label[i];
    }
    opt->options[opt->optionCount][labelLen < UI_MAX_OPTION_LENGTH - 1 ? labelLen : UI_MAX_OPTION_LENGTH - 1] = '\0';
    opt->optionCount++;
}

// Set left/right callback for value adjustment (call before option_show)
static inline void option_set_leftright(OptionPrompt* opt, OptionCallback onLeftRight) {
    opt->onLeftRight = onLeftRight;
}

// Show the prompt
static inline void option_show(OptionPrompt* opt, OptionCallback onSelect, OptionCallback onCancel) {
    // Don't restart if already closing (wait for animation to complete)
    if (opt->animState == UI_ANIM_CLOSING) {
        return;
    }

    opt->active = true;
    opt->selectedIndex = 0;
    opt->blinkTimer = 0;
    opt->onSelect = onSelect;
    opt->onCancel = onCancel;
    opt->pendingCallback = NULL;  // Clear any pending callback
    // Note: onLeftRight is preserved from option_set_leftright() call

    // Start opening animation
    opt->animState = UI_ANIM_OPENING;
    opt->animTime = 0.0f;
    opt->scale = 0.0f;

    // Play open sound
    ui_play_ui_open_sound();
    if (opt->onOpenSound) opt->onOpenSound();

    // Calculate height based on content
    int titleHeight = opt->title[0] ? 25 : 0;
    opt->height = opt->padding * 2 + titleHeight + opt->optionCount * opt->itemSpacing;

    // Center if requested
    if (opt->centered) {
        opt->x = (UI_SCREEN_WIDTH - opt->width) / 2;
        opt->y = (UI_SCREEN_HEIGHT - opt->height) / 2;
    }
}

// Close prompt (starts closing animation)
static inline void option_close(OptionPrompt* opt) {
    opt->animState = UI_ANIM_CLOSING;
    opt->animTime = 0.0f;
    opt->scale = 1.0f;  // Start at full scale for close animation
    // Play close sound
    if (opt->onCloseSound) opt->onCloseSound();
}

// Check if fully closed (animation complete)
static inline bool option_is_active(OptionPrompt* opt) {
    return opt->active;
}

static inline int option_get_selected(OptionPrompt* opt) {
    return opt->selectedIndex;
}

// Update option prompt - returns true if consumed input
static inline bool option_update(OptionPrompt* opt, joypad_port_t port) {
    if (!opt->active) return false;

    opt->blinkTimer++;
    if (opt->blinkTimer >= UI_BLINK_RATE * 2) {
        opt->blinkTimer = 0;
    }

    // Update animation
    if (opt->animState == UI_ANIM_OPENING) {
        opt->animTime += UI_ANIM_SPEED;
        if (opt->animTime >= 1.0f) {
            opt->animTime = 1.0f;
            opt->animState = UI_ANIM_IDLE;
        }
        opt->scale = ui_ease_out_back(opt->animTime);
    } else if (opt->animState == UI_ANIM_CLOSING) {
        opt->animTime += UI_ANIM_SPEED * 1.5f;  // Close slightly faster
        if (opt->animTime >= 1.0f) {
            opt->animTime = 1.0f;
            opt->animState = UI_ANIM_NONE;
            opt->active = false;  // Actually close now
            // Call pending callback after animation finishes
            if (opt->pendingCallback) {
                OptionCallback cb = opt->pendingCallback;
                int sel = opt->pendingSelection;
                opt->pendingCallback = NULL;
                cb(sel);
            }
            return false;
        }
        opt->scale = 1.0f - ui_ease_in_back(opt->animTime);
        if (opt->scale < 0.0f) opt->scale = 0.0f;
    }

    // Don't process input while animating open/close
    if (opt->animState != UI_ANIM_IDLE) {
        return true;
    }

    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);
    joypad_inputs_t inputs = joypad_get_inputs(port);

    // Track stick positions
    bool stickUp = inputs.stick_y > 50;
    bool stickDown = inputs.stick_y < -50;
    bool stickLeft = inputs.stick_x < -50;
    bool stickRight = inputs.stick_x > 50;

    // Navigate up
    if (pressed.d_up || (stickUp && !opt->stickHeldUp)) {
        opt->selectedIndex--;
        if (opt->selectedIndex < 0) {
            opt->selectedIndex = opt->optionCount - 1;
        }
        if (opt->onMoveSound) opt->onMoveSound();
    }
    opt->stickHeldUp = stickUp;

    // Navigate down
    if (pressed.d_down || (stickDown && !opt->stickHeldDown)) {
        opt->selectedIndex++;
        if (opt->selectedIndex >= opt->optionCount) {
            opt->selectedIndex = 0;
        }
        if (opt->onMoveSound) opt->onMoveSound();
    }
    opt->stickHeldDown = stickDown;

    // Left/Right value adjustment (if callback is set)
    if (opt->onLeftRight) {
        if (pressed.d_left || (stickLeft && !opt->stickHeldLeft)) {
            // Encode index and direction: index * 100 + 0 for left
            int encoded = opt->selectedIndex * 100 + 0;
            opt->onLeftRight(encoded);
            if (opt->onMoveSound) opt->onMoveSound();
        }
        opt->stickHeldLeft = stickLeft;

        if (pressed.d_right || (stickRight && !opt->stickHeldRight)) {
            // Encode index and direction: index * 100 + 1 for right
            int encoded = opt->selectedIndex * 100 + 1;
            opt->onLeftRight(encoded);
            if (opt->onMoveSound) opt->onMoveSound();
        }
        opt->stickHeldRight = stickRight;
    }

    // Select (A button only - START is used to open/close menus)
    if (pressed.a) {
        if (!opt->suppressSelectSound) {
            ui_play_press_a_sound();
            if (opt->onSelectSound) opt->onSelectSound();
        }

        if (opt->stayOpenOnSelect) {
            // Call callback immediately without closing
            if (opt->onSelect) {
                opt->onSelect(opt->selectedIndex);
            }
        } else {
            // Store callback to be called after close animation
            opt->pendingCallback = opt->onSelect;
            opt->pendingSelection = opt->selectedIndex;
            option_close(opt);  // Start closing animation
        }
        return true;
    }

    // Cancel
    if (pressed.b) {
        ui_play_press_a_sound();
        if (opt->onSelectSound) opt->onSelectSound();
        // Store callback to be called after close animation
        opt->pendingCallback = opt->onCancel;
        opt->pendingSelection = opt->selectedIndex;
        option_close(opt);  // Start closing animation
        return true;
    }

    return true; // Prompt is active, consume input
}

// Draw option prompt
static inline void option_draw(OptionPrompt* opt) {
    if (!opt->active) return;
    if (opt->scale <= 0.01f) return;  // Don't draw if too small

    // DEBUG: Log first time we draw an option prompt
    static bool firstOptionDraw = true;
    if (firstOptionDraw) {
        debugf("First option_draw: active=%d w=%d h=%d x=%d y=%d scale=%.2f cnt=%d\n",
               opt->active, opt->width, opt->height, opt->x, opt->y, opt->scale, opt->optionCount);
        firstOptionDraw = false;
    }

    // Validate - but be more lenient (height could be small for few options)
    if (opt->width <= 0 || opt->width > 1000 ||
        opt->height < 0 || opt->height > 1000 ||  // Allow height=0 edge case
        opt->scale < 0.0f || opt->scale > 10.0f ||
        opt->optionCount < 0 || opt->optionCount > 20) {
        debugf("ERROR: OptionPrompt has garbage values!\n");
        debugf("  active=%d width=%d height=%d x=%d y=%d scale=%.2f\n",
               opt->active, opt->width, opt->height, opt->x, opt->y, opt->scale);
        debugf("  optionCount=%d padding=%d itemSpacing=%d\n",
               opt->optionCount, opt->padding, opt->itemSpacing);
        opt->active = false;  // Force deactivate to prevent crash
        return;
    }

    // Calculate scaled dimensions and position (scale from center)
    // Sanitize scale to prevent denormal float exceptions
    float scale = ui_sanitize_float(opt->scale);
    int scaledW = (int)(opt->width * scale);
    int scaledH = (int)(opt->height * scale);
    int centerX = opt->x + opt->width / 2;
    int centerY = opt->y + opt->height / 2;
    int drawX = centerX - scaledW / 2;
    int drawY = centerY - scaledH / 2;

    // Draw box with scaled size
    ui_draw_box(drawX, drawY, scaledW, scaledH, UI_COLOR_BG, UI_COLOR_BORDER);

    // Only draw text content if scale is large enough to be readable
    if (scale < 0.5f) return;

    // Validate draw positions are on screen (prevent overflow in text rendering)
    if (drawX < -500 || drawX > 500 || drawY < -500 || drawY > 500 ||
        scaledW < 0 || scaledW > 500 || scaledH < 0 || scaledH > 500) {
        debugf("ERROR: option_draw bad positions: drawX=%d drawY=%d scaledW=%d scaledH=%d\n",
               drawX, drawY, scaledW, scaledH);
        return;
    }

    // Calculate text positions relative to scaled box
    int textY = drawY + (int)(opt->padding * scale) + (int)(10 * scale);

    // Small right offset to visually center text in box
    int centerOffset = 12;

    // Draw title
    if (opt->title[0] != '\0') {
        rdpq_set_prim_color(UI_COLOR_TEXT);
        // Center title
        int titleLen = strlen(opt->title);
        int titleX = drawX + (scaledW - titleLen * 8) / 2 + centerOffset;
        // Clamp text positions to prevent overflow in libdragon text rendering
        if (titleX < -100) titleX = -100;
        if (titleX > 400) titleX = 400;
        if (textY < -100) textY = -100;
        if (textY > 300) textY = 300;
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, titleX, textY, "%s", opt->title);
        textY += (int)(25 * scale);
    }

    // Draw options
    int scaledSpacing = (int)(opt->itemSpacing * scale);
    for (int i = 0; i < opt->optionCount; i++) {
        int itemY = textY + i * scaledSpacing;

        // Highlight selected
        if (i == opt->selectedIndex) {
            rdpq_set_prim_color(UI_COLOR_HIGHLIGHT);
            rdpq_fill_rectangle(drawX + 4, itemY - 2,
                               drawX + scaledW - 4, itemY + 12);
        }

        // Draw option text (centered like title)
        rdpq_set_prim_color(UI_COLOR_TEXT);
        const char* prefix = (i == opt->selectedIndex) ? "> " : "  ";
        int optLen = strlen(prefix) + strlen(opt->options[i]);
        int optX = drawX + (scaledW - optLen * 8) / 2 + centerOffset;
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, optX, itemY, "%s%s", prefix, opt->options[i]);
    }
}

// ============================================================
// DIALOGUE SCRIPT SYSTEM
// ============================================================
// Allows defining dialogue sequences with branching based on options

// Forward declarations for script system
typedef struct DialogueScript DialogueScript;
typedef struct DialogueScriptNode DialogueScriptNode;

// Node types
typedef enum {
    SCRIPT_NODE_DIALOGUE,       // Show dialogue text
    SCRIPT_NODE_OPTIONS,        // Show option prompt
    SCRIPT_NODE_END,            // End of script
} ScriptNodeType;

// Option definition for branching
typedef struct {
    char label[UI_MAX_OPTION_LENGTH];
    int nextNode;               // Node index to jump to (-1 = end script)
} ScriptOption;

// A single node in the dialogue script
struct DialogueScriptNode {
    ScriptNodeType type;

    // For DIALOGUE nodes
    char text[UI_MAX_DIALOGUE_LENGTH];
    char speaker[32];
    int nextNode;               // Next node after this dialogue (-1 = end)

    // For OPTIONS nodes
    char optionTitle[64];
    ScriptOption options[UI_MAX_OPTIONS];
    int optionCount;
};

// The complete dialogue script
struct DialogueScript {
    DialogueScriptNode nodes[UI_MAX_SCRIPT_NODES];
    int nodeCount;
    int currentNode;
    bool active;
    bool waitingForOption;      // True when showing options
    int triggerId;              // ID of the trigger that started this (for tracking)
};

// Initialize a script
static inline void script_init(DialogueScript* script) {
    memset(script, 0, sizeof(DialogueScript));
    script->currentNode = -1;
}

// Add a dialogue node, returns node index
static inline int script_add_dialogue(DialogueScript* script, const char* text, const char* speaker, int nextNode) {
    if (script->nodeCount >= UI_MAX_SCRIPT_NODES) return -1;

    int idx = script->nodeCount++;
    DialogueScriptNode* node = &script->nodes[idx];
    node->type = SCRIPT_NODE_DIALOGUE;
    node->nextNode = nextNode;

    int textLen = strlen(text);
    for (int i = 0; i < textLen && i < UI_MAX_DIALOGUE_LENGTH - 1; i++) {
        node->text[i] = text[i];
    }
    node->text[textLen < UI_MAX_DIALOGUE_LENGTH ? textLen : UI_MAX_DIALOGUE_LENGTH - 1] = '\0';

    if (speaker) {
        int speakerLen = strlen(speaker);
        for (int i = 0; i < speakerLen && i < 31; i++) {
            node->speaker[i] = speaker[i];
        }
        node->speaker[speakerLen < 31 ? speakerLen : 31] = '\0';
    } else {
        node->speaker[0] = '\0';
    }

    return idx;
}

// Add an options node, returns node index
static inline int script_add_options(DialogueScript* script, const char* title) {
    if (script->nodeCount >= UI_MAX_SCRIPT_NODES) return -1;

    int idx = script->nodeCount++;
    DialogueScriptNode* node = &script->nodes[idx];
    node->type = SCRIPT_NODE_OPTIONS;
    node->optionCount = 0;

    if (title) {
        int titleLen = strlen(title);
        for (int i = 0; i < titleLen && i < 63; i++) {
            node->optionTitle[i] = title[i];
        }
        node->optionTitle[titleLen < 63 ? titleLen : 63] = '\0';
    } else {
        node->optionTitle[0] = '\0';
    }

    return idx;
}

// Add option to an options node
static inline void script_node_add_option(DialogueScript* script, int nodeIdx, const char* label, int nextNode) {
    if (nodeIdx < 0 || nodeIdx >= script->nodeCount) return;
    DialogueScriptNode* node = &script->nodes[nodeIdx];
    if (node->type != SCRIPT_NODE_OPTIONS) return;
    if (node->optionCount >= UI_MAX_OPTIONS) return;

    ScriptOption* opt = &node->options[node->optionCount++];
    int labelLen = strlen(label);
    for (int i = 0; i < labelLen && i < UI_MAX_OPTION_LENGTH - 1; i++) {
        opt->label[i] = label[i];
    }
    opt->label[labelLen < UI_MAX_OPTION_LENGTH ? labelLen : UI_MAX_OPTION_LENGTH - 1] = '\0';
    opt->nextNode = nextNode;
}

// Add end node
static inline int script_add_end(DialogueScript* script) {
    if (script->nodeCount >= UI_MAX_SCRIPT_NODES) return -1;
    int idx = script->nodeCount++;
    script->nodes[idx].type = SCRIPT_NODE_END;
    return idx;
}

// Start running a script
static inline void script_start(DialogueScript* script, DialogueBox* dlg, OptionPrompt* opt) {
    script->active = true;
    script->currentNode = 0;
    script->waitingForOption = false;

    // Process first node
    if (script->nodeCount > 0) {
        DialogueScriptNode* node = &script->nodes[0];
        if (node->type == SCRIPT_NODE_DIALOGUE) {
            dialogue_show(dlg, node->text, node->speaker[0] ? node->speaker : NULL);
        } else if (node->type == SCRIPT_NODE_OPTIONS) {
            option_set_title(opt, node->optionTitle);
            for (int i = 0; i < node->optionCount; i++) {
                option_add(opt, node->options[i].label);
            }
            option_show(opt, NULL, NULL);
            script->waitingForOption = true;
        } else if (node->type == SCRIPT_NODE_END) {
            script->active = false;
        }
    }
}

// Advance script to next node (call when dialogue dismissed or option selected)
static inline void script_advance(DialogueScript* script, DialogueBox* dlg, OptionPrompt* opt, int optionSelected) {
    if (!script->active || script->currentNode < 0) return;

    DialogueScriptNode* current = &script->nodes[script->currentNode];
    int nextIdx = -1;

    if (current->type == SCRIPT_NODE_DIALOGUE) {
        nextIdx = current->nextNode;
    } else if (current->type == SCRIPT_NODE_OPTIONS) {
        if (optionSelected >= 0 && optionSelected < current->optionCount) {
            nextIdx = current->options[optionSelected].nextNode;
        }
        script->waitingForOption = false;
    }

    // Move to next node
    if (nextIdx < 0 || nextIdx >= script->nodeCount) {
        script->active = false;
        script->currentNode = -1;
        return;
    }

    script->currentNode = nextIdx;
    DialogueScriptNode* next = &script->nodes[nextIdx];

    if (next->type == SCRIPT_NODE_DIALOGUE) {
        dialogue_show(dlg, next->text, next->speaker[0] ? next->speaker : NULL);
    } else if (next->type == SCRIPT_NODE_OPTIONS) {
        option_set_title(opt, next->optionTitle);
        for (int i = 0; i < next->optionCount; i++) {
            option_add(opt, next->options[i].label);
        }
        option_show(opt, NULL, NULL);
        script->waitingForOption = true;
    } else if (next->type == SCRIPT_NODE_END) {
        script->active = false;
        script->currentNode = -1;
    }
}

// Check if script is active
static inline bool script_is_active(DialogueScript* script) {
    return script->active;
}

// Check if script is waiting for option selection
static inline bool script_is_waiting_for_option(DialogueScript* script) {
    return script->active && script->waitingForOption;
}

// ============================================================
// CONVENIENCE FUNCTIONS
// ============================================================

// Quick dialogue - show a simple message
static inline void ui_show_message(DialogueBox* dlg, const char* message) {
    dialogue_show_text(dlg, message);
}

// Quick yes/no prompt
static inline void ui_show_yes_no(OptionPrompt* opt, const char* question,
                                   OptionCallback onSelect, OptionCallback onCancel) {
    option_set_title(opt, question);
    option_add(opt, "Yes");
    option_add(opt, "No");
    option_show(opt, onSelect, onCancel);
}

// Quick confirm prompt
static inline void ui_show_confirm(OptionPrompt* opt, const char* message,
                                    OptionCallback onConfirm) {
    option_set_title(opt, message);
    option_add(opt, "OK");
    option_show(opt, onConfirm, NULL);
}

// ============================================================
// LEVEL BANNER - Shows level name on entry
// ============================================================

#define LEVEL_BANNER_MAX_LENGTH 32
#define LEVEL_BANNER_DISPLAY_TIME 3.0f   // Seconds to display
#define LEVEL_BANNER_ANIM_TIME 0.4f      // Seconds for slide animation
#define LEVEL_BANNER_PADDING 6
#define LEVEL_BANNER_HEIGHT 14
#define LEVEL_BANNER_BOTTOM_MARGIN 4     // Distance from bottom of screen when visible

typedef enum {
    BANNER_STATE_HIDDEN,
    BANNER_STATE_SLIDING_IN,
    BANNER_STATE_VISIBLE,
    BANNER_STATE_SLIDING_OUT,
    BANNER_STATE_PAUSED,         // Raised up when game is paused
} LevelBannerState;

typedef struct {
    char text[LEVEL_BANNER_MAX_LENGTH];
    LevelBannerState state;
    float animTime;              // Animation progress (0-1)
    float displayTimer;          // Time remaining to display
    float bannerY;               // Current Y position
    int width;                   // Calculated width based on text
    int height;                  // Banner height
} LevelBanner;

// Initialize level banner
static inline void level_banner_init(LevelBanner* banner) {
    memset(banner, 0, sizeof(LevelBanner));
    banner->state = BANNER_STATE_HIDDEN;
    banner->height = LEVEL_BANNER_HEIGHT + LEVEL_BANNER_PADDING * 2;
    banner->bannerY = UI_SCREEN_HEIGHT + 10;  // Start off screen below
}

// Show level banner with text
static inline void level_banner_show(LevelBanner* banner, const char* levelName) {
    ui_fpu_flush_denormals();  // Prevent FPU exceptions
    strncpy(banner->text, levelName, LEVEL_BANNER_MAX_LENGTH - 1);
    banner->text[LEVEL_BANNER_MAX_LENGTH - 1] = '\0';

    // Calculate width based on text length (debug font is 8 pixels wide)
    int textLen = strlen(banner->text);
    banner->width = textLen * 8 + LEVEL_BANNER_PADDING * 4;
    if (banner->width < 60) banner->width = 60;  // Minimum width

    banner->height = LEVEL_BANNER_HEIGHT + LEVEL_BANNER_PADDING * 2;
    banner->state = BANNER_STATE_SLIDING_IN;
    banner->animTime = 0.0f;
    banner->displayTimer = LEVEL_BANNER_DISPLAY_TIME;
    banner->bannerY = UI_SCREEN_HEIGHT + 10;  // Start off screen below
}

// Hide the banner (slide out)
static inline void level_banner_hide(LevelBanner* banner) {
    if (banner->state == BANNER_STATE_VISIBLE ||
        banner->state == BANNER_STATE_SLIDING_IN ||
        banner->state == BANNER_STATE_PAUSED) {
        banner->state = BANNER_STATE_SLIDING_OUT;
        banner->animTime = 0.0f;
    }
}

// Show banner in paused position (always shows when pausing)
static inline void level_banner_pause(LevelBanner* banner) {
    // Always show banner when pausing, even if it was hidden
    if (banner->text[0] != '\0') {
        banner->state = BANNER_STATE_PAUSED;
        banner->animTime = 0.0f;  // Reset animation to slide up from bottom
        banner->bannerY = UI_SCREEN_HEIGHT + 10;  // Start below screen
    }
}

// Hide banner when unpaused
static inline void level_banner_unpause(LevelBanner* banner) {
    // Slide out from paused state or visible state
    if (banner->state == BANNER_STATE_PAUSED || banner->state == BANNER_STATE_VISIBLE) {
        banner->state = BANNER_STATE_SLIDING_OUT;
        banner->animTime = 0.0f;
    }
}

// Update banner animation
static inline void level_banner_update(LevelBanner* banner, float deltaTime) {
    ui_fpu_flush_denormals();  // Prevent FPU exceptions from float ops
    // Y position when visible (near bottom of screen)
    float visibleY = UI_SCREEN_HEIGHT - banner->height - LEVEL_BANNER_BOTTOM_MARGIN;
    // Y position when hidden (below screen)
    float hiddenY = UI_SCREEN_HEIGHT + 10;

    switch (banner->state) {
        case BANNER_STATE_HIDDEN:
            banner->bannerY = hiddenY;
            break;

        case BANNER_STATE_SLIDING_IN: {
            banner->animTime += deltaTime / LEVEL_BANNER_ANIM_TIME;
            if (banner->animTime >= 1.0f) {
                banner->animTime = 1.0f;
                banner->state = BANNER_STATE_VISIBLE;
            }
            // Ease out for smooth arrival (sliding UP from below)
            float t = ui_ease_out_back(banner->animTime);
            banner->bannerY = hiddenY + (visibleY - hiddenY) * t;
            banner->bannerY = ui_sanitize_float(banner->bannerY);  // Prevent denormals
            break;
        }

        case BANNER_STATE_VISIBLE:
            banner->bannerY = ui_sanitize_float(visibleY);  // Prevent denormals
            banner->displayTimer -= deltaTime;
            if (banner->displayTimer <= 0.0f) {
                banner->state = BANNER_STATE_SLIDING_OUT;
                banner->animTime = 0.0f;
            }
            break;

        case BANNER_STATE_SLIDING_OUT: {
            banner->animTime += deltaTime / LEVEL_BANNER_ANIM_TIME;
            if (banner->animTime >= 1.0f) {
                banner->animTime = 1.0f;
                banner->state = BANNER_STATE_HIDDEN;
            }
            // Ease in for accelerating exit (sliding DOWN off screen)
            float t = ui_ease_in_back(banner->animTime);
            banner->bannerY = visibleY + (hiddenY - visibleY) * t;
            banner->bannerY = ui_sanitize_float(banner->bannerY);  // Prevent denormals
            break;
        }

        case BANNER_STATE_PAUSED:
            // Animate sliding UP from below to visible position
            banner->animTime += deltaTime / LEVEL_BANNER_ANIM_TIME;
            if (banner->animTime >= 1.0f) {
                banner->animTime = 1.0f;
            }
            float t = ui_ease_out_back(banner->animTime);
            // Animate from hidden to visible position
            float startY = hiddenY;
            banner->bannerY = startY + (visibleY - startY) * t;
            banner->bannerY = ui_sanitize_float(banner->bannerY);  // Prevent denormals
            break;
    }
}

// Draw level banner
static inline void level_banner_draw(LevelBanner* banner) {
    ui_fpu_flush_denormals();  // Prevent FPU exceptions
    if (banner->state == BANNER_STATE_HIDDEN) return;

    // Sanitize float before cast to prevent denormal FPU exception
    float safeY = ui_sanitize_float(banner->bannerY);
    int bannerY = (int)safeY;
    int bannerX = (UI_SCREEN_WIDTH - banner->width) / 2;

    // Don't draw if completely off screen
    if (bannerY >= UI_SCREEN_HEIGHT) return;

    // Draw box using UI system (with proper sprites)
    ui_draw_box(bannerX, bannerY, banner->width, banner->height, UI_COLOR_BG, UI_COLOR_BORDER);

    // Draw text centered (debug font is 8x8 pixels)
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(UI_COLOR_TEXT);

    // Calculate text position (centered in box)
    int textLen = strlen(banner->text);
    int textWidth = textLen * 8;  // Debug font is 8 pixels wide
    int textX = bannerX + (banner->width - textWidth) / 2;
    int textY = bannerY + banner->height / 2 + 4;

    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, textX, textY, "%s", banner->text);
}

// Check if banner is active (visible or animating)
static inline bool level_banner_is_active(LevelBanner* banner) {
    return banner->state != BANNER_STATE_HIDDEN;
}

#endif // UI_H
