#ifndef SAVE_H
#define SAVE_H

#include <libdragon.h>
#include <string.h>
#include <stdbool.h>

// ============================================================
// SAVE SYSTEM
// ============================================================
// 3 save files with:
// - Per-level bolt collection tracking
// - Level completion flags
// - Death counter
// - Time played
// - Percentage completion (calculated at runtime)
// ============================================================

#define SAVE_FILE_COUNT 3
#define MAX_BOLTS_PER_LEVEL 32    // Max bolts we track per level
#define MAX_SCREWG_PER_LEVEL 8    // Max golden screws we track per level (rare collectibles)
#define SAVE_MAGIC 0x4E363442     // "N64B" - magic number to verify save validity
#define SAVE_MAX_LEVELS 12        // Must match LEVEL_COUNT in levels_generated.h
#define SETTINGS_MAGIC 0x53455454 // "SETT" - magic for settings header

// Replay system - stored in RAM only (too large for EEPROM)
#define REPLAY_MAX_FRAMES 5400    // 3 minutes at 30fps
#define REPLAY_MAGIC 0x52504C59   // "RPLY"

// Save file structure (fits in EEPROM 4K = 512 bytes)
typedef struct {
    // Validation
    uint32_t magic;              // SAVE_MAGIC if valid
    uint8_t version;             // Save format version
    uint8_t flags;               // Reserved for future use
    uint16_t checksum;           // Simple checksum

    // Per-level data (SAVE_MAX_LEVELS = 12 levels)
    struct {
        uint8_t completed : 1;   // Level beaten
        uint8_t hasReplay : 1;   // Replay data available for this level
        uint8_t reserved : 6;    // Reserved
        uint8_t bestRank;        // Best rank achieved: 0=none, 1=D, 2=C, 3=B, 4=A, 5=S
        uint32_t boltsCollected; // Bitmask of collected bolts (up to 32 per level)
        uint8_t screwgCollected; // Bitmask of collected golden screws (up to 8 per level)
        uint8_t reserved2;       // Padding for alignment
        uint16_t bestTimeSeconds;// Best completion time in seconds (0 = no time recorded)
        uint16_t deathsOnLevel;  // Deaths on this specific level
    } levels[SAVE_MAX_LEVELS];

    // Stats
    uint32_t deathCount;         // Total deaths
    uint32_t playTimeSeconds;    // Total play time in seconds
    uint32_t totalBoltsCollected;// Running total of bolts collected
    uint16_t totalScrewgCollected;// Running total of golden screws collected
    uint16_t reserved3;          // Padding for alignment

    // Current progress
    uint8_t currentLevel;        // Last played level
    uint8_t padding[3];          // Alignment padding

} SaveFile;

// Global settings (stored in EEPROM header, shared across all saves)
typedef struct {
    uint32_t magic;              // SETTINGS_MAGIC if valid
    uint8_t musicVolume;         // 0-10 (0 = mute, 10 = max)
    uint8_t sfxVolume;           // 0-10 (0 = mute, 10 = max)
    uint8_t reserved[6];         // Reserved for future settings
} GlobalSettings;

// Global save state
typedef struct {
    bool initialized;
    bool eepromPresent;
    int activeSaveSlot;          // -1 if no save loaded, 0-2 for active slot
    SaveFile saves[SAVE_FILE_COUNT];
    GlobalSettings settings;     // Global settings (volume, etc.)
} SaveSystem;

// Single global instance (defined in save.c)
extern SaveSystem g_saveSystem;

// Level info callbacks (set by game code to avoid circular dependency)
typedef struct {
    int totalBolts;      // Total bolts across all levels
    int totalScrewg;     // Total golden screws across all levels
    int realLevelCount;  // Number of real (non-placeholder) levels
} SaveLevelInfo;

extern SaveLevelInfo g_saveLevelInfo;

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

// Checksum
uint16_t save_calc_checksum(SaveFile* save);

// Save file operations
void save_init_new(SaveFile* save);
bool save_is_valid(SaveFile* save);

// EEPROM I/O
size_t save_get_eeprom_offset(int slot);
void save_system_init(void);
bool save_write_to_eeprom(int slot);
bool save_delete(int slot);
bool save_create_new(int slot);
bool save_load(int slot);
bool save_slot_has_data(int slot);
SaveFile* save_get_active(void);

// Game integration
void save_collect_bolt(int levelId, int boltIndex);
bool save_is_bolt_collected(int levelId, int boltIndex);
void save_collect_screwg(int levelId, int screwgIndex);
bool save_is_screwg_collected(int levelId, int screwgIndex);
void save_complete_level(int levelId);
void save_increment_deaths(void);
void save_add_play_time(float seconds);
void save_set_current_level(int levelId);
void save_auto_save(void);          // Throttled save (5s minimum between writes)
void save_force_save(void);         // Bypass throttling (use sparingly)
void save_flush_pending(void);      // Flush pending save (call on scene exit)

// Set level info (call once at startup with generated values)
void save_set_level_info(int totalBolts, int totalScrewg, int realLevelCount);

