#include "save.h"

// Single instance of save system (defined here, declared extern in save.h)
SaveSystem g_saveSystem = {0};

// Level info for percentage calculation (set by game code)
SaveLevelInfo g_saveLevelInfo = {1, 0, 1};  // Default: 1 bolt, 0 golden screws, 1 level

// Accumulated fractional seconds for play time tracking
static float g_playTimeAccumulated = 0.0f;

// EEPROM write throttling - prevent wear from rapid saves
#define SAVE_MIN_INTERVAL_SECONDS 5.0f
static float g_lastSaveTime = -SAVE_MIN_INTERVAL_SECONDS;  // Allow immediate first save
static bool g_savePending = false;  // Flag for deferred save

// Volume update system - deferred to safe mixer window
static bool g_volumeUpdatePending = false;

// ============================================================
// CHECKSUM
// ============================================================

uint16_t save_calc_checksum(SaveFile* save) {
    uint16_t sum = 0;
    uint8_t* data = (uint8_t*)save;
    // Skip magic, version, flags, and checksum fields (first 8 bytes)
    for (size_t i = 8; i < sizeof(SaveFile); i++) {
        sum += data[i];
    }
    return sum;
}

// ============================================================
// SAVE FILE OPERATIONS
// ============================================================

void save_init_new(SaveFile* save) {
    memset(save, 0, sizeof(SaveFile));
    save->magic = SAVE_MAGIC;
    save->version = 1;
    save->flags = 0;
    save->deathCount = 0;
    save->playTimeSeconds = 0;
    save->totalBoltsCollected = 0;
    save->totalScrewgCollected = 0;
    save->currentLevel = 0;

    for (int i = 0; i < SAVE_MAX_LEVELS; i++) {
        save->levels[i].completed = 0;
        save->levels[i].hasReplay = 0;
        save->levels[i].bestRank = 0;
        save->levels[i].boltsCollected = 0;
        save->levels[i].screwgCollected = 0;
        save->levels[i].bestTimeSeconds = 0;
        save->levels[i].deathsOnLevel = 0;
    }

    save->checksum = save_calc_checksum(save);
}

bool save_is_valid(SaveFile* save) {
    if (save->magic != SAVE_MAGIC) return false;
    if (save->version == 0 || save->version > 1) return false;
    if (save->checksum != save_calc_checksum(save)) return false;
    return true;
}

// ============================================================
// EEPROM I/O
// ============================================================

size_t save_get_eeprom_offset(int slot) {
    // Header is 16 bytes at start, then save files follow
    return 16 + (slot * sizeof(SaveFile));
}

void save_system_init(void) {
    if (g_saveSystem.initialized) return;

    memset(&g_saveSystem, 0, sizeof(SaveSystem));
    g_saveSystem.activeSaveSlot = -1;

    // Check for EEPROM
    eeprom_type_t eepType = eeprom_present();
    debugf("Save: eeprom_present() returned %d\n", eepType);

    // Force EEPROM on since we know the ROM is built with eeprom4k
    // Some emulators don't properly report EEPROM presence
    if (eepType == EEPROM_NONE) {
        debugf("Save: EEPROM not detected, forcing on (emulator workaround)\n");
        g_saveSystem.eepromPresent = true;
    } else {
        g_saveSystem.eepromPresent = true;
        debugf("Save: EEPROM detected (type %d)\n", eepType);
    }

    if (g_saveSystem.eepromPresent) {
        debugf("Save: EEPROM enabled\n");

        // Load global settings from EEPROM header (first 16 bytes)
        eeprom_read_bytes((uint8_t*)&g_saveSystem.settings, 0, sizeof(GlobalSettings));
        if (g_saveSystem.settings.magic != SETTINGS_MAGIC) {
            // Initialize default settings
            debugf("Save: Settings invalid, using defaults\n");
            g_saveSystem.settings.magic = SETTINGS_MAGIC;
            g_saveSystem.settings.musicVolume = 10;  // Max by default
            g_saveSystem.settings.sfxVolume = 10;    // Max by default
        } else {
            debugf("Save: Settings loaded (music=%d, sfx=%d)\n",
                   g_saveSystem.settings.musicVolume, g_saveSystem.settings.sfxVolume);
        }

        // Load all save files from EEPROM
        for (int i = 0; i < SAVE_FILE_COUNT; i++) {
            size_t offset = save_get_eeprom_offset(i);
            eeprom_read_bytes((uint8_t*)&g_saveSystem.saves[i], offset, sizeof(SaveFile));

            if (save_is_valid(&g_saveSystem.saves[i])) {
                debugf("Save: Slot %d valid (deaths=%ld, time=%lds)\n",
                    i, (long)g_saveSystem.saves[i].deathCount,
                    (long)g_saveSystem.saves[i].playTimeSeconds);
            } else {
                debugf("Save: Slot %d empty/invalid\n", i);
            }
        }
    } else {
        debugf("Save: No EEPROM detected, saves disabled\n");
        // Set defaults even without EEPROM
        g_saveSystem.settings.musicVolume = 10;
        g_saveSystem.settings.sfxVolume = 10;
    }

    // Apply volume settings to mixer
    save_apply_volume_settings();

    g_saveSystem.initialized = true;
}

