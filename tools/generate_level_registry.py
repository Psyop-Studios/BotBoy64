#!/usr/bin/env python3
"""
Auto-generates level registry code from level*.h files in src/levels/
Run this before compiling to keep level registry in sync.
Also auto-updates chunk references in level files based on filesystem.
"""

import os
import re
import glob

LEVELS_DIR = "src/levels"
OUTPUT_FILE = "src/levels_generated.h"
FILESYSTEM_DIR = "filesystem"
MAX_LEVELS = 12  # Total number of level slots


def get_chunks_for_map(map_name):
    """Find all chunk files for a map in the filesystem directory."""
    pattern = os.path.join(FILESYSTEM_DIR, f"{map_name}_chunk*.t3dm")
    chunk_files = sorted(glob.glob(pattern))

    # Extract chunk numbers and sort properly
    chunks = []
    for f in chunk_files:
        basename = os.path.basename(f)
        match = re.search(rf'{map_name}_chunk(\d+)\.t3dm', basename)
        if match:
            chunks.append((int(match.group(1)), basename))

    # Sort by chunk number
    chunks.sort(key=lambda x: x[0])
    return [f"rom:/{c[1]}" for c in chunks]


def update_level_chunks(filepath):
    """Update a level file's segment list to match actual filesystem chunks."""
    with open(filepath, 'r') as f:
        content = f.read()

    # Find the segments block
    segments_match = re.search(
        r'\.segments\s*=\s*\{([^}]+)\}',
        content, re.DOTALL
    )
    if not segments_match:
        return False

    segments_block = segments_match.group(1)

    # Extract the first segment to determine the map name
    first_segment = re.search(r'"rom:/([^"]+)\.t3dm"', segments_block)
    if not first_segment:
        return False

    first_name = first_segment.group(1)

    # Check if this is a chunked map
    if '_chunk' in first_name:
        # Extract base map name
        base_name = re.sub(r'_chunk\d+$', '', first_name)
    else:
        # Not chunked, check if chunks exist in filesystem
        base_name = first_name

    # Get actual chunks from filesystem
    chunks = get_chunks_for_map(base_name)

    if not chunks:
        # No chunks found, might be a non-chunked map - leave as is
        return False

    # Build new segments block
    new_segments = ',\n        '.join(f'"{c}"' for c in chunks)
    new_segments_block = f'''
        {new_segments},
    '''

    # Build new content
    new_content = content[:segments_match.start()]
    new_content += f'.segments = {{{new_segments_block}}}'
    new_content += content[segments_match.end():]

    # Update segment count
    new_content = re.sub(
        r'\.segmentCount\s*=\s*\d+',
        f'.segmentCount = {len(chunks)}',
        new_content
    )

    # Check if anything changed
    if new_content != content:
        with open(filepath, 'w') as f:
            f.write(new_content)
        print(f"  Updated {os.path.basename(filepath)}: {len(chunks)} chunks for {base_name}")
        return True

    return False

def find_level_data_name(filepath):
    """Extract the LevelData variable name from a level header."""
    with open(filepath, 'r') as f:
        content = f.read()

    # Look for: static const LevelData VARNAME = {
    match = re.search(r'static\s+const\s+LevelData\s+(\w+)\s*=', content)
    if match:
        return match.group(1)
    return None

def count_bolts_in_level(filepath):
    """Count the number of DECO_BOLT entries in a level file."""
    with open(filepath, 'r') as f:
        content = f.read()

    # Count occurrences of DECO_BOLT in decorations
    return len(re.findall(r'DECO_BOLT', content))

def count_screwg_in_level(filepath):
    """Count the number of DECO_SCREWG (golden screw) entries in a level file."""
    with open(filepath, 'r') as f:
        content = f.read()

    # Count occurrences of DECO_SCREWG in decorations
    return len(re.findall(r'DECO_SCREWG', content))

def extract_level_number(filename):
    """Extract level number from filename like 'level1.h' -> 1"""
    match = re.search(r'level(\d+)\.h', filename)
    if match:
        return int(match.group(1))
    return None