// Utility
void save_format_time(uint32_t seconds, char* buffer, size_t bufSize);
int save_count_collected_bolts(SaveFile* save);
int save_count_completed_levels(SaveFile* save);
int save_calc_percentage(SaveFile* save);
int save_get_total_bolts_collected(void);  // Quick access to bolt count for HUD
int save_get_level_bolts_collected(int levelId);  // Count bolts collected for a specific level
int save_get_total_screwg_collected(void);  // Quick access to golden screw count for HUD
int save_get_level_screwg_collected(int levelId);  // Count golden screws collected for a specific level

// Volume settings (0-10 scale)
void save_set_music_volume(int volume);
void save_set_sfx_volume(int volume);
int save_get_music_volume(void);
int save_get_sfx_volume(void);
void save_apply_volume_settings(void);      // Apply current volume to mixer (call at safe times)
void save_apply_volume_settings_safe(void); // Apply pending volume changes if needed (call before mixer_poll)
void save_write_settings(void);             // Write settings to EEPROM

// Best time tracking
void save_update_best_time(int levelId, uint16_t timeSeconds);
uint16_t save_get_best_time(int levelId);
void save_increment_level_deaths(int levelId);
uint16_t save_get_level_deaths(int levelId);

// Rank tracking (0=none, 1=D, 2=C, 3=B, 4=A, 5=S)
void save_update_best_rank(int levelId, char rankChar);
uint8_t save_get_best_rank(int levelId);
char save_rank_to_char(uint8_t rankValue);
uint8_t save_char_to_rank(char rankChar);
bool save_has_all_s_ranks(void);
int save_count_s_ranks(void);

// Save file flags (stored in flags byte)
#define SAVE_FLAG_CS2_WATCHED       0x01  // CS2 cutscene has been watched
#define SAVE_FLAG_TUTORIAL_TORSO    0x02  // Torso controls tutorial shown
#define SAVE_FLAG_TUTORIAL_ARMS     0x04  // Arms controls tutorial shown
#define SAVE_FLAG_TUTORIAL_FULLBODY 0x08  // Fullbody controls tutorial shown
#define SAVE_FLAG_CS4_WATCHED       0x10  // CS4 cutscene has been watched

// Cutscene tracking
bool save_has_watched_cs2(void);
void save_mark_cs2_watched(void);
bool save_has_watched_cs4(void);
void save_mark_cs4_watched(void);

// Tutorial tracking
bool save_has_seen_tutorial_torso(void);
void save_mark_tutorial_torso_seen(void);
bool save_has_seen_tutorial_arms(void);
void save_mark_tutorial_arms_seen(void);
bool save_has_seen_tutorial_fullbody(void);
void save_mark_tutorial_fullbody_seen(void);

// ============================================================
// REPLAY SYSTEM (RAM only - too large for EEPROM)
// ============================================================

// Compact input frame - 4 bytes per frame
typedef struct {
    int8_t stickX;          // Analog stick X (-128 to 127)
    int8_t stickY;          // Analog stick Y (-128 to 127)
    uint16_t buttons;       // Button bitmask (A, B, Z, Start, D-pad, L, R, C-buttons)
} ReplayFrame;

// Replay data header
typedef struct {
    uint32_t magic;         // REPLAY_MAGIC to validate
    uint8_t levelId;        // Which level this replay is for
    uint8_t saveSlot;       // Which save slot this belongs to
    uint16_t frameCount;    // Number of recorded frames
    uint16_t timeSeconds;   // Level completion time
    uint16_t reserved;      // Padding
    float startX;           // Player X position when recording started
    float startY;           // Player Y position when recording started
    float startZ;           // Player Z position when recording started
} ReplayHeader;

// Replay buffer - one per level that's been completed
typedef struct {
    ReplayHeader header;
    ReplayFrame frames[REPLAY_MAX_FRAMES];
} ReplayData;

// Replay system state
typedef struct {
    bool recording;         // Currently recording
    bool playing;           // Currently playing back
    int currentFrame;       // Current frame index
    int levelId;            // Which level we're recording/playing
    ReplayData* activeReplay; // Pointer to current replay buffer
} ReplayState;

// Global replay buffers (one per level, allocated as needed)
extern ReplayData* g_levelReplays[SAVE_MAX_LEVELS];
extern ReplayState g_replayState;

// Replay functions
void replay_init(void);
void replay_cleanup(void);
bool replay_start_recording(int levelId, float startX, float startY, float startZ);
void replay_stop_recording(bool saveReplay);
void replay_get_start_position(float* outX, float* outY, float* outZ);
void replay_record_frame(int8_t stickX, int8_t stickY, uint16_t buttons);
bool replay_start_playback(int levelId);
void replay_stop_playback(void);
bool replay_get_frame(int8_t* stickX, int8_t* stickY, uint16_t* buttons);
bool replay_is_recording(void);
bool replay_is_playing(void);
bool replay_has_data(int levelId);
int replay_count_available(void);
void replay_dump_as_code(int levelId);  // Print replay data as C code to terminal

#endif // SAVE_H