bool save_write_to_eeprom(int slot) {
    if (!g_saveSystem.eepromPresent || slot < 0 || slot >= SAVE_FILE_COUNT) {
        debugf("Save: Write failed (eeprom=%d, slot=%d)\n", g_saveSystem.eepromPresent, slot);
        return false;
    }

    SaveFile* save = &g_saveSystem.saves[slot];
    save->checksum = save_calc_checksum(save);

    size_t offset = save_get_eeprom_offset(slot);
    eeprom_write_bytes((uint8_t*)save, offset, sizeof(SaveFile));

    debugf("Save: Wrote slot %d to EEPROM\n", slot);
    return true;
}

bool save_delete(int slot) {
    if (slot < 0 || slot >= SAVE_FILE_COUNT) return false;

    memset(&g_saveSystem.saves[slot], 0, sizeof(SaveFile));

    if (g_saveSystem.eepromPresent) {
        save_write_to_eeprom(slot);
    }

    if (g_saveSystem.activeSaveSlot == slot) {
        g_saveSystem.activeSaveSlot = -1;
    }

    debugf("Save: Deleted slot %d\n", slot);
    return true;
}

bool save_create_new(int slot) {
    if (slot < 0 || slot >= SAVE_FILE_COUNT) return false;

    save_init_new(&g_saveSystem.saves[slot]);
    g_saveSystem.activeSaveSlot = slot;

    if (g_saveSystem.eepromPresent) {
        save_write_to_eeprom(slot);
    }

    debugf("Save: Created new save in slot %d\n", slot);
    return true;
}

bool save_load(int slot) {
    if (slot < 0 || slot >= SAVE_FILE_COUNT) return false;
    if (!save_is_valid(&g_saveSystem.saves[slot])) return false;

    g_saveSystem.activeSaveSlot = slot;
    debugf("Save: Loaded slot %d\n", slot);
    return true;
}

bool save_slot_has_data(int slot) {
    if (slot < 0 || slot >= SAVE_FILE_COUNT) return false;
    return save_is_valid(&g_saveSystem.saves[slot]);
}

SaveFile* save_get_active(void) {
    if (g_saveSystem.activeSaveSlot < 0) return NULL;
    return &g_saveSystem.saves[g_saveSystem.activeSaveSlot];
}

// ============================================================
// GAME INTEGRATION
// ============================================================

void save_collect_bolt(int levelId, int boltIndex) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return;
    if (boltIndex < 0 || boltIndex >= MAX_BOLTS_PER_LEVEL) return;

    uint32_t mask = 1u << boltIndex;
    if (!(save->levels[levelId].boltsCollected & mask)) {
        save->levels[levelId].boltsCollected |= mask;
        save->totalBoltsCollected++;
        debugf("Save: Collected bolt %d in level %d\n", boltIndex, levelId);
    }
}