def main():
    # Find all level files
    pattern = os.path.join(LEVELS_DIR, "level*.h")
    level_files = sorted(glob.glob(pattern))

    # First pass: update chunk references in level files
    print("Checking level chunk references...")
    for filepath in level_files:
        update_level_chunks(filepath)

    # Parse each file
    levels = {}  # level_number -> data_name
    for filepath in level_files:
        filename = os.path.basename(filepath)
        level_num = extract_level_number(filename)

        if level_num is None:
            print(f"Warning: Could not extract level number from {filename}")
            continue

        if level_num < 1 or level_num > MAX_LEVELS:
            print(f"Warning: Level number {level_num} out of range (1-{MAX_LEVELS})")
            continue

        data_name = find_level_data_name(filepath)
        bolt_count = count_bolts_in_level(filepath)
        screwg_count = count_screwg_in_level(filepath)
        if data_name:
            levels[level_num] = {
                'filename': filename,
                'data_name': data_name,
                'bolt_count': bolt_count,
                'screwg_count': screwg_count,
            }
        else:
            print(f"Warning: Could not find LevelData in {filename}")

    # Generate includes
    includes = '\n'.join(f'#include "levels/{levels[i]["filename"]}"'
                         for i in sorted(levels.keys()))

    # Generate enum entries
    enum_entries = ',\n    '.join(f'LEVEL_{i}' for i in range(1, MAX_LEVELS + 1))

    # Generate registry entries
    registry_lines = []
    for i in range(1, MAX_LEVELS + 1):
        if i in levels:
            registry_lines.append(f'    &{levels[i]["data_name"]},         // Level {i}')
        else:
            registry_lines.append(f'    &PLACEHOLDER_LEVEL,    // Level {i}')
    registry_entries = '\n'.join(registry_lines)

    # Generate bolt count array
    bolt_count_lines = []
    total_bolts = 0
    real_level_count = 0
    for i in range(1, MAX_LEVELS + 1):
        if i in levels:
            bolt_count_lines.append(f'    {levels[i]["bolt_count"]},  // Level {i}: {levels[i]["data_name"]}')
            total_bolts += levels[i]["bolt_count"]
            real_level_count += 1
        else:
            bolt_count_lines.append(f'    0,  // Level {i}: placeholder')
    bolt_count_entries = '\n'.join(bolt_count_lines)

    # Generate golden screw count array
    screwg_count_lines = []
    total_screwg = 0
    for i in range(1, MAX_LEVELS + 1):
        if i in levels:
            screwg_count_lines.append(f'    {levels[i]["screwg_count"]},  // Level {i}: {levels[i]["data_name"]}')
            total_screwg += levels[i]["screwg_count"]
        else:
            screwg_count_lines.append(f'    0,  // Level {i}: placeholder')
    screwg_count_entries = '\n'.join(screwg_count_lines)

    # Generate the header file
    output = f'''#ifndef LEVELS_GENERATED_H
#define LEVELS_GENERATED_H

#include <string.h>
#include "mapLoader.h"
#include "mapData.h"
#include "save.h"

// ============================================================
// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
// Generated by tools/generate_level_registry.py
// ============================================================

#define MAX_LEVEL_SEGMENTS 16
#define MAX_LEVEL_DECORATIONS 80

// ============================================================
// LEVEL STATISTICS (auto-generated from level files)
// ============================================================
#define TOTAL_BOLTS_IN_GAME {total_bolts}
#define TOTAL_SCREWG_IN_GAME {total_screwg}
#define REAL_LEVEL_COUNT {real_level_count}

// Bolts per level (for percentage calculation)
static const int BOLTS_PER_LEVEL[{MAX_LEVELS}] = {{
{bolt_count_entries}
}};

// Golden screws per level (rare collectibles)
static const int SCREWG_PER_LEVEL[{MAX_LEVELS}] = {{
{screwg_count_entries}
}};

// Get total bolts across all levels
static inline int level_get_total_bolts(void) {{
    return TOTAL_BOLTS_IN_GAME;
}}

// Get total golden screws across all levels
static inline int level_get_total_screwg(void) {{
    return TOTAL_SCREWG_IN_GAME;
}}

// Get number of real (non-placeholder) levels
static inline int level_get_real_count(void) {{
    return REAL_LEVEL_COUNT;
}}

// Decoration placement data
typedef struct {{
    DecoType type;
    float x, y, z;
    float rotY;
    float scaleX, scaleY, scaleZ;
    // Optional: transition target (for DECO_TRANSITIONCOLLISION, DECO_LEVEL_TRANSITION)
    int targetLevel;   // Which level to load (0-based index)
    int targetSpawn;   // Which PlayerSpawn to use (0-based index)
    // Optional: activation system (for buttons, doors, etc.)
    int activationId;  // Links buttons to doors/objects (0 = none, 1-31 = valid IDs)
    // Optional: dialogue trigger script ID (for DECO_DIALOGUETRIGGER)
    int scriptId;      // Which script to run (0-based index into scene's script array)
    // Optional: patrol points (for DECO_RAT and other patrolling entities)
    const T3DVec3* patrolPoints;  // Pointer to patrol point array (NULL if no patrol)
    int patrolPointCount;         // Number of patrol points (0 if no patrol)
    // Optional: platform target position (for DECO_MOVE_PLAT)
    float platformTargetX, platformTargetY, platformTargetZ;  // Target position to move to
    float platformSpeed;  // Movement speed in units/sec (0 = use default)
    // Optional: waypoints for DECO_MOVING_ROCK (up to 4 waypoints, waypoint 0 = start position)
    float waypoint1X, waypoint1Y, waypoint1Z;  // Second waypoint
    float waypoint2X, waypoint2Y, waypoint2Z;  // Third waypoint (optional)
    float waypoint3X, waypoint3Y, waypoint3Z;  // Fourth waypoint (optional)
    int waypointCount;  // Number of waypoints (2-4, 0 = use 2 with platformTarget as waypoint 1)
    // Optional: start inactive (for decorations that should be disabled until triggered)
    bool startInactive;  // If true, decoration starts inactive (default: false = active)
    // Optional: start stationary (for moving platforms that should be visible but not moving until triggered)
    bool startStationary;  // If true, platform is visible but doesn't move until activated
    // Optional: light color (for DECO_LIGHT)
    uint8_t lightColorR, lightColorG, lightColorB;  // Light color RGB (default: 255,255,255 white)
    float lightRadius;  // Light radius (default: 2000.0f)
    // Optional: checkpoint lighting (for DECO_CHECKPOINT)
    // When player hits checkpoint, ambient/directional colors lerp to these values
    // Set all to 0 to not change lighting
    uint8_t checkpointAmbientR, checkpointAmbientG, checkpointAmbientB;
    uint8_t checkpointDirectionalR, checkpointDirectionalG, checkpointDirectionalB;
    // Optional: light trigger direction (for DECO_LIGHT_TRIGGER)
    // Sets the entity light direction when triggered (affects player/decorations only)
    // Use 999.0f for any component to mean "no change to this direction"
    float lightTriggerDirX, lightTriggerDirY, lightTriggerDirZ;
    // Optional: fog color trigger (for DECO_FOGCOLOR)
    // Changes fog color and range when player enters the trigger zone
    uint8_t fogColorR, fogColorG, fogColorB;  // Target fog color
    float fogNear, fogFar;                     // Target fog range (0,0 = keep level fog range)
}} DecoPlacement;

// Level definition
typedef struct {{
    const char* name;

    // Map segments (in order, will be auto-aligned)
    const char* segments[MAX_LEVEL_SEGMENTS];
    int segmentCount;

    // Extra map rotation in degrees (added to base 90 degree rotation)
    float mapRotY;

    // Decorations
    DecoPlacement decorations[MAX_LEVEL_DECORATIONS];
    int decorationCount;

    // Player start position
    float playerStartX, playerStartY, playerStartZ;

    // Background music (NULL for no music, or filename like "rom:/scrap1.wav64")
    const char* music;

    // Body part to use for this level (0=head, 1=torso, 2=arms, 3=legs)
    int bodyPart;

    // Point light position (0,0,0 = use default directional lighting only)
    float lightX, lightY, lightZ;
    bool hasPointLight;  // If true, use point light at lightX/Y/Z

    // Directional light settings (default: direction (1,1,1), white light, gray ambient)
    float lightDirX, lightDirY, lightDirZ;  // Direction vector (will be normalized, 0,0,0 = use default 1,1,1)
    uint8_t ambientR, ambientG, ambientB;   // Ambient light color (0,0,0 = use default 80,80,80)
    uint8_t directionalR, directionalG, directionalB;  // Directional light color (0,0,0 = use default 255,255,255)

    // Background/clear color
    uint8_t bgR, bgG, bgB;  // Background color for screen clear (default 0,0,0 = black)

    // Fog settings
    bool fogEnabled;         // Whether fog is enabled (default false)
    uint8_t fogR, fogG, fogB;  // Fog color RGB 0-255
    float fogNear, fogFar;   // Fog distance range (0,0 = use defaults 100,500)
}} LevelData;

// ============================================================
// LEVEL REGISTRY
// ============================================================

typedef enum {{
    {enum_entries},
    LEVEL_COUNT
}} LevelID;

// Include level data files
{includes}

// Placeholder level for empty slots
static const LevelData PLACEHOLDER_LEVEL = {{
    .name = "Empty Level",
    .segments = {{"rom:/test_map.t3dm"}},
    .segmentCount = 1,
    .decorations = {{}},
    .decorationCount = 0,
    .playerStartX = 0,
    .playerStartY = 50,
    .playerStartZ = 0,
    .music = NULL,
    .bodyPart = 1,  // Default to torso
    .hasPointLight = false,
    .lightX = 0, .lightY = 0, .lightZ = 0,
    // Directional light defaults (0 = use engine defaults)
    .lightDirX = 0, .lightDirY = 0, .lightDirZ = 0,
    .ambientR = 0, .ambientG = 0, .ambientB = 0,
    .directionalR = 0, .directionalG = 0, .directionalB = 0,
    // Background color (black)
    .bgR = 0, .bgG = 0, .bgB = 0,
    // Fog (disabled by default)
    .fogEnabled = false,
    .fogR = 0, .fogG = 0, .fogB = 0,
    .fogNear = 0, .fogFar = 0
}};

// ============================================================
// LEVEL AUDIO SYSTEM
// ============================================================

// Current playing music (managed by level system)
// Music uses channel 0 (mono), SFX uses channels 1-7
// Note: Define LEVELS_AUDIO_IMPLEMENTATION in exactly one .c file before including this
#ifdef LEVELS_AUDIO_IMPLEMENTATION
wav64_t g_levelMusic;
bool g_levelMusicLoaded = false;
const char* g_currentMusicPath = NULL;
#else
extern wav64_t g_levelMusic;
extern bool g_levelMusicLoaded;
extern const char* g_currentMusicPath;
#endif

// Stop currently playing level music
static inline void level_stop_music(void) {{
    debugf("level_stop_music called: loaded=%d, current=%s\\n",
           g_levelMusicLoaded,
           g_currentMusicPath ? g_currentMusicPath : "NULL");
    if (g_levelMusicLoaded) {{
        mixer_ch_stop(0);  // Stop music channel
        // CRITICAL: VADPCM audio decoding uses RSP highpri queue, not lowpri.
        // rspq_wait() only waits for lowpri queue to complete.
        // We must sync highpri first, then wait for lowpri, before closing the file.
        rspq_highpri_sync();  // Wait for highpri audio operations
        rspq_wait();          // Wait for lowpri queue
        wav64_close(&g_levelMusic);
        g_levelMusicLoaded = false;
        debugf("Music stopped and closed\\n");
    }}
    g_currentMusicPath = NULL;
}}

// Play music for a level (handles loading, stopping previous, etc.)
static inline void level_play_music(const char* musicPath) {{
    debugf("level_play_music called: path=%s, current=%s, loaded=%d\\n",
           musicPath ? musicPath : "NULL",
           g_currentMusicPath ? g_currentMusicPath : "NULL",
           g_levelMusicLoaded);

    // Skip if same music is already playing
    if (g_currentMusicPath != NULL && musicPath != NULL &&
        strcmp(g_currentMusicPath, musicPath) == 0) {{
        debugf("Same music already playing, skipping\\n");
        return;
    }}

    // Stop any currently playing music
    level_stop_music();

    // If no music specified, just stop
    if (musicPath == NULL) {{
        debugf("Level has no music\\n");
        return;
    }}

    // Load and play new music
    debugf("Loading music: %s\\n", musicPath);
    wav64_open(&g_levelMusic, musicPath);
    wav64_set_loop(&g_levelMusic, true);  // Enable looping for background music
    g_levelMusicLoaded = true;

    // Apply user's volume settings before starting music
    float musicVol = g_saveSystem.settings.musicVolume / 10.0f;
    float sfxVol = g_saveSystem.settings.sfxVolume / 10.0f;
    mixer_ch_set_vol(0, musicVol, musicVol);
    // Also set SFX channels
    for (int i = 1; i < 8; i++) {{
        mixer_ch_set_vol(i, sfxVol, sfxVol);
    }}
    
    // Play mono music on channel 0
    wav64_play(&g_levelMusic, 0);
    g_currentMusicPath = musicPath;
    debugf("Music started on channel 0 (loop=true, user volume applied)\\n");
}}

// Level registry
static const LevelData* ALL_LEVELS[LEVEL_COUNT] = {{
{registry_entries}
}};

// ============================================================
// LEVEL API
// ============================================================

// Load a level into the map loader and runtime
static inline void level_load(LevelID id, MapLoader* loader, MapRuntime* runtime) {{
    if (id >= LEVEL_COUNT) return;

    const LevelData* level = ALL_LEVELS[id];
    debugf("Loading level: %s\\n", level->name);

    // Load map segments
    maploader_load_simple(loader, level->segments, level->segmentCount);

    // Store level's extra rotation in loader (for collision system)
    float extraRotY = T3D_DEG_TO_RAD(level->mapRotY);
    loader->mapRotY = extraRotY;

    // Apply level's extra map rotation to all visual segments
    for (int i = 0; i < loader->count; i++) {{
        loader->segments[i].rotY = extraRotY;
    }}

    // Rebuild collision grids with correct rotation (always rebuild to ensure correct rotation)
    maploader_rebuild_collision_grids(loader);

    // Set collision reference
    runtime->mapLoader = loader;

    // Track bolt and golden screw indices for save system
    int boltIndex = 0;
    int screwgIndex = 0;

    // NOTE: Decoration offset is NOT auto-applied anymore.
    // If a level's decorations are offset, either:
    // 1. Re-export the map with origin at center in Blender
    // 2. Or manually adjust decoration coordinates in the level file
    // The auto-offset was causing issues with levels that had decorations placed correctly.

    // Add decorations
    for (int i = 0; i < level->decorationCount; i++) {{
        const DecoPlacement* d = &level->decorations[i];

        // Skip bolts that have already been collected in this save
        if (d->type == DECO_BOLT) {{
            if (save_is_bolt_collected(id, boltIndex)) {{
                debugf("Bolt %d in level %d already collected, skipping\\n", boltIndex, id);
                boltIndex++;
                continue;  // Don't spawn this bolt
            }}
            boltIndex++;
        }}

        // Skip golden screws that have already been collected in this save
        if (d->type == DECO_SCREWG) {{
            if (save_is_screwg_collected(id, screwgIndex)) {{
                debugf("Golden Screw %d in level %d already collected, skipping\\n", screwgIndex, id);
                screwgIndex++;
                continue;  // Don't spawn this golden screw
            }}
            screwgIndex++;
        }}

        int idx;

        // Use patrol version if patrol points are provided
        if (d->patrolPoints != NULL && d->patrolPointCount > 0) {{
            idx = map_add_decoration_patrol(runtime, d->type,
                d->x, d->y, d->z, d->rotY,
                d->scaleX, d->scaleY, d->scaleZ,
                d->patrolPoints, d->patrolPointCount);
        }} else {{
            idx = map_add_decoration(runtime, d->type,
                d->x, d->y, d->z, d->rotY,
                d->scaleX, d->scaleY, d->scaleZ);
        }}

        // Set transition target if this is a TransitionCollision or LevelTransition
        if (idx >= 0 && (d->type == DECO_TRANSITIONCOLLISION || d->type == DECO_LEVEL_TRANSITION)) {{
            runtime->decorations[idx].state.transition.targetLevel = d->targetLevel;
            runtime->decorations[idx].state.transition.targetSpawn = d->targetSpawn;
            debugf("TransitionCollision [%d] configured: Level %d, Spawn %d\\n",
                idx, d->targetLevel, d->targetSpawn);
        }}

        // Store bolt index in decoration for save tracking
        if (idx >= 0 && d->type == DECO_BOLT) {{
            runtime->decorations[idx].state.bolt.saveIndex = boltIndex - 1;
            runtime->decorations[idx].state.bolt.levelId = id;
            debugf("Bolt [%d] assigned saveIndex=%d, levelId=%d\\n",
                idx, boltIndex - 1, id);
        }}

        // Store golden screw index in decoration for save tracking
        if (idx >= 0 && d->type == DECO_SCREWG) {{
            runtime->decorations[idx].state.screwG.saveIndex = screwgIndex - 1;
            runtime->decorations[idx].state.screwG.levelId = id;
            debugf("Golden Screw [%d] assigned saveIndex=%d, levelId=%d\\n",
                idx, screwgIndex - 1, id);
        }}

        // Copy activation ID for buttons and activatable objects
        if (idx >= 0 && d->activationId > 0) {{
            runtime->decorations[idx].activationId = d->activationId;
            debugf("Decoration [%d] assigned activationId=%d\\n", idx, d->activationId);
        }}

        // Set script ID for dialogue triggers
        if (idx >= 0 && d->type == DECO_DIALOGUETRIGGER) {{
            runtime->decorations[idx].state.dialogueTrigger.scriptId = d->scriptId;
            debugf("DialogueTrigger [%d] assigned scriptId=%d\\n", idx, d->scriptId);
        }}

        // Set platform target for moving platforms (MOVE_PLAT, MOVE_COAL, MOVING_LASER, HIVE_MOVING use same behavior)
        if (idx >= 0 && (d->type == DECO_MOVE_PLAT || d->type == DECO_MOVE_COAL || d->type == DECO_MOVING_LASER || d->type == DECO_HIVE_MOVING)) {{
            runtime->decorations[idx].state.movePlat.targetX = d->platformTargetX;
            runtime->decorations[idx].state.movePlat.targetY = d->platformTargetY;
            runtime->decorations[idx].state.movePlat.targetZ = d->platformTargetZ;
            if (d->platformSpeed > 0.0f) {{
                runtime->decorations[idx].state.movePlat.speed = d->platformSpeed;
            }}
            // If startStationary, platform is visible but won't move until activated
            if (d->startStationary) {{
                runtime->decorations[idx].state.movePlat.activated = false;
                debugf("MovePlat [%d] starts stationary (waiting for activation)\\n", idx);
            }}
            debugf("MovePlat [%d] target: (%.1f, %.1f, %.1f) speed: %.1f\\n",
                idx, d->platformTargetX, d->platformTargetY, d->platformTargetZ, d->platformSpeed);
        }}

        // Set waypoints for moving rock (moves between up to 4 waypoints)
        if (idx >= 0 && d->type == DECO_MOVING_ROCK) {{
            // Waypoint 0 is the starting position (set by init from posX/Y/Z)
            // Waypoint 1 can use platformTarget or waypoint1 fields
            if (d->waypointCount >= 2) {{
                runtime->decorations[idx].state.movingRock.waypoints[1][0] = d->waypoint1X;
                runtime->decorations[idx].state.movingRock.waypoints[1][1] = d->waypoint1Y;
                runtime->decorations[idx].state.movingRock.waypoints[1][2] = d->waypoint1Z;
                runtime->decorations[idx].state.movingRock.waypointCount = d->waypointCount;
            }} else if (d->platformTargetX != 0.0f || d->platformTargetY != 0.0f || d->platformTargetZ != 0.0f) {{
                // Fallback: use platformTarget as waypoint 1 for 2-point movement
                runtime->decorations[idx].state.movingRock.waypoints[1][0] = d->platformTargetX;
                runtime->decorations[idx].state.movingRock.waypoints[1][1] = d->platformTargetY;
                runtime->decorations[idx].state.movingRock.waypoints[1][2] = d->platformTargetZ;
                runtime->decorations[idx].state.movingRock.waypointCount = 2;
            }}
            // Additional waypoints
            if (d->waypointCount >= 3) {{
                runtime->decorations[idx].state.movingRock.waypoints[2][0] = d->waypoint2X;
                runtime->decorations[idx].state.movingRock.waypoints[2][1] = d->waypoint2Y;
                runtime->decorations[idx].state.movingRock.waypoints[2][2] = d->waypoint2Z;
            }}
            if (d->waypointCount >= 4) {{
                runtime->decorations[idx].state.movingRock.waypoints[3][0] = d->waypoint3X;
                runtime->decorations[idx].state.movingRock.waypoints[3][1] = d->waypoint3Y;
                runtime->decorations[idx].state.movingRock.waypoints[3][2] = d->waypoint3Z;
            }}
            if (d->platformSpeed > 0.0f) {{
                runtime->decorations[idx].state.movingRock.speed = d->platformSpeed;
            }}
            // If startStationary, rock is visible but won't move until activated
            if (d->startStationary) {{
                runtime->decorations[idx].state.movingRock.activated = false;
                debugf("MovingRock [%d] starts stationary (waiting for activation)\\n", idx);
            }}
            debugf("MovingRock [%d] waypoints: %d, speed: %.1f\\n",
                idx, runtime->decorations[idx].state.movingRock.waypointCount, d->platformSpeed);
        }}

        // Set initial active state (for decorations that start inactive)
        if (idx >= 0 && d->startInactive) {{
            runtime->decorations[idx].active = false;
            debugf("Decoration [%d] starts inactive\\n", idx);
        }}

        // Set light color for DECO_LIGHT and DECO_LIGHT_NOMAP
        if (idx >= 0 && (d->type == DECO_LIGHT || d->type == DECO_LIGHT_NOMAP)) {{
            runtime->decorations[idx].state.light.colorR = d->lightColorR > 0 ? d->lightColorR : 255;
            runtime->decorations[idx].state.light.colorG = d->lightColorG > 0 ? d->lightColorG : 255;
            runtime->decorations[idx].state.light.colorB = d->lightColorB > 0 ? d->lightColorB : 255;
            runtime->decorations[idx].state.light.radius = d->lightRadius > 0 ? d->lightRadius : 2000.0f;
            debugf("Light [%d] color: (%d,%d,%d) radius: %.0f nomap: %d\\n",
                idx, runtime->decorations[idx].state.light.colorR,
                runtime->decorations[idx].state.light.colorG,
                runtime->decorations[idx].state.light.colorB,
                runtime->decorations[idx].state.light.radius,
                d->type == DECO_LIGHT_NOMAP ? 1 : 0);
        }}

        // Set checkpoint lighting colors
        if (idx >= 0 && d->type == DECO_CHECKPOINT) {{
            runtime->decorations[idx].state.checkpoint.ambientR = d->checkpointAmbientR;
            runtime->decorations[idx].state.checkpoint.ambientG = d->checkpointAmbientG;
            runtime->decorations[idx].state.checkpoint.ambientB = d->checkpointAmbientB;
            runtime->decorations[idx].state.checkpoint.directionalR = d->checkpointDirectionalR;
            runtime->decorations[idx].state.checkpoint.directionalG = d->checkpointDirectionalG;
            runtime->decorations[idx].state.checkpoint.directionalB = d->checkpointDirectionalB;
            if (d->checkpointAmbientR != 0 || d->checkpointAmbientG != 0 || d->checkpointAmbientB != 0 ||
                d->checkpointDirectionalR != 0 || d->checkpointDirectionalG != 0 || d->checkpointDirectionalB != 0) {{
                debugf("Checkpoint [%d] lighting: ambient=(%d,%d,%d) directional=(%d,%d,%d)\\n",
                    idx, d->checkpointAmbientR, d->checkpointAmbientG, d->checkpointAmbientB,
                    d->checkpointDirectionalR, d->checkpointDirectionalG, d->checkpointDirectionalB);
            }}
        }}

        // Set light trigger settings (controls entity light for player/decorations)
        // Reuses checkpoint lighting fields for colors, plus new direction fields
        if (idx >= 0 && d->type == DECO_LIGHT_TRIGGER) {{
            runtime->decorations[idx].state.lightTrigger.ambientR = d->checkpointAmbientR;
            runtime->decorations[idx].state.lightTrigger.ambientG = d->checkpointAmbientG;
            runtime->decorations[idx].state.lightTrigger.ambientB = d->checkpointAmbientB;
            runtime->decorations[idx].state.lightTrigger.directionalR = d->checkpointDirectionalR;
            runtime->decorations[idx].state.lightTrigger.directionalG = d->checkpointDirectionalG;
            runtime->decorations[idx].state.lightTrigger.directionalB = d->checkpointDirectionalB;
            // Light direction (999 = no change)
            runtime->decorations[idx].state.lightTrigger.lightDirX = d->lightTriggerDirX != 0 ? d->lightTriggerDirX : 999.0f;
            runtime->decorations[idx].state.lightTrigger.lightDirY = d->lightTriggerDirY != 0 ? d->lightTriggerDirY : 999.0f;
            runtime->decorations[idx].state.lightTrigger.lightDirZ = d->lightTriggerDirZ != 0 ? d->lightTriggerDirZ : 999.0f;
            // playerInside and hasSavedLighting are initialized by lighttrigger_init
            bool hasColor = d->checkpointAmbientR != 0 || d->checkpointAmbientG != 0 || d->checkpointAmbientB != 0 ||
                           d->checkpointDirectionalR != 0 || d->checkpointDirectionalG != 0 || d->checkpointDirectionalB != 0;
            bool hasDir = d->lightTriggerDirX != 0 || d->lightTriggerDirY != 0 || d->lightTriggerDirZ != 0;
            if (hasColor || hasDir) {{
                debugf("LightTrigger [%d] colors: ambient=(%d,%d,%d) entity=(%d,%d,%d) dir=(%.1f,%.1f,%.1f)\\n",
                    idx, d->checkpointAmbientR, d->checkpointAmbientG, d->checkpointAmbientB,
                    d->checkpointDirectionalR, d->checkpointDirectionalG, d->checkpointDirectionalB,
                    d->lightTriggerDirX, d->lightTriggerDirY, d->lightTriggerDirZ);
            }}
        }}

        // Set fog color trigger settings
        if (idx >= 0 && d->type == DECO_FOGCOLOR) {{
            runtime->decorations[idx].state.fogColor.fogR = d->fogColorR;
            runtime->decorations[idx].state.fogColor.fogG = d->fogColorG;
            runtime->decorations[idx].state.fogColor.fogB = d->fogColorB;
            runtime->decorations[idx].state.fogColor.fogNear = d->fogNear;
            runtime->decorations[idx].state.fogColor.fogFar = d->fogFar;
            // playerInside and hasSavedFog are initialized by fogcolor_init
            if (d->fogColorR != 0 || d->fogColorG != 0 || d->fogColorB != 0) {{
                debugf("FogColor [%d] color=(%d,%d,%d) range=(%.1f-%.1f)\\n",
                    idx, d->fogColorR, d->fogColorG, d->fogColorB, d->fogNear, d->fogFar);
            }}
        }}

        // Set permanent light trigger settings (reuses checkpoint lighting fields like DECO_LIGHT_TRIGGER)
        if (idx >= 0 && d->type == DECO_LIGHT_TRIGGER_PERMANENT) {{
            runtime->decorations[idx].state.lightTriggerPermanent.ambientR = d->checkpointAmbientR;
            runtime->decorations[idx].state.lightTriggerPermanent.ambientG = d->checkpointAmbientG;
            runtime->decorations[idx].state.lightTriggerPermanent.ambientB = d->checkpointAmbientB;
            runtime->decorations[idx].state.lightTriggerPermanent.directionalR = d->checkpointDirectionalR;
            runtime->decorations[idx].state.lightTriggerPermanent.directionalG = d->checkpointDirectionalG;
            runtime->decorations[idx].state.lightTriggerPermanent.directionalB = d->checkpointDirectionalB;
            // Light direction (999 = no change)
            runtime->decorations[idx].state.lightTriggerPermanent.lightDirX = d->lightTriggerDirX != 0 ? d->lightTriggerDirX : 999.0f;
            runtime->decorations[idx].state.lightTriggerPermanent.lightDirY = d->lightTriggerDirY != 0 ? d->lightTriggerDirY : 999.0f;
            runtime->decorations[idx].state.lightTriggerPermanent.lightDirZ = d->lightTriggerDirZ != 0 ? d->lightTriggerDirZ : 999.0f;
            // hasTriggered is initialized by lighttrigger_permanent_init
            bool hasColor = d->checkpointAmbientR != 0 || d->checkpointAmbientG != 0 || d->checkpointAmbientB != 0 ||
                           d->checkpointDirectionalR != 0 || d->checkpointDirectionalG != 0 || d->checkpointDirectionalB != 0;
            if (hasColor) {{
                debugf("LightTriggerPermanent [%d] colors: ambient=(%d,%d,%d) entity=(%d,%d,%d)\\n",
                    idx, d->checkpointAmbientR, d->checkpointAmbientG, d->checkpointAmbientB,
                    d->checkpointDirectionalR, d->checkpointDirectionalG, d->checkpointDirectionalB);
            }}
        }}
    }}

    // Start level music
    level_play_music(level->music);

    // Update save with current level
    save_set_current_level(id);

    debugf("Level loaded: %d segments, %d decorations\\n",
        level->segmentCount, level->decorationCount);
}}

// Get player start position for a level
static inline void level_get_player_start(LevelID id, float* x, float* y, float* z) {{
    if (id >= LEVEL_COUNT) {{
        *x = 0; *y = 50; *z = 0;
        return;
    }}
    const LevelData* level = ALL_LEVELS[id];
    *x = level->playerStartX;
    *y = level->playerStartY;
    *z = level->playerStartZ;
}}

// Get body part for a level (0=head, 1=torso, 2=arms, 3=legs)
static inline int level_get_body_part(LevelID id) {{
    if (id >= LEVEL_COUNT) {{
        return 1;  // Default to torso
    }}
    int part = ALL_LEVELS[id]->bodyPart;
    // Clamp to valid range (0=head, 1=torso, 2=arms, 3=legs)
    if (part < 0 || part > 3) part = 1;
    return part;
}}

// Get point light position for a level (returns false if level uses default directional only)
static inline bool level_get_point_light(LevelID id, float* x, float* y, float* z) {{
    if (id >= LEVEL_COUNT) {{
        return false;
    }}
    const LevelData* level = ALL_LEVELS[id];
    if (!level->hasPointLight) {{
        return false;
    }}
    *x = level->lightX;
    *y = level->lightY;
    *z = level->lightZ;
    return true;
}}

// Get directional light direction for a level (outputs normalized direction)
// Returns true if level has custom direction, false if using default (1,1,1)
static inline bool level_get_light_direction(LevelID id, float* x, float* y, float* z) {{
    if (id >= LEVEL_COUNT) {{
        *x = 1.0f; *y = 1.0f; *z = 1.0f;
        return false;
    }}
    const LevelData* level = ALL_LEVELS[id];
    // Check if custom direction is set (not all zeros)
    if (level->lightDirX == 0.0f && level->lightDirY == 0.0f && level->lightDirZ == 0.0f) {{
        *x = 1.0f; *y = 1.0f; *z = 1.0f;
        return false;
    }}
    *x = level->lightDirX;
    *y = level->lightDirY;
    *z = level->lightDirZ;
    return true;
}}

// Get ambient light color for a level (outputs RGB values 0-255)
// Returns true if level has custom ambient, false if using default (80,80,80)
static inline bool level_get_ambient_color(LevelID id, uint8_t* r, uint8_t* g, uint8_t* b) {{
    if (id >= LEVEL_COUNT) {{
        *r = 80; *g = 80; *b = 80;
        return false;
    }}
    const LevelData* level = ALL_LEVELS[id];
    // Check if custom ambient is set (not all zeros)
    if (level->ambientR == 0 && level->ambientG == 0 && level->ambientB == 0) {{
        *r = 80; *g = 80; *b = 80;
        return false;
    }}
    *r = level->ambientR;
    *g = level->ambientG;
    *b = level->ambientB;
    return true;
}}

// Get directional light color for a level (outputs RGB values 0-255)
// Returns true if level has custom color, false if using default (255,255,255)
static inline bool level_get_directional_color(LevelID id, uint8_t* r, uint8_t* g, uint8_t* b) {{
    if (id >= LEVEL_COUNT) {{
        *r = 255; *g = 255; *b = 255;
        return false;
    }}
    const LevelData* level = ALL_LEVELS[id];
    // Check if custom color is set (not all zeros)
    if (level->directionalR == 0 && level->directionalG == 0 && level->directionalB == 0) {{
        *r = 255; *g = 255; *b = 255;
        return false;
    }}
    *r = level->directionalR;
    *g = level->directionalG;
    *b = level->directionalB;
    return true;
}}

// Get background/clear color for a level (outputs RGB values 0-255)
// Always returns the level's bg color (default is 0,0,0 = black)
static inline void level_get_bg_color(LevelID id, uint8_t* r, uint8_t* g, uint8_t* b) {{
    if (id >= LEVEL_COUNT) {{
        *r = 0; *g = 0; *b = 0;
        return;
    }}
    const LevelData* level = ALL_LEVELS[id];
    *r = level->bgR;
    *g = level->bgG;
    *b = level->bgB;
}}

// Get fog settings for a level
// Returns true if fog is enabled, false otherwise
// Outputs fog color (RGB 0-255) and distance range
static inline bool level_get_fog_settings(LevelID id, uint8_t* r, uint8_t* g, uint8_t* b, float* fogNear, float* fogFar) {{
    if (id >= LEVEL_COUNT) {{
        *r = 0; *g = 0; *b = 0;
        *fogNear = 100.0f; *fogFar = 500.0f;
        return false;
    }}
    const LevelData* level = ALL_LEVELS[id];
    *r = level->fogR;
    *g = level->fogG;
    *b = level->fogB;
    // Use defaults if near/far are both 0
    if (level->fogNear == 0 && level->fogFar == 0) {{
        *fogNear = 100.0f;
        *fogFar = 500.0f;
    }} else {{
        *fogNear = level->fogNear;
        *fogFar = level->fogFar;
    }}
    return level->fogEnabled;
}}

#endif // LEVELS_GENERATED_H
'''

    with open(OUTPUT_FILE, 'w') as f:
        f.write(output)

    print(f"Generated {OUTPUT_FILE} with {len(levels)} level(s):")
    for i in sorted(levels.keys()):
        print(f"  - Level {i}: {levels[i]['data_name']}")
    if len(levels) < MAX_LEVELS:
        print(f"  - {MAX_LEVELS - len(levels)} placeholder slot(s)")

if __name__ == "__main__":
    main()
