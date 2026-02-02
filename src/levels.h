#ifndef LEVELS_H
#define LEVELS_H

// ============================================================
// LEVEL SYSTEM
// ============================================================
// This file includes the auto-generated level registry.
// To add a new level:
// 1. Create src/levels/levelN.h (where N is 1-12)
// 2. Define a LevelData structure in that file
// 3. Run make - the level will be auto-registered
// ============================================================

#include "levels_generated.h"

// ============================================================
// LEVEL API HELPERS
// ============================================================

// Get level name by ID
static inline const char* get_level_name(int levelId) {
    if (levelId < 0 || levelId >= LEVEL_COUNT) return "Unknown";
    return ALL_LEVELS[levelId]->name;
}

// Get level name with number prefix (e.g. "Level 1: Playground")
// Returns static buffer - not thread-safe, copy if needed
static inline const char* get_level_name_with_number(int levelId) {
    static char buffer[64];
    if (levelId < 0 || levelId >= LEVEL_COUNT) {
        return "Unknown";
    }
    snprintf(buffer, sizeof(buffer), "Level %d: %s", levelId, ALL_LEVELS[levelId]->name);
    return buffer;
}

// Get number of bolts in a specific level
static inline int get_level_bolt_count(int levelId) {
    if (levelId < 0 || levelId >= LEVEL_COUNT) return 0;
    return BOLTS_PER_LEVEL[levelId];
}

// Get number of real (non-placeholder) levels
static inline int get_real_level_count(void) {
    return REAL_LEVEL_COUNT;
}

#endif // LEVELS_H