bool save_is_bolt_collected(int levelId, int boltIndex) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return false;
    if (boltIndex < 0 || boltIndex >= MAX_BOLTS_PER_LEVEL) return false;

    return (save->levels[levelId].boltsCollected & (1u << boltIndex)) != 0;
}

void save_collect_screwg(int levelId, int screwgIndex) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return;
    if (screwgIndex < 0 || screwgIndex >= MAX_SCREWG_PER_LEVEL) return;

    uint8_t mask = 1u << screwgIndex;
    if (!(save->levels[levelId].screwgCollected & mask)) {
        save->levels[levelId].screwgCollected |= mask;
        save->totalScrewgCollected++;
        debugf("Save: Collected golden screw %d in level %d\n", screwgIndex, levelId);
    }
}

bool save_is_screwg_collected(int levelId, int screwgIndex) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return false;
    if (screwgIndex < 0 || screwgIndex >= MAX_SCREWG_PER_LEVEL) return false;

    return (save->levels[levelId].screwgCollected & (1u << screwgIndex)) != 0;
}

void save_complete_level(int levelId) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return;

    if (!save->levels[levelId].completed) {
        save->levels[levelId].completed = 1;
        debugf("Save: Completed level %d\n", levelId);
    }
}

void save_increment_deaths(void) {
    SaveFile* save = save_get_active();
    if (!save) {
        debugf("Save: No active save, death not counted\n");
        return;
    }
    save->deathCount++;
    debugf("Save: Death count now %ld\n", (long)save->deathCount);
}

void save_add_play_time(float seconds) {
    SaveFile* save = save_get_active();
    if (!save) return;

    g_playTimeAccumulated += seconds;
    if (g_playTimeAccumulated >= 1.0f) {
        save->playTimeSeconds += (uint32_t)g_playTimeAccumulated;
        g_playTimeAccumulated -= (uint32_t)g_playTimeAccumulated;
    }
}

void save_set_current_level(int levelId) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return;
    save->currentLevel = (uint8_t)levelId;
}

void save_auto_save(void) {
    if (g_saveSystem.activeSaveSlot < 0 || !g_saveSystem.eepromPresent) {
        debugf("Save: Auto-save skipped (slot=%d, eeprom=%d)\n",
            g_saveSystem.activeSaveSlot, g_saveSystem.eepromPresent);
        return;
    }

    // Get current time (uses play time as proxy for elapsed time)
    SaveFile* save = save_get_active();
    float currentTime = save ? (float)save->playTimeSeconds : 0.0f;

    // Check if enough time has passed since last save
    float timeSinceSave = currentTime - g_lastSaveTime;
    if (timeSinceSave < SAVE_MIN_INTERVAL_SECONDS) {
        // Mark save as pending - will be written later
        g_savePending = true;
        debugf("Save: Throttled (%.1fs since last), pending\n", timeSinceSave);
        return;
    }

    // Perform the save
    debugf("Save: Auto-saving to slot %d\n", g_saveSystem.activeSaveSlot);
    save_write_to_eeprom(g_saveSystem.activeSaveSlot);
    g_lastSaveTime = currentTime;
    g_savePending = false;
}

// Force an immediate save (bypasses throttling) - use sparingly
void save_force_save(void) {
    if (g_saveSystem.activeSaveSlot >= 0 && g_saveSystem.eepromPresent) {
        debugf("Save: Force-saving to slot %d\n", g_saveSystem.activeSaveSlot);
        save_write_to_eeprom(g_saveSystem.activeSaveSlot);
        SaveFile* save = save_get_active();
        g_lastSaveTime = save ? (float)save->playTimeSeconds : 0.0f;
        g_savePending = false;
    }
}

// Flush any pending save (call on scene exit or pause)
void save_flush_pending(void) {
    if (g_savePending && g_saveSystem.activeSaveSlot >= 0 && g_saveSystem.eepromPresent) {
        debugf("Save: Flushing pending save to slot %d\n", g_saveSystem.activeSaveSlot);
        save_write_to_eeprom(g_saveSystem.activeSaveSlot);
        SaveFile* save = save_get_active();
        g_lastSaveTime = save ? (float)save->playTimeSeconds : 0.0f;
        g_savePending = false;
    }
}

