#ifndef PSYOPS_CODE_H
#define PSYOPS_CODE_H

#include <libdragon.h>
#include <stdbool.h>

// Sequential cheat code: Up, Up, Down, Down, Left, Right, Left, Right, B, A
#define CHEAT_CODE_LENGTH 10
#define CHEAT_TIMEOUT 2.0f  // Reset after 2 seconds of no input

static const int CHEAT_SEQUENCE[CHEAT_CODE_LENGTH] = {
    0,  // D-Up
    0,  // D-Up
    1,  // D-Down
    1,  // D-Down
    2,  // D-Left
    3,  // D-Right
    2,  // D-Left
    3,  // D-Right
    4,  // B
    5,  // A
};

// State storage - define in exactly one .c file with PSYOPS_IMPLEMENTATION
#ifdef PSYOPS_IMPLEMENTATION
bool debugModeUnlocked = false;
bool debugModeActive = false;
int cheatCodeIndex = 0;
float cheatCodeTimeout = 0.0f;
#else
extern bool debugModeUnlocked;
extern bool debugModeActive;
extern int cheatCodeIndex;
extern float cheatCodeTimeout;
#endif

// Convert button press to code index (-1 if not a code button)
static inline int psyops_button_to_code(joypad_buttons_t pressed) {
    if (pressed.d_up)    return 0;
    if (pressed.d_down)  return 1;
    if (pressed.d_left)  return 2;
    if (pressed.d_right) return 3;
    if (pressed.b)       return 4;
    if (pressed.a)       return 5;
    return -1;
}

// Check for cheat code sequence
// Call this every frame with deltaTime
static inline void psyops_check_code(joypad_port_t port, float deltaTime) {
    if (debugModeUnlocked) return;  // Already unlocked

    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);

    // Timeout - reset sequence if too slow
    if (cheatCodeIndex > 0) {
        cheatCodeTimeout += deltaTime;
        if (cheatCodeTimeout > CHEAT_TIMEOUT) {
            cheatCodeIndex = 0;
            cheatCodeTimeout = 0.0f;
        }
    }

    int buttonCode = psyops_button_to_code(pressed);

    if (buttonCode == -1) return;  // No relevant button pressed

    // Check if this matches the next expected button
    if (buttonCode == CHEAT_SEQUENCE[cheatCodeIndex]) {
        cheatCodeIndex++;
        cheatCodeTimeout = 0.0f;

        // Check if code is complete
        if (cheatCodeIndex >= CHEAT_CODE_LENGTH) {
            debugModeUnlocked = true;
            cheatCodeIndex = 0;
            debugf("DEBUG MODE UNLOCKED!\n");
        }
    } else {
        // Wrong button - check if it's the start of the sequence
        if (buttonCode == CHEAT_SEQUENCE[0]) {
            cheatCodeIndex = 1;
            cheatCodeTimeout = 0.0f;
        } else {
            cheatCodeIndex = 0;
            cheatCodeTimeout = 0.0f;
        }
    }
}

// Check for debug mode toggle (D-Pad Up when unlocked)
// Returns true if debug mode state changed
static inline bool psyops_check_debug_toggle(joypad_port_t port) {
    if (!debugModeUnlocked) return false;

    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);

    if (pressed.d_up) {
        debugModeActive = !debugModeActive;
        debugf("Debug mode: %s\n", debugModeActive ? "ON" : "OFF");
        return true;
    }

    return false;
}

// Getters
static inline bool psyops_is_unlocked(void) {
    return debugModeUnlocked;
}

static inline bool psyops_is_debug_active(void) {
    return debugModeActive;
}

// Reset (for testing)
static inline void psyops_reset(void) {
    debugModeUnlocked = false;
    debugModeActive = false;
    cheatCodeIndex = 0;
    cheatCodeTimeout = 0.0f;
}

#endif // PSYOPS_CODE_H