void save_format_time(uint32_t seconds, char* buffer, size_t bufSize) {
    int hours = (int)(seconds / 3600);
    int mins = (int)((seconds % 3600) / 60);
    int secs = (int)(seconds % 60);

    if (hours > 0) {
        snprintf(buffer, bufSize, "%d:%02d:%02d", hours, mins, secs);
    } else {
        snprintf(buffer, bufSize, "%d:%02d", mins, secs);
    }
}

int save_count_collected_bolts(SaveFile* save) {
    int count = 0;
    for (int i = 0; i < SAVE_MAX_LEVELS; i++) {
        uint32_t mask = save->levels[i].boltsCollected;
        while (mask) {
            count += mask & 1;
            mask >>= 1;
        }
    }
    return count;
}

int save_count_completed_levels(SaveFile* save) {
    int count = 0;
    for (int i = 0; i < SAVE_MAX_LEVELS; i++) {
        if (save->levels[i].completed) {
            count++;
        }
    }
    return count;
}

void save_set_level_info(int totalBolts, int totalScrewg, int realLevelCount) {
    g_saveLevelInfo.totalBolts = totalBolts > 0 ? totalBolts : 1;
    g_saveLevelInfo.totalScrewg = totalScrewg > 0 ? totalScrewg : 0;
    g_saveLevelInfo.realLevelCount = realLevelCount > 0 ? realLevelCount : 1;
    debugf("Save: Level info set - %d total bolts, %d golden screws, %d levels\n",
           g_saveLevelInfo.totalBolts, g_saveLevelInfo.totalScrewg, g_saveLevelInfo.realLevelCount);
}

int save_calc_percentage(SaveFile* save) {
    int completedLevels = save_count_completed_levels(save);
    int collectedBolts = save_count_collected_bolts(save);

    // Use actual totals from level definitions
    int totalBolts = g_saveLevelInfo.totalBolts;
    int totalLevels = g_saveLevelInfo.realLevelCount;

    // 50% for levels, 50% for bolts
    int levelPercent = (completedLevels * 50) / totalLevels;
    int boltPercent = (collectedBolts * 50) / totalBolts;

    // Clamp to valid range
    if (levelPercent > 50) levelPercent = 50;
    if (boltPercent > 50) boltPercent = 50;

    return levelPercent + boltPercent;
}

int save_get_total_bolts_collected(void) {
    SaveFile* save = save_get_active();
    if (!save) return 0;
    return save_count_collected_bolts(save);
}

int save_get_level_bolts_collected(int levelId) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return 0;

    // Count bits set in the level's boltsCollected bitmask
    uint32_t mask = save->levels[levelId].boltsCollected;
    int count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

int save_get_total_screwg_collected(void) {
    SaveFile* save = save_get_active();
    if (!save) return 0;

    // Count all golden screws across all levels
    int count = 0;
    for (int i = 0; i < SAVE_MAX_LEVELS; i++) {
        uint8_t mask = save->levels[i].screwgCollected;
        while (mask) {
            count += mask & 1;
            mask >>= 1;
        }
    }
    return count;
}

int save_get_level_screwg_collected(int levelId) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return 0;

    // Count bits set in the level's screwgCollected bitmask
    uint8_t mask = save->levels[levelId].screwgCollected;
    int count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

// ============================================================
// VOLUME SETTINGS
// ============================================================

void save_set_music_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 10) volume = 10;
    g_saveSystem.settings.musicVolume = (uint8_t)volume;
    g_volumeUpdatePending = true;  // Mark for deferred update
}

void save_set_sfx_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 10) volume = 10;
    g_saveSystem.settings.sfxVolume = (uint8_t)volume;
    g_volumeUpdatePending = true;  // Mark for deferred update
}

int save_get_music_volume(void) {
    return g_saveSystem.settings.musicVolume;
}

int save_get_sfx_volume(void) {
    return g_saveSystem.settings.sfxVolume;
}

void save_apply_volume_settings(void) {
    // Convert 0-10 scale to 0.0-1.0
    float musicVol = g_saveSystem.settings.musicVolume / 10.0f;
    float sfxVol = g_saveSystem.settings.sfxVolume / 10.0f;

    // Channel 0 is music, channels 1-7 are SFX
    mixer_ch_set_vol(0, musicVol, musicVol);
    for (int i = 1; i < 8; i++) {
        mixer_ch_set_vol(i, sfxVol, sfxVol);
    }
    g_volumeUpdatePending = false;  // Clear pending flag
}

void save_apply_volume_settings_safe(void) {
    // Only apply if there's a pending update
    // This should be called RIGHT BEFORE mixer_poll() in each scene's update
    if (g_volumeUpdatePending) {
        save_apply_volume_settings();
    }
}

void save_write_settings(void) {
    if (!g_saveSystem.eepromPresent) return;

    g_saveSystem.settings.magic = SETTINGS_MAGIC;
    eeprom_write_bytes((uint8_t*)&g_saveSystem.settings, 0, sizeof(GlobalSettings));
    debugf("Save: Wrote settings (music=%d, sfx=%d)\n",
           g_saveSystem.settings.musicVolume, g_saveSystem.settings.sfxVolume);
}

// ============================================================
// BEST TIME TRACKING
// ============================================================

void save_update_best_time(int levelId, uint16_t timeSeconds) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return;

    // Only update if this is a new record (or no previous time)
    if (save->levels[levelId].bestTimeSeconds == 0 ||
        timeSeconds < save->levels[levelId].bestTimeSeconds) {
        save->levels[levelId].bestTimeSeconds = timeSeconds;
        debugf("Save: New best time for level %d: %d seconds\n", levelId, timeSeconds);
    }
}

uint16_t save_get_best_time(int levelId) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return 0;
    return save->levels[levelId].bestTimeSeconds;
}

void save_increment_level_deaths(int levelId) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return;
    if (save->levels[levelId].deathsOnLevel < 65535) {
        save->levels[levelId].deathsOnLevel++;
    }
}

uint16_t save_get_level_deaths(int levelId) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return 0;
    return save->levels[levelId].deathsOnLevel;
}

// ============================================================
// RANK TRACKING
// ============================================================

// Convert rank character to numeric value (higher = better)
uint8_t save_char_to_rank(char rankChar) {
    switch (rankChar) {
        case 'S': return 5;
        case 'A': return 4;
        case 'B': return 3;
        case 'C': return 2;
        case 'D': return 1;
        default:  return 0;
    }
}

// Convert numeric rank value back to character
char save_rank_to_char(uint8_t rankValue) {
    switch (rankValue) {
        case 5: return 'S';
        case 4: return 'A';
        case 3: return 'B';
        case 2: return 'C';
        case 1: return 'D';
        default: return '-';
    }
}

void save_update_best_rank(int levelId, char rankChar) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return;

    uint8_t newRank = save_char_to_rank(rankChar);
    uint8_t currentRank = save->levels[levelId].bestRank;

    // Only update if new rank is better
    if (newRank > currentRank) {
        save->levels[levelId].bestRank = newRank;
        debugf("Save: New best rank for level %d: %c\n", levelId, rankChar);
    }
}

uint8_t save_get_best_rank(int levelId) {
    SaveFile* save = save_get_active();
    if (!save || levelId < 0 || levelId >= SAVE_MAX_LEVELS) return 0;
    return save->levels[levelId].bestRank;
}

bool save_has_all_s_ranks(void) {
    SaveFile* save = save_get_active();
    if (!save) return false;

    int realLevels = g_saveLevelInfo.realLevelCount;
    for (int i = 0; i < realLevels && i < SAVE_MAX_LEVELS; i++) {
        if (save->levels[i].bestRank != 5) {  // 5 = S rank
            return false;
        }
    }
    return true;
}

int save_count_s_ranks(void) {
    SaveFile* save = save_get_active();
    if (!save) return 0;

    int count = 0;
    int realLevels = g_saveLevelInfo.realLevelCount;
    for (int i = 0; i < realLevels && i < SAVE_MAX_LEVELS; i++) {
        if (save->levels[i].bestRank == 5) {  // 5 = S rank
            count++;
        }
    }
    return count;
}

// ============================================================
// CUTSCENE TRACKING
// ============================================================

bool save_has_watched_cs2(void) {
    SaveFile* save = save_get_active();
    if (!save) return false;
    return (save->flags & SAVE_FLAG_CS2_WATCHED) != 0;
}

void save_mark_cs2_watched(void) {
    SaveFile* save = save_get_active();
    if (!save) return;

    if (!(save->flags & SAVE_FLAG_CS2_WATCHED)) {
        save->flags |= SAVE_FLAG_CS2_WATCHED;
        debugf("Save: Marked CS2 cutscene as watched\n");
        save_auto_save();
    }
}

bool save_has_watched_cs4(void) {
    SaveFile* save = save_get_active();
    if (!save) return false;
    return (save->flags & SAVE_FLAG_CS4_WATCHED) != 0;
}

void save_mark_cs4_watched(void) {
    SaveFile* save = save_get_active();
    if (!save) return;

    if (!(save->flags & SAVE_FLAG_CS4_WATCHED)) {
        save->flags |= SAVE_FLAG_CS4_WATCHED;
        debugf("Save: Marked CS4 cutscene as watched\n");
        save_auto_save();
    }
}

// ============================================================
// TUTORIAL TRACKING
// ============================================================

bool save_has_seen_tutorial_torso(void) {
    SaveFile* save = save_get_active();
    if (!save) return false;
    return (save->flags & SAVE_FLAG_TUTORIAL_TORSO) != 0;
}

void save_mark_tutorial_torso_seen(void) {
    SaveFile* save = save_get_active();
    if (!save) return;

    if (!(save->flags & SAVE_FLAG_TUTORIAL_TORSO)) {
        save->flags |= SAVE_FLAG_TUTORIAL_TORSO;
        debugf("Save: Marked Torso tutorial as seen\n");
        save_auto_save();
    }
}

bool save_has_seen_tutorial_arms(void) {
    SaveFile* save = save_get_active();
    if (!save) return false;
    return (save->flags & SAVE_FLAG_TUTORIAL_ARMS) != 0;
}

void save_mark_tutorial_arms_seen(void) {
    SaveFile* save = save_get_active();
    if (!save) return;

    if (!(save->flags & SAVE_FLAG_TUTORIAL_ARMS)) {
        save->flags |= SAVE_FLAG_TUTORIAL_ARMS;
        debugf("Save: Marked Arms tutorial as seen\n");
        save_auto_save();
    }
}

bool save_has_seen_tutorial_fullbody(void) {
    SaveFile* save = save_get_active();
    if (!save) return false;
    return (save->flags & SAVE_FLAG_TUTORIAL_FULLBODY) != 0;
}

void save_mark_tutorial_fullbody_seen(void) {
    SaveFile* save = save_get_active();
    if (!save) return;

    if (!(save->flags & SAVE_FLAG_TUTORIAL_FULLBODY)) {
        save->flags |= SAVE_FLAG_TUTORIAL_FULLBODY;
        debugf("Save: Marked Fullbody tutorial as seen\n");
        save_auto_save();
    }
}

// ============================================================
// REPLAY SYSTEM
// ============================================================

// Global replay buffers (one per level, allocated on demand)
ReplayData* g_levelReplays[SAVE_MAX_LEVELS] = {0};
ReplayState g_replayState = {0};

static bool replay_system_initialized = false;

void replay_init(void) {
    // Only reset state, preserve replay data buffers
    // Stop any active recording/playback
    g_replayState.recording = false;
    g_replayState.playing = false;
    g_replayState.currentFrame = 0;
    g_replayState.activeReplay = NULL;
    // Don't reset levelId - it's set when recording/playback starts

    // Only clear buffers on first init (app startup), not on each level load
    if (!replay_system_initialized) {
        for (int i = 0; i < SAVE_MAX_LEVELS; i++) {
            g_levelReplays[i] = NULL;
        }
        replay_system_initialized = true;
        debugf("Replay: System initialized (first time)\n");
    } else {
        debugf("Replay: State reset (preserving replay data)\n");
    }
}

void replay_cleanup(void) {
    // Free all allocated replay buffers
    for (int i = 0; i < SAVE_MAX_LEVELS; i++) {
        if (g_levelReplays[i]) {
            free(g_levelReplays[i]);
            g_levelReplays[i] = NULL;
        }
    }
    memset(&g_replayState, 0, sizeof(ReplayState));
    debugf("Replay: Cleaned up\n");
}

bool replay_start_recording(int levelId, float startX, float startY, float startZ) {
    if (levelId < 0 || levelId >= SAVE_MAX_LEVELS) return false;
    if (g_replayState.recording || g_replayState.playing) return false;

    // Allocate buffer if not already
    if (!g_levelReplays[levelId]) {
        g_levelReplays[levelId] = malloc(sizeof(ReplayData));
        if (!g_levelReplays[levelId]) {
            debugf("Replay: Failed to allocate buffer for level %d\n", levelId);
            return false;
        }
    }

    // Initialize replay data
    ReplayData* replay = g_levelReplays[levelId];
    memset(replay, 0, sizeof(ReplayData));
    replay->header.magic = REPLAY_MAGIC;
    replay->header.levelId = (uint8_t)levelId;
    replay->header.saveSlot = (uint8_t)(g_saveSystem.activeSaveSlot >= 0 ? g_saveSystem.activeSaveSlot : 0);
    replay->header.frameCount = 0;
    replay->header.startX = startX;
    replay->header.startY = startY;
    replay->header.startZ = startZ;

    g_replayState.recording = true;
    g_replayState.playing = false;
    g_replayState.currentFrame = 0;
    g_replayState.levelId = levelId;
    g_replayState.activeReplay = replay;

    debugf("Replay: Started recording level %d at (%.1f, %.1f, %.1f)\n", levelId, startX, startY, startZ);
    return true;
}

void replay_get_start_position(float* outX, float* outY, float* outZ) {
    if (g_replayState.activeReplay) {
        *outX = g_replayState.activeReplay->header.startX;
        *outY = g_replayState.activeReplay->header.startY;
        *outZ = g_replayState.activeReplay->header.startZ;
    } else {
        *outX = 0;
        *outY = 0;
        *outZ = 0;
    }
}

void replay_stop_recording(bool saveReplay) {
    if (!g_replayState.recording) return;

    if (saveReplay && g_replayState.activeReplay) {
        // Mark as having replay data in save file
        SaveFile* save = save_get_active();
        if (save && g_replayState.levelId >= 0 && g_replayState.levelId < SAVE_MAX_LEVELS) {
            save->levels[g_replayState.levelId].hasReplay = 1;
        }
        debugf("Replay: Saved recording (%d frames) for level %d\n",
               g_replayState.activeReplay->header.frameCount, g_replayState.levelId);
    } else {
        // Discard the replay - free the buffer and clear the pointer
        if (g_replayState.activeReplay && g_replayState.levelId >= 0 && g_replayState.levelId < SAVE_MAX_LEVELS) {
            free(g_levelReplays[g_replayState.levelId]);
            g_levelReplays[g_replayState.levelId] = NULL;
            // Also clear hasReplay flag
            SaveFile* save = save_get_active();
            if (save) {
                save->levels[g_replayState.levelId].hasReplay = 0;
            }
        }
        debugf("Replay: Discarded recording\n");
    }

    g_replayState.recording = false;
    g_replayState.activeReplay = NULL;
}

void replay_record_frame(int8_t stickX, int8_t stickY, uint16_t buttons) {
    if (!g_replayState.recording || !g_replayState.activeReplay) return;

    ReplayData* replay = g_replayState.activeReplay;
    if (replay->header.frameCount >= REPLAY_MAX_FRAMES) {
        // Buffer full - stop recording but DON'T save (too long)
        // The game will decide whether to save when level completes
        debugf("Replay: Buffer full, stopping recording (will not save - too long)\n");
        g_replayState.recording = false;  // Just stop, don't call replay_stop_recording
        return;
    }

    ReplayFrame* frame = &replay->frames[replay->header.frameCount];
    frame->stickX = stickX;
    frame->stickY = stickY;
    frame->buttons = buttons;
    replay->header.frameCount++;
}

bool replay_start_playback(int levelId) {
    if (levelId < 0 || levelId >= SAVE_MAX_LEVELS) return false;
    if (g_replayState.recording || g_replayState.playing) return false;

    ReplayData* replay = g_levelReplays[levelId];
    if (!replay || replay->header.magic != REPLAY_MAGIC || replay->header.frameCount == 0) {
        debugf("Replay: No valid replay data for level %d\n", levelId);
        return false;
    }

    g_replayState.playing = true;
    g_replayState.recording = false;
    g_replayState.currentFrame = 0;
    g_replayState.levelId = levelId;
    g_replayState.activeReplay = replay;

    debugf("Replay: Started playback of level %d (%d frames)\n", levelId, replay->header.frameCount);
    return true;
}

void replay_stop_playback(void) {
    if (!g_replayState.playing) return;

    debugf("Replay: Stopped playback\n");
    g_replayState.playing = false;
    g_replayState.activeReplay = NULL;
}

bool replay_get_frame(int8_t* stickX, int8_t* stickY, uint16_t* buttons) {
    if (!g_replayState.playing || !g_replayState.activeReplay) return false;

    ReplayData* replay = g_replayState.activeReplay;
    if (g_replayState.currentFrame >= replay->header.frameCount) {
        // Playback complete
        replay_stop_playback();
        return false;
    }

    ReplayFrame* frame = &replay->frames[g_replayState.currentFrame];
    *stickX = frame->stickX;
    *stickY = frame->stickY;
    *buttons = frame->buttons;
    g_replayState.currentFrame++;

    return true;
}

bool replay_is_recording(void) {
    return g_replayState.recording;
}

bool replay_is_playing(void) {
    return g_replayState.playing;
}

bool replay_has_data(int levelId) {
    if (levelId < 0 || levelId >= SAVE_MAX_LEVELS) return false;
    ReplayData* replay = g_levelReplays[levelId];
    return replay && replay->header.magic == REPLAY_MAGIC && replay->header.frameCount > 0;
}

int replay_count_available(void) {
    int count = 0;
    for (int i = 0; i < SAVE_MAX_LEVELS; i++) {
        if (replay_has_data(i)) count++;
    }
    return count;
}

void replay_dump_as_code(int levelId) {
    if (!replay_has_data(levelId)) {
        debugf("// No replay data for level %d\n", levelId);
        return;
    }

    ReplayData* replay = g_levelReplays[levelId];
    int frameCount = replay->header.frameCount;

    debugf("\n// ============================================================\n");
    debugf("// DEMO REPLAY DATA - Level %d (%d frames)\n", levelId, frameCount);
    debugf("// ============================================================\n\n");

    debugf("#define DEMO_LEVEL_%d_FRAMES %d\n\n", levelId, frameCount);

    debugf("static const ReplayFrame demo_level_%d_frames[] = {\n", levelId);

    for (int i = 0; i < frameCount; i++) {
        ReplayFrame* f = &replay->frames[i];
        debugf("    {%4d, %4d, 0x%04X},", f->stickX, f->stickY, f->buttons);
        if ((i + 1) % 4 == 0 || i == frameCount - 1) {
            debugf("\n");
        }
    }

    debugf("};\n\n");
    debugf("// Start position: (%.1f, %.1f, %.1f)\n",
           replay->header.startX, replay->header.startY, replay->header.startZ);
    debugf("// DEMO_LIST entry: { %d, DEMO_LEVEL_%d_FRAMES, demo_level_%d_frames, %.1ff, %.1ff, %.1ff },\n",
           levelId, levelId, levelId,
           replay->header.startX, replay->header.startY, replay->header.startZ);
    debugf("// ============================================================\n\n");
}
