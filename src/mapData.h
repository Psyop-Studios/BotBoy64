#ifndef MAP_DATA_H
#define MAP_DATA_H

#include <libdragon.h>

// Debug logging macro - stripped in release builds to prevent USB serial stalls
#ifndef NDEBUG
    #define DECO_DEBUG(...) debugf(__VA_ARGS__)
#else
    #define DECO_DEBUG(...) ((void)0)
#endif
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include "collision.h"
#include "collision_registry.h"
#include "mapLoader.h"
#include "constants.h"
#include "platform.h"
#include "scenes/demo_scene.h"
#include "save.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Sound effects (defined in game.c)
extern wav64_t sfxBoltCollect;
extern wav64_t sfxTurretFire;
extern wav64_t sfxTurretZap;

// Particle spawning (defined in game.c)
extern void game_spawn_splash_particles(float x, float y, float z, int count, uint8_t r, uint8_t g, uint8_t b);
extern void game_spawn_spark_particles(float x, float y, float z, int count);
extern void game_spawn_trail_particles(float x, float y, float z, int count);
extern void game_spawn_death_decal(float x, float y, float z, float scale, bool isLava);

// Lighting system (defined in game.c) - for checkpoint-triggered lighting changes
extern void game_trigger_lighting_change(uint8_t ambientR, uint8_t ambientG, uint8_t ambientB,
                                          uint8_t directionalR, uint8_t directionalG, uint8_t directionalB);
extern void game_trigger_lighting_change_ex(uint8_t ambientR, uint8_t ambientG, uint8_t ambientB,
                                             uint8_t entityDirectR, uint8_t entityDirectG, uint8_t entityDirectB,
                                             float entityDirX, float entityDirY, float entityDirZ);

// Slime performance profiling counters (defined in game.c)
extern uint32_t g_slimeGroundTicks;
extern uint32_t g_slimeDecoGroundTicks;
extern uint32_t g_slimeWallTicks;
extern uint32_t g_slimeMathTicks;
extern uint32_t g_slimeSpringTicks;
extern int g_slimeUpdateCount;

#define MAX_CHUNK_SEGMENTS 8
#define MAX_DECORATIONS 80
#define MAX_CHUNKS 16
#define DECO_TYPE_COUNT 73
#define MAX_DECO_ANIMS 4
#define MAX_ACTIVATION_IDS 32

// Global checkpoint state (set when player touches a checkpoint)
// Note: Defined in game.c - must use extern to share across compilation units
extern bool g_checkpointActive;
extern float g_checkpointX;
extern float g_checkpointY;
extern float g_checkpointZ;

// ============================================================
// ACTIVATION SYSTEM
// ============================================================
// Buttons set activation states, doors/other objects check them.
// Use the same activationId on a button and door to link them.
// Multiple buttons can activate the same ID (OR logic).
// Multiple objects can check the same ID.

// Activation state array - defined in game.c, shared across all files
extern bool g_activationState[MAX_ACTIVATION_IDS];

static inline void activation_set(int id, bool state) {
    if (id >= 0 && id < MAX_ACTIVATION_IDS) {
        g_activationState[id] = state;
    }
}

static inline bool activation_get(int id) {
    if (id >= 0 && id < MAX_ACTIVATION_IDS) {
        return g_activationState[id];
    }
    return false;
}

static inline void activation_reset_all(void) {
    for (int i = 0; i < MAX_ACTIVATION_IDS; i++) {
        g_activationState[i] = false;
    }
}

// ============================================================
// FPU SAFETY UTILITIES
// ============================================================

// Flush denormals to zero and clear FPU exception state
// This prevents MIPS FPU NOTIMPL exceptions from denormal floats
static inline void fpu_flush_denormals(void) {
    uint32_t fcr31;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcr31));
    fcr31 |= (1 << 24);     // FS bit: flush denormals to zero
    fcr31 &= ~(0x1F << 7);  // Clear exception enable bits
    fcr31 &= ~(0x3F << 2);  // Clear cause bits
    fcr31 &= ~(1 << 17);    // Clear unimplemented operation cause
    fcr31 &= ~(0x1F << 12); // Clear sticky FLAG bits
    __asm__ volatile("ctc1 %0, $31" : : "r"(fcr31));
}

// Check if a T3DSkeleton was properly initialized by t3d_skeleton_create()
// Validates all critical pointers that t3d_skeleton_create sets:
// - bones: allocated array of T3DBone
// - boneMatricesFP: allocated uncached matrix array
// - skeletonRef: reference to model's skeleton definition (CRITICAL: set by create, cleared by destroy)
static inline bool skeleton_is_valid(const T3DSkeleton* skel) {
    return skel != NULL &&
           skel->bones != NULL &&
           skel->boneMatricesFP != NULL &&
           skel->skeletonRef != NULL;
}

// ============================================================
// DECORATION TYPES
// ============================================================

typedef enum {
    DECO_BARREL,
    DECO_RAT,
    DECO_BOLT,
    DECO_IBEAM,
    DECO_ROCK,
    DECO_DOORCOLLIDER,
    DECO_PLAYERSPAWN,
    DECO_DAMAGECOLLISION,
    DECO_ELEVATOR,
    DECO_SIGN,
    DECO_TRANSITIONCOLLISION,
    DECO_PATROLPOINT,
    DECO_SLIME,
    DECO_SLIME_LAVA,  // Lava-colored slime variant with orange/red pools
    DECO_OILPUDDLE,
    DECO_COG,
    DECO_SPIKE,
    DECO_ROUNDBUTTON,
    DECO_CONVEYERLARGE,
    DECO_LASERWALL,
    DECO_LASERWALLOFF,
    DECO_FAN,
    DECO_DIALOGUETRIGGER,
    DECO_TOXICPIPE,
    DECO_TOXICRUNNING,
    DECO_CACTUS,
    DECO_INTERACTTRIGGER,
    DECO_JUKEBOX,
    DECO_MONITORTABLE,
    DECO_DISCOBALL,
    DECO_BULLDOZER,
    DECO_LEVEL3_STREAM,
    DECO_LEVEL3_WATERLEVEL,
    DECO_TORSO_PICKUP,
    DECO_HANGING_L,
    DECO_HANGING_S,
    DECO_LASER,  // Alias for LaserWall (used in level files)
    DECO_PAIN_TUBE,  // Invisible damage volume (1 damage on contact)
    DECO_CHARGEPAD,  // Buff-granting platform (2x jump/glide/speed based on body part)
    DECO_TURRET,     // Rotating turret that fires projectiles at player
    DECO_DROID_SEC,  // Security droid enemy (shoots projectiles, 3HP, activates on death)
    DECO_DROID_BULLET,  // Projectile fired by security droid
    DECO_FAN2,  // Second fan type (single model with spinning animation)
    DECO_LAVAFLOOR,  // Lava floor with scrolling texture
    DECO_LAVAFALLS,  // Lava waterfall with scrolling texture
    DECO_GRINDER,    // Rotating grinder (spins like cog, damages player)
    DECO_CUTSCENE_FALLOFF,  // Triggers cutscene where robot falls off cliff losing body parts
    DECO_SINK_PLAT,  // Platform that sinks when player stands on it
    DECO_SPINNER,   // Spinning spinner decoration
    DECO_MOVE_PLAT,  // Platform that moves between start and target positions
    DECO_HOTPIPE,    // Hot pipe - solid collision, damages player unless invincible
    DECO_CHECKPOINT, // Checkpoint - sets new spawn point when touched
    DECO_MOVE_COAL,  // Moving coal platform (same as MOVE_PLAT but different model)
    DECO_LIGHT,      // Point light source with configurable color
    DECO_LIGHT_NOMAP, // Point light that only affects player/decorations, not map geometry
    DECO_LIGHT_TRIGGER, // Trigger zone that changes lighting without setting checkpoint
    DECO_MOVING_ROCK,   // Rock that moves between up to 4 waypoints (for conveyor sequences)
    DECO_FOGCOLOR,      // Trigger zone that changes fog color when player is inside
    DECO_LIGHT_TRIGGER_PERMANENT, // Trigger that permanently changes lighting until another is touched
    DECO_FLOATING_ROCK,   // Floating rock platform that sinks when stood on
    DECO_FLOATING_ROCK2,  // Alternate floating rock platform that sinks when stood on
    DECO_SPIN_ROCK,       // Spinning rock platform that sinks when stood on
    DECO_SPIN_ROCK2,      // Alternate spinning rock platform that sinks when stood on
    DECO_TURRET_PULSE,    // Turret that fires slow homing projectiles that damage on hit
    DECO_LAVA_RIVER,      // Static lava river decoration
    DECO_SCREWG,          // Golden screw - rare collectible tracked separately from regular bolts
    DECO_DAMAGECUBE_LIGHT, // Damage cube that does 1 damage instead of instant death
    DECO_MOVING_LASER,     // Always-on laser wall that does 1 damage (uses custom collision)
    DECO_HIVE_MOVING,      // Platform that moves to target when stood on, returns when player gets off
    DECO_STAGE7,           // Visual-only stage decoration for level 7 (no collision)
    DECO_CS_2,             // Cutscene 2 trigger - plays slideshow cutscene
    DECO_LEVEL_TRANSITION, // Immediate level transition (fade to black, no celebration)
    DECO_CS_3,             // Cutscene 3 trigger - plays ending slideshow, returns to menu
    DECO_NONE,
} DecoType;

// Forward declarations
struct DecoInstance;
struct MapRuntime;

// Behavior function types
typedef void (*DecoInitFn)(struct DecoInstance* inst, struct MapRuntime* map);
typedef void (*DecoUpdateFn)(struct DecoInstance* inst, struct MapRuntime* map, float deltaTime);
typedef void (*DecoOnPlayerCollideFn)(struct DecoInstance* inst, struct MapRuntime* map, float playerX, float playerY, float playerZ);

// Per-instance custom state (union for different decoration types)
typedef union {
    // Rat enemy state
    struct {
        float moveDir;      // Current movement direction
        float moveTimer;    // Time until direction change
        float attackCooldown;
        bool isAggro;
        float speed;
        float velY;         // Vertical velocity for gravity
        bool isGrounded;
        int currentAnim;    // Animation index (shared skeleton)
        float animTimeOffset; // Random offset to desync animations
        T3DVec3* patrolPoints; // Array of patrol points
        int patrolPointCount;  // Number of patrol points
        int currentPatrolIndex; // Current target patrol point index
        float pauseTimer;   // Countdown timer for pausing at waypoints
        // Pounce state
        bool isPouncing;        // Currently in pounce animation
        float pounceTimer;      // Time remaining in pounce
        float pounceVelX;       // Horizontal velocity during pounce
        float pounceVelZ;       // Horizontal velocity during pounce
        float pounceCooldown;   // Cooldown until can pounce again
        // Spawn position (for respawn on fall)
        float spawnX;
        float spawnY;
        float spawnZ;
        float spawnRotY;
    } rat;

    // Bolt state
    struct {
        bool collected;
        bool wasPreCollected;  // True if bolt was already collected (from save) - show grayed out
        float spinAngle;
        int saveIndex;       // Index in save file's bolt bitmask
        int levelId;         // Level this bolt belongs to
    } bolt;

    // Golden screw state (rare collectible)
    struct {
        bool collected;
        float spinAngle;
        int saveIndex;       // Index in save file's golden screw bitmask
        int levelId;         // Level this golden screw belongs to
    } screwG;

    // Barrel state
    struct {
        int health;
        bool destroyed;
    } barrel;

    // Collider state
    struct {
        bool triggered;
    } collider;
    
    struct {
        float elevatorY;
        float targetY;
        float speed;
        float lastDelta;  // Last frame's Y movement for carrying player
        bool moving;
    } elevator;

    // Sink platform state (sinks when player stands on it)
    struct {
        float originalY;     // Starting Y position
        float currentY;      // Current Y offset from original
        float sinkDepth;     // How far to sink (set from scaleY)
        float sinkSpeed;     // How fast to sink
        float riseSpeed;     // How fast to rise back up
        float lastDelta;     // Last frame's Y movement for carrying player
        bool playerOnPlat;   // Is player currently on platform?
    } sinkPlat;

    // Moving platform state (moves between start and target positions)
    struct {
        float startX, startY, startZ;   // Starting position
        float targetX, targetY, targetZ; // Target position
        float progress;                  // 0.0 = at start, 1.0 = at target
        float speed;                     // Movement speed (units/sec)
        int direction;                   // 1 = moving to target, -1 = moving to start
        float lastDeltaX, lastDeltaY, lastDeltaZ;  // Last frame movement for carrying player
        bool playerOnPlat;               // Is player currently on platform?
        bool activated;                  // Is platform active/moving?
        float returnDelayTimer;          // Timer before platform returns (for DECO_HIVE_MOVING)
        int lastCheckFrame;              // Last frame player-on-platform was checked (for staggering)
    } movePlat;

    // Transition collision state
    struct {
        int targetLevel;     // Which level to load (0-based index into LEVEL enum)
        int targetSpawn;     // Which PlayerSpawn to use in that level (0 = first, 1 = second, etc.)
        bool triggered;      // Has been triggered this frame
    } transition;

    // Sign state (tilts when player stands on sign part)
    struct {
        float tilt;          // Current tilt angle
        float tiltVel;       // Tilt velocity
        float baseRotY;      // Original Y rotation (pole twist base)
        bool playerOnSign;   // Is player standing on the sign board?
    } sign;

    // Slime enemy state - spring-mass physics for jiggle
    struct {
        // Physics state
        float velX;              // Horizontal velocity X (for arc movement)
        float velY;              // Vertical velocity for jumping
        float velZ;              // Horizontal velocity Z (for arc movement)
        bool isGrounded;         // Is slime on ground?
        float jumpTimer;         // Timer until next jump
        float moveDir;           // Current movement direction (radians)
        float windupTimer;       // Anticipation windup before jump (0 = not winding up)

        // Spring jiggle state (point mass above slime base)
        float jiggleX;           // Jiggle point X offset from base
        float jiggleZ;           // Jiggle point Z offset from base
        float jiggleVelX;        // Jiggle velocity X
        float jiggleVelZ;        // Jiggle velocity Z

        // Stretch state (vertical squash/stretch)
        float stretchY;          // Current Y scale factor (1.0 = normal)
        float stretchVelY;       // Stretch velocity

        // AI state
        bool isAggro;            // Is chasing player?
        float attackCooldown;    // Seconds until slime can damage player again

        // Health and splitting
        int health;              // Hits remaining (1 for small, 2 for medium, 3 for large)
        bool pendingSplit;       // Should spawn child slimes this frame?
        float mergeTimer;        // Cooldown before slime can merge (prevents instant re-merge)
        float shakeTimer;        // Shake effect timer (decays to 0)
        bool isDying;            // Is in death animation?
        float deathTimer;        // Timer for death animation (shrink to nothing)
        float invincibleTimer;   // Invincibility after spawning (prevents instant death)

        // Oil trail decals (circular buffer of 5)
        float decalX[5];         // Decal world X positions
        float decalY[5];         // Decal world Y positions (ground height)
        float decalZ[5];         // Decal world Z positions
        float decalAlpha[5];     // Decal alpha (fades over time)
        float decalScale;        // Decal scale (based on slime scale)
        int decalHead;           // Next decal slot to write
        int decalCount;          // Number of active decals (0-5)
        // Spawn position (for respawn on fall)
        float spawnX;
        float spawnY;
        float spawnZ;
        float spawnRotY;
    } slime;

    // Oil puddle - slippery hazard
    struct {
        float radius;            // Puddle radius (from scaleX)
    } oilpuddle;

    // Cog - rotating platform player can ride
    struct {
        float rotSpeed;          // Rotation speed (radians per second)
        float prevRotZ;          // Previous frame's Z rotation
        float playerDeltaX;      // Movement to apply to player this frame
        float playerDeltaY;      // Vertical movement to apply to player
        float playerDeltaZ;      // Movement to apply to player this frame
        float lastGroundY;       // Last frame's ground height at player pos
        bool playerOnCog;        // Is player standing on this cog?
        float wallPushX;         // Wall collision push X
        float wallPushZ;         // Wall collision push Z
        bool hitWall;            // Did player hit cog wall this frame?
    } cog;

    // Round button state
    struct {
        bool pressed;            // Is button currently pressed?
        float pressDepth;        // How far the button top is pushed down (0-1)
        float pressVel;          // Velocity of press animation
    } button;

    // Conveyor belt state
    struct {
        float textureOffset;     // Current texture scroll offset (0-1, wraps)
        float speed;             // Belt speed (units per second)
        bool playerOnBelt;       // Is player standing on this conveyor?
        float pushX, pushZ;      // Direction to push player (based on belt rotation)
    } conveyor;

    // Fan state
    struct {
        float spinAngle;         // Current rotation of fan top
        bool playerInStream;     // Is player in the air stream?
    } fan;

    // Fan2 state (single-model fan with same wind behavior)
    struct {
        float spinAngle;         // Current rotation angle
        bool playerInStream;     // Is player in the air stream?
    } fan2;

    // Dialogue trigger state
    struct {
        int scriptId;            // Which script to run (index into script array)
        float triggerRadius;     // How close player must be to trigger
        bool triggered;          // Has been triggered (prevents re-trigger while active)
        bool onceOnly;           // If true, only trigger once ever
        bool hasTriggered;       // For once-only triggers, tracks if already triggered
    } dialogueTrigger;

    // Interact trigger state (optional dialogue with A button prompt)
    struct {
        int scriptId;            // Which script to run
        float triggerRadius;     // How close player must be
        float lookAtAngle;       // Direction player should face when interacting (radians)
        bool playerInRange;      // Is player currently in range?
        bool interacting;        // Currently showing dialogue?
        float savedPlayerAngle;  // Player's angle before interaction (to restore after)
        bool onceOnly;           // If true, only trigger once ever
        bool hasTriggered;       // For once-only triggers, tracks if already triggered
    } interactTrigger;

    // Toxic pipe state (pipe + liquid with scrolling texture)
    struct {
        float textureOffset;     // Current texture scroll offset (0-1, wraps)
    } toxicPipe;

    // Toxic running state (scrolling texture)
    struct {
        float textureOffset;     // Current texture scroll offset (0-1, wraps)
    } toxicRunning;

    // Lava floor state (scrolling texture)
    struct {
        float textureOffset;     // Current texture scroll offset (0-1, wraps)
    } lavaFloor;

    // Lava falls state (scrolling texture)
    struct {
        float textureOffset;     // Current texture scroll offset (0-1, wraps)
    } lavaFalls;

    // Grinder - rotating like cog (visual only, no player riding)
    struct {
        float rotSpeed;          // Rotation speed (radians per second)
    } grinder;

    // Spinner - spinning decoration
    struct {
        float rotSpeed;          // Rotation speed (radians per second)
    } spinner;

    // Spin rock - sinking platform that also rotates
    struct {
        float originalY;     // Original Y position
        float currentY;      // How far down from original (0 = at rest)
        float sinkDepth;     // Maximum sink depth (scaled by scaleY)
        float sinkSpeed;     // How fast to sink
        float riseSpeed;     // How fast to rise back up
        float lastDelta;     // Last frame's Y movement for carrying player
        bool playerOnPlat;   // Is player currently on platform?
        float rotSpeed;      // Rotation speed (radians per second)
    } spinRock;

    // Jukebox state (animated with scrolling FX texture)
    struct {
        float textureOffset;     // Current texture scroll offset for FX (0-1, wraps)
        bool isPlaying;          // Is music currently playing from this jukebox?
        float blendIn;           // Blend factor from idle to animation (0-1)
    } jukebox;

    // Monitor table state (table + monitor with scrolling screen texture)
    struct {
        float textureOffset;     // Current texture scroll offset (0-1, wraps)
    } monitorTable;

    // Disco ball state (descends and rotates when jukebox plays)
    struct {
        float startY;            // Starting Y position (up high)
        float targetY;           // Target Y position (lowered)
        float currentY;          // Current Y offset from original position
        float rotation;          // Current rotation angle
        bool isActive;           // Is the disco ball active (descending/spinning)?
        bool isSpinning;         // Has it finished descending and started spinning?
    } discoBall;

    // Bulldozer enemy state (chases player and pushes them)
    struct {
        float velX;              // Current velocity X
        float velZ;              // Current velocity Z
        float velY;              // Vertical velocity for gravity
        bool isGrounded;         // Is bulldozer on ground?
        float moveDir;           // Current movement direction (radians)
        float pushCooldown;      // Cooldown after pushing player
        float damageCooldown;    // Cooldown between damage ticks
        float spawnX;            // Spawn position X (for patrol and respawn)
        float spawnY;            // Spawn position Y (for respawn)
        float spawnZ;            // Spawn position Z (for patrol and respawn)
        float spawnRotY;         // Spawn rotation (for respawn)
        float speed;             // Movement speed
        T3DVec3* patrolPoints;   // Array of patrol points (like rat)
        int patrolPointCount;    // Number of patrol points
        int currentPatrolIndex;  // Current target patrol point index
        float pauseTimer;        // Countdown timer for pausing at waypoints
        bool chasingPlayer;      // Currently chasing player (overrides patrol)
        float cliffPauseTimer;   // Pause at cliff edge before reversing
        float reverseTimer;      // Time to reverse away from cliff
    } bulldozer;

    // Torso pickup state (collectible body part)
    struct {
        bool collected;          // Has been collected?
        float spinAngle;         // Current Y rotation for spinning
        float bobOffset;         // Current bob offset for bouncing
        float bobPhase;          // Phase offset for bob animation
        int demoIndex;           // Which demo to play after collection (tutorial)
    } torsoPickup;

    // Hanging platform state (spinning back and forth)
    struct {
        float spinPhase;         // Current phase in spin cycle (0 to 2*PI)
        float spinSpeed;         // Speed of oscillation (radians per second)
        float spinAmplitude;     // Max spin angle (radians, ~15 degrees = 0.26)
        float baseRotY;          // Original Y rotation to oscillate around
        float prevRotY;          // Previous frame's Y rotation (for delta calc)
        float playerDeltaX;      // Movement to apply to player this frame
        float playerDeltaZ;      // Movement to apply to player this frame
        bool playerOnPlatform;   // Is player standing on this platform?
    } hanging;

    // Laser wall state
    struct {
        bool isOn;               // Is laser currently active? (true = on, deals damage)
    } laser;

    // Chargepad state (buff-granting platform)
    struct {
        float sparkTimer;        // Timer for spark particle spawning (unused, kept for save compat)
        float cooldownTimer;     // Cooldown after buff is granted (prevents instant re-trigger)
        bool isActive;           // Currently active (can grant buff)
        bool glowing;            // Should render with blue glow (active and player has no buff)
    } chargepad;

    // Turret state (sniper-style cannon that locks on and fires)
    struct {
        float cannonRotY;        // Current Y rotation of cannon (tracks player horizontally)
        float cannonRotX;        // Current X rotation of cannon (pitch - tracks player vertically)
        float trackingSpeed;     // How fast cannon rotates to track player
        bool isActive;           // Is turret active and firing?
        int health;              // Turret health (destroyed at 0)
        bool isDead;             // Is turret destroyed?
        int cachedIndex;         // Cached index in decorations array (optimization)
        bool animInitialized;    // Whether initial animation frame has been applied (fixes spawn pose)
        // Sniper lock-on state
        float lockOnTimer;       // Time spent aiming at player (counts up to TURRET_LOCKON_TIME)
        bool lockOnComplete;     // Whether turret has locked on
        bool zapPlayed;          // Whether firing zap sound has been played
        bool isFiring;           // Whether turret is in firing sequence (frozen aim)
        float fireTimer;         // Timer for firing sequence (warning -> projectile -> hitscan)
        float shotDelay;         // Countdown after rail fires before hitscan (TURRET_HITSCAN_DELAY)
        bool railFired;          // Whether rail has been fired (waiting for hitscan)
        bool hitPlayer;          // Hitscan result - did we hit the player?
        // Locked aim direction (stored when zap plays, used when firing)
        float lockedAimX;        // Aim direction X at lock time
        float lockedAimY;        // Aim direction Y at lock time
        float lockedAimZ;        // Aim direction Z at lock time
        float lockedCannonRotX;  // Cannon pitch at lock time
        float lockedCannonRotY;  // Cannon yaw at lock time
        // Projectile pool (visual only - damage is hitscan)
        float projPosX[4];       // Projectile X positions
        float projPosY[4];       // Projectile Y positions
        float projPosZ[4];       // Projectile Z positions
        float projVelX[4];       // Projectile X velocities
        float projVelY[4];       // Projectile Y velocities (slight gravity)
        float projVelZ[4];       // Projectile Z velocities
        float projRotX[4];       // Projectile X rotation (pitch at fire time)
        float projRotY[4];       // Projectile Y rotation (yaw at fire time)
        float projLife[4];       // Remaining lifetime (0 = inactive)
        int activeProjectiles;   // Count of active projectiles
    } turret;

    // Pulse turret state (slow homing projectiles that damage on hit)
    struct {
        float cannonRotY;        // Current Y rotation of cannon
        float cannonRotX;        // Current X rotation of cannon (pitch)
        float trackingSpeed;     // How fast cannon rotates to track player
        bool isActive;           // Is turret active?
        int health;              // Turret health (destroyed at 0)
        bool isDead;             // Is turret destroyed?
        bool animInitialized;    // Whether initial animation frame has been applied (fixes spawn pose)
        float fireCooldown;      // Time until next shot
        // Projectile pool (actual damage projectiles)
        float projPosX[4];       // Projectile X positions
        float projPosY[4];       // Projectile Y positions
        float projPosZ[4];       // Projectile Z positions
        float projVelX[4];       // Projectile X velocities
        float projVelY[4];       // Projectile Y velocities
        float projVelZ[4];       // Projectile Z velocities
        float projLife[4];       // Remaining lifetime (0 = inactive)
        // Lagged target positions (for chasing behavior - aims where player WAS)
        float projTargetX[4];    // Lagged target X (slowly catches up to player)
        float projTargetY[4];    // Lagged target Y
        float projTargetZ[4];    // Lagged target Z
        int activeProjectiles;   // Count of active projectiles
    } pulseTurret;

    // Security droid state
    struct {
        int health;              // Current HP (starts at 3)
        bool isAggro;            // Has detected player (combat mode)
        bool isDead;             // Death animation playing
        float shootCooldown;     // Time until can shoot again
        float shootAnimTimer;    // Timer for shoot animation (1.25s sec_shoot)
        bool hasFiredProjectile; // Has projectile been spawned this attack cycle?
        float deathTimer;        // Timer for death animation
        int currentAnim;         // Current animation index
        // Physics
        float velY;              // Vertical velocity (for gravity)
        bool isGrounded;         // Is droid on ground?
        // Spawn position (for respawn on fall)
        float spawnX;            // Initial spawn X position
        float spawnY;            // Initial spawn Y position
        float spawnZ;            // Initial spawn Z position
        float spawnRotY;         // Initial spawn rotation
        // Patrol state
        float moveDir;           // Current movement direction (radians)
        float walkSpeed;         // Movement speed (units/sec)
        float idlePauseTimer;    // Countdown for idle pause between patrol legs
        bool isIdling;           // Currently idle (pausing)
        // Cliff detection
        bool cliffAhead;         // Is there a cliff in front of the droid?
        float cliffShootCooldown;  // Halved cooldown when cliff-blocked
        // Projectile pool
        float projPosX[4];       // Projectile X positions
        float projPosY[4];       // Projectile Y positions
        float projPosZ[4];       // Projectile Z positions
        float projVelX[4];       // Projectile X velocities
        float projVelY[4];       // Projectile Y velocities
        float projVelZ[4];       // Projectile Z velocities
        float projLife[4];       // Remaining lifetime (0 = inactive)
        int activeProjectiles;   // Count of active projectiles
    } droid;

    // Droid bullet state
    struct {
        float velX;              // Velocity X
        float velY;              // Velocity Y
        float velZ;              // Velocity Z
        float lifetime;          // Time before bullet despawns
        int ownerIndex;          // Index of droid that fired this bullet
    } bullet;

    // Cutscene falloff trigger state
    struct {
        float triggerRadius;     // How close player must be to trigger
        bool triggered;          // Has cutscene been triggered?
        bool playing;            // Is cutscene currently playing?
        float animTime;          // Current animation time
    } cutsceneFalloff;

    // Generic state
    struct {
        int intData[4];
        float floatData[4];
    } generic;

    // Light state (point light with color)
    struct {
        uint8_t colorR, colorG, colorB;  // Light color
        float radius;                     // Light radius/range
    } light;

    // Checkpoint state (lighting changes when activated)
    struct {
        uint8_t ambientR, ambientG, ambientB;       // Target ambient color (0=no change)
        uint8_t directionalR, directionalG, directionalB;  // Target directional color (0=no change)
    } checkpoint;

    // Light trigger state (changes entity lighting while player is inside)
    // Note: "directional" here means ENTITY light (player/decorations only)
    // Map light is static and cannot be changed at runtime
    struct {
        uint8_t ambientR, ambientG, ambientB;       // Target ambient color (0=no change)
        uint8_t directionalR, directionalG, directionalB;  // Target entity light color (0=no change)
        float lightDirX, lightDirY, lightDirZ;      // Target entity light direction (999=no change)
        bool playerInside;                           // Is player currently inside this trigger?
        // Stored previous lighting to revert to when player exits
        uint8_t prevAmbientR, prevAmbientG, prevAmbientB;
        uint8_t prevDirectionalR, prevDirectionalG, prevDirectionalB;
        float prevLightDirX, prevLightDirY, prevLightDirZ;
        bool hasSavedLighting;                       // Have we saved the previous lighting?
    } lightTrigger;

    // Fog color trigger state (changes fog color while player is inside)
    struct {
        uint8_t fogR, fogG, fogB;           // Target fog color
        float fogNear, fogFar;               // Target fog distance range (0,0 = keep level fog range)
        bool playerInside;                   // Is player currently inside this trigger?
        // Stored previous fog to revert to when player exits
        uint8_t prevFogR, prevFogG, prevFogB;
        float prevFogNear, prevFogFar;
        bool hasSavedFog;                    // Have we saved the previous fog?
        float blendProgress;                 // Current blend (0=prev fog, 1=target fog)
    } fogColor;

    // Permanent light trigger state (changes lighting permanently until another is touched)
    struct {
        uint8_t ambientR, ambientG, ambientB;       // Target ambient color
        uint8_t directionalR, directionalG, directionalB;  // Target entity light color
        float lightDirX, lightDirY, lightDirZ;      // Target entity light direction (999=no change)
        bool hasTriggered;                           // Has this trigger been activated this entry?
    } lightTriggerPermanent;

    // Moving rock state (moves between up to 4 waypoints in a loop)
    struct {
        float waypoints[4][3];       // Up to 4 waypoints (x, y, z for each)
        int waypointCount;           // Number of valid waypoints (2-4)
        int currentTarget;           // Index of current target waypoint (0-3)
        float progress;              // 0.0 = at current waypoint, 1.0 = at target
        float speed;                 // Movement speed (units/sec)
        float lastDeltaX, lastDeltaY, lastDeltaZ;  // Last frame movement for carrying player
        bool playerOnPlat;           // Is player currently on platform?
        bool activated;              // Is platform active/moving?
        int lastCheckFrame;          // Last frame player-on-platform was checked (for staggering)
    } movingRock;
} DecoState;

// ============================================================
// DEFERRED AI PROCESSING (distance-based LOD optimization)
// ============================================================

typedef enum {
    AI_UPDATE_EVERY_FRAME,   // Close enemies, attacking, or just hit
    AI_UPDATE_HALF,          // Medium distance (every 2 frames)
    AI_UPDATE_QUARTER,       // Far distance (every 4 frames)
    AI_UPDATE_EIGHTH,        // Very far (every 8 frames)
    AI_UPDATE_PAUSED         // Off-screen or extremely far - skip updates
} AIUpdateFrequency;

// Distance thresholds for AI update frequency
#define AI_DIST_CLOSE     450.0f   // Within this: every frame
#define AI_DIST_MEDIUM    900.0f   // Within this: every 2 frames
#define AI_DIST_FAR       1575.0f  // Within this: every 4 frames
#define AI_DIST_VERY_FAR  2250.0f  // Within this: every 8 frames
                                    // Beyond: paused

// Squared distance thresholds (avoid sqrtf for comparisons)
#define AI_DIST_CLOSE_SQ     (AI_DIST_CLOSE * AI_DIST_CLOSE)         // 202500
#define AI_DIST_MEDIUM_SQ    (AI_DIST_MEDIUM * AI_DIST_MEDIUM)       // 810000
#define AI_DIST_FAR_SQ       (AI_DIST_FAR * AI_DIST_FAR)             // 2480625
#define AI_DIST_VERY_FAR_SQ  (AI_DIST_VERY_FAR * AI_DIST_VERY_FAR)   // 5062500

// ============================================================
// DECORATION INSTANCE (per-placed decoration)
// ============================================================

typedef struct DecoInstance {
    DecoType type;
    float posX, posY, posZ;
    float rotX, rotY, rotZ;
    float scaleX, scaleY, scaleZ;
    bool active;            // Is this decoration alive/active?
    bool initialized;       // Has init been called?
    DecoState state;        // Per-instance state
    int id;                 // Unique instance ID
    int activationId;       // Links to activation system (0 = none)

    // Deferred AI processing
    AIUpdateFrequency aiUpdateFreq;  // How often to update this decoration's AI
    bool forceUpdate;                // Force update next frame (damage, alert, etc.)

    // Per-instance animation (for enemies that need independent animations)
    bool hasOwnSkeleton;
    T3DSkeleton skeleton;
    T3DAnim anims[MAX_DECO_ANIMS];
    int animCount;
} DecoInstance;

// ============================================================
// DECORATION TYPE INFO (shared per type)
// ============================================================

typedef struct {
    const char* modelPath;
    const char* collisionPath;  // Optional: custom collision file path (NULL = derive from modelPath)
    const char* name;
    DecoInitFn initFn;
    DecoUpdateFn updateFn;
    DecoOnPlayerCollideFn onPlayerCollideFn;
    bool vertexColorsOnly;  // True if model uses ONLY vertex colors (no textures)
    float collisionScale;   // Multiplier for collision radius (0 = use default 1.0)
} DecoTypeDef;

// Decoration type runtime data (shared skeleton/anim for all instances)
typedef struct {
    T3DModel* model;
    CollisionMesh* collision;
    bool loaded;
    bool hasSkeleton;
    T3DSkeleton skeleton;
    T3DSkeleton skeletonIdle;   // For blending (stores idle pose)
    T3DAnim anims[MAX_DECO_ANIMS];
    int animCount;
    int currentAnim;
    // Cached AABB for collision mesh (avoids rebuilding every frame)
    float aabbMinX, aabbMinY, aabbMinZ;
    float aabbMaxX, aabbMaxY, aabbMaxZ;
    bool aabbCached;
} DecoTypeRuntime;

// ============================================================
// BEHAVIOR FUNCTION DECLARATIONS
// ============================================================

// Rat behavior
static void rat_init(DecoInstance* inst, struct MapRuntime* map);
static void rat_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void rat_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Player spawn point - snaps to ground on init so player never hovers
static void playerspawn_init(DecoInstance* inst, struct MapRuntime* map);

// Bolt behavior
static void bolt_init(DecoInstance* inst, struct MapRuntime* map);
static void bolt_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void bolt_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Golden screw behavior (rare collectible)
static void screwg_init(DecoInstance* inst, struct MapRuntime* map);
static void screwg_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void screwg_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Damage collision behavior
static void damagecollision_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);
static void damagecube_light_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);
static void movinglaser_init(DecoInstance* inst, struct MapRuntime* map);
static void movinglaser_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void movinglaser_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Pain tube behavior (invisible damage volume, 1 damage)
static void paintube_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Chargepad behavior (grants buffs based on body part)
static void chargepad_init(DecoInstance* inst, struct MapRuntime* map);
static void chargepad_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void chargepad_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Turret behavior (rotating cannon that fires projectiles at player)
static void turret_init(DecoInstance* inst, struct MapRuntime* map);
static void turret_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void turret_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);
static inline bool turret_check_projectile_collision(DecoInstance* inst, float px, float py, float pz, float playerRadius, float playerHeight);

// Pulse turret behavior (slow homing projectiles that damage on hit)
static void turret_pulse_init(DecoInstance* inst, struct MapRuntime* map);
static void turret_pulse_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void turret_pulse_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);
static inline bool turret_pulse_check_projectile_collision(DecoInstance* inst, float px, float py, float pz, float playerRadius, float playerHeight);

// Security droid behavior (shooting enemy with 3HP, activates on death)
static void droid_sec_init(DecoInstance* inst, struct MapRuntime* map);
static void droid_sec_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void droid_sec_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Droid bullet behavior (projectile fired by security droid)
static void droid_bullet_init(DecoInstance* inst, struct MapRuntime* map);
static void droid_bullet_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void droid_bullet_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Elevator behavior
static void elevator_init(DecoInstance* inst, struct MapRuntime* map);
static void elevator_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Transition collision behavior
static void transition_init(DecoInstance* inst, struct MapRuntime* map);
static void transition_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Immediate level transition behavior (fade to black, no celebration)
static void level_transition_init(DecoInstance* inst, struct MapRuntime* map);
static void level_transition_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Sign behavior (tilts when player stands on sign board)
static void sign_init(DecoInstance* inst, struct MapRuntime* map);
static void sign_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Hanging platform behavior (pendulum swing)
static void hanging_init(DecoInstance* inst, struct MapRuntime* map);
static void hanging_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Slime behavior (jumping enemy with spring physics jiggle)
static void slime_init(DecoInstance* inst, struct MapRuntime* map);
static void slime_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void slime_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Oil puddle behavior (slippery hazard)
static void oilpuddle_init(DecoInstance* inst, struct MapRuntime* map);
static void oilpuddle_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Cog behavior (rotating platform player can ride)
static void cog_init(DecoInstance* inst, struct MapRuntime* map);
static void cog_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Round button behavior (pressable button that activates linked objects)
static void roundbutton_init(DecoInstance* inst, struct MapRuntime* map);
static void roundbutton_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Conveyor belt behavior (pushes player when activated)
static void conveyor_init(DecoInstance* inst, struct MapRuntime* map);
static void conveyor_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Laser wall behavior (damages player, can be deactivated)
static void laserwall_init(DecoInstance* inst, struct MapRuntime* map);
static void laserwall_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void laserwall_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Fan behavior (blows player up when activated)
static void fan_init(DecoInstance* inst, struct MapRuntime* map);
static void fan_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Fan2 behavior (single model fan with wind)
static void fan2_init(DecoInstance* inst, struct MapRuntime* map);
static void fan2_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Dialogue trigger behavior
static void dialoguetrigger_init(DecoInstance* inst, struct MapRuntime* map);
static void dialoguetrigger_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Interact trigger behavior (optional dialogue with A button prompt)
static void interacttrigger_init(DecoInstance* inst, struct MapRuntime* map);
static void interacttrigger_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Toxic pipe behavior (pipe + liquid with scrolling texture)
static void toxicpipe_init(DecoInstance* inst, struct MapRuntime* map);
static void toxicpipe_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Toxic running behavior (scrolling texture)
static void toxicrunning_init(DecoInstance* inst, struct MapRuntime* map);
static void toxicrunning_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Lava floor behavior (scrolling texture)
static void lavafloor_init(DecoInstance* inst, struct MapRuntime* map);
static void lavafloor_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Lava falls behavior (scrolling texture)
static void lavafalls_init(DecoInstance* inst, struct MapRuntime* map);
static void lavafalls_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Level 3 stream behavior (scrolling poison water)
static void lvl3stream_init(DecoInstance* inst, struct MapRuntime* map);
static void lvl3stream_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void lvl3stream_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Grinder behavior (rotating like cog)
static void grinder_init(DecoInstance* inst, struct MapRuntime* map);
static void grinder_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void grinder_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Cutscene falloff behavior (triggers cutscene where robot falls off cliff)
static void cutscene_falloff_init(DecoInstance* inst, struct MapRuntime* map);
static void cutscene_falloff_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Sink platform behavior (sinks when player stands on it)
static void sinkplat_init(DecoInstance* inst, struct MapRuntime* map);
static void sinkplat_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Spinner behavior (spinning decoration)
static void spinner_init(DecoInstance* inst, struct MapRuntime* map);
static void spinner_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Spin rock behavior (sinking platform that also rotates)
static void spinrock_init(DecoInstance* inst, struct MapRuntime* map);
static void spinrock_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Moving platform behavior (moves between start and target positions)
static void moveplat_init(DecoInstance* inst, struct MapRuntime* map);
static void moveplat_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Hive moving platform behavior (moves to target when player on, returns when off)
static void hivemoving_init(DecoInstance* inst, struct MapRuntime* map);
static void hivemoving_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Cutscene 2 trigger (slideshow cutscene)
static void cs2_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Cutscene 3 trigger (ending slideshow, returns to menu)
static void cs3_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Moving rock behavior (moves between up to 4 waypoints)
static void movingrock_init(DecoInstance* inst, struct MapRuntime* map);
static void movingrock_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Hot pipe behavior (solid collision, damages player unless invincible)
static void hotpipe_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void hotpipe_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Checkpoint behavior (sets new spawn point when touched)
static void checkpoint_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Light change trigger behavior (changes lighting while player is inside)
static void lighttrigger_init(DecoInstance* inst, struct MapRuntime* map);
static void lighttrigger_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Fog color trigger behavior (changes fog color while player is inside)
static void fogcolor_init(DecoInstance* inst, struct MapRuntime* map);
static void fogcolor_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Permanent light trigger behavior (changes lighting permanently until another is touched)
static void lighttrigger_permanent_init(DecoInstance* inst, struct MapRuntime* map);
static void lighttrigger_permanent_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Light behavior (point light with configurable color)
static void light_init(DecoInstance* inst, struct MapRuntime* map);

// Jukebox behavior (animated with scrolling FX texture)
static void jukebox_init(DecoInstance* inst, struct MapRuntime* map);
static void jukebox_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Monitor table behavior (table + monitor with scrolling screen texture)
static void monitortable_init(DecoInstance* inst, struct MapRuntime* map);
static void monitortable_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Disco ball behavior (descends and rotates when jukebox plays)
static void discoball_init(DecoInstance* inst, struct MapRuntime* map);
static void discoball_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);

// Bulldozer behavior (chases player and pushes them)
static void bulldozer_init(DecoInstance* inst, struct MapRuntime* map);
static void bulldozer_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void bulldozer_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// Torso pickup behavior (collectible body part with spinning/bouncing)
static void torsopickup_init(DecoInstance* inst, struct MapRuntime* map);
static void torsopickup_update(DecoInstance* inst, struct MapRuntime* map, float deltaTime);
static void torsopickup_on_player_collide(DecoInstance* inst, struct MapRuntime* map, float px, float py, float pz);

// ============================================================
// DECORATION TYPE DEFINITIONS
// ============================================================

static const DecoTypeDef DECO_TYPES[DECO_TYPE_COUNT] = {
    // DECO_BARREL
    {
        .modelPath = "rom:/barrel.t3dm",
        .name = "Barrel",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_RAT
    {
        .modelPath = "rom:/rat.t3dm",
        .collisionPath = "rom:/rat.col",
        .name = "Rat",
        .initFn = rat_init,
        .updateFn = rat_update,
        .onPlayerCollideFn = rat_on_player_collide,
    },
    // DECO_BOLT
    {
        .modelPath = "rom:/Screw.t3dm",
        .name = "Bolt",
        .initFn = bolt_init,
        .updateFn = bolt_update,
        .onPlayerCollideFn = bolt_on_player_collide,
        .collisionScale = BOLT_COLLISION_SCALE,
    },
    // DECO_IBEAM
    {
        .modelPath = "rom:/Ibeam.t3dm",
        .name = "IBeam",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_ROCK
    {
        .modelPath = "rom:/Rock.t3dm",
        .name = "Rock",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_DOORCOLLIDER
    {
        .modelPath = NULL,
        .name = "Door Collider",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_PLAYERSPAWN
    {
        .modelPath = "rom:/BlueCube.t3dm",
        .name = "Player Spawn",
        .initFn = playerspawn_init,  // Snaps to ground so player never hovers
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_DAMAGECOLLISION
    {
        .modelPath = "rom:/DECO_DAMAGECOLLISION.t3dm",
        .name = "Damage Collision",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = damagecollision_on_player_collide,
    },
    // DECO_ELEVATOR
    {
        .modelPath = "rom:/Elevator.t3dm",
        .name = "Elevator",
        .initFn = elevator_init,
        .updateFn = elevator_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_SIGN
    {
        .modelPath = "rom:/Sign.t3dm",
        .name = "Sign",
        .initFn = sign_init,
        .updateFn = sign_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_TRANSITIONCOLLISION
    {
        .modelPath = "rom:/TransitionCollision.t3dm",
        .name = "Transition Collision",
        .initFn = transition_init,
        .updateFn = NULL,
        .onPlayerCollideFn = transition_on_player_collide,
    },
    // DECO_PATROLPOINT
    {
        .modelPath = "rom:/PatrolPoint.t3dm",
        .name = "Patrol Point",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_SLIME
    {
        .modelPath = "rom:/slime.t3dm",
        .name = "Slime",
        .initFn = slime_init,
        .updateFn = slime_update,
        .onPlayerCollideFn = slime_on_player_collide,
        .vertexColorsOnly = true,  // Use pure vertex colors (no material tint)
    },
    // DECO_SLIME_LAVA - lava-colored slime variant with scrolling lava texture
    {
        .modelPath = "rom:/Slime_Lava.t3dm",
        .name = "Lava Slime",
        .initFn = slime_init,
        .updateFn = slime_update,
        .onPlayerCollideFn = slime_on_player_collide,
        // Note: vertexColorsOnly = false (default) so lava texture is visible and can scroll
    },
    // DECO_OILPUDDLE - rendered as decal, no model
    {
        .modelPath = NULL,
        .name = "Oil Puddle",
        .initFn = oilpuddle_init,
        .updateFn = oilpuddle_update,
        .onPlayerCollideFn = NULL,  // Collision checked in update
    },
    // DECO_COG - rotating gear platform player can ride
    {
        .modelPath = "rom:/cog.t3dm",
        .collisionPath = "rom:/cog.col",
        .name = "Cog",
        .initFn = cog_init,
        .updateFn = cog_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_SPIKE - damaging spike hazard
    {
        .modelPath = "rom:/spike.t3dm",
        .name = "Spike",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_ROUNDBUTTON - pressable button (renders both top and bottom models)
    {
        .modelPath = "rom:/RoundButtonBottom.t3dm",  // Base model
        .collisionPath = "rom:/RoundButtonBottom.col",
        .name = "RoundButton",
        .initFn = roundbutton_init,
        .updateFn = roundbutton_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_CONVEYERLARGE - conveyor belt (pushes player when activated)
    // Frame model is the base, belt is loaded separately for texture scrolling
    {
        .modelPath = "rom:/ConveyerLargeFrame.t3dm",
        .name = "ConveyerLarge",
        .initFn = conveyor_init,
        .updateFn = conveyor_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_LASERWALL - laser wall (on by default, can be deactivated)
    {
        .modelPath = "rom:/LaserWall.t3dm",
        .collisionPath = "rom:/LaserWall.col",
        .name = "LaserWall",
        .initFn = laserwall_init,
        .updateFn = laserwall_update,
        .onPlayerCollideFn = laserwall_collide,
    },
    // DECO_LASERWALLOFF - laser wall off state (no collision, just visual)
    {
        .modelPath = "rom:/LaserWallOff.t3dm",
        .name = "LaserWallOff",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_FAN - fan that blows player up (two parts: bottom static, top rotates)
    {
        .modelPath = "rom:/FanBottom.t3dm",
        .name = "Fan",
        .initFn = fan_init,
        .updateFn = fan_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_DIALOGUETRIGGER - invisible trigger zone for dialogue/menu scripts
    {
        .modelPath = NULL,  // Invisible - no model
        .name = "DialogueTrigger",
        .initFn = dialoguetrigger_init,
        .updateFn = dialoguetrigger_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_TOXICPIPE - toxic pipe with liquid (liquid has scrolling texture)
    // Pipe is base model, liquid is loaded separately for texture scrolling
    {
        .modelPath = "rom:/Toxic_Level2_Pipe.t3dm",
        .name = "ToxicPipe",
        .initFn = toxicpipe_init,
        .updateFn = toxicpipe_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_TOXICRUNNING - toxic goo running (scrolling texture)
    {
        .modelPath = "rom:/Toxic_Level2_Running.t3dm",
        .name = "ToxicRunning",
        .initFn = toxicrunning_init,
        .updateFn = toxicrunning_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_CACTUS - static cactus decoration
    {
        .modelPath = "rom:/Cactus.t3dm",
        .name = "Cactus",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_INTERACTTRIGGER - optional dialogue trigger (requires A button press)
    {
        .modelPath = NULL,
        .name = "InteractTrigger",
        .initFn = interacttrigger_init,
        .updateFn = interacttrigger_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_JUKEBOX - animated jukebox with scrolling FX texture
    {
        .modelPath = "rom:/JukeBox.t3dm",
        .name = "JukeBox",
        .initFn = jukebox_init,
        .updateFn = jukebox_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_MONITORTABLE - table with monitor that has scrolling screen texture
    // Table is base model, monitor screen is loaded separately for texture scrolling
    {
        .modelPath = "rom:/Table.t3dm",
        .name = "MonitorTable",
        .initFn = monitortable_init,
        .updateFn = monitortable_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_DISCOBALL - disco ball that descends and rotates when jukebox plays
    {
        .modelPath = "rom:/discoball.t3dm",
        .name = "DiscoBall",
        .initFn = discoball_init,
        .updateFn = discoball_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_BULLDOZER - enemy that chases and pushes player
    {
        .modelPath = "rom:/Bulldozer.t3dm",
        .name = "Bulldozer",
        .initFn = bulldozer_init,
        .updateFn = bulldozer_update,
        .onPlayerCollideFn = bulldozer_on_player_collide,
    },
    // DECO_LEVEL3_STREAM - poison stream/waterfall for level 3 (always visible, damages player)
    {
        .modelPath = "rom:/DECO_LVL3_STREAM.t3dm",
        .name = "Level3 Stream",
        .initFn = lvl3stream_init,
        .updateFn = lvl3stream_update,
        .onPlayerCollideFn = lvl3stream_on_player_collide,
    },
    // DECO_LEVEL3_WATERLEVEL - water level plane for level 3 (rendered at origin, no culling)
    {
        .modelPath = "rom:/level3_water_level.t3dm",
        .name = "Level3 Water Level",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_TORSO_PICKUP - collectible torso body part (spinning/bobbing like bolt)
    // Uses shared torsoModel from game.c (already loaded for player)
    // modelPath is NULL - model is set manually in torsopickup_init
    {
        .modelPath = NULL,
        .name = "Torso Pickup",
        .initFn = torsopickup_init,
        .updateFn = torsopickup_update,
        .onPlayerCollideFn = torsopickup_on_player_collide,
        .collisionScale = 2.0f,  // Larger collision radius for easier pickup
    },
    // DECO_HANGING_L - Large hanging platform (stationary)
    {
        .modelPath = "rom:/Cliff_Hanging_Platform_L.t3dm",
        .collisionPath = "rom:/Cliff_Hanging_Platform_L.col",
        .name = "Hanging Platform L",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_HANGING_S - Small hanging platform (spins back and forth)
    {
        .modelPath = "rom:/Cliff_Hanging_Platform_S.t3dm",
        .collisionPath = "rom:/Cliff_Hanging_Platform_S.col",
        .name = "Hanging Platform S",
        .initFn = hanging_init,
        .updateFn = hanging_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_LASER - Laser (alias for LaserWall, used in level files)
    {
        .modelPath = "rom:/LaserWall.t3dm",
        .name = "Laser",
        .initFn = laserwall_init,
        .updateFn = laserwall_update,
        .onPlayerCollideFn = laserwall_collide,
    },
    // DECO_PAIN_TUBE - Invisible damage volume (1 damage on contact)
    {
        .modelPath = "rom:/DECO_PAIN_TUBE.t3dm",
        .name = "Pain Tube",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = paintube_on_player_collide,
    },
    // DECO_CHARGEPAD - Buff-granting platform (2x jump/glide/speed based on body part)
    {
        .modelPath = "rom:/Chargepad.t3dm",
        .collisionPath = "rom:/Chargepad.col",
        .name = "Chargepad",
        .initFn = chargepad_init,
        .updateFn = chargepad_update,
        .onPlayerCollideFn = chargepad_on_player_collide,
    },
    // DECO_TURRET - Rotating turret that fires projectiles at player
    // Cannon is the primary model (has animation), base is loaded separately
    {
        .modelPath = "rom:/RTurret_Cannon.t3dm",
        .name = "Turret",
        .initFn = turret_init,
        .updateFn = turret_update,
        .onPlayerCollideFn = turret_on_player_collide,
    },
    // DECO_DROID_SEC - Security droid enemy (shoots projectiles, 3HP, activates on death)
    {
        .modelPath = "rom:/droid_sec.t3dm",
        .name = "Security Droid",
        .initFn = droid_sec_init,
        .updateFn = droid_sec_update,
        .onPlayerCollideFn = droid_sec_on_player_collide,
    },
    // DECO_DROID_BULLET - Projectile fired by security droid
    {
        .modelPath = "rom:/projectile_pulse.t3dm",
        .name = "Droid Bullet",
        .initFn = droid_bullet_init,
        .updateFn = droid_bullet_update,
        .onPlayerCollideFn = droid_bullet_on_player_collide,
    },
    // DECO_FAN2 - Second fan type (single model with spinning animation and wind)
    {
        .modelPath = "rom:/DECO_FAN2.t3dm",
        .collisionPath = "rom:/DECO_FAN2.col",
        .name = "Fan2",
        .initFn = fan2_init,
        .updateFn = fan2_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_LAVAFLOOR - Lava floor with scrolling texture
    {
        .modelPath = "rom:/DECO_LAVAFLOOR.t3dm",
        .name = "LavaFloor",
        .initFn = lavafloor_init,
        .updateFn = lavafloor_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_LAVAFALLS - Lava waterfall with scrolling texture
    {
        .modelPath = "rom:/DECO_LAVAFALLS.t3dm",
        .name = "LavaFalls",
        .initFn = lavafalls_init,
        .updateFn = lavafalls_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_GRINDER - Rotating grinder (spins like cog, damages player)
    {
        .modelPath = "rom:/DECO_GRINDER.t3dm",
        .name = "Grinder",
        .initFn = grinder_init,
        .updateFn = grinder_update,
        .onPlayerCollideFn = grinder_on_player_collide,
    },
    // DECO_CUTSCENE_FALLOFF - Cutscene trigger (uses barrel as debug visual)
    {
#ifdef DEBUG
        .modelPath = "rom:/DECO_BARREL.t3dm",  // Debug: visible barrel to show trigger area
#else
        .modelPath = NULL,  // Release: invisible trigger
#endif
        .name = "CutsceneFalloff",
        .initFn = cutscene_falloff_init,
        .updateFn = cutscene_falloff_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_SINK_PLAT - Platform that sinks when player stands on it
    {
        .modelPath = "rom:/DECO_SINK_PLAT.t3dm",
        .name = "SinkPlat",
        .initFn = sinkplat_init,
        .updateFn = sinkplat_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_SPINNER - Spinning spinner decoration
    {
        .modelPath = "rom:/DECO_BEYBLADE.t3dm",
        .name = "Spinner",
        .initFn = spinner_init,
        .updateFn = spinner_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_MOVE_PLAT - Moving platform (moves between start and target)
    {
        .modelPath = "rom:/DECO_MOVE_PLAT.t3dm",
        .name = "MovePlat",
        .initFn = moveplat_init,
        .updateFn = moveplat_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_HOTPIPE - Hot pipe (solid collision, damages player unless invincible)
    {
        .modelPath = "rom:/DECO_HOTPIPE.t3dm",
        .name = "HotPipe",
        .initFn = NULL,
        .updateFn = hotpipe_update,
        .onPlayerCollideFn = hotpipe_on_player_collide,
    },
    // DECO_CHECKPOINT - Sets new spawn point when player touches it
    {
        .modelPath = "rom:/BlueCube.t3dm",
        .name = "Checkpoint",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = checkpoint_on_player_collide,
        .collisionScale = 2.0f,  // Larger trigger radius
    },
    // DECO_MOVE_COAL - Moving coal platform (same behavior as MOVE_PLAT)
    {
        .modelPath = "rom:/DECO_MOVE_COAL.t3dm",
        .collisionPath = "rom:/DECO_MOVE_COAL.col",
        .name = "MoveCoal",
        .initFn = moveplat_init,
        .updateFn = moveplat_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_LIGHT - Point light source with configurable color
    {
        .modelPath = NULL,  // No visual model - light only
        .name = "Light",
        .initFn = light_init,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_LIGHT_NOMAP - Point light that only affects player/decorations (not map geometry)
    {
        .modelPath = NULL,  // No visual model - light only
        .name = "LightNoMap",
        .initFn = light_init,  // Reuses same init as DECO_LIGHT
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_LIGHT_TRIGGER - Trigger zone that changes lighting while player is inside
    {
        .modelPath = NULL,  // Invisible - no visual, collision loaded separately
        .name = "LightTrigger",
        .initFn = lighttrigger_init,
        .updateFn = lighttrigger_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_MOVING_ROCK - Rock that moves between up to 4 waypoints
    {
        .modelPath = "rom:/DECO_MOVING_ROCK.t3dm",
        .name = "MovingRock",
        .initFn = movingrock_init,
        .updateFn = movingrock_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_FOGCOLOR - Trigger zone that changes fog color while player is inside
    {
        .modelPath = NULL,  // Invisible - no visual, collision loaded separately
        .name = "FogColor",
        .initFn = fogcolor_init,
        .updateFn = fogcolor_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_LIGHT_TRIGGER_PERMANENT - Permanently changes lighting when touched
    {
        .modelPath = NULL,  // Invisible - no visual, collision loaded separately
        .name = "LightTriggerPerm",
        .initFn = lighttrigger_permanent_init,
        .updateFn = lighttrigger_permanent_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_FLOATING_ROCK - Floating rock platform that sinks when stood on
    {
        .modelPath = "rom:/DECO_FLOATING_ROCK.t3dm",
        .name = "FloatingRock",
        .initFn = sinkplat_init,
        .updateFn = sinkplat_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_FLOATING_ROCK2 - Alternate floating rock platform that sinks when stood on
    {
        .modelPath = "rom:/DECO_FLOATING_ROCK_2.t3dm",
        .name = "FloatingRock2",
        .initFn = sinkplat_init,
        .updateFn = sinkplat_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_SPIN_ROCK - Spinning rock platform that sinks when stood on
    {
        .modelPath = "rom:/DECO_SPIN_ROCK.t3dm",
        .name = "SpinRock",
        .initFn = spinrock_init,
        .updateFn = spinrock_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_SPIN_ROCK2 - Alternate spinning rock platform that sinks when stood on
    {
        .modelPath = "rom:/DECO_SPIN_ROCK_2.t3dm",
        .name = "SpinRock2",
        .initFn = spinrock_init,
        .updateFn = spinrock_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_TURRET_PULSE - Turret that fires slow homing projectiles that damage on hit
    // Uses pulse-specific base and cannon models (cannon is primary for animation)
    {
        .modelPath = "rom:/RTurret_P_Cannon.t3dm",
        .name = "PulseTurret",
        .initFn = turret_pulse_init,
        .updateFn = turret_pulse_update,
        .onPlayerCollideFn = turret_pulse_on_player_collide,
    },
    // DECO_LAVA_RIVER - Static lava river decoration (visual only, no collision)
    {
        .modelPath = "rom:/DECO_LAVA_RIVER.t3dm",
        .name = "Lava River",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_SCREWG - Golden screw (rare collectible, tracked separately from regular bolts)
    {
        .modelPath = "rom:/DECO_SCREWG.t3dm",
        .name = "GoldenScrew",
        .initFn = screwg_init,
        .updateFn = screwg_update,
        .onPlayerCollideFn = screwg_on_player_collide,
        .collisionScale = BOLT_COLLISION_SCALE,
    },
    // DECO_DAMAGECUBE_LIGHT - Damage cube that does 1 damage (not instant death)
    {
        .modelPath = "rom:/DamageCollision.t3dm",  // Uses same model as regular damage cube
        .name = "DamageCubeLight",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = damagecube_light_on_player_collide,
    },
    // DECO_MOVING_LASER - Moving laser that oscillates between start and target, does 1 damage
    {
        .modelPath = "rom:/DECO_MOVING_LASER.t3dm",
        .name = "MovingLaser",
        .initFn = movinglaser_init,
        .updateFn = movinglaser_update,
        .onPlayerCollideFn = movinglaser_on_player_collide,
    },
    // DECO_HIVE_MOVING - Platform that moves to target when player stands on it, returns when off
    {
        .modelPath = "rom:/DECO_HIVE_MOVING.t3dm",
        .name = "HiveMoving",
        .initFn = hivemoving_init,
        .updateFn = hivemoving_update,
        .onPlayerCollideFn = NULL,
    },
    // DECO_STAGE7 - Visual-only stage decoration for level 7 (no collision)
    {
        .modelPath = "rom:/DECO_STAGE7.t3dm",
        .name = "Stage7",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = NULL,
    },
    // DECO_CS_2 - Cutscene 2 trigger (triggers slideshow cutscene)
    {
        .modelPath = "rom:/Col_Cube.t3dm",  // Invisible trigger volume
        .name = "Cutscene2",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = cs2_on_player_collide,
    },
    // DECO_LEVEL_TRANSITION - Immediate level transition (fade to black, no celebration)
    {
        .modelPath = "rom:/Col_Cube.t3dm",  // Invisible trigger volume
        .name = "LevelTransition",
        .initFn = level_transition_init,
        .updateFn = NULL,
        .onPlayerCollideFn = level_transition_on_player_collide,
    },
    // DECO_CS_3 - Cutscene 3 trigger (ending slideshow, returns to menu)
    {
        .modelPath = "rom:/Col_Cube.t3dm",  // Invisible trigger volume
        .name = "Cutscene3",
        .initFn = NULL,
        .updateFn = NULL,
        .onPlayerCollideFn = cs3_on_player_collide,
    },
    // DECO_NONE is not included - it's a sentinel value, not a real decoration type
};

// ============================================================
// MAP RUNTIME STATE
// ============================================================

typedef struct MapRuntime {
    // Decoration instances
    DecoInstance decorations[MAX_DECORATIONS];
    int decoCount;
    int nextDecoId;

    // Decoration type data (lazy loaded, shared per type)
    DecoTypeRuntime decoTypes[DECO_TYPE_COUNT];

    // Special models for multi-part decorations
    T3DModel* buttonTopModel;          // RoundButton top (separate from bottom)
    CollisionMesh* buttonTopCollision; // Collision for button top
    bool buttonTopLoaded;

    T3DModel* fanTopModel;             // Fan top (rotating blades)
    bool fanTopLoaded;

    T3DModel* laserOffModel;           // Laser wall off state (frame only, no lasers)
    bool laserOffLoaded;

    T3DModel* conveyorBeltModel;       // Conveyor belt (scrolling texture)
    CollisionMesh* conveyorBeltCollision; // Collision for belt (for player detection)
    bool conveyorBeltLoaded;

    T3DModel* toxicPipeRunningModel;    // Toxic pipe running goo (scrolling texture, rotated 180°)
    bool toxicPipeRunningLoaded;

    T3DModel* jukeboxFxModel;          // Jukebox FX overlay (scrolling texture)
    bool jukeboxFxLoaded;

    T3DModel* monitorScreenModel;      // Monitor screen (scrolling texture)
    bool monitorScreenLoaded;

    // Level 3 special decorations (drawn at origin, no culling, UV scrolling)
    T3DModel* level3StreamModel;        // Water stream for level 3
    bool level3StreamLoaded;
    float level3StreamOffset;           // UV scroll offset

    T3DModel* level3WaterLevelModel;    // Water level plane for level 3
    bool level3WaterLevelLoaded;
    float level3WaterLevelOffset;       // UV scroll offset

    // Turret multi-model components (cannon is primary model with animation)
    T3DModel* turretBaseModel;          // Stationary base under cannon
    bool turretBaseLoaded;
    T3DModel* turretRailModel;          // Projectile model (drawn per active projectile)
    bool turretRailLoaded;

    // Pulse turret multi-model components
    T3DModel* pulseTurretBaseModel;        // Stationary base (cannon is primary)
    bool pulseTurretBaseLoaded;
    T3DModel* pulseTurretProjectileModel;  // Slow homing projectile model
    CollisionMesh* pulseTurretProjectileCollision;  // Collision mesh for projectile
    // Cached AABB for projectile collision (local space, before scale)
    float pulseProjAABBMinX, pulseProjAABBMinY, pulseProjAABBMinZ;
    float pulseProjAABBMaxX, pulseProjAABBMaxY, pulseProjAABBMaxZ;
    bool pulseTurretProjectileLoaded;

    // Rendering
    T3DMat4FP* decoMatrices;
    int fbCount;
    float visibilityRange;

    // Player reference for AI and platforms (set each frame before update)
    float playerX, playerY, playerZ;
    float playerVelY;  // For platform detection (don't attach if jumping)

    // Oil puddle flag (set by oilpuddle_update, read by game.c to set playerState)
    bool playerOnOil;

    // Reference to map loader for collision (set by game.c)
    MapLoader* mapLoader;

    // Collision parameters for enemies
    float gravity;
    float enemyRadius;
    float enemyHeight;
    
    // Performance optimization: frame counter for staggered updates
    uint32_t frameCounter;
    
    // Ground height cache (reduces redundant raycasts)
    float cachedGroundY;
    float cachedGroundX;
    float cachedGroundZ;
    uint32_t cachedGroundFrame;

    // Multiplayer: which player is currently being checked for collisions
    // Set before map_check_deco_collisions() so decoration callbacks know which player
    // -1 means single-player mode (use global player_take_damage)
    int currentPlayerIndex;
} MapRuntime;

// ============================================================
// MULTIPLAYER HELPERS
// ============================================================

// Set player position for single-player (simple assignment)
static inline void map_set_player_pos(MapRuntime* map, float x, float y, float z) {
    map->playerX = x;
    map->playerY = y;
    map->playerZ = z;
}

// Set player position from closest of multiple players (for multiplayer)
// Each decoration/AI will see the closest player as "the player"
// This is called per-decoration in map_update_decorations_multi() if needed,
// but for simplicity we just pick the globally closest player here
static inline void map_set_closest_player_pos(MapRuntime* map,
                                               const float* playerXs,
                                               const float* playerYs,
                                               const float* playerZs,
                                               int playerCount,
                                               float refX, float refZ) {
    if (playerCount <= 0) return;

    // Find closest player to reference point
    float closestDist = 1e9f;
    int closestIdx = 0;

    for (int i = 0; i < playerCount; i++) {
        float dx = playerXs[i] - refX;
        float dz = playerZs[i] - refZ;
        float dist = dx * dx + dz * dz;  // Squared distance is fine for comparison
        if (dist < closestDist) {
            closestDist = dist;
            closestIdx = i;
        }
    }

    map->playerX = playerXs[closestIdx];
    map->playerY = playerYs[closestIdx];
    map->playerZ = playerZs[closestIdx];
}

// ============================================================
// DECORATION MODEL LOADING (LAZY)
// ============================================================

static inline DecoTypeRuntime* map_get_deco_type(MapRuntime* map, DecoType type) {
    if (map == NULL) return NULL;
    if (type >= DECO_TYPE_COUNT || type == DECO_NONE) return NULL;

    DecoTypeRuntime* deco = &map->decoTypes[type];
    const DecoTypeDef* def = &DECO_TYPES[type];

    // Lazy load if not already loaded
    if (!deco->loaded && def->modelPath != NULL) {
        // CRITICAL: Wait for RSP queue before loading model
        // Rapid successive model loads can overwhelm the RSP queue,
        // causing queue misalignment and RSP crashes during demo mode.
        rspq_wait();
        deco->model = t3d_model_load(def->modelPath);
        deco->loaded = true;
        
        // Check if model has skeleton (for animation support)
        const T3DChunkSkeleton* skelChunk = deco->model ? t3d_model_get_skeleton(deco->model) : NULL;
        deco->hasSkeleton = (skelChunk != NULL);
        
        deco->animCount = 0;
        deco->currentAnim = 0;
        deco->collision = NULL;

        if (deco->model) {
            DECO_DEBUG("Lazy loaded decoration: %s\n", def->name);

            // Auto-lookup collision from model path (skip for decorations that don't need it)
            char collisionName[64] = "";
            bool skipCollision = (type == DECO_DISCOBALL || type == DECO_PLAYERSPAWN ||
                                  type == DECO_RAT || type == DECO_BULLDOZER ||
                                  type == DECO_TORSO_PICKUP || type == DECO_TOXICRUNNING ||
                                  type == DECO_DROID_BULLET || type == DECO_STAGE7);
            if (!skipCollision) {
                // Use custom collisionPath if specified, otherwise derive from modelPath
                if (def->collisionPath != NULL) {
                    extract_model_name(def->collisionPath, collisionName, sizeof(collisionName));
                    DECO_DEBUG("  -> Using custom collision path: %s\n", def->collisionPath);
                } else {
                    extract_model_name(def->modelPath, collisionName, sizeof(collisionName));
                }
                deco->collision = collision_find(collisionName);

                // Special case: CONVEYOR uses frame collision (matches visual)
                if (type == DECO_CONVEYERLARGE) {
                    deco->collision = collision_find("ConveyerLargeFrame");
                    DECO_DEBUG("  -> Using ConveyerLargeFrame collision\n");
                }

                // Special case: DECO_FAN2 uses its own collision (not shared DECO_FAN model collision)
                if (type == DECO_FAN2) {
                    deco->collision = collision_find("DECO_FAN2");
                    DECO_DEBUG("  -> Using DECO_FAN2 custom collision\n");
                }

                // Special case: Trigger decorations use Col_Cube for consistent collision
                if (type == DECO_DAMAGECOLLISION || type == DECO_DAMAGECUBE_LIGHT ||
                    type == DECO_TRANSITIONCOLLISION || type == DECO_CHECKPOINT ||
                    type == DECO_LEVEL_TRANSITION) {
                    deco->collision = collision_find("Col_Cube");
                    DECO_DEBUG("  -> Using Col_Cube collision for trigger\n");
                }

            } else {
                deco->collision = NULL;
            }

            if (deco->collision) {
                DECO_DEBUG("  -> Found collision: %s (%d triangles)\n",
                    collisionName, deco->collision->count);

                // Pre-calculate AABB for this collision mesh (cached for all instances)
                // This is in LOCAL space (before any transform) - will be transformed per-instance
                deco->aabbMinX = 9999.0f;  deco->aabbMinY = 9999.0f;  deco->aabbMinZ = 9999.0f;
                deco->aabbMaxX = -9999.0f; deco->aabbMaxY = -9999.0f; deco->aabbMaxZ = -9999.0f;

                for (int i = 0; i < deco->collision->count; i++) {
                    CollisionTriangle* t = &deco->collision->triangles[i];
                    // Note: These are in LOCAL space, will be rotated/scaled per-instance
                    // Apply 90° Y rotation: (x,y,z) -> (-z,y,x)
                    float x0 = -t->z0, y0 = t->y0, z0 = t->x0;
                    float x1 = -t->z1, y1 = t->y1, z1 = t->x1;
                    float x2 = -t->z2, y2 = t->y2, z2 = t->x2;

                    if (x0 < deco->aabbMinX) deco->aabbMinX = x0;
                    if (x1 < deco->aabbMinX) deco->aabbMinX = x1;
                    if (x2 < deco->aabbMinX) deco->aabbMinX = x2;
                    if (y0 < deco->aabbMinY) deco->aabbMinY = y0;
                    if (y1 < deco->aabbMinY) deco->aabbMinY = y1;
                    if (y2 < deco->aabbMinY) deco->aabbMinY = y2;
                    if (z0 < deco->aabbMinZ) deco->aabbMinZ = z0;
                    if (z1 < deco->aabbMinZ) deco->aabbMinZ = z1;
                    if (z2 < deco->aabbMinZ) deco->aabbMinZ = z2;

                    if (x0 > deco->aabbMaxX) deco->aabbMaxX = x0;
                    if (x1 > deco->aabbMaxX) deco->aabbMaxX = x1;
                    if (x2 > deco->aabbMaxX) deco->aabbMaxX = x2;
                    if (y0 > deco->aabbMaxY) deco->aabbMaxY = y0;
                    if (y1 > deco->aabbMaxY) deco->aabbMaxY = y1;
                    if (y2 > deco->aabbMaxY) deco->aabbMaxY = y2;
                    if (z0 > deco->aabbMaxZ) deco->aabbMaxZ = z0;
                    if (z1 > deco->aabbMaxZ) deco->aabbMaxZ = z1;
                    if (z2 > deco->aabbMaxZ) deco->aabbMaxZ = z2;
                }
                deco->aabbCached = true;
                DECO_DEBUG("  -> Cached AABB: X[%.1f,%.1f] Y[%.1f,%.1f] Z[%.1f,%.1f]\n",
                    deco->aabbMinX, deco->aabbMaxX,
                    deco->aabbMinY, deco->aabbMaxY,
                    deco->aabbMinZ, deco->aabbMaxZ);
            }

            // Check if model has skeleton
            // Skip skeleton/animation creation in demo mode to prevent FILE handle exhaustion
            // Animations stream from ROM via file handles that leak during level transitions
            extern bool g_demoMode;
            extern bool g_replayMode;
            bool skipAnimSetup = g_demoMode || g_replayMode;

            const T3DChunkSkeleton* skelChunk = t3d_model_get_skeleton(deco->model);
            if (skelChunk != NULL && !skipAnimSetup) {
                deco->skeleton = t3d_skeleton_create(deco->model);
                deco->hasSkeleton = true;
                DECO_DEBUG("  -> Has skeleton with %d bones\n", skelChunk->boneCount);

                // For jukebox, create a second skeleton for idle pose blending
                if (type == DECO_JUKEBOX) {
                    deco->skeletonIdle = t3d_skeleton_create(deco->model);
                    t3d_skeleton_update(&deco->skeletonIdle);  // Set to default/idle pose
                    DECO_DEBUG("  -> Created idle skeleton for blending\n");
                }

                // Load animations from model
                uint32_t animCount = t3d_model_get_animation_count(deco->model);
                if (animCount > 0) {
                    T3DChunkAnim* animChunks[MAX_DECO_ANIMS];
                    uint32_t toLoad = animCount < MAX_DECO_ANIMS ? animCount : MAX_DECO_ANIMS;
                    t3d_model_get_animations(deco->model, animChunks);

                    for (uint32_t i = 0; i < toLoad; i++) {
                        T3DAnim anim = t3d_anim_create(deco->model, animChunks[i]->name);
                        t3d_anim_attach(&anim, &deco->skeleton);
                        t3d_anim_set_looping(&anim, true);
                        t3d_anim_set_playing(&anim, true);
                        deco->anims[deco->animCount++] = anim;
                        DECO_DEBUG("  -> Loaded animation '%s'\n", animChunks[i]->name);
                    }
                    // Default to walk animation (index 1) if available
                    if (deco->animCount > 1) {
                        deco->currentAnim = 1;
                    }
                }

            }

            // Special case: ROUNDBUTTON needs to load the top model too
            if (type == DECO_ROUNDBUTTON && !map->buttonTopLoaded) {
                debugf("Loading RoundButtonTop model and collision...\n");
                map->buttonTopModel = t3d_model_load("rom:/RoundButtonTop.t3dm");
                map->buttonTopCollision = collision_find("RoundButtonTop");
                map->buttonTopLoaded = true;
                debugf("RoundButtonTop loaded: model=%p collision=%p\n",
                    (void*)map->buttonTopModel, (void*)map->buttonTopCollision);
            }

            // Special case: FAN needs to load the top model too
            if (type == DECO_FAN && !map->fanTopLoaded) {
                DECO_DEBUG("  -> Loading FanTop model...\n");
                map->fanTopModel = t3d_model_load("rom:/FanTop.t3dm");
                DECO_DEBUG("  -> FanTop model ptr: %p\n", (void*)map->fanTopModel);
                map->fanTopLoaded = true;
                DECO_DEBUG("  -> Also loaded FanTop model\n");
            }

            // Special case: LASER needs to load the off state model too
            if ((type == DECO_LASERWALL || type == DECO_LASER) && !map->laserOffLoaded) {
                DECO_DEBUG("  -> Loading LaserWallOff model...\n");
                map->laserOffModel = t3d_model_load("rom:/LaserWallOff.t3dm");
                DECO_DEBUG("  -> LaserWallOff model ptr: %p\n", (void*)map->laserOffModel);
                map->laserOffLoaded = true;
                DECO_DEBUG("  -> Also loaded LaserWallOff model\n");
            }

            // Special case: CONVEYOR needs to load the belt model too
            if (type == DECO_CONVEYERLARGE && !map->conveyorBeltLoaded) {
                DECO_DEBUG("  -> Loading ConveyerLargeBelt model...\n");
                map->conveyorBeltModel = t3d_model_load("rom:/ConveyerLargeBelt.t3dm");
                DECO_DEBUG("  -> ConveyerLargeBelt model ptr: %p\n", (void*)map->conveyorBeltModel);
                // Use ConveyerLargeBelt collision for ground (matches visual)
                map->conveyorBeltCollision = collision_find("ConveyerLargeBelt");
                map->conveyorBeltLoaded = true;
                DECO_DEBUG("  -> Also loaded ConveyerLargeBelt model and collision\n");
            }

            // Special case: TOXICPIPE needs to load the liquid model too
            if (type == DECO_TOXICPIPE && !map->toxicPipeRunningLoaded) {
                DECO_DEBUG("  -> Loading Toxic_Level2_Running model...\n");
                map->toxicPipeRunningModel = t3d_model_load("rom:/Toxic_Level2_Running.t3dm");
                DECO_DEBUG("  -> Toxic_Level2_Running model ptr: %p\n", (void*)map->toxicPipeRunningModel);
                map->toxicPipeRunningLoaded = true;
                DECO_DEBUG("  -> Also loaded Toxic_Level2_Running model\n");
            }

            // Special case: JUKEBOX needs to load the FX overlay model too
            if (type == DECO_JUKEBOX && !map->jukeboxFxLoaded) {
                DECO_DEBUG("  -> Loading JukeBox_fx model...\n");
                map->jukeboxFxModel = t3d_model_load("rom:/JukeBox_fx.t3dm");
                DECO_DEBUG("  -> JukeBox_fx model ptr: %p\n", (void*)map->jukeboxFxModel);
                map->jukeboxFxLoaded = true;
                DECO_DEBUG("  -> Also loaded JukeBox_fx model\n");
            }

            // Special case: MONITORTABLE needs to load the screen model too
            if (type == DECO_MONITORTABLE && !map->monitorScreenLoaded) {
                DECO_DEBUG("  -> Loading MonitorScreen model...\n");
                map->monitorScreenModel = t3d_model_load("rom:/MonitorScreen.t3dm");
                DECO_DEBUG("  -> MonitorScreen model ptr: %p\n", (void*)map->monitorScreenModel);
                map->monitorScreenLoaded = true;
                DECO_DEBUG("  -> Also loaded MonitorScreen model\n");
            }

            // Special case: TURRET needs to load base and projectile models too
            if (type == DECO_TURRET && !map->turretBaseLoaded) {
                debugf("TURRET: Loading base model...\n");
                map->turretBaseModel = t3d_model_load("rom:/RTurret_Base.t3dm");
                debugf("TURRET: Base ptr=%p\n", (void*)map->turretBaseModel);
                map->turretBaseLoaded = true;
            }
            if (type == DECO_TURRET && !map->turretRailLoaded) {
                debugf("TURRET: Loading rail model...\n");
                map->turretRailModel = t3d_model_load("rom:/RTurret_Rail.t3dm");
                debugf("TURRET: Rail ptr=%p\n", (void*)map->turretRailModel);
                map->turretRailLoaded = true;
            }

            // Special case: PULSE TURRET needs base model and projectile model (cannon is primary)
            if (type == DECO_TURRET_PULSE && !map->pulseTurretBaseLoaded) {
                debugf("PULSE TURRET: Loading base model...\n");
                map->pulseTurretBaseModel = t3d_model_load("rom:/RTurret_P_Base.t3dm");
                debugf("PULSE TURRET: Base ptr=%p\n", (void*)map->pulseTurretBaseModel);
                map->pulseTurretBaseLoaded = true;
            }
            // Load projectile model for both pulse turrets AND security droids
            if ((type == DECO_TURRET_PULSE || type == DECO_DROID_SEC) && !map->pulseTurretProjectileLoaded) {
                debugf("PROJECTILE: Loading projectile model for %s...\n", 
                    type == DECO_TURRET_PULSE ? "PULSE TURRET" : "DROID");
                map->pulseTurretProjectileModel = t3d_model_load("rom:/projectile_pulse.t3dm");
                debugf("PROJECTILE: Model ptr=%p\n", (void*)map->pulseTurretProjectileModel);
                // Load collision mesh for proper hit detection
                map->pulseTurretProjectileCollision = collision_find("projectile_pulse");
                debugf("PULSE TURRET: Projectile collision ptr=%p\n", (void*)map->pulseTurretProjectileCollision);

                // Pre-calculate AABB from collision mesh (local space)
                map->pulseProjAABBMinX = 9999.0f;  map->pulseProjAABBMinY = 9999.0f;  map->pulseProjAABBMinZ = 9999.0f;
                map->pulseProjAABBMaxX = -9999.0f; map->pulseProjAABBMaxY = -9999.0f; map->pulseProjAABBMaxZ = -9999.0f;
                if (map->pulseTurretProjectileCollision) {
                    for (int ci = 0; ci < map->pulseTurretProjectileCollision->count; ci++) {
                        CollisionTriangle* t = &map->pulseTurretProjectileCollision->triangles[ci];
                        // Transform from collision space (swapped X/Z with 90° rotation)
                        float x0 = -t->z0, y0 = t->y0, z0 = t->x0;
                        float x1 = -t->z1, y1 = t->y1, z1 = t->x1;
                        float x2 = -t->z2, y2 = t->y2, z2 = t->x2;

                        if (x0 < map->pulseProjAABBMinX) map->pulseProjAABBMinX = x0;
                        if (x1 < map->pulseProjAABBMinX) map->pulseProjAABBMinX = x1;
                        if (x2 < map->pulseProjAABBMinX) map->pulseProjAABBMinX = x2;
                        if (y0 < map->pulseProjAABBMinY) map->pulseProjAABBMinY = y0;
                        if (y1 < map->pulseProjAABBMinY) map->pulseProjAABBMinY = y1;
                        if (y2 < map->pulseProjAABBMinY) map->pulseProjAABBMinY = y2;
                        if (z0 < map->pulseProjAABBMinZ) map->pulseProjAABBMinZ = z0;
                        if (z1 < map->pulseProjAABBMinZ) map->pulseProjAABBMinZ = z1;
                        if (z2 < map->pulseProjAABBMinZ) map->pulseProjAABBMinZ = z2;

                        if (x0 > map->pulseProjAABBMaxX) map->pulseProjAABBMaxX = x0;
                        if (x1 > map->pulseProjAABBMaxX) map->pulseProjAABBMaxX = x1;
                        if (x2 > map->pulseProjAABBMaxX) map->pulseProjAABBMaxX = x2;
                        if (y0 > map->pulseProjAABBMaxY) map->pulseProjAABBMaxY = y0;
                        if (y1 > map->pulseProjAABBMaxY) map->pulseProjAABBMaxY = y1;
                        if (y2 > map->pulseProjAABBMaxY) map->pulseProjAABBMaxY = y2;
                        if (z0 > map->pulseProjAABBMaxZ) map->pulseProjAABBMaxZ = z0;
                        if (z1 > map->pulseProjAABBMaxZ) map->pulseProjAABBMaxZ = z1;
                        if (z2 > map->pulseProjAABBMaxZ) map->pulseProjAABBMaxZ = z2;
                    }
                    debugf("PULSE TURRET: Projectile AABB: X[%.1f,%.1f] Y[%.1f,%.1f] Z[%.1f,%.1f]\n",
                        map->pulseProjAABBMinX, map->pulseProjAABBMaxX,
                        map->pulseProjAABBMinY, map->pulseProjAABBMaxY,
                        map->pulseProjAABBMinZ, map->pulseProjAABBMaxZ);
                }
                map->pulseTurretProjectileLoaded = true;
            }
        } else {
            // Model failed to load - log warning but continue
            DECO_DEBUG("WARNING: Failed to load decoration model: %s\n", def->modelPath);
        }
    }

    // Special case: Invisible triggers with no model need Col_Cube collision for trigger detection
    if ((type == DECO_LIGHT_TRIGGER || type == DECO_INTERACTTRIGGER || type == DECO_FOGCOLOR ||
         type == DECO_LIGHT_TRIGGER_PERMANENT) && !deco->loaded) {
        deco->loaded = true;
        deco->collision = collision_find("Col_Cube");
        if (deco->collision) {
            DECO_DEBUG("Loaded %s collision: Col_Cube (%d triangles)\n",
                type == DECO_LIGHT_TRIGGER ? "DECO_LIGHT_TRIGGER" :
                type == DECO_FOGCOLOR ? "DECO_FOGCOLOR" :
                type == DECO_LIGHT_TRIGGER_PERMANENT ? "DECO_LIGHT_TRIGGER_PERMANENT" : "DECO_INTERACTTRIGGER",
                deco->collision->count);
            // Cache AABB
            deco->aabbMinX = 9999.0f;  deco->aabbMinY = 9999.0f;  deco->aabbMinZ = 9999.0f;
            deco->aabbMaxX = -9999.0f; deco->aabbMaxY = -9999.0f; deco->aabbMaxZ = -9999.0f;
            for (int i = 0; i < deco->collision->count; i++) {
                CollisionTriangle* t = &deco->collision->triangles[i];
                float x0 = -t->z0, y0 = t->y0, z0 = t->x0;
                float x1 = -t->z1, y1 = t->y1, z1 = t->x1;
                float x2 = -t->z2, y2 = t->y2, z2 = t->x2;
                if (x0 < deco->aabbMinX) deco->aabbMinX = x0;
                if (x1 < deco->aabbMinX) deco->aabbMinX = x1;
                if (x2 < deco->aabbMinX) deco->aabbMinX = x2;
                if (y0 < deco->aabbMinY) deco->aabbMinY = y0;
                if (y1 < deco->aabbMinY) deco->aabbMinY = y1;
                if (y2 < deco->aabbMinY) deco->aabbMinY = y2;
                if (z0 < deco->aabbMinZ) deco->aabbMinZ = z0;
                if (z1 < deco->aabbMinZ) deco->aabbMinZ = z1;
                if (z2 < deco->aabbMinZ) deco->aabbMinZ = z2;
                if (x0 > deco->aabbMaxX) deco->aabbMaxX = x0;
                if (x1 > deco->aabbMaxX) deco->aabbMaxX = x1;
                if (x2 > deco->aabbMaxX) deco->aabbMaxX = x2;
                if (y0 > deco->aabbMaxY) deco->aabbMaxY = y0;
                if (y1 > deco->aabbMaxY) deco->aabbMaxY = y1;
                if (y2 > deco->aabbMaxY) deco->aabbMaxY = y2;
                if (z0 > deco->aabbMaxZ) deco->aabbMaxZ = z0;
                if (z1 > deco->aabbMaxZ) deco->aabbMaxZ = z1;
                if (z2 > deco->aabbMaxZ) deco->aabbMaxZ = z2;
            }
            deco->aabbCached = true;
        }
    }

    return deco;
}

static inline T3DModel* map_get_deco_model(MapRuntime* map, DecoType type) {
    DecoTypeRuntime* deco = map_get_deco_type(map, type);
    return deco ? deco->model : NULL;
}

// ============================================================
// INITIALIZATION
// ============================================================

static inline void map_runtime_init(MapRuntime* map, int fbCount, float visibilityRange) {
    map->decoCount = 0;
    map->nextDecoId = 0;
    map->fbCount = fbCount;
    map->visibilityRange = visibilityRange;
    map->decoMatrices = NULL;
    map->playerX = 0;
    map->playerY = 0;
    map->playerZ = 0;
    map->mapLoader = NULL;
    map->gravity = GRAVITY;
    map->enemyRadius = ENEMY_RADIUS;
    map->enemyHeight = ENEMY_HEIGHT;
    map->currentPlayerIndex = -1;  // -1 = single-player mode (use game.c handlers)
    
    // Initialize performance optimization state
    map->frameCounter = 0;
    map->cachedGroundY = 0.0f;
    map->cachedGroundX = 0.0f;
    map->cachedGroundZ = 0.0f;
    map->cachedGroundFrame = 0;

    // Initialize button top resources
    map->buttonTopModel = NULL;
    map->buttonTopCollision = NULL;
    map->buttonTopLoaded = false;

    // Initialize fan top resources
    map->fanTopModel = NULL;
    map->fanTopLoaded = false;

    // Initialize laser off resources
    map->laserOffModel = NULL;
    map->laserOffLoaded = false;

    // Initialize conveyor belt resources
    map->conveyorBeltModel = NULL;
    map->conveyorBeltCollision = NULL;
    map->conveyorBeltLoaded = false;

    // Initialize toxic pipe liquid resources
    map->toxicPipeRunningModel = NULL;
    map->toxicPipeRunningLoaded = false;

    // Initialize jukebox FX resources
    map->jukeboxFxModel = NULL;
    map->jukeboxFxLoaded = false;

    // Initialize monitor screen resources
    map->monitorScreenModel = NULL;
    map->monitorScreenLoaded = false;

    // Initialize turret resources
    map->turretBaseModel = NULL;
    map->turretBaseLoaded = false;
    map->turretRailModel = NULL;
    map->turretRailLoaded = false;

    // Initialize pulse turret resources
    map->pulseTurretBaseModel = NULL;
    map->pulseTurretBaseLoaded = false;
    map->pulseTurretProjectileModel = NULL;
    map->pulseTurretProjectileCollision = NULL;
    map->pulseTurretProjectileLoaded = false;

    for (int i = 0; i < DECO_TYPE_COUNT; i++) {
        map->decoTypes[i].model = NULL;
        map->decoTypes[i].collision = NULL;
        map->decoTypes[i].loaded = false;
        map->decoTypes[i].hasSkeleton = false;
        map->decoTypes[i].animCount = 0;
        map->decoTypes[i].aabbCached = false;
        // CRITICAL: Zero skeleton structs to prevent garbage pointers from causing crashes
        // when skeleton_is_valid checks for non-NULL (garbage is often non-NULL)
        memset(&map->decoTypes[i].skeleton, 0, sizeof(T3DSkeleton));
        memset(&map->decoTypes[i].skeletonIdle, 0, sizeof(T3DSkeleton));
    }

    for (int i = 0; i < MAX_DECORATIONS; i++) {
        map->decorations[i].type = DECO_NONE;
        map->decorations[i].active = false;
        map->decorations[i].initialized = false;
        map->decorations[i].hasOwnSkeleton = false;
        map->decorations[i].animCount = 0;
        map->decorations[i].activationId = 0;
    }
}

// ============================================================
// CLEANUP
// ============================================================

static inline void map_runtime_free(MapRuntime* map) {
    // Free per-instance skeletons/animations and patrol points
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (inst->hasOwnSkeleton) {
            for (int j = 0; j < inst->animCount; j++) {
                t3d_anim_destroy(&inst->anims[j]);
            }
            t3d_skeleton_destroy(&inst->skeleton);
            inst->hasOwnSkeleton = false;
            inst->animCount = 0;
        }
        // Free patrol points for rats
        if (inst->type == DECO_RAT && inst->state.rat.patrolPoints != NULL) {
            free(inst->state.rat.patrolPoints);
            inst->state.rat.patrolPoints = NULL;
        }
        // Free patrol points for bulldozers
        if (inst->type == DECO_BULLDOZER && inst->state.bulldozer.patrolPoints != NULL) {
            free(inst->state.bulldozer.patrolPoints);
            inst->state.bulldozer.patrolPoints = NULL;
        }
    }

    // Free shared type data
    for (int i = 0; i < DECO_TYPE_COUNT; i++) {
        DecoTypeRuntime* deco = &map->decoTypes[i];
        if (deco->loaded) {
            for (int j = 0; j < deco->animCount; j++) {
                t3d_anim_destroy(&deco->anims[j]);
            }
            if (deco->hasSkeleton) {
                t3d_skeleton_destroy(&deco->skeleton);
                // Destroy idle skeleton for jukebox
                if (i == DECO_JUKEBOX) {
                    t3d_skeleton_destroy(&deco->skeletonIdle);
                }
            }
            // Skip freeing model for TORSO_PICKUP - it uses shared torsoModel from game.c
            if (deco->model && i != DECO_TORSO_PICKUP) {
                t3d_model_free(deco->model);
            }
            deco->model = NULL;
            deco->collision = NULL;
            deco->loaded = false;
            deco->hasSkeleton = false;
            deco->animCount = 0;
        }
    }

    if (map->decoMatrices) {
        free_uncached(map->decoMatrices);
        map->decoMatrices = NULL;
    }

    // Free extra overlay models
    if (map->buttonTopModel) {
        t3d_model_free(map->buttonTopModel);
        map->buttonTopModel = NULL;
    }
    if (map->fanTopModel) {
        t3d_model_free(map->fanTopModel);
        map->fanTopModel = NULL;
    }
    if (map->laserOffModel) {
        t3d_model_free(map->laserOffModel);
        map->laserOffModel = NULL;
    }
    if (map->conveyorBeltModel) {
        t3d_model_free(map->conveyorBeltModel);
        map->conveyorBeltModel = NULL;
    }
    if (map->jukeboxFxModel) {
        t3d_model_free(map->jukeboxFxModel);
        map->jukeboxFxModel = NULL;
        map->jukeboxFxLoaded = false;
    }
    if (map->toxicPipeRunningModel) {
        t3d_model_free(map->toxicPipeRunningModel);
        map->toxicPipeRunningModel = NULL;
        map->toxicPipeRunningLoaded = false;
    }
    if (map->monitorScreenModel) {
        t3d_model_free(map->monitorScreenModel);
        map->monitorScreenModel = NULL;
        map->monitorScreenLoaded = false;
    }

    // Free turret models (no separate animation/skeleton, uses standard system)
    if (map->turretBaseModel) {
        t3d_model_free(map->turretBaseModel);
        map->turretBaseModel = NULL;
        map->turretBaseLoaded = false;
    }
    if (map->turretRailModel) {
        t3d_model_free(map->turretRailModel);
        map->turretRailModel = NULL;
        map->turretRailLoaded = false;
    }
    if (map->pulseTurretBaseModel) {
        t3d_model_free(map->pulseTurretBaseModel);
        map->pulseTurretBaseModel = NULL;
        map->pulseTurretBaseLoaded = false;
    }
    if (map->pulseTurretProjectileModel) {
        t3d_model_free(map->pulseTurretProjectileModel);
        map->pulseTurretProjectileModel = NULL;
        map->pulseTurretProjectileLoaded = false;
    }

    map->decoCount = 0;
}

// ============================================================
// UPDATE (animations + behaviors)
// ============================================================

static inline void map_update_decorations(MapRuntime* map, float deltaTime) {
    if (map == NULL) return;
    
    // Increment frame counter for staggered updates
    map->frameCounter++;
    
    // Check if any jukebox is playing and get blend factor
    bool anyJukeboxPlaying = false;
    float jukeboxBlendIn = 0.0f;
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (inst->active && inst->type == DECO_JUKEBOX && inst->state.jukebox.isPlaying) {
            anyJukeboxPlaying = true;
            jukeboxBlendIn = inst->state.jukebox.blendIn;
            break;
        }
    }

    // Update shared animations for all decoration types
    for (int i = 0; i < DECO_TYPE_COUNT; i++) {
        DecoTypeRuntime* deco = &map->decoTypes[i];
        if (deco->loaded && deco->hasSkeleton && deco->animCount > 0) {
            // Jukebox: blend between idle pose and animation
            if (i == DECO_JUKEBOX) {
                // Only update animation when playing
                if (anyJukeboxPlaying) {
                    t3d_anim_update(&deco->anims[deco->currentAnim], deltaTime);
                }
                if (skeleton_is_valid(&deco->skeleton)) {
                    t3d_skeleton_update(&deco->skeleton);

                    // Blend from idle to animated based on blendIn factor (0 = idle, 1 = animated)
                    if (skeleton_is_valid(&deco->skeletonIdle)) {
                        t3d_skeleton_blend(&deco->skeleton, &deco->skeletonIdle, &deco->skeleton, jukeboxBlendIn);
                    }
                }
                continue;
            }
            t3d_anim_update(&deco->anims[deco->currentAnim], deltaTime);
            if (skeleton_is_valid(&deco->skeleton)) {
                t3d_skeleton_update(&deco->skeleton);
            }
        }
    }

    // Update individual decoration instances with deferred AI processing (distance-based LOD)
    // Frame counter for staggered updates - distributes AI load across frames
    static uint32_t aiFrameCounter = 0;
    aiFrameCounter++;

    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || inst->type == DECO_NONE) continue;

        // Bounds check for type index to prevent memory corruption
        if (inst->type >= DECO_TYPE_COUNT) {
            inst->active = false;  // Deactivate corrupted decoration
            continue;
        }

        const DecoTypeDef* def = &DECO_TYPES[inst->type];

        // Initialize if not yet done
        if (!inst->initialized) {
            if (def->initFn) {
                def->initFn(inst, map);
            }
            inst->initialized = true;
            inst->aiUpdateFreq = AI_UPDATE_EVERY_FRAME;  // Start with full updates
            inst->forceUpdate = true;  // Force first update to initialize skeleton matrices
        }

        // Skip decorations without update functions
        if (!def->updateFn) continue;

        // Calculate squared distance to player for AI frequency (avoid sqrtf)
        float dx = inst->posX - map->playerX;
        float dz = inst->posZ - map->playerZ;
        float distSq = dx * dx + dz * dz;

        // Update AI frequency based on distance (LOD optimization)
        if (distSq < AI_DIST_CLOSE_SQ) {
            inst->aiUpdateFreq = AI_UPDATE_EVERY_FRAME;
        } else if (distSq < AI_DIST_MEDIUM_SQ) {
            inst->aiUpdateFreq = AI_UPDATE_HALF;
        } else if (distSq < AI_DIST_FAR_SQ) {
            inst->aiUpdateFreq = AI_UPDATE_QUARTER;
        } else if (distSq < AI_DIST_VERY_FAR_SQ) {
            inst->aiUpdateFreq = AI_UPDATE_EIGHTH;
        } else {
            inst->aiUpdateFreq = AI_UPDATE_PAUSED;
        }

        // Visual-only decorations (scrolling textures) should always update regardless of distance
        // Also include moving platforms that need consistent position updates
        // HOTPIPE needs every-frame updates to check for player contact damage
        // CONVEYERLARGE needs every-frame updates for consistent player detection
        // ROUNDBUTTON needs every-frame updates for consistent player detection
        if (inst->type == DECO_LAVAFLOOR || inst->type == DECO_LAVAFALLS ||
            inst->type == DECO_TOXICRUNNING || inst->type == DECO_TOXICPIPE ||
            inst->type == DECO_MOVING_ROCK || inst->type == DECO_HOTPIPE ||
            inst->type == DECO_CONVEYERLARGE || inst->type == DECO_ROUNDBUTTON) {
            inst->aiUpdateFreq = AI_UPDATE_EVERY_FRAME;
        }

        // Determine if this decoration should update this frame
        // Use decoration index to stagger updates across frames
        bool shouldUpdate = inst->forceUpdate;  // Always update if forced
        if (!shouldUpdate) {
            switch (inst->aiUpdateFreq) {
                case AI_UPDATE_EVERY_FRAME:
                    shouldUpdate = true;
                    break;
                case AI_UPDATE_HALF:
                    // Update on even or odd frames based on index
                    shouldUpdate = ((aiFrameCounter + i) % 2 == 0);
                    break;
                case AI_UPDATE_QUARTER:
                    // Update every 4 frames, staggered by index
                    shouldUpdate = ((aiFrameCounter + i) % 4 == 0);
                    break;
                case AI_UPDATE_EIGHTH:
                    // Update every 8 frames, staggered by index
                    shouldUpdate = ((aiFrameCounter + i) % 8 == 0);
                    break;
                case AI_UPDATE_PAUSED:
                    // Only update if forced
                    shouldUpdate = false;
                    break;
            }
        }

        // Clear force flag after checking
        inst->forceUpdate = false;

        if (shouldUpdate) {
            def->updateFn(inst, map, deltaTime);
        }
    }
}

// Force a decoration to update next frame (call when enemy takes damage, alerts, etc.)
static inline void deco_force_update(DecoInstance* inst) {
    if (inst) {
        inst->forceUpdate = true;
    }
}

// Force all decorations of a type to update next frame
static inline void map_force_update_type(MapRuntime* map, DecoType type) {
    for (int i = 0; i < map->decoCount; i++) {
        if (map->decorations[i].type == type && map->decorations[i].active) {
            map->decorations[i].forceUpdate = true;
        }
    }
}

// Optimized volume check using cached AABB (much faster than rebuilding AABB every frame)
// playerHeight: if > 0, checks if player's vertical range (py to py+playerHeight) overlaps with AABB
//               if = 0, does strict point-in-AABB check (for platforms where foot must be inside)
// playerRadius: if > 0, expands AABB check in XZ to account for player width (cylinder vs point)
// rotY: decoration's Y rotation in radians - player position is transformed to local space
static inline bool deco_check_inside_volume_cached_rotated(DecoTypeRuntime* decoType,
    float px, float py, float pz, float playerHeight, float playerRadius,
    float posX, float posY, float posZ,
    float scaleX, float scaleY, float scaleZ,
    float rotY) {

    if (!decoType->aabbCached) {
        // Fallback: no cached AABB available, shouldn't happen but handle gracefully
        return false;
    }

    // Transform player position to decoration's local space:
    // 1. Translate (subtract position)
    // 2. Inverse rotate (rotate by -rotY)
    // 3. Inverse scale (divide by scale)
    float cosR = fm_cosf(rotY);
    float sinR = fm_sinf(rotY);

    // Translate player relative to decoration center
    float relX = px - posX;
    float relZ = pz - posZ;

    // Inverse rotate (rotate by -rotY: use cos(rotY), -sin(rotY))
    float localX = relX * cosR + relZ * sinR;
    float localZ = -relX * sinR + relZ * cosR;

    // Inverse scale
    float localScaledX = localX / scaleX;
    float localScaledZ = localZ / scaleZ;

    // Expand AABB by player radius (scaled to local space) for cylinder vs box overlap
    // Use average scale for radius since player is circular in XZ
    float avgScale = (scaleX + scaleZ) * 0.5f;
    float localRadius = playerRadius / avgScale;

    // Now check against expanded AABB in local space (point + radius vs box)
    if (localScaledX < decoType->aabbMinX - localRadius || localScaledX > decoType->aabbMaxX + localRadius ||
        localScaledZ < decoType->aabbMinZ - localRadius || localScaledZ > decoType->aabbMaxZ + localRadius) {
        return false;
    }

    // Y: transform and check
    float localY = py - posY;
    float localScaledY = localY / scaleY;

    // Y: range overlap check if playerHeight > 0 (for pickups/triggers)
    //    or point containment if playerHeight = 0 (for platforms)
    if (playerHeight > 0.0f) {
        // Check if player's vertical range overlaps with AABB
        float localScaledTop = (py + playerHeight - posY) / scaleY;
        return (localScaledY < decoType->aabbMaxY && localScaledTop > decoType->aabbMinY);
    } else {
        // Strict point check for foot position
        return (localScaledY >= decoType->aabbMinY && localScaledY <= decoType->aabbMaxY);
    }
}

// Non-rotated version for backwards compatibility
static inline bool deco_check_inside_volume_cached(DecoTypeRuntime* decoType,
    float px, float py, float pz, float playerHeight, float playerRadius,
    float posX, float posY, float posZ,
    float scaleX, float scaleY, float scaleZ) {
    return deco_check_inside_volume_cached_rotated(decoType, px, py, pz, playerHeight, playerRadius,
        posX, posY, posZ, scaleX, scaleY, scaleZ, 0.0f);
}

// Check if a ray is blocked by any decoration collision meshes
// Used for turret line-of-sight checking so turrets can't shoot through walls/platforms
static inline bool map_raycast_blocked_by_decorations(MapRuntime* map,
    float fromX, float fromY, float fromZ,
    float toX, float toY, float toZ,
    int excludeDecoIndex) {  // -1 to check all, or index of turret to skip

    if (map == NULL) return false;

    // Types that should block line of sight (solid decorations with collision)
    // Exclude invisible triggers, particles, pickups, enemies, etc.
    for (int i = 0; i < map->decoCount; i++) {
        if (i == excludeDecoIndex) continue;  // Skip the turret itself
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || !inst->initialized || inst->type == DECO_NONE) continue;
        if (inst->type >= DECO_TYPE_COUNT) continue;

        // Skip decoration types that shouldn't block LOS
        // (invisible triggers, pickups, enemies, effects, etc.)
        DecoType t = inst->type;
        if (t == DECO_BOLT || t == DECO_PLAYERSPAWN || t == DECO_DAMAGECOLLISION ||
            t == DECO_TRANSITIONCOLLISION || t == DECO_PATROLPOINT || t == DECO_OILPUDDLE ||
            t == DECO_DIALOGUETRIGGER || t == DECO_INTERACTTRIGGER || t == DECO_DISCOBALL ||
            t == DECO_TORSO_PICKUP || t == DECO_PAIN_TUBE ||
            t == DECO_CHARGEPAD || t == DECO_DROID_BULLET || t == DECO_CUTSCENE_FALLOFF || t == DECO_CS_2 || t == DECO_CS_3 ||
            t == DECO_LIGHT || t == DECO_LIGHT_NOMAP || t == DECO_LIGHT_TRIGGER ||
            t == DECO_FOGCOLOR || t == DECO_LIGHT_TRIGGER_PERMANENT ||
            t == DECO_RAT || t == DECO_SLIME || t == DECO_SLIME_LAVA || t == DECO_BULLDOZER || t == DECO_DROID_SEC ||
            t == DECO_CHECKPOINT || t == DECO_TURRET || t == DECO_LASERWALLOFF) continue;

        // Get collision mesh for this decoration
        DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
        if (!decoType || !decoType->collision) continue;

        CollisionMesh* mesh = decoType->collision;
        if (!mesh || mesh->count == 0) continue;

        // Use rotation-aware raycast that transforms triangles to world space
        if (collision_raycast_blocked_rotated(mesh,
            fromX, fromY, fromZ,
            toX, toY, toZ,
            inst->posX, inst->posY, inst->posZ,
            inst->scaleX, inst->scaleY, inst->scaleZ,
            inst->rotY)) {
            return true;  // Ray is blocked by this decoration
        }
    }

    return false;  // No decoration blocked the ray
}

// Check player behavior collision with decorations (for pickups, damage, etc.)
// Collision range multiplier - increase effective collision radius for non-hazardous decorations
#define DECO_COLLISION_RANGE_MULTIPLIER 1.3f

static inline void map_check_deco_collisions(MapRuntime* map, float px, float py, float pz, float radius) {
    if (map == NULL) return;
    float baseRadius = radius;
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || !inst->initialized || inst->type == DECO_NONE) continue;

        // Bounds check for type index to prevent memory corruption
        if (inst->type >= DECO_TYPE_COUNT) {
            inst->active = false;  // Deactivate corrupted decoration
            continue;
        }

        const DecoTypeDef* def = &DECO_TYPES[inst->type];
        if (!def->onPlayerCollideFn) continue;

        // Skip dead enemies/turrets - no collision after destroyed
        if (inst->type == DECO_TURRET && inst->state.turret.isDead) continue;
        if (inst->type == DECO_TURRET_PULSE && inst->state.pulseTurret.isDead) continue;
        if (inst->type == DECO_DROID_SEC && inst->state.droid.isDead) continue;
        if ((inst->type == DECO_SLIME || inst->type == DECO_SLIME_LAVA) && inst->state.slime.isDying) continue;

        // Get decoration type (triggers lazy loading if needed)
        DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
        if (!decoType) continue;

        // Apply range multiplier only for non-hazardous decorations (pickups, triggers)
        // Don't expand collision for things that hurt the player
        bool isHazard = (inst->type == DECO_LASERWALL || inst->type == DECO_LASER ||
                         inst->type == DECO_DAMAGECOLLISION || inst->type == DECO_DAMAGECUBE_LIGHT ||
                         inst->type == DECO_PAIN_TUBE || inst->type == DECO_GRINDER || inst->type == DECO_HOTPIPE ||
                         inst->type == DECO_MOVING_LASER || inst->type == DECO_LEVEL3_STREAM);
        float effectiveRadius = isHazard ? baseRadius : (baseRadius * DECO_COLLISION_RANGE_MULTIPLIER);

        if (decoType->loaded && decoType->collision) {
            // Use volume check for triggers/pickups, wall check for physical obstacles
            bool collided = false;

            float decoX = inst->posX;
            float decoY = inst->posY;
            float decoZ = inst->posZ;

            if (inst->type == DECO_DAMAGECOLLISION || inst->type == DECO_DAMAGECUBE_LIGHT ||
                inst->type == DECO_BOLT || inst->type == DECO_TRANSITIONCOLLISION ||
                inst->type == DECO_LASERWALL || inst->type == DECO_LASER || inst->type == DECO_PAIN_TUBE ||
                inst->type == DECO_CHARGEPAD || inst->type == DECO_CHECKPOINT || inst->type == DECO_LIGHT_TRIGGER ||
                inst->type == DECO_FOGCOLOR || inst->type == DECO_LIGHT_TRIGGER_PERMANENT ||
                inst->type == DECO_MOVING_LASER || inst->type == DECO_LEVEL3_STREAM || inst->type == DECO_CS_2 || inst->type == DECO_CS_3 ||
                inst->type == DECO_LEVEL_TRANSITION || inst->type == DECO_GRINDER) {
                // Check full 3D volume (includes floors, ceilings, all surfaces)
                // Used for trigger zones and pickups - uses cached AABB for performance
                // Pass playerHeight=20.0f so player's full body can overlap with pickup/hazard
                // Pass radius to expand AABB check for thin obstacles (e.g., laser walls)
                // Supports rotation by transforming player pos to local space
                // NOTE: AABB computation applies 90° rotation to collision mesh, so we must
                // compensate with -90° (or +270°, i.e. -π/2) for collision to match visual
                float collisionRotY = inst->rotY;
                if (inst->type == DECO_LASERWALL || inst->type == DECO_LASER || inst->type == DECO_MOVING_LASER) {
                    collisionRotY -= 1.5707963f;  // -90 degrees to align with visual
                }
                collided = deco_check_inside_volume_cached_rotated(decoType,
                    px, py, pz, 20.0f, effectiveRadius,  // playerHeight and radius for body overlap
                    inst->posX, inst->posY, inst->posZ,
                    inst->scaleX, inst->scaleY, inst->scaleZ,
                    collisionRotY);
            } else {
                // Check walls only (for physical collisions)
                float pushX = 0.0f, pushZ = 0.0f;
                collided = collision_check_walls(decoType->collision,
                    px, py, pz, effectiveRadius, 20.0f,  // playerHeight = 20.0f
                    decoX, decoY, decoZ,
                    inst->scaleX, inst->scaleY, inst->scaleZ,
                    &pushX, &pushZ);
            }

            if (collided) {
                def->onPlayerCollideFn(inst, map, px, py, pz);
            }
        } else {
            // Fallback: Simple sphere collision check
            float dx = px - inst->posX;
            float dy = py - inst->posY;
            float dz = pz - inst->posZ;
            float distSq = dx*dx + dy*dy + dz*dz;
            float scale = def->collisionScale > 0.0f ? def->collisionScale : 1.0f;
            float collideRadius = (effectiveRadius + 20.0f * inst->scaleX) * scale;

            if (distSq < collideRadius * collideRadius) {
                def->onPlayerCollideFn(inst, map, px, py, pz);
            }
        }
    }
}

// Check turret projectile collisions with player
// Returns true if player was hit by a projectile (call player_take_damage if true)
static inline bool map_check_turret_projectiles(MapRuntime* map, float px, float py, float pz, float playerRadius, float playerHeight) {
    if (map == NULL) return false;

    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || !inst->initialized || inst->type != DECO_TURRET) continue;

        if (turret_check_projectile_collision(inst, px, py, pz, playerRadius, playerHeight)) {
            return true;  // Player was hit
        }
    }
    return false;
}

// Check pulse turret projectile collisions with player using actual model AABB
// Returns true if player was hit by a projectile (call player_take_damage if true)
static inline bool map_check_pulse_turret_projectiles(MapRuntime* map, float px, float py, float pz, float playerRadius, float playerHeight) {
    if (map == NULL) return false;

    // Get projectile AABB (use defaults if collision not loaded)
    float aabbMinX = map->pulseProjAABBMinX;
    float aabbMinY = map->pulseProjAABBMinY;
    float aabbMinZ = map->pulseProjAABBMinZ;
    float aabbMaxX = map->pulseProjAABBMaxX;
    float aabbMaxY = map->pulseProjAABBMaxY;
    float aabbMaxZ = map->pulseProjAABBMaxZ;

    // Fallback to small sphere if no collision loaded
    bool hasAABB = (aabbMinX < aabbMaxX && aabbMinY < aabbMaxY && aabbMinZ < aabbMaxZ);
    float fallbackRadius = 5.0f;  // Small fallback radius

    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || !inst->initialized || inst->type != DECO_TURRET_PULSE) continue;

        // Check each active projectile (4 max per turret)
        for (int p = 0; p < 4; p++) {
            if (inst->state.pulseTurret.projLife[p] <= 0.0f) continue;

            // Grace period: projectile can't damage for first 0.5 seconds after spawn
            // This prevents instant hits when player is near the turret
            float lifeRemaining = inst->state.pulseTurret.projLife[p];
            if (lifeRemaining > 7.5f) continue;  // 8.0 - 0.5 = 7.5 (first 0.5s grace period)

            float projX = inst->state.pulseTurret.projPosX[p];
            float projY = inst->state.pulseTurret.projPosY[p];
            float projZ = inst->state.pulseTurret.projPosZ[p];

            bool hit = false;

            if (hasAABB) {
                // Use actual model AABB - pulsates and shrinks (matches rendering)
                float baseScale = 2.0f;
                float lifeElapsed = 8.0f - lifeRemaining;  // Time since fired

                // Pulsating effect - oscillate scale with sine wave
                float pulseFreq = 6.0f;  // Pulses per second
                float pulseAmount = 0.25f;  // +/- 25% scale variation
                float pulse = 1.0f + sinf(lifeElapsed * pulseFreq * 6.28318f) * pulseAmount;

                // Shrink during last 2 seconds
                float shrinkStart = 2.0f;
                float shrinkMult = 1.0f;
                if (lifeRemaining < shrinkStart) {
                    shrinkMult = lifeRemaining / shrinkStart;
                    if (shrinkMult < 0.1f) shrinkMult = 0.1f;
                }
                float scale = baseScale * pulse * shrinkMult;

                float worldMinX = projX + aabbMinX * scale;
                float worldMaxX = projX + aabbMaxX * scale;
                float worldMinY = projY + aabbMinY * scale;
                float worldMaxY = projY + aabbMaxY * scale;
                float worldMinZ = projZ + aabbMinZ * scale;
                float worldMaxZ = projZ + aabbMaxZ * scale;

                // Expand AABB by player radius for cylinder check
                worldMinX -= playerRadius;
                worldMaxX += playerRadius;
                worldMinZ -= playerRadius;
                worldMaxZ += playerRadius;

                // Check player cylinder vs expanded projectile AABB
                float playerTop = py + playerHeight;
                hit = (px >= worldMinX && px <= worldMaxX &&
                       pz >= worldMinZ && pz <= worldMaxZ &&
                       py <= worldMaxY && playerTop >= worldMinY);
            } else {
                // Fallback: simple sphere collision
                float dx = projX - px;
                float dy = projY - (py + playerHeight * 0.5f);
                float dz = projZ - pz;
                float distSq = dx*dx + dy*dy + dz*dz;
                float combinedRadius = playerRadius + fallbackRadius;
                hit = (distSq < combinedRadius * combinedRadius);
            }

            if (hit) {
                // Debug: log hit details
                debugf("PULSE HIT! Proj[%.1f,%.1f,%.1f] Player[%.1f,%.1f,%.1f] dist=%.1f life=%.1f\n",
                    projX, projY, projZ, px, py, pz,
                    sqrtf((projX-px)*(projX-px) + (projY-py)*(projY-py) + (projZ-pz)*(projZ-pz)),
                    inst->state.pulseTurret.projLife[p]);
                // Hit! Deactivate projectile and spawn particles
                game_spawn_spark_particles(projX, projY, projZ, 10);
                inst->state.pulseTurret.projLife[p] = 0.0f;
                inst->state.pulseTurret.activeProjectiles--;
                return true;  // Player was hit
            }
        }
    }
    return false;
}

// Check if player is on an elevator and return vertical movement
static inline float map_check_elevator_movement(MapRuntime* map, float px, float py, float pz) {
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || inst->type != DECO_ELEVATOR) continue;

        // Use collision mesh to detect if player is on the elevator
        DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
        if (decoType && decoType->loaded && decoType->collision) {
            bool onElevator = deco_check_inside_volume_cached_rotated(decoType,
                px, py, pz, 5.0f, 0.0f,  // Small tolerance for foot placement, no radius expansion
                inst->posX, inst->posY, inst->posZ,
                inst->scaleX, inst->scaleY, inst->scaleZ,
                inst->rotY);

            if (onElevator) {
                // Return the Y delta from last update
                return inst->state.elevator.lastDelta;
            }
        }
    }
    return 0.0f;
}

// Check if player is on a cog and get movement delta (including vertical)
// DEPRECATED: Use platform_get_displacement() instead
static inline bool map_check_cog_movement(MapRuntime* map, float* outDeltaX, float* outDeltaY, float* outDeltaZ) {
    *outDeltaX = 0.0f;
    *outDeltaY = 0.0f;
    *outDeltaZ = 0.0f;

    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || inst->type != DECO_COG) continue;

        if (inst->state.cog.playerOnCog) {
            *outDeltaX = inst->state.cog.playerDeltaX;
            *outDeltaY = inst->state.cog.playerDeltaY;
            *outDeltaZ = inst->state.cog.playerDeltaZ;
            return true;
        }
    }
    return false;
}

// ============================================================
// UNIFIED PLATFORM DISPLACEMENT SYSTEM
// ============================================================
// One function to check ALL platforms and return displacement
// Call this ONCE per frame, BEFORE applying movement and collision
//
// Returns a PlatformResult containing:
// - deltaX/Y/Z: Position change to apply to player
// - onPlatform: Whether player is on any platform
// - overrideGroundPhysics: Whether to skip normal ground collision
// - type: Which platform type (for special handling if needed)
// ============================================================
static inline PlatformResult platform_get_displacement(MapRuntime* map, float px, float py, float pz) {
    (void)px; (void)py; (void)pz;  // Used by cog_update, not here directly
    PlatformResult result;
    platform_result_init(&result);

    // Priority order: Cog > Elevator (cog needs more control)

    // --- CHECK COG PLATFORMS ---
    // Check wall collision from ALL cogs, and ground displacement from the one we're standing on
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || inst->type != DECO_COG) continue;

        // Accumulate wall push from any cog that hit us
        if (inst->state.cog.hitWall) {
            result.wallPushX += inst->state.cog.wallPushX;
            result.wallPushZ += inst->state.cog.wallPushZ;
            result.hitWall = true;
        }

        // If standing on this cog, get displacement
        if (inst->state.cog.playerOnCog) {
            result.deltaX = inst->state.cog.playerDeltaX;
            result.deltaY = inst->state.cog.playerDeltaY;
            result.deltaZ = inst->state.cog.playerDeltaZ;
            result.onPlatform = true;
            result.overrideGroundPhysics = true;  // Cog handles its own ground
            result.type = PLATFORM_COG;
            result.platform = inst;
            // Don't return early - continue checking other cogs for wall collision
        }
    }

    // If we found a cog platform, return now
    if (result.onPlatform && result.type == PLATFORM_COG) {
        return result;
    }

    // --- CHECK ELEVATOR PLATFORMS ---
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || inst->type != DECO_ELEVATOR) continue;

        DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
        if (decoType && decoType->loaded && decoType->collision) {
            bool onElevator = deco_check_inside_volume_cached_rotated(decoType,
                px, py, pz, 5.0f, 0.0f,  // Small tolerance for foot placement, no radius expansion
                inst->posX, inst->posY, inst->posZ,
                inst->scaleX, inst->scaleY, inst->scaleZ,
                inst->rotY);

            if (onElevator && inst->state.elevator.lastDelta != 0.0f) {
                result.deltaY = inst->state.elevator.lastDelta;
                result.onPlatform = true;
                result.overrideGroundPhysics = false;  // Elevator only moves Y
                result.type = PLATFORM_ELEVATOR;
                result.platform = inst;
                return result;
            }
        }
    }

    // --- CHECK SINK PLATFORMS (including floating rocks) ---
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || (inst->type != DECO_SINK_PLAT &&
            inst->type != DECO_FLOATING_ROCK && inst->type != DECO_FLOATING_ROCK2)) continue;

        DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
        if (decoType && decoType->loaded && decoType->collision) {
            bool onPlat = deco_check_inside_volume_cached_rotated(decoType,
                px, py, pz, 5.0f, 0.0f,  // Small tolerance for foot placement, no radius expansion
                inst->posX, inst->posY, inst->posZ,
                inst->scaleX, inst->scaleY, inst->scaleZ,
                inst->rotY);

            if (onPlat && inst->state.sinkPlat.lastDelta != 0.0f) {
                result.deltaY = inst->state.sinkPlat.lastDelta;
                result.onPlatform = true;
                result.overrideGroundPhysics = false;  // Sink plat only moves Y
                result.type = PLATFORM_ELEVATOR;  // Reuse elevator type for vertical-only platforms
                result.platform = inst;
                return result;
            }
        }
    }

    // --- CHECK SPIN ROCKS (spinning + sinking platforms) ---
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || (inst->type != DECO_SPIN_ROCK && inst->type != DECO_SPIN_ROCK2)) continue;

        DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
        if (decoType && decoType->loaded && decoType->collision) {
            bool onPlat = deco_check_inside_volume_cached_rotated(decoType,
                px, py, pz, 5.0f, 0.0f,  // Small tolerance for foot placement, no radius expansion
                inst->posX, inst->posY, inst->posZ,
                inst->scaleX, inst->scaleY, inst->scaleZ,
                inst->rotY);

            if (onPlat && inst->state.spinRock.lastDelta != 0.0f) {
                result.deltaY = inst->state.spinRock.lastDelta;
                result.onPlatform = true;
                result.overrideGroundPhysics = false;  // Spin rock only moves Y
                result.type = PLATFORM_ELEVATOR;  // Reuse elevator type for vertical-only platforms
                result.platform = inst;
                return result;
            }
        }
    }

    // --- CHECK MOVING PLATFORMS ---
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || (inst->type != DECO_MOVE_PLAT && inst->type != DECO_MOVE_COAL && inst->type != DECO_HIVE_MOVING)) continue;

        // Check if player is on platform (uses triangle collision same as update)
        if (inst->state.movePlat.playerOnPlat) {
            // Return all 3 deltas for full XYZ movement
            result.deltaX = inst->state.movePlat.lastDeltaX;
            result.deltaY = inst->state.movePlat.lastDeltaY;
            result.deltaZ = inst->state.movePlat.lastDeltaZ;
            result.onPlatform = true;
            result.overrideGroundPhysics = false;
            result.type = PLATFORM_COG;  // Use COG type for XYZ movement
            result.platform = inst;
            return result;
        }
    }

    // --- CHECK HANGING PLATFORMS (SPINNING) ---
    // NOTE: Player movement on rotating hanging platforms disabled - rotation math was buggy
    // Platform still rotates visually, player just doesn't get moved with it
    // (keeping this loop structure in case we want to re-enable with fixed math later)

    // --- CHECK MOVING ROCK PLATFORMS ---
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || inst->type != DECO_MOVING_ROCK) continue;

        // Check if player is on this moving rock
        if (inst->state.movingRock.playerOnPlat) {
            // Return all 3 deltas for full XYZ movement
            result.deltaX = inst->state.movingRock.lastDeltaX;
            result.deltaY = inst->state.movingRock.lastDeltaY;
            result.deltaZ = inst->state.movingRock.lastDeltaZ;
            result.onPlatform = true;
            result.overrideGroundPhysics = false;
            result.type = PLATFORM_COG;  // Use COG type for XYZ movement
            result.platform = inst;
            return result;
        }
    }

    return result;
}

// Check decoration wall collision (physics - pushes player out)
// excludeIndex: set to decoration index to skip (for enemies), or -1 for player
static inline bool map_check_deco_walls_ex(MapRuntime* map,
    float px, float py, float pz, float radius, float playerHeight,
    float* outPushX, float* outPushZ, int excludeIndex) {

    // CRITICAL: Clear FPU exception state to prevent NOTIMPL crashes
    // Previous operations (trig, sqrt) may leave invalid FPU state
    uint32_t fcr31;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcr31));
    fcr31 |= (1 << 24);  // Set FS bit (Flush denormalized results to zero)
    fcr31 &= ~(0x1F << 7);  // Clear all exception enable bits
    fcr31 &= ~(0x3F << 2);  // Clear all cause bits
    fcr31 &= ~(1 << 17);    // Clear cause bit for unimplemented operation
    __asm__ volatile("ctc1 %0, $31" : : "r"(fcr31));

    *outPushX = 0.0f;
    *outPushZ = 0.0f;
    bool collided = false;
    const float cullDist = COLLISION_CULL_DISTANCE;

    for (int i = 0; i < map->decoCount; i++) {
        if (i == excludeIndex) continue;  // Skip self
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || !inst->initialized || inst->type == DECO_NONE) continue;

        // Skip trigger-only volumes (DamageCollision, Bolt, TransitionCollision, PainTube, Lava, Checkpoint, LightTrigger, FogColor, LightTriggerPermanent, Level3Stream, CS2, LevelTransition)
        if (inst->type == DECO_DAMAGECOLLISION || inst->type == DECO_DAMAGECUBE_LIGHT ||
            inst->type == DECO_BOLT || inst->type == DECO_TRANSITIONCOLLISION ||
            inst->type == DECO_PAIN_TUBE || inst->type == DECO_LAVAFLOOR || inst->type == DECO_LAVAFALLS ||
            inst->type == DECO_CHECKPOINT || inst->type == DECO_LIGHT_TRIGGER || inst->type == DECO_FOGCOLOR ||
            inst->type == DECO_LIGHT_TRIGGER_PERMANENT || inst->type == DECO_LEVEL3_STREAM || inst->type == DECO_CS_2 || inst->type == DECO_CS_3 ||
            inst->type == DECO_LEVEL_TRANSITION) continue;

        // Skip laser walls entirely - they're hazards, not physical walls
        // Player should pass through and take damage, not be pushed back
        if (inst->type == DECO_LASERWALL || inst->type == DECO_LASER || inst->type == DECO_MOVING_LASER) continue;

        // Skip cog entirely - cog physics handles its own collision
        if (inst->type == DECO_COG) continue;

        // Skip conveyor belt frame - it has wall triangles that cause teleportation bugs
        // The belt surface (ground collision) is handled separately in map_get_deco_ground_height
        if (inst->type == DECO_CONVEYERLARGE) continue;

        // Skip dead enemies/turrets - no collision after destroyed
        if (inst->type == DECO_TURRET && inst->state.turret.isDead) continue;
        if (inst->type == DECO_TURRET_PULSE && inst->state.pulseTurret.isDead) continue;
        if (inst->type == DECO_DROID_SEC && inst->state.droid.isDead) continue;
        if ((inst->type == DECO_SLIME || inst->type == DECO_SLIME_LAVA) && inst->state.slime.isDying) continue;

        // Distance culling - skip far decorations (except HOTPIPE which has model offset)
        float dx = px - inst->posX;
        float dz = pz - inst->posZ;
        float distSq = dx*dx + dz*dz;
        // Conveyor belts use scaled cull distance based on their dimensions
        if (inst->type == DECO_CONVEYERLARGE) {
            float maxScale = inst->scaleX > inst->scaleZ ? inst->scaleX : inst->scaleZ;
            float scaledCull = cullDist * maxScale * 2.0f;
            if (distSq > scaledCull*scaledCull) continue;
        } else if (inst->type != DECO_HOTPIPE && distSq > cullDist*cullDist) continue;

        DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
        if (!decoType || !collision_mesh_is_valid(decoType->collision)) continue;

        // HOTPIPE needs small Z offset to align collision with visual
        float collisionOffZ = (inst->type == DECO_HOTPIPE) ? -64.0f : 0.0f;
        float pushX = 0.0f, pushZ = 0.0f;
        bool hit = collision_check_walls_rotated(decoType->collision,
            px, py, pz, radius, playerHeight,
            inst->posX, inst->posY, inst->posZ + collisionOffZ,
            inst->scaleX, inst->scaleY, inst->scaleZ,
            inst->rotY,
            &pushX, &pushZ);

        if (hit) {
            *outPushX += pushX;
            *outPushZ += pushZ;
            collided = true;
        }
    }

    return collided;
}

// Convenience wrapper for player (no exclusion)
static inline bool map_check_deco_walls(MapRuntime* map,
    float px, float py, float pz, float radius, float playerHeight,
    float* outPushX, float* outPushZ) {
    return map_check_deco_walls_ex(map, px, py, pz, radius, playerHeight, outPushX, outPushZ, -1);
}

// Get ground height from decoration collision meshes (for standing on top)
// excludeIndex: set to decoration index to skip (for enemies), or -1 for player
static inline float map_get_deco_ground_height_ex(MapRuntime* map,
    float px, float py, float pz, int excludeIndex) {

    float bestY = INVALID_GROUND_Y;
    const float cullDist = COLLISION_CULL_DISTANCE;

    for (int i = 0; i < map->decoCount; i++) {
        if (i == excludeIndex) continue;  // Skip self
        DecoInstance* inst = &map->decorations[i];
        if (!inst->active || !inst->initialized || inst->type == DECO_NONE) continue;

        // Skip trigger-only volumes (DamageCollision, Bolt, TransitionCollision, PainTube, Lava, Checkpoint, LightTrigger, FogColor, LightTriggerPermanent, Level3Stream, CS2, LevelTransition)
        if (inst->type == DECO_DAMAGECOLLISION || inst->type == DECO_DAMAGECUBE_LIGHT ||
            inst->type == DECO_BOLT || inst->type == DECO_TRANSITIONCOLLISION ||
            inst->type == DECO_PAIN_TUBE || inst->type == DECO_LAVAFLOOR || inst->type == DECO_LAVAFALLS ||
            inst->type == DECO_CHECKPOINT || inst->type == DECO_LIGHT_TRIGGER || inst->type == DECO_FOGCOLOR ||
            inst->type == DECO_LIGHT_TRIGGER_PERMANENT || inst->type == DECO_LEVEL3_STREAM || inst->type == DECO_CS_2 || inst->type == DECO_CS_3 ||
            inst->type == DECO_LEVEL_TRANSITION) continue;

        // Skip laser walls entirely - they're hazards, not ground surfaces
        if (inst->type == DECO_LASERWALL || inst->type == DECO_LASER || inst->type == DECO_MOVING_LASER) continue;

        // Skip cog entirely - cog physics handles its own collision
        if (inst->type == DECO_COG) continue;

        // Skip dead enemies/turrets - no collision after destroyed
        if (inst->type == DECO_TURRET && inst->state.turret.isDead) continue;
        if (inst->type == DECO_TURRET_PULSE && inst->state.pulseTurret.isDead) continue;
        if (inst->type == DECO_DROID_SEC && inst->state.droid.isDead) continue;
        if ((inst->type == DECO_SLIME || inst->type == DECO_SLIME_LAVA) && inst->state.slime.isDying) continue;

        // Distance culling (except HOTPIPE which has model offset, CONVEYERLARGE which is long)
        float dx = px - inst->posX;
        float dz = pz - inst->posZ;
        float distSq = dx*dx + dz*dz;
        // Conveyor belts use scaled cull distance based on their dimensions
        if (inst->type == DECO_CONVEYERLARGE) {
            float maxScale = inst->scaleX > inst->scaleZ ? inst->scaleX : inst->scaleZ;
            float scaledCull = cullDist * maxScale * 2.0f;
            if (distSq > scaledCull*scaledCull) continue;
        } else if (inst->type != DECO_HOTPIPE && distSq > cullDist*cullDist) continue;

        DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
        if (!decoType || !collision_mesh_is_valid(decoType->collision)) continue;

        // HOTPIPE needs small Z offset to align collision with visual
        float collisionOffZ = (inst->type == DECO_HOTPIPE) ? -64.0f : 0.0f;

        // For CONVEYERLARGE: ONLY use belt collision for ground (frame has bottom surface that causes clipping)
        float groundY;
        if (inst->type == DECO_CONVEYERLARGE) {
            // Skip frame collision entirely for ground - it has a bottom surface at Y=-0.1 that causes
            // players to clip under the belt when approaching from the side
            if (map->conveyorBeltLoaded && map->conveyorBeltCollision) {
                groundY = collision_get_ground_height_rotated(map->conveyorBeltCollision,
                    px, py, pz,
                    inst->posX, inst->posY, inst->posZ,
                    inst->scaleX, inst->scaleY, inst->scaleZ,
                    inst->rotY);
            } else {
                groundY = INVALID_GROUND_Y;
            }
        } else {
            groundY = collision_get_ground_height_rotated(decoType->collision,
                px, py, pz,
                inst->posX, inst->posY, inst->posZ + collisionOffZ,
                inst->scaleX, inst->scaleY, inst->scaleZ,
                inst->rotY);
        }

        // For ROUNDBUTTON: check button top collision (which moves down when pressed)
        if (inst->type == DECO_ROUNDBUTTON && inst->initialized &&
            map->buttonTopLoaded && map->buttonTopCollision) {
            // Button top is offset downward by pressDepth
            float topY = inst->posY - inst->state.button.pressDepth;
            float topGroundY = collision_get_ground_height_rotated(map->buttonTopCollision,
                px, py, pz,
                inst->posX, topY, inst->posZ,
                inst->scaleX, inst->scaleY, inst->scaleZ,
                inst->rotY);
            // Use the higher of bottom or top collision
            if (topGroundY > groundY) {
                groundY = topGroundY;
            }
        }

        // For SIGN: apply tilt offset to ground height based on X position
        if (inst->type == DECO_SIGN && groundY > INVALID_GROUND_Y) {
            float signDx = px - inst->posX;
            // Height offset: sin(tilt) * X distance from center
            float tiltOffset = sinf(inst->state.sign.tilt) * signDx;
            groundY += tiltOffset;
        }

        if (groundY > bestY) {
            bestY = groundY;
        }
    }

    return bestY;
}

// Convenience wrapper for player (no exclusion)
static inline float map_get_deco_ground_height(MapRuntime* map,
    float px, float py, float pz) {
    return map_get_deco_ground_height_ex(map, px, py, pz, -1);
}

// ============================================================
// DECORATION MANAGEMENT
// ============================================================

// Clear all decorations (for level restart)
static inline void map_clear_decorations(MapRuntime* map) {
    if (map == NULL) return;
    // Clean up per-instance skeletons, animations, and patrol points before clearing
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* inst = &map->decorations[i];

        // Free per-instance skeleton and animations
        if (inst->hasOwnSkeleton) {
            for (int j = 0; j < inst->animCount; j++) {
                t3d_anim_destroy(&inst->anims[j]);
            }
            t3d_skeleton_destroy(&inst->skeleton);
            inst->hasOwnSkeleton = false;
            inst->animCount = 0;
        }

        // Free patrol points for rats
        if (inst->type == DECO_RAT && inst->state.rat.patrolPoints != NULL) {
            free(inst->state.rat.patrolPoints);
            inst->state.rat.patrolPoints = NULL;
        }

        // Free patrol points for bulldozers
        if (inst->type == DECO_BULLDOZER && inst->state.bulldozer.patrolPoints != NULL) {
            free(inst->state.bulldozer.patrolPoints);
            inst->state.bulldozer.patrolPoints = NULL;
        }

        // Deactivate decoration (models are shared, don't free)
        inst->active = false;
        inst->initialized = false;
    }
    map->decoCount = 0;
    map->nextDecoId = 0;
}

static inline int map_add_decoration(MapRuntime* map, DecoType type,
    float x, float y, float z, float rotY,
    float scaleX, float scaleY, float scaleZ) {
    if (map->decoCount >= MAX_DECORATIONS) return -1;
    if (type >= DECO_TYPE_COUNT || type == DECO_NONE) return -1;

    // Load monitor screen model if placing MONITORTABLE and not already loaded
    if (type == DECO_MONITORTABLE && !map->monitorScreenLoaded) {
        map->monitorScreenModel = t3d_model_load("rom:/MonitorScreen.t3dm");
        DECO_DEBUG("MonitorTable placed at (%.1f, %.1f, %.1f), screen model: %p\n", x, y, z, (void*)map->monitorScreenModel);
        map->monitorScreenLoaded = true;
    }

    DecoInstance* inst = &map->decorations[map->decoCount];
    inst->type = type;
    inst->posX = x;
    inst->posY = y;
    inst->posZ = z;
    inst->rotX = 0.0f;
    inst->rotY = rotY;
    inst->rotZ = 0.0f;
    inst->scaleX = scaleX;
    inst->scaleY = scaleY;
    inst->scaleZ = scaleZ;
    inst->active = true;
    inst->initialized = false;
    inst->id = map->nextDecoId++;
    inst->activationId = 0;  // Must be set by caller if needed

    // Zero out state
    memset(&inst->state, 0, sizeof(DecoState));
    inst->hasOwnSkeleton = false;
    inst->animCount = 0;
    memset(&inst->skeleton, 0, sizeof(T3DSkeleton));

    return map->decoCount++;
}

static inline int map_add_decoration_patrol(MapRuntime* map, DecoType type,
    float x, float y, float z, float rotY,
    float scaleX, float scaleY, float scaleZ, const T3DVec3* patrolPoints, int patrolCount) {
    if (map->decoCount >= MAX_DECORATIONS) return -1;
    if (type >= DECO_TYPE_COUNT || type == DECO_NONE) return -1;

    DecoInstance* inst = &map->decorations[map->decoCount];
    inst->type = type;
    inst->posX = x;
    inst->posY = y;
    inst->posZ = z;
    inst->rotX = 0.0f;
    inst->rotY = rotY;
    inst->rotZ = 0.0f;
    inst->scaleX = scaleX;
    inst->scaleY = scaleY;
    inst->scaleZ = scaleZ;
    inst->active = true;
    inst->initialized = false;
    inst->id = map->nextDecoId++;
    inst->activationId = 0;

    // Zero out state first
    memset(&inst->state, 0, sizeof(DecoState));
    inst->hasOwnSkeleton = false;
    inst->animCount = 0;
    memset(&inst->skeleton, 0, sizeof(T3DSkeleton));

    // Set patrol points for rats (copy the array since it may be static const)
    if (type == DECO_RAT && patrolPoints != NULL && patrolCount > 0) {
        inst->state.rat.patrolPoints = malloc(sizeof(T3DVec3) * patrolCount);
        memcpy(inst->state.rat.patrolPoints, patrolPoints, sizeof(T3DVec3) * patrolCount);
        inst->state.rat.patrolPointCount = patrolCount;
        inst->state.rat.currentPatrolIndex = 0;
        debugf("Patrol route attached to rat: %d points\n", patrolCount);
    }

    // Set patrol points for bulldozers
    if (type == DECO_BULLDOZER && patrolPoints != NULL && patrolCount > 0) {
        inst->state.bulldozer.patrolPoints = malloc(sizeof(T3DVec3) * patrolCount);
        memcpy(inst->state.bulldozer.patrolPoints, patrolPoints, sizeof(T3DVec3) * patrolCount);
        inst->state.bulldozer.patrolPointCount = patrolCount;
        inst->state.bulldozer.currentPatrolIndex = 0;
        debugf("Patrol route attached to bulldozer: %d points\n", patrolCount);
    }

    return map->decoCount++;
}

static inline void map_remove_decoration(MapRuntime* map, int index) {
    if (index < 0 || index >= map->decoCount) return;
    map->decorations[index].active = false;
    map->decorations[index].type = DECO_NONE;
}

// Print all decorations as copyable code
static inline void map_print_all_decorations(MapRuntime* map) {
    // MUST match DecoType enum order exactly!
    const char* typeNames[] = {
        "DECO_BARREL",              // 0
        "DECO_RAT",                 // 1
        "DECO_BOLT",                // 2
        "DECO_IBEAM",               // 3
        "DECO_ROCK",                // 4
        "DECO_DOORCOLLIDER",        // 5
        "DECO_PLAYERSPAWN",         // 6
        "DECO_DAMAGECOLLISION",     // 7
        "DECO_ELEVATOR",            // 8
        "DECO_SIGN",                // 9
        "DECO_TRANSITIONCOLLISION", // 10
        "DECO_PATROLPOINT",         // 11
        "DECO_SLIME",               // 12
        "DECO_SLIME_LAVA",          // 13
        "DECO_OILPUDDLE",           // 14
        "DECO_COG",                 // 14
        "DECO_SPIKE",               // 15
        "DECO_ROUNDBUTTON",         // 16
        "DECO_CONVEYERLARGE",       // 17
        "DECO_LASERWALL",           // 18
        "DECO_LASERWALLOFF",        // 19
        "DECO_FAN",                 // 20
        "DECO_DIALOGUETRIGGER",     // 21
        "DECO_TOXICPIPE",           // 22
        "DECO_TOXICRUNNING",        // 23
        "DECO_CACTUS",              // 24
        "DECO_INTERACTTRIGGER",     // 25
        "DECO_JUKEBOX",             // 26
        "DECO_MONITORTABLE",        // 27
        "DECO_DISCOBALL",           // 28
        "DECO_BULLDOZER",           // 29
        "DECO_LEVEL3_STREAM",       // 30
        "DECO_LEVEL3_WATERLEVEL",   // 31
        "DECO_TORSO_PICKUP",        // 32
        "DECO_HANGING_L",           // 33
        "DECO_HANGING_S",           // 34
        "DECO_LASER",               // 35
        "DECO_PAIN_TUBE",           // 36
        "DECO_CHARGEPAD",           // 37
        "DECO_TURRET",              // 38
        "DECO_DROID_SEC",           // 39
        "DECO_DROID_BULLET",        // 40
        "DECO_FAN2",                // 41
        "DECO_LAVAFLOOR",           // 42
        "DECO_LAVAFALLS",           // 43
        "DECO_GRINDER",             // 44
        "DECO_CUTSCENE_FALLOFF",    // 45
        "DECO_SINK_PLAT",           // 46
        "DECO_SPINNER",            // 47
        "DECO_MOVE_PLAT",           // 48
        "DECO_HOTPIPE",             // 49
        "DECO_CHECKPOINT",          // 50
        "DECO_MOVE_COAL",           // 51
        "DECO_LIGHT",               // 52
        "DECO_LIGHT_NOMAP",         // 53
        "DECO_LIGHT_TRIGGER",       // 54
        "DECO_MOVING_ROCK",         // 55
        "DECO_FOGCOLOR",            // 56
        "DECO_LIGHT_TRIGGER_PERMANENT", // 57
        "DECO_FLOATING_ROCK",       // 58
        "DECO_FLOATING_ROCK2",      // 59
        "DECO_SPIN_ROCK",           // 60
        "DECO_SPIN_ROCK2",          // 61
        "DECO_TURRET_PULSE",        // 62
        "DECO_LAVA_RIVER",          // 63
        "DECO_SCREWG",              // 64
        "DECO_DAMAGECUBE_LIGHT",    // 65
        "DECO_MOVING_LASER",        // 66
        "DECO_HIVE_MOVING",         // 67
        "DECO_STAGE7",              // 68
        "DECO_CS_2",                // 69
        "DECO_LEVEL_TRANSITION",    // 70
        "DECO_CS_3",                // 71
        "DECO_NONE",                // 72
    };

    // Count active decorations (excluding patrol points)
    int activeCount = 0;
    for (int i = 0; i < map->decoCount; i++) {
        if (map->decorations[i].active && map->decorations[i].type != DECO_NONE &&
            map->decorations[i].type != DECO_PATROLPOINT) {
            activeCount++;
        }
    }

    // Collect patrol points for rats
    int patrolRouteId = 0;

    debugf("\n// ========== COPY TO LEVEL FILE ==========\n");

    // First pass: Print patrol route arrays
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* d = &map->decorations[i];
        if (!d->active || d->type != DECO_RAT) continue;
        if (d->state.rat.patrolPointCount == 0 || d->state.rat.patrolPoints == NULL) continue;

        // Found a rat with patrol points - print the route
        debugf("static const T3DVec3 rat_patrol_route_%d[] = {\n", patrolRouteId);
        for (int p = 0; p < d->state.rat.patrolPointCount; p++) {
            T3DVec3* pt = &d->state.rat.patrolPoints[p];
            DECO_DEBUG("    {{%.1ff, %.1ff, %.1ff}},\n", pt->v[0], pt->v[1], pt->v[2]);
        }
        debugf("};\n\n");

        // Store route ID in the rat for second pass
        // We'll use the animTimeOffset as a temporary holder (it's random anyway)
        d->state.rat.animTimeOffset = (float)patrolRouteId;
        patrolRouteId++;
    }

    // Second pass: Print decorations
    debugf(".decorations = {\n");
    patrolRouteId = 0;
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* d = &map->decorations[i];
        if (!d->active || d->type == DECO_NONE || d->type == DECO_PATROLPOINT) continue;
        const char* typeName = (d->type < DECO_TYPE_COUNT) ? typeNames[d->type] : "DECO_BARREL";

        // Check if this rat has patrol points
        if (d->type == DECO_RAT && d->state.rat.patrolPointCount > 0 && d->state.rat.patrolPoints != NULL) {
            int routeId = (int)d->state.rat.animTimeOffset;
            debugf("    { .type = %s, .x = %.1ff, .y = %.1ff, .z = %.1ff, .rotY = %.2ff,\n",
                typeName, d->posX, d->posY, d->posZ, d->rotY);
            debugf("      .scaleX = %.2ff, .scaleY = %.2ff, .scaleZ = %.2ff,\n",
                d->scaleX, d->scaleY, d->scaleZ);
            debugf("      .patrolPoints = rat_patrol_route_%d, .patrolPointCount = %d },\n",
                routeId, d->state.rat.patrolPointCount);
        } else {
            // Normal decoration (short syntax)
            debugf("    { %s, %.1ff, %.1ff, %.1ff, %.2ff, %.2ff, %.2ff, %.2ff },",
                typeName,
                d->posX, d->posY, d->posZ,
                d->rotY,
                d->scaleX, d->scaleY, d->scaleZ);

            // Add transition data comment if it's a transition collision
            if (d->type == DECO_TRANSITIONCOLLISION) {
                debugf("  // TO: Level %d, Spawn %d",
                    d->state.transition.targetLevel,
                    d->state.transition.targetSpawn);
            }
            debugf("\n");
        }
    }
    debugf("},\n");
    debugf(".decorationCount = %d,\n", activeCount);
    debugf("// ========== END COPY ==========\n\n");
}

// ============================================================
// BEHAVIOR IMPLEMENTATIONS
// ============================================================

// --- RAT BEHAVIOR ---
static void rat_init(DecoInstance* inst, MapRuntime* map) {
    inst->state.rat.moveDir = ((float)(rand() % 628) / 100.0f) - 3.14f;  // Random initial direction
    inst->state.rat.moveTimer = 1.0f + (rand() % 30) / 10.0f;
    inst->state.rat.attackCooldown = 0.0f;
    inst->state.rat.isAggro = false;
    inst->state.rat.velY = 0.0f;
    inst->state.rat.isGrounded = false;
    inst->state.rat.currentAnim = RAT_IDLE_ANIM;  // Start with idle animation
    inst->state.rat.pauseTimer = 0.5f;  // Brief pause on spawn to settle and detect ground
    // Pounce state initialization
    inst->state.rat.isPouncing = false;
    inst->state.rat.pounceTimer = 0.0f;
    inst->state.rat.pounceVelX = 0.0f;
    inst->state.rat.pounceVelZ = 0.0f;
    inst->state.rat.pounceCooldown = 0.0f;
    // Note: patrolPoints are set by map_add_decoration_patrol, don't reset them here

    // Store spawn position for respawn on fall
    inst->state.rat.spawnX = inst->posX;
    inst->state.rat.spawnY = inst->posY;
    inst->state.rat.spawnZ = inst->posZ;
    inst->state.rat.spawnRotY = inst->rotY;

    // Skip per-instance skeleton/animation in demo mode to prevent FILE handle exhaustion
    extern bool g_demoMode;
    extern bool g_replayMode;
    if (g_demoMode || g_replayMode) {
        inst->hasOwnSkeleton = false;
        inst->animCount = 0;
        // CRITICAL: Zero skeleton to prevent garbage pointers from causing crashes
        // skeleton_is_valid checks for non-NULL, and garbage is often non-NULL
        memset(&inst->skeleton, 0, sizeof(T3DSkeleton));
        return;
    }

    // Create per-instance skeleton for independent animations
    DecoTypeRuntime* ratType = map_get_deco_type(map, DECO_RAT);
    if (ratType && ratType->model && ratType->hasSkeleton) {
        inst->skeleton = t3d_skeleton_create(ratType->model);
        inst->hasOwnSkeleton = true;
        inst->animCount = 0;

        // Load animations for this instance
        uint32_t animCount = t3d_model_get_animation_count(ratType->model);
        if (animCount > 0) {
            T3DChunkAnim* animChunks[MAX_DECO_ANIMS];
            uint32_t toLoad = animCount < MAX_DECO_ANIMS ? animCount : MAX_DECO_ANIMS;
            t3d_model_get_animations(ratType->model, animChunks);

            for (uint32_t i = 0; i < toLoad; i++) {
                T3DAnim anim = t3d_anim_create(ratType->model, animChunks[i]->name);
                t3d_anim_attach(&anim, &inst->skeleton);
                t3d_anim_set_looping(&anim, true);
                t3d_anim_set_playing(&anim, true);
                inst->anims[inst->animCount++] = anim;
            }
        }
    }
}

static void rat_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Calculate Euclidean distance to player (used for all range checks)
    float dx = map->playerX - inst->posX;
    float dz = map->playerZ - inst->posZ;
    float distToPlayer = sqrtf(dx*dx + dz*dz);

    // Update pounce cooldown
    if (inst->state.rat.pounceCooldown > 0) {
        inst->state.rat.pounceCooldown -= deltaTime;
    }

    // Check if player is close
    if (distToPlayer < RAT_AGGRO_RANGE) {
        inst->state.rat.isAggro = true;
    } else if (distToPlayer > RAT_DEAGGRO_RANGE) {
        inst->state.rat.isAggro = false;
    }

    // Range checks for behavior selection
    bool inAttackRange = (distToPlayer < RAT_ATTACK_RANGE);
    bool inPounceRange = (distToPlayer < RAT_POUNCE_RANGE && distToPlayer > RAT_POUNCE_MIN_RANGE);
    float moveX = 0.0f, moveZ = 0.0f;

    // Handle active pounce
    if (inst->state.rat.isPouncing) {
        inst->state.rat.pounceTimer -= deltaTime;

        if (inst->state.rat.pounceTimer <= 0) {
            // Pounce finished
            inst->state.rat.isPouncing = false;
            inst->state.rat.pounceCooldown = RAT_POUNCE_COOLDOWN;
            inst->state.rat.pounceVelX = 0.0f;
            inst->state.rat.pounceVelZ = 0.0f;
        } else {
            
            // Continue pouncing - use pounce animation and velocity
            inst->state.rat.currentAnim = RAT_POUNCE_ANIM;
            moveX = inst->state.rat.pounceVelX * deltaTime;
            moveZ = inst->state.rat.pounceVelZ * deltaTime;
        }
    } else if (inst->state.rat.isAggro) {
        // Check if should start pounce (not too close, not too far, off cooldown, grounded)
        if (inPounceRange && inst->state.rat.pounceCooldown <= 0 && inst->state.rat.isGrounded && distToPlayer > 1.0f) {
            // Start pounce toward player
            inst->state.rat.isPouncing = true;
            inst->state.rat.pounceTimer = RAT_POUNCE_DURATION;

            // Calculate pounce velocity (distance / duration)
            float pounceSpeed = RAT_POUNCE_DISTANCE / RAT_POUNCE_DURATION;
            inst->state.rat.pounceVelX = (dx / distToPlayer) * pounceSpeed;
            inst->state.rat.pounceVelZ = (dz / distToPlayer) * pounceSpeed;

            // Face player and use pounce animation
            inst->rotY = atan2f(-dx, dz);
            inst->state.rat.currentAnim = RAT_POUNCE_ANIM;

            // Apply initial movement this frame
            moveX = inst->state.rat.pounceVelX * deltaTime;
            moveZ = inst->state.rat.pounceVelZ * deltaTime;
        } else if (inAttackRange && inst->state.rat.attackCooldown <= 0) {
            // In attack range and cooldown expired - use attack animation, stand still
            inst->state.rat.currentAnim = RAT_ATTACK_ANIM;
            inst->rotY = atan2f(-dx, dz);  // Face player
            // moveX/moveZ stay at 0 - rat stands still while attacking
        } else if (!inAttackRange && distToPlayer > 1.0f) {
            // Not in attack range - walk toward player
            inst->state.rat.currentAnim = RAT_WALK_ANIM;
            float speed = RAT_WALK_SPEED * deltaTime;
            moveX = (dx / distToPlayer) * speed;
            moveZ = (dz / distToPlayer) * speed;
            inst->rotY = atan2f(-dx, dz);  // Face player
        } else if (inAttackRange) {
            // In attack range but on cooldown - idle/wait
            inst->state.rat.currentAnim = RAT_IDLE_ANIM;
            inst->rotY = atan2f(-dx, dz);  // Face player
            // moveX/moveZ stay at 0 - rat waits for cooldown
        }
    } else {
        // Patrol/wander mode

        // Check if rat is paused
        if (inst->state.rat.pauseTimer > 0.0f) {
            inst->state.rat.pauseTimer -= deltaTime;
            inst->state.rat.currentAnim = RAT_IDLE_ANIM;  // Idle animation while paused at waypoint
            // Don't move while paused
            moveX = 0.0f;
            moveZ = 0.0f;
        } else if (inst->state.rat.patrolPoints != NULL && inst->state.rat.patrolPointCount > 1) {
            // Has patrol points - follow them
            inst->state.rat.currentAnim = RAT_WALK_ANIM;  // Walk animation

            // Get current target patrol point
            T3DVec3* target = &inst->state.rat.patrolPoints[inst->state.rat.currentPatrolIndex];

            // Calculate direction to patrol point
            float dx = target->v[0] - inst->posX;
            float dz = target->v[2] - inst->posZ;
            float distToTarget = sqrtf(dx*dx + dz*dz);

            // If close enough to current point, pause then move to next one
            if (distToTarget < 5.0f) {
                inst->state.rat.currentPatrolIndex = (inst->state.rat.currentPatrolIndex + 1) % inst->state.rat.patrolPointCount;
                inst->state.rat.pauseTimer = 1.0f;  // Pause for 1 second at waypoint
                moveX = 0.0f;
                moveZ = 0.0f;
            } else {
                // Move toward patrol point
                float speed = RAT_PATROL_SPEED * deltaTime;
                moveX = (dx / distToTarget) * speed;
                moveZ = (dz / distToTarget) * speed;
                inst->rotY = atan2f(-dx, dz);  // Face direction of movement
            }
        } else {
            // No patrol points - random wander (fallback)
            inst->state.rat.currentAnim = RAT_WALK_ANIM;  // Walk animation

            inst->state.rat.moveTimer -= deltaTime;
            if (inst->state.rat.moveTimer <= 0.0f) {
                // Pause before choosing new direction
                inst->state.rat.pauseTimer = 1.0f;
                inst->state.rat.moveDir = ((float)(rand() % 628) / 100.0f) - 3.14f;
                inst->state.rat.moveTimer = 1.0f + (rand() % 30) / 10.0f;
            }

            float speed = RAT_PATROL_SPEED * deltaTime;
            // Calculate movement direction from angle
            float dx = sinf(inst->state.rat.moveDir);
            float dz = -cosf(inst->state.rat.moveDir);  // Negative to match coordinate system
            moveX = dx * speed;
            moveZ = dz * speed;
            inst->rotY = atan2f(-dx, dz);  // Use same rotation formula as patrol
        }
    }

    // Cliff detection - prevent walking off ledges (but pounces commit regardless)
    if (!inst->state.rat.isPouncing && (moveX != 0.0f || moveZ != 0.0f) && map->mapLoader) {
        float nextX = inst->posX + moveX;
        float nextZ = inst->posZ + moveZ;
        float currentGroundY = maploader_get_ground_height(map->mapLoader, inst->posX, inst->posY, inst->posZ);
        float nextGroundY = maploader_get_ground_height(map->mapLoader, nextX, inst->posY, nextZ);

        // If next position would be a cliff (ground drops too much), cancel movement
        if (currentGroundY > INVALID_GROUND_Y && nextGroundY > INVALID_GROUND_Y) {
            if (currentGroundY - nextGroundY > RAT_MAX_STEP_DOWN) {
                moveX = 0.0f;
                moveZ = 0.0f;
                // Switch to idle when blocked by cliff
                if (!inst->state.rat.isAggro) {
                    inst->state.rat.currentAnim = RAT_IDLE_ANIM;
                }
            }
        }
    }

    // Apply horizontal movement
    inst->posX += moveX;
    inst->posZ += moveZ;

    // Apply gravity
    inst->state.rat.velY -= map->gravity;
    inst->posY += inst->state.rat.velY;

    // Collision detection (if mapLoader is set)
    if (map->mapLoader) {
        // Get our index to exclude self from decoration collision
        int myIndex = (int)(inst - map->decorations);
        // Validate index bounds (safety check for pointer arithmetic)
        if (myIndex < 0 || myIndex >= map->decoCount) {
            myIndex = -1;  // Invalid index, don't exclude anything
        }

        // Wall collision with terrain
        float pushX = 0.0f, pushZ = 0.0f;
        if (maploader_check_walls(map->mapLoader, inst->posX, inst->posY, inst->posZ,
            map->enemyRadius, map->enemyHeight, &pushX, &pushZ)) {
            inst->posX += pushX;
            inst->posZ += pushZ;
        }

        // Wall collision with decorations (barrels, etc.) - exclude self
        float decoPushX = 0.0f, decoPushZ = 0.0f;
        if (map_check_deco_walls_ex(map, inst->posX, inst->posY, inst->posZ,
            map->enemyRadius, map->enemyHeight, &decoPushX, &decoPushZ, myIndex)) {
            inst->posX += decoPushX;
            inst->posZ += decoPushZ;
        }

        // Ground collision with terrain
        inst->state.rat.isGrounded = false;
        float groundY = maploader_get_ground_height(map->mapLoader, inst->posX, inst->posY, inst->posZ);

        // Also check decoration ground (barrels, etc.) - exclude self
        float decoGroundY = map_get_deco_ground_height_ex(map, inst->posX, inst->posY, inst->posZ, myIndex);
        if (decoGroundY > groundY) {
            groundY = decoGroundY;
        }

        if (groundY > INVALID_GROUND_Y && inst->state.rat.velY <= 0 && inst->posY <= groundY + 1.0f) {
            inst->posY = groundY;
            inst->state.rat.velY = 0.0f;
            inst->state.rat.isGrounded = true;
        }

        // Respawn at spawn position if fallen off map
        if (inst->posY < -500.0f) {
            inst->posX = inst->state.rat.spawnX;
            inst->posY = inst->state.rat.spawnY;
            inst->posZ = inst->state.rat.spawnZ;
            inst->rotY = inst->state.rat.spawnRotY;
            inst->state.rat.velY = 0.0f;
            inst->state.rat.isGrounded = false;
            inst->state.rat.moveDir = inst->state.rat.spawnRotY;
            inst->state.rat.isAggro = false;
            inst->state.rat.isPouncing = false;
            inst->state.rat.pounceTimer = 0.0f;
            inst->state.rat.pauseTimer = 0.5f;  // Brief pause to settle
            inst->state.rat.currentPatrolIndex = 0;  // Reset patrol
            return;
        }
    }

    // Update per-instance animation
    if (inst->hasOwnSkeleton && inst->animCount > inst->state.rat.currentAnim) {
        t3d_anim_update(&inst->anims[inst->state.rat.currentAnim], deltaTime);
        if (skeleton_is_valid(&inst->skeleton)) {
            t3d_skeleton_update(&inst->skeleton);
        }
    }

    // Update attack cooldown
    if (inst->state.rat.attackCooldown > 0) {
        inst->state.rat.attackCooldown -= deltaTime;
    }
}

static void rat_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map; (void)px; (void)py; (void)pz;

    // Don't attack if player is dead
    extern bool player_is_dead(void);
    if (player_is_dead()) {
        return;
    }

    // Only deal damage if rat is playing attack animation
    // and animation is in second half
    if (inst->state.rat.currentAnim == RAT_ATTACK_ANIM && inst->hasOwnSkeleton && inst->animCount > RAT_ATTACK_ANIM) {
        T3DAnim* attackAnim = &inst->anims[RAT_ATTACK_ANIM];
        if (attackAnim->animRef != NULL) {
            float animProgress = attackAnim->time / attackAnim->animRef->duration;

            // Only deal damage in second half of attack animation
            if (animProgress >= RAT_ATTACK_DAMAGE_THRESHOLD && inst->state.rat.attackCooldown <= 0) {
                DECO_DEBUG("Rat %d attacked player! (anim: %.2f)\n", inst->id, animProgress);
                inst->state.rat.attackCooldown = RAT_ATTACK_COOLDOWN;
                // Deal damage to player
                extern void player_take_damage(int damage);
                player_take_damage(1);
            }
        }
    }
    if (inst->state.rat.currentAnim == RAT_POUNCE_ANIM && inst->hasOwnSkeleton && inst->animCount > RAT_POUNCE_ANIM) {
        T3DAnim* attackAnim = &inst->anims[RAT_POUNCE_ANIM];
        if (attackAnim->animRef != NULL) {
            float animProgress = attackAnim->time / attackAnim->animRef->duration;

            // Only deal damage in second half of attack animation
            if (animProgress >= RAT_ATTACK_DAMAGE_THRESHOLD && inst->state.rat.attackCooldown <= 0) {
                DECO_DEBUG("Rat %d attacked player! (anim: %.2f)\n", inst->id, animProgress);
                inst->state.rat.attackCooldown = RAT_POUNCE_COOLDOWN;
                // Deal damage to player
                extern void player_take_damage(int damage);
                player_take_damage(1);
            }
        }
    }
}

// --- PLAYER SPAWN BEHAVIOR ---
// Snap spawn point to ground so player never hovers at level start
static void playerspawn_init(DecoInstance* inst, MapRuntime* map) {
    if (!map || !map->mapLoader) return;

    // Find ground height below spawn point
    float groundY = maploader_get_ground_height(map->mapLoader, inst->posX, inst->posY + 50.0f, inst->posZ);
    if (groundY > -9000.0f) {
        // Snap to ground + small offset for player feet
        inst->posY = groundY + 2.0f;
        debugf("PlayerSpawn snapped to ground: Y=%.1f\n", inst->posY);
    }
}

// --- BOLT BEHAVIOR ---
static void bolt_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.bolt.collected = false;
    inst->state.bolt.spinAngle = 0.0f;
    // Only set save fields to -1 if they haven't been set by level_load already
    // level_load sets these BEFORE bolt_init is called, so preserve them
    // (They start at 0 from memset, so -1 means "not set by level_load")
}

static void bolt_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    if (!inst->state.bolt.collected) {
        // Spin the bolt
        inst->state.bolt.spinAngle += 3.0f * deltaTime;
        inst->rotY = inst->state.bolt.spinAngle;

        // Bolt magnetism - drift toward player when close
        float dx = map->playerX - inst->posX;
        float dy = map->playerY - inst->posY;
        float dz = map->playerZ - inst->posZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        float magnetRange = 80.0f;  // Start pulling at this distance

        if (distSq < magnetRange * magnetRange && distSq > 1.0f) {
            float dist = sqrtf(distSq);
            // Stronger pull when closer
            float pullStrength = (1.0f - dist / magnetRange) * 150.0f * deltaTime;
            inst->posX += (dx / dist) * pullStrength;
            inst->posY += (dy / dist) * pullStrength;
            inst->posZ += (dz / dist) * pullStrength;
        }
    }
}

// SFX channels start at 2 (channels 0-1 reserved for stereo music)
#define SFX_CHANNEL_START 2
#define SFX_CHANNEL_COUNT 6  // Channels 2-7
static int sfxChannel = SFX_CHANNEL_START;

// Forward declaration for save function
extern void game_on_bolt_collected(int levelId, int boltIndex);

static void bolt_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map; (void)px; (void)py; (void)pz;
    if (!inst->state.bolt.collected) {
        inst->state.bolt.collected = true;

        // Spawn dramatic starburst particles at bolt position before deactivating
        game_spawn_spark_particles(inst->posX, inst->posY, inst->posZ, 16);

        // Save bolt collection to save file
        if (inst->state.bolt.saveIndex >= 0 && inst->state.bolt.levelId >= 0) {
            game_on_bolt_collected(inst->state.bolt.levelId, inst->state.bolt.saveIndex);
        }

        inst->active = false;  // Remove bolt
        wav64_play(&sfxBoltCollect, sfxChannel);
        sfxChannel = SFX_CHANNEL_START + ((sfxChannel - SFX_CHANNEL_START + 1) % SFX_CHANNEL_COUNT);
        debugf("Bolt %d (save idx %d, level %d) collected!\n",
            inst->id, inst->state.bolt.saveIndex, inst->state.bolt.levelId);
    }
}

// --- GOLDEN SCREW BEHAVIOR ---
// Forward declaration for save function
extern void game_on_screwg_collected(int levelId, int screwgIndex);

static void screwg_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.screwG.collected = false;
    inst->state.screwG.spinAngle = 0.0f;
    // saveIndex and levelId are set by level_load, preserve them
}

static void screwg_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    if (!inst->state.screwG.collected) {
        // Spin the golden screw (slightly faster than regular bolt)
        inst->state.screwG.spinAngle += 4.0f * deltaTime;
        inst->rotY = inst->state.screwG.spinAngle;

        // Golden screw magnetism - stronger than regular bolt
        float dx = map->playerX - inst->posX;
        float dy = map->playerY - inst->posY;
        float dz = map->playerZ - inst->posZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        float magnetRange = 100.0f;  // Slightly larger range than regular bolt

        if (distSq < magnetRange * magnetRange && distSq > 1.0f) {
            float dist = sqrtf(distSq);
            // Stronger pull when closer
            float pullStrength = (1.0f - dist / magnetRange) * 180.0f * deltaTime;
            inst->posX += (dx / dist) * pullStrength;
            inst->posY += (dy / dist) * pullStrength;
            inst->posZ += (dz / dist) * pullStrength;
        }
    }
}

static void screwg_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map; (void)px; (void)py; (void)pz;
    if (!inst->state.screwG.collected) {
        inst->state.screwG.collected = true;

        // Spawn extra dramatic golden starburst particles
        game_spawn_spark_particles(inst->posX, inst->posY, inst->posZ, 24);

        // Save golden screw collection to save file
        if (inst->state.screwG.saveIndex >= 0 && inst->state.screwG.levelId >= 0) {
            game_on_screwg_collected(inst->state.screwG.levelId, inst->state.screwG.saveIndex);
        }

        inst->active = false;  // Remove golden screw
        wav64_play(&sfxBoltCollect, sfxChannel);
        sfxChannel = SFX_CHANNEL_START + ((sfxChannel - SFX_CHANNEL_START + 1) % SFX_CHANNEL_COUNT);
        debugf("Golden Screw %d (save idx %d, level %d) collected!\n",
            inst->id, inst->state.screwG.saveIndex, inst->state.screwG.levelId);
    }
}

// --- TORSO PICKUP BEHAVIOR ---
// Get current level for tutorial return
extern int selectedLevelID;

// External reference to player's torso model (loaded in game.c)
extern T3DModel *torsoModel;

static void torsopickup_init(DecoInstance* inst, MapRuntime* map) {
    inst->state.torsoPickup.collected = false;
    inst->state.torsoPickup.spinAngle = 0.0f;
    inst->state.torsoPickup.bobOffset = 0.0f;
    inst->state.torsoPickup.bobPhase = (float)(inst->id % 100) * 0.1f;  // Randomize phase per instance
    inst->state.torsoPickup.demoIndex = 0;  // Default demo, can be set per-decoration
    inst->hasOwnSkeleton = false;
    inst->animCount = 0;

    // Use the shared torsoModel from game.c (already loaded for player)
    // Set it on the DecoTypeRuntime so rendering works
    DecoTypeRuntime* decoType = map_get_deco_type(map, DECO_TORSO_PICKUP);
    if (decoType && torsoModel) {
        decoType->model = torsoModel;
        decoType->loaded = true;

        // Skip skeleton/animation in demo mode to prevent FILE handle exhaustion
        extern bool g_demoMode;
        extern bool g_replayMode;
        if (g_demoMode || g_replayMode) {
            // Just use the model without skeleton in demo mode
            debugf("Torso pickup: skipping skeleton in demo mode\n");
        } else if (!decoType->hasSkeleton) {
            // Create skeleton for torso_broken pose (if animation exists)
            // Check if animation exists first to avoid assertion failure
            T3DChunkAnim* animDef = t3d_model_get_animation(torsoModel, "torso_broken");
            if (animDef) {
                decoType->skeleton = t3d_skeleton_create(torsoModel);
                decoType->hasSkeleton = true;
                // Load and apply torso_broken animation (static pose)
                T3DAnim anim = t3d_anim_create(torsoModel, "torso_broken");
                if (anim.animRef && skeleton_is_valid(&decoType->skeleton)) {
                    t3d_anim_attach(&anim, &decoType->skeleton);
                    t3d_anim_set_time(&anim, 0.0f);  // Set to first frame
                    t3d_anim_update(&anim, 0.0f);
                    t3d_skeleton_update(&decoType->skeleton);
                    decoType->anims[0] = anim;
                    decoType->animCount = 1;
                    debugf("Torso pickup: using shared model with torso_broken pose\n");
                }
            } else {
                // Animation not available - use model without pose
                debugf("Torso pickup: torso_broken animation not found, using default pose\n");
            }
        }
    }
    debugf("Torso pickup initialized\n");
}

static void torsopickup_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    if (inst->state.torsoPickup.collected) return;

    // Spin the pickup
    inst->state.torsoPickup.spinAngle += 2.0f * deltaTime;
    inst->rotY = inst->state.torsoPickup.spinAngle;

    // Bob up and down (slower than spinning)
    inst->state.torsoPickup.bobPhase += 2.5f * deltaTime;
    inst->state.torsoPickup.bobOffset = sinf(inst->state.torsoPickup.bobPhase) * 5.0f;

    // Magnetism toward player (like bolt but weaker)
    float dx = map->playerX - inst->posX;
    float dy = map->playerY - inst->posY;
    float dz = map->playerZ - inst->posZ;
    float distSq = dx * dx + dy * dy + dz * dz;
    float magnetRange = 60.0f;

    if (distSq < magnetRange * magnetRange && distSq > 1.0f) {
        float dist = sqrtf(distSq);
        float pullStrength = (1.0f - dist / magnetRange) * 100.0f * deltaTime;
        inst->posX += (dx / dist) * pullStrength;
        inst->posY += (dy / dist) * pullStrength;
        inst->posZ += (dz / dist) * pullStrength;
    }
}

// Track if torso tutorial has been completed this session
static bool g_torsoTutorialCompleted = false;

static void torsopickup_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map;
    if (inst->state.torsoPickup.collected) return;

    // Check if torso tutorial was already completed this session
    if (g_torsoTutorialCompleted) {
        // Already did tutorial, just remove the pickup silently
        inst->state.torsoPickup.collected = true;
        inst->active = false;
        debugf("Torso pickup: tutorial already completed, removing pickup\n");
        return;
    }

    inst->state.torsoPickup.collected = true;
    g_torsoTutorialCompleted = true;  // Mark tutorial as done

    // Spawn sparks at pickup position
    game_spawn_spark_particles(inst->posX, inst->posY, inst->posZ, 12);

    // Play collection sound
    wav64_play(&sfxBoltCollect, sfxChannel);
    sfxChannel = SFX_CHANNEL_START + ((sfxChannel - SFX_CHANNEL_START + 1) % SFX_CHANNEL_COUNT);

    inst->active = false;  // Remove pickup

    debugf("Torso pickup collected! Starting tutorial demo\n");

    // Start the torso tutorial - plays demo with dialogue, then returns player here with torso
    demo_start_tutorial(TUTORIAL_TORSO, px, py, pz, selectedLevelID);
}

static void damagecollision_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    extern void player_take_damage(int damage);
    extern bool player_is_dead(void);

    if (!player_is_dead()) {
        player_take_damage(999);  // Instant death
    }
}

static void damagecube_light_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    extern void player_take_damage(int damage);
    extern bool player_is_dead(void);

    if (!player_is_dead()) {
        player_take_damage(1);  // Light damage
    }
}

// --- MOVING LASER BEHAVIOR ---
// Laser that moves between start and target positions, always does 1 damage on contact

#define MOVING_LASER_DEFAULT_SPEED 60.0f  // Units per second

static void movinglaser_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Store starting position
    inst->state.movePlat.startX = inst->posX;
    inst->state.movePlat.startY = inst->posY;
    inst->state.movePlat.startZ = inst->posZ;
    // Only set defaults if target wasn't configured (all zeros means unconfigured)
    if (inst->state.movePlat.targetX == 0.0f &&
        inst->state.movePlat.targetY == 0.0f &&
        inst->state.movePlat.targetZ == 0.0f) {
        inst->state.movePlat.targetX = inst->posX;
        inst->state.movePlat.targetY = inst->posY;
        inst->state.movePlat.targetZ = inst->posZ;
    }
    inst->state.movePlat.progress = 0.0f;
    // Only set default speed if not already configured
    if (inst->state.movePlat.speed <= 0.0f) {
        inst->state.movePlat.speed = MOVING_LASER_DEFAULT_SPEED;
    }
    inst->state.movePlat.direction = 1;  // Start moving toward target
    inst->state.movePlat.lastDeltaX = 0.0f;
    inst->state.movePlat.lastDeltaY = 0.0f;
    inst->state.movePlat.lastDeltaZ = 0.0f;
    inst->state.movePlat.playerOnPlat = false;
    inst->state.movePlat.activated = true;  // Always active
    debugf("MovingLaser init at (%.1f, %.1f, %.1f) -> target (%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ,
           inst->state.movePlat.targetX, inst->state.movePlat.targetY, inst->state.movePlat.targetZ);
}

static void movinglaser_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;

    // Calculate total distance for speed normalization
    float dx = inst->state.movePlat.targetX - inst->state.movePlat.startX;
    float dy = inst->state.movePlat.targetY - inst->state.movePlat.startY;
    float dz = inst->state.movePlat.targetZ - inst->state.movePlat.startZ;
    float totalDist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (totalDist > 0.1f) {
        // Calculate progress delta based on speed and total distance
        float progressDelta = (inst->state.movePlat.speed * deltaTime) / totalDist;
        inst->state.movePlat.progress += progressDelta * inst->state.movePlat.direction;

        // Reverse direction at endpoints
        if (inst->state.movePlat.progress >= 1.0f) {
            inst->state.movePlat.progress = 1.0f;
            inst->state.movePlat.direction = -1;
        } else if (inst->state.movePlat.progress <= 0.0f) {
            inst->state.movePlat.progress = 0.0f;
            inst->state.movePlat.direction = 1;
        }

        // Linear interpolation
        float t = inst->state.movePlat.progress;

        // Interpolate position linearly
        inst->posX = inst->state.movePlat.startX + dx * t;
        inst->posY = inst->state.movePlat.startY + dy * t;
        inst->posZ = inst->state.movePlat.startZ + dz * t;
    }
}

static void movinglaser_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    extern void player_take_damage(int damage);
    extern bool player_is_dead(void);

    if (!player_is_dead()) {
        player_take_damage(1);  // 1 damage on contact
    }
}

// --- HOT PIPE BEHAVIOR ---
// Damages player when standing on it unless they have the invincibility buff
static void hotpipe_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)deltaTime;
    extern void player_take_damage(int damage);
    extern bool player_is_dead(void);
    extern bool buff_get_invincible(void);

    if (buff_get_invincible() || player_is_dead()) return;

    // Get deco type for collision check
    DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
    if (!decoType || !decoType->loaded || !decoType->collision) return;

    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    // Use wall collision check with large radius to detect proximity to mesh
    // If player is within ~15 units of any surface, they take damage
    float pushX = 0.0f, pushZ = 0.0f;
    bool nearMesh = collision_check_walls(decoType->collision,
        px, py, pz,
        20.0f,  // Large radius for proximity detection
        30.0f,  // Player height
        inst->posX, inst->posY, inst->posZ,
        inst->scaleX, inst->scaleY, inst->scaleZ,
        &pushX, &pushZ);

    if (nearMesh) {
        player_take_damage(1);
    }
}

static void hotpipe_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    // Damage handled in update via proximity check
}

// --- CHECKPOINT BEHAVIOR ---
// Sets the checkpoint as the new spawn point when player touches it
// Can also trigger lighting changes (smooth lerp to new ambient/directional colors)
static void checkpoint_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map;

    // Only activate if not already the active checkpoint
    if (!g_checkpointActive ||
        g_checkpointX != inst->posX ||
        g_checkpointY != inst->posY ||
        g_checkpointZ != inst->posZ) {

        g_checkpointActive = true;
        g_checkpointX = inst->posX;
        // BlueCube model spans Y [17.8, 145.8], center is at +81.8 from origin
        g_checkpointY = inst->posY + (81.8f * inst->scaleY);  // Spawn at geometric center of scaled cube
        g_checkpointZ = inst->posZ;

        debugf("Checkpoint activated! Deco pos=(%.1f, %.1f, %.1f), spawn=(%.1f, %.1f, %.1f), player was at (%.1f, %.1f, %.1f)\n",
               inst->posX, inst->posY, inst->posZ,
               g_checkpointX, g_checkpointY, g_checkpointZ,
               px, py, pz);

        // Trigger lighting change if checkpoint has lighting colors configured
        game_trigger_lighting_change(
            inst->state.checkpoint.ambientR,
            inst->state.checkpoint.ambientG,
            inst->state.checkpoint.ambientB,
            inst->state.checkpoint.directionalR,
            inst->state.checkpoint.directionalG,
            inst->state.checkpoint.directionalB);
    }
}

// --- LIGHT TRIGGER BEHAVIOR ---
// Trigger zone that blends ENTITY lighting based on distance from center
// Closer to center = more of the trigger's lighting, farther = base lighting
// Note: Only affects player/decoration lighting, NOT map geometry (which uses static level light)
// Forward declarations (defined in game.c)
void game_trigger_lighting_change_ex(uint8_t ambientR, uint8_t ambientG, uint8_t ambientB,
                                      uint8_t entityDirectR, uint8_t entityDirectG, uint8_t entityDirectB,
                                      float entityDirX, float entityDirY, float entityDirZ);
void game_get_current_lighting(uint8_t* ambientR, uint8_t* ambientG, uint8_t* ambientB,
                                uint8_t* entityDirectR, uint8_t* entityDirectG, uint8_t* entityDirectB,
                                float* entityDirX, float* entityDirY, float* entityDirZ);
void game_set_lighting_direct(uint8_t ambientR, uint8_t ambientG, uint8_t ambientB,
                               uint8_t entityDirectR, uint8_t entityDirectG, uint8_t entityDirectB,
                               float entityDirX, float entityDirY, float entityDirZ);

// Fog state functions (used by DECO_FOGCOLOR to override fog while player is in trigger zone)
void game_get_current_fog(uint8_t* fogR, uint8_t* fogG, uint8_t* fogB, float* fogNear, float* fogFar);
void game_set_fog_override(uint8_t fogR, uint8_t fogG, uint8_t fogB, float fogNear, float fogFar, float blend);
void game_clear_fog_override(void);

static void lighttrigger_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.lightTrigger.playerInside = false;
    inst->state.lightTrigger.hasSavedLighting = false;
    debugf("LightTrigger init at (%.1f, %.1f, %.1f) scale(%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ, inst->scaleX, inst->scaleY, inst->scaleZ);
}

static void lighttrigger_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)deltaTime;

    // Calculate distance from player to trigger center
    float dx = map->playerX - inst->posX;
    float dy = map->playerY - inst->posY;
    float dz = map->playerZ - inst->posZ;
    float distSq = dx*dx + dy*dy + dz*dz;
    float dist = sqrtf(distSq);

    // Calculate effective radius from scale (use average of XZ for horizontal, ignore Y for now)
    // Col_Cube is 64 units, so effective radius is 32 * scale
    float effectiveRadius = 32.0f * fmaxf(inst->scaleX, inst->scaleZ);

    // Check if player is within influence range (use 1.5x radius for smooth falloff)
    float maxRange = effectiveRadius * 1.5f;
    bool inRange = dist < maxRange;

    // Save base lighting when first entering range
    if (inRange && !inst->state.lightTrigger.hasSavedLighting) {
        game_get_current_lighting(
            &inst->state.lightTrigger.prevAmbientR,
            &inst->state.lightTrigger.prevAmbientG,
            &inst->state.lightTrigger.prevAmbientB,
            &inst->state.lightTrigger.prevDirectionalR,
            &inst->state.lightTrigger.prevDirectionalG,
            &inst->state.lightTrigger.prevDirectionalB,
            &inst->state.lightTrigger.prevLightDirX,
            &inst->state.lightTrigger.prevLightDirY,
            &inst->state.lightTrigger.prevLightDirZ);
        inst->state.lightTrigger.hasSavedLighting = true;
    }

    // Calculate blend factor based on distance (1.0 at center, 0.0 at edge)
    if (inRange && inst->state.lightTrigger.hasSavedLighting) {
        // Smoothstep falloff: full effect at center, fades to 0 at maxRange
        float t = dist / maxRange;  // 0 at center, 1 at edge
        t = fminf(1.0f, fmaxf(0.0f, t));  // Clamp to [0, 1]
        // Invert and apply smoothstep for smooth falloff
        float blend = 1.0f - t;
        blend = blend * blend * (3.0f - 2.0f * blend);  // Smoothstep

        // Lerp between base lighting and trigger lighting
        uint8_t ambR = (uint8_t)(inst->state.lightTrigger.prevAmbientR + blend * ((int)inst->state.lightTrigger.ambientR - (int)inst->state.lightTrigger.prevAmbientR));
        uint8_t ambG = (uint8_t)(inst->state.lightTrigger.prevAmbientG + blend * ((int)inst->state.lightTrigger.ambientG - (int)inst->state.lightTrigger.prevAmbientG));
        uint8_t ambB = (uint8_t)(inst->state.lightTrigger.prevAmbientB + blend * ((int)inst->state.lightTrigger.ambientB - (int)inst->state.lightTrigger.prevAmbientB));
        uint8_t dirR = (uint8_t)(inst->state.lightTrigger.prevDirectionalR + blend * ((int)inst->state.lightTrigger.directionalR - (int)inst->state.lightTrigger.prevDirectionalR));
        uint8_t dirG = (uint8_t)(inst->state.lightTrigger.prevDirectionalG + blend * ((int)inst->state.lightTrigger.directionalG - (int)inst->state.lightTrigger.prevDirectionalG));
        uint8_t dirB = (uint8_t)(inst->state.lightTrigger.prevDirectionalB + blend * ((int)inst->state.lightTrigger.directionalB - (int)inst->state.lightTrigger.prevDirectionalB));

        // Lerp light direction if specified
        float lDirX = 999.0f, lDirY = 999.0f, lDirZ = 999.0f;
        if (inst->state.lightTrigger.lightDirX < 900.0f) {
            lDirX = inst->state.lightTrigger.prevLightDirX + blend * (inst->state.lightTrigger.lightDirX - inst->state.lightTrigger.prevLightDirX);
            lDirY = inst->state.lightTrigger.prevLightDirY + blend * (inst->state.lightTrigger.lightDirY - inst->state.lightTrigger.prevLightDirY);
            lDirZ = inst->state.lightTrigger.prevLightDirZ + blend * (inst->state.lightTrigger.lightDirZ - inst->state.lightTrigger.prevLightDirZ);
        }

        // Apply blended lighting directly
        game_set_lighting_direct(ambR, ambG, ambB, dirR, dirG, dirB, lDirX, lDirY, lDirZ);

        inst->state.lightTrigger.playerInside = true;
    } else if (!inRange && inst->state.lightTrigger.playerInside) {
        // Restore base lighting when leaving range
        game_set_lighting_direct(
            inst->state.lightTrigger.prevAmbientR,
            inst->state.lightTrigger.prevAmbientG,
            inst->state.lightTrigger.prevAmbientB,
            inst->state.lightTrigger.prevDirectionalR,
            inst->state.lightTrigger.prevDirectionalG,
            inst->state.lightTrigger.prevDirectionalB,
            inst->state.lightTrigger.prevLightDirX,
            inst->state.lightTrigger.prevLightDirY,
            inst->state.lightTrigger.prevLightDirZ);
        inst->state.lightTrigger.playerInside = false;
        inst->state.lightTrigger.hasSavedLighting = false;
    }
}

// --- FOG COLOR TRIGGER BEHAVIOR ---
// Changes fog color and range while player is inside the trigger zone
// Uses time-based lerp for smooth transitions
#define FOGCOLOR_LERP_SPEED 2.0f  // Blend speed (1.0 = 1 second to full blend)

static void fogcolor_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.fogColor.playerInside = false;
    inst->state.fogColor.hasSavedFog = false;
    inst->state.fogColor.blendProgress = 0.0f;
    debugf("FogColor init at (%.1f, %.1f, %.1f) scale(%.1f, %.1f, %.1f) color(%d,%d,%d) range(%.1f-%.1f)\n",
           inst->posX, inst->posY, inst->posZ, inst->scaleX, inst->scaleY, inst->scaleZ,
           inst->state.fogColor.fogR, inst->state.fogColor.fogG, inst->state.fogColor.fogB,
           inst->state.fogColor.fogNear, inst->state.fogColor.fogFar);
}

static void fogcolor_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Check if player is inside the trigger volume using Col_Cube collision
    // Col_Cube is 64 units, scaled by instance scale
    float halfW = 32.0f * inst->scaleX;
    float halfH = 32.0f * inst->scaleY;
    float halfD = 32.0f * inst->scaleZ;

    float dx = map->playerX - inst->posX;
    float dy = map->playerY - inst->posY;
    float dz = map->playerZ - inst->posZ;

    bool insideVolume = (dx > -halfW && dx < halfW &&
                         dy > -halfH && dy < halfH &&
                         dz > -halfD && dz < halfD);

    // Save base fog when first entering
    if (insideVolume && !inst->state.fogColor.hasSavedFog) {
        game_get_current_fog(
            &inst->state.fogColor.prevFogR,
            &inst->state.fogColor.prevFogG,
            &inst->state.fogColor.prevFogB,
            &inst->state.fogColor.prevFogNear,
            &inst->state.fogColor.prevFogFar);
        inst->state.fogColor.hasSavedFog = true;
    }

    // Update blend progress based on whether player is inside
    if (insideVolume) {
        // Lerp toward 1.0 (target fog)
        inst->state.fogColor.blendProgress += deltaTime * FOGCOLOR_LERP_SPEED;
        if (inst->state.fogColor.blendProgress > 1.0f) {
            inst->state.fogColor.blendProgress = 1.0f;
        }
        inst->state.fogColor.playerInside = true;
    } else if (inst->state.fogColor.playerInside) {
        // Just exited - mark as outside but KEEP the fog (persistent)
        inst->state.fogColor.playerInside = false;
        // Reset hasSavedFog so re-entering this zone will capture new base fog
        inst->state.fogColor.hasSavedFog = false;
        // Fog stays at full blend until player enters another fog zone
    }

    // Apply blended fog if we have saved fog state
    if (inst->state.fogColor.hasSavedFog) {
        float blend = inst->state.fogColor.blendProgress;

        // Lerp between base fog and trigger fog
        uint8_t fogR = (uint8_t)(inst->state.fogColor.prevFogR + blend * ((int)inst->state.fogColor.fogR - (int)inst->state.fogColor.prevFogR));
        uint8_t fogG = (uint8_t)(inst->state.fogColor.prevFogG + blend * ((int)inst->state.fogColor.fogG - (int)inst->state.fogColor.prevFogG));
        uint8_t fogB = (uint8_t)(inst->state.fogColor.prevFogB + blend * ((int)inst->state.fogColor.fogB - (int)inst->state.fogColor.prevFogB));

        // Lerp fog range (only if target range is specified, i.e., not 0,0)
        float fogNear = inst->state.fogColor.prevFogNear;
        float fogFar = inst->state.fogColor.prevFogFar;
        if (inst->state.fogColor.fogNear > 0 || inst->state.fogColor.fogFar > 0) {
            fogNear = inst->state.fogColor.prevFogNear + blend * (inst->state.fogColor.fogNear - inst->state.fogColor.prevFogNear);
            fogFar = inst->state.fogColor.prevFogFar + blend * (inst->state.fogColor.fogFar - inst->state.fogColor.prevFogFar);
        }

        // Apply blended fog
        game_set_fog_override(fogR, fogG, fogB, fogNear, fogFar, blend);
    }
}

// --- PERMANENT LIGHT TRIGGER BEHAVIOR ---
// Permanently changes lighting when player enters, uses time-based lerp (not distance-based)
static void lighttrigger_permanent_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.lightTriggerPermanent.hasTriggered = false;
    debugf("LightTriggerPermanent init at (%.1f, %.1f, %.1f) scale(%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ, inst->scaleX, inst->scaleY, inst->scaleZ);
}

static void lighttrigger_permanent_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)deltaTime;

    // Calculate distance from player to trigger center
    float dx = map->playerX - inst->posX;
    float dy = map->playerY - inst->posY;
    float dz = map->playerZ - inst->posZ;
    float distSq = dx*dx + dy*dy + dz*dz;
    float dist = sqrtf(distSq);

    // Calculate effective radius from scale (Col_Cube is 64 units, so effective radius is 32 * scale)
    float effectiveRadius = 32.0f * fmaxf(inst->scaleX, inst->scaleZ);

    // Check if player is inside the trigger volume
    bool inside = dist < effectiveRadius;

    if (inside && !inst->state.lightTriggerPermanent.hasTriggered) {
        // Player just entered - trigger the lighting change with time-based lerp
        inst->state.lightTriggerPermanent.hasTriggered = true;

        // Use the game's built-in lerp system for smooth transition
        // If light direction is specified, use the extended version
        if (inst->state.lightTriggerPermanent.lightDirX < 900.0f) {
            game_trigger_lighting_change_ex(
                inst->state.lightTriggerPermanent.ambientR,
                inst->state.lightTriggerPermanent.ambientG,
                inst->state.lightTriggerPermanent.ambientB,
                inst->state.lightTriggerPermanent.directionalR,
                inst->state.lightTriggerPermanent.directionalG,
                inst->state.lightTriggerPermanent.directionalB,
                inst->state.lightTriggerPermanent.lightDirX,
                inst->state.lightTriggerPermanent.lightDirY,
                inst->state.lightTriggerPermanent.lightDirZ);
        } else {
            // No direction change, use standard version
            game_trigger_lighting_change(
                inst->state.lightTriggerPermanent.ambientR,
                inst->state.lightTriggerPermanent.ambientG,
                inst->state.lightTriggerPermanent.ambientB,
                inst->state.lightTriggerPermanent.directionalR,
                inst->state.lightTriggerPermanent.directionalG,
                inst->state.lightTriggerPermanent.directionalB);
        }

        debugf("LightTriggerPermanent activated: ambient=(%d,%d,%d) entity=(%d,%d,%d)\n",
               inst->state.lightTriggerPermanent.ambientR,
               inst->state.lightTriggerPermanent.ambientG,
               inst->state.lightTriggerPermanent.ambientB,
               inst->state.lightTriggerPermanent.directionalR,
               inst->state.lightTriggerPermanent.directionalG,
               inst->state.lightTriggerPermanent.directionalB);
    } else if (!inside && inst->state.lightTriggerPermanent.hasTriggered) {
        // Player left the trigger - reset so it can trigger again on re-entry
        inst->state.lightTriggerPermanent.hasTriggered = false;
    }
}

// --- LIGHT BEHAVIOR ---
// Point light source - color is set from DecoPlacement during level load
static void light_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Default white light with 2000 unit radius if not configured
    if (inst->state.light.colorR == 0 && inst->state.light.colorG == 0 && inst->state.light.colorB == 0) {
        inst->state.light.colorR = 255;
        inst->state.light.colorG = 255;
        inst->state.light.colorB = 255;
    }
    if (inst->state.light.radius <= 0.0f) {
        inst->state.light.radius = 2000.0f;
    }
    debugf("Light initialized at (%.1f, %.1f, %.1f) color=(%d,%d,%d) radius=%.0f\n",
           inst->posX, inst->posY, inst->posZ,
           inst->state.light.colorR, inst->state.light.colorG, inst->state.light.colorB,
           inst->state.light.radius);
}

// --- PAIN TUBE BEHAVIOR ---
static void paintube_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    extern void player_take_damage(int damage);
    extern bool player_is_dead(void);

    if (!player_is_dead()) {
        player_take_damage(1);  // Light damage
    }
}

// --- CHARGEPAD BEHAVIOR ---
// Chargepad grants temporary buffs based on current body part:
// - Torso (1): 2x jump height on next charge jump
// - Arms (2): 2x glide distance (reduced gravity during glide)
// - Legs/Fullbody (3): 10 seconds of invincibility + double run speed

#define CHARGEPAD_COOLDOWN 1.0f       // Seconds before can re-activate after granting buff
#define CHARGEPAD_SPARK_INTERVAL 0.1f // Seconds between spark particle spawns
#define CHARGEPAD_BUFF_DURATION 10.0f // Duration of speed/invincibility buff (legs mode)

// Extern declarations for buff system (defined in game.c)
extern void buff_set_jump_active(bool active);
extern void buff_set_glide_active(bool active);
extern void buff_set_speed_timer(float timer);
extern bool buff_get_jump_active(void);
extern bool buff_get_glide_active(void);
extern float buff_get_speed_timer(void);

// Extern declaration for current body part (defined in game.c)
extern int get_current_body_part(void);

// Helper to check if player has any active chargepad buff
static inline bool player_has_chargepad_buff(void) {
    return buff_get_jump_active() || buff_get_glide_active() || buff_get_speed_timer() > 0.0f;
}

static void chargepad_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.chargepad.sparkTimer = 0.0f;
    inst->state.chargepad.cooldownTimer = 0.0f;
    inst->state.chargepad.isActive = true;
}

static void chargepad_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;

    // Update cooldown timer
    if (inst->state.chargepad.cooldownTimer > 0.0f) {
        inst->state.chargepad.cooldownTimer -= deltaTime;
        if (inst->state.chargepad.cooldownTimer <= 0.0f) {
            inst->state.chargepad.cooldownTimer = 0.0f;
            inst->state.chargepad.isActive = true;
        }
    }

    // Track if chargepad should glow (active and player doesn't have buff)
    bool playerHasBuff = player_has_chargepad_buff();
    inst->state.chargepad.glowing = inst->state.chargepad.isActive && !playerHasBuff;
}

static void chargepad_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map; (void)px; (void)py; (void)pz;
    extern bool player_is_dead(void);

    // Don't activate if on cooldown, dead, or already has buff
    if (!inst->state.chargepad.isActive || player_is_dead()) return;

    // Get current body part
    int bodyPart = get_current_body_part();

    // Grant buff based on body part
    switch (bodyPart) {
        case 1:  // PART_TORSO
            buff_set_jump_active(true);
            debugf("Chargepad: Jump buff activated!\n");
            break;
        case 2:  // PART_ARMS
            buff_set_glide_active(true);
            debugf("Chargepad: Glide buff activated!\n");
            break;
        case 0:  // PART_HEAD (fallthrough to legs behavior)
        case 3:  // PART_LEGS (fullbody)
        default:
            buff_set_speed_timer(CHARGEPAD_BUFF_DURATION);
            debugf("Chargepad: Speed/invincibility buff activated for %.1fs!\n", CHARGEPAD_BUFF_DURATION);
            break;
    }

    // Set cooldown
    inst->state.chargepad.cooldownTimer = CHARGEPAD_COOLDOWN;
    inst->state.chargepad.isActive = false;

    // Visual/audio feedback: spawn burst of sparks
    game_spawn_splash_particles(
        inst->posX, inst->posY + 10.0f * inst->scaleY, inst->posZ,
        8,        // Burst of particles
        100, 200, 255  // Bright cyan
    );
}

// --- TURRET BEHAVIOR ---
// Turret constants (sniper-style: lock on, beep, then fire hitscan)
#define TURRET_TRACKING_SPEED 2.3f       // Radians per second cannon rotates
#define TURRET_LOCKON_TIME 2.0f          // Seconds to lock on to player
#define TURRET_FIRE_WARNING_TIME 0.17f   // Seconds between zap sound and projectile spawn
#define TURRET_HITSCAN_DELAY 0.08f       // Seconds after projectile spawns before hitscan check
#define TURRET_COOLDOWN_TIME 1.0f        // Seconds between shots (lock-on resets)
#define TURRET_AIM_THRESHOLD 0.05f        // Radians - how close to aiming at player to fire
#define TURRET_PROJECTILE_SPEED 1400.0f   // Units per second (very fast - visual only)
#define TURRET_PROJECTILE_GRAVITY 0.0f   // No gravity for sniper projectile
#define TURRET_PROJECTILE_LIFETIME 1.5f  // Seconds before projectile expires (short - fast travel)
#define TURRET_DETECTION_RANGE 300.0f    // Range at which turret activates
#define TURRET_CANNON_HEIGHT 18.0f       // Height offset for cannon pivot point
#define TURRET_PROJECTILE_RADIUS 5.0f    // Collision radius for projectiles (visual only)
#define TURRET_MAX_PROJECTILES 4         // Max active projectiles per turret
#define TURRET_FIRE_ANIM_DURATION 0.833f // Duration of fire animation in seconds

// ============================================================
// COLLISION OPTIMIZATION CONSTANTS
// ============================================================
#define COLLISION_UPDATE_RANGE_SQ (800.0f * 800.0f)  // Square of range for full collision checks
#define COLLISION_FAR_RANGE_SQ (1500.0f * 1500.0f)   // Square of range where objects are culled entirely
#define GROUND_CACHE_DISTANCE 10.0f                   // Distance within which to reuse cached ground height
#define PROJECTILE_CHECK_INTERVAL 3                   // Check every Nth frame for fast projectiles

static void turret_init(DecoInstance* inst, MapRuntime* map) {
    inst->state.turret.cannonRotY = inst->rotY;  // Start facing same direction as base
    inst->state.turret.cannonRotX = 0.0f;        // Start level (no pitch)
    inst->state.turret.trackingSpeed = TURRET_TRACKING_SPEED;
    inst->state.turret.isActive = true;
    inst->state.turret.health = 1;               // 1 hit to destroy
    inst->state.turret.isDead = false;
    inst->state.turret.activeProjectiles = 0;
    inst->state.turret.animInitialized = false;  // Will be set on first update

    // Initialize sniper lock-on state
    inst->state.turret.lockOnTimer = 0.0f;
    inst->state.turret.lockOnComplete = false;
    inst->state.turret.zapPlayed = false;
    inst->state.turret.isFiring = false;
    inst->state.turret.fireTimer = 0.0f;
    inst->state.turret.shotDelay = 0.0f;
    inst->state.turret.railFired = false;
    inst->state.turret.hitPlayer = false;
    inst->state.turret.lockedAimX = 0.0f;
    inst->state.turret.lockedAimY = 0.0f;
    inst->state.turret.lockedAimZ = 1.0f;  // Default forward
    inst->state.turret.lockedCannonRotX = 0.0f;
    inst->state.turret.lockedCannonRotY = inst->rotY;

    // Initialize projectile pool (visual only)
    for (int i = 0; i < TURRET_MAX_PROJECTILES; i++) {
        inst->state.turret.projLife[i] = 0.0f;  // All projectiles inactive
    }
    
    // Cache our index in the decorations array (performance optimization)
    inst->state.turret.cachedIndex = -1;
    for (int i = 0; i < map->decoCount; i++) {
        if (&map->decorations[i] == inst) {
            inst->state.turret.cachedIndex = i;
            break;
        }
    }

    // Create per-instance skeleton for fire animation (cannon is primary model)
    DecoTypeRuntime* turretType = map_get_deco_type(map, DECO_TURRET);
    if (turretType && turretType->model && turretType->hasSkeleton) {
        inst->skeleton = t3d_skeleton_create(turretType->model);
        inst->hasOwnSkeleton = true;
        inst->animCount = 0;

        // Load turret_fire animation
        uint32_t animCount = t3d_model_get_animation_count(turretType->model);
        if (animCount > 0) {
            // Look for turret_fire animation by name
            for (uint32_t i = 0; i < animCount && inst->animCount < MAX_DECO_ANIMS; i++) {
                T3DAnim anim = t3d_anim_create(turretType->model, "turret_fire");
                if (anim.animRef) {
                    inst->anims[inst->animCount] = anim;
                    t3d_anim_attach(&inst->anims[inst->animCount], &inst->skeleton);
                    t3d_anim_set_looping(&inst->anims[inst->animCount], false);
                    t3d_anim_set_playing(&inst->anims[inst->animCount], false);
                    // Set to first actual frame (0.001s past bind pose at frame 0)
                    t3d_anim_set_time(&inst->anims[inst->animCount], 0.001f);
                    // Update animation to calculate bone transforms, then update skeleton
                    t3d_anim_update(&inst->anims[inst->animCount], 0.0f);
                    t3d_skeleton_update(&inst->skeleton);
                    inst->animCount++;
                    debugf("TURRET: Loaded turret_fire animation (cached index: %d)\n", inst->state.turret.cachedIndex);
                    break;  // Only need one animation
                }
            }
        }
    }
}

static void turret_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Skip if destroyed
    if (inst->state.turret.isDead) return;

    // First-frame animation initialization (fixes spawn in default pose)
    // IMPORTANT: Must happen before distance culling so it runs even when spawned far away
    if (!inst->state.turret.animInitialized && inst->hasOwnSkeleton && inst->animCount > 0) {
        T3DAnim* fireAnim = &inst->anims[0];
        t3d_anim_set_playing(fireAnim, true);     // Start playing
        t3d_anim_set_time(fireAnim, 0.001f);      // Set to first frame
        t3d_anim_update(fireAnim, 0.0f);          // Calculate transforms
        t3d_skeleton_update(&inst->skeleton);     // Apply to skeleton
        t3d_anim_set_playing(fireAnim, false);    // Pause on first frame
        inst->state.turret.animInitialized = true;
    }

    // SFX channel cycling (shared with other decorations)
    static int sfxChannel = SFX_CHANNEL_START;

    // Check activation system
    bool isActive = (inst->activationId == 0) || activation_get(inst->activationId);
    inst->state.turret.isActive = isActive;

    // Get player position
    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    // OPTIMIZATION: Calculate distance to player (horizontal and 3D)
    float dx = px - inst->posX;
    float dz = pz - inst->posZ;
    float distSq = dx * dx + dz * dz;
    
    // Far distance culling - completely skip update if too far
    if (distSq > COLLISION_FAR_RANGE_SQ) {
        return;
    }
    
    // Distance-based LOD flag
    bool useLOD = (distSq > COLLISION_UPDATE_RANGE_SQ);
    
    float horizontalDist = sqrtf(distSq);

    // Update fire animation (standard system handles playback)
    if (inst->hasOwnSkeleton && inst->animCount > 0) {
        T3DAnim* fireAnim = &inst->anims[0];  // turret_fire is first animation
        if (fireAnim->isPlaying) {
            t3d_anim_update(fireAnim, deltaTime);
            t3d_skeleton_update(&inst->skeleton);  // Update skeleton after animation
            // Check if animation finished
            if (fireAnim->time >= fireAnim->animRef->duration) {
                t3d_anim_set_playing(fireAnim, false);
                t3d_anim_set_time(fireAnim, 0.001f);  // Reset to first frame (not bind pose)
                t3d_anim_update(fireAnim, 0.0f);      // Force update to first frame
                t3d_skeleton_update(&inst->skeleton);  // Apply first frame pose
            }
        }
    }

    // Cannon tracking and firing (only if active and player in range)
    bool playerInRange = isActive && distSq < TURRET_DETECTION_RANGE * TURRET_DETECTION_RANGE;

    if (playerInRange) {
        // Calculate target angles for tracking (skip tracking during firing sequence)
        float targetAngleY = atan2f(-dx, dz);
        float targetAngleX = 0.0f;  // Declared here for aim check later

        // Only track player if not in firing sequence (firing locks aim)
        if (!inst->state.turret.isFiring) {
            // Smoothly rotate cannon yaw toward target
            float angleDiffY = targetAngleY - inst->state.turret.cannonRotY;

            // Normalize angle difference to [-PI, PI]
            while (angleDiffY > 3.14159265f) angleDiffY -= 6.28318530f;
            while (angleDiffY < -3.14159265f) angleDiffY += 6.28318530f;

            // Rotate toward target at tracking speed
            float maxRot = inst->state.turret.trackingSpeed * deltaTime;
            if (angleDiffY > maxRot) {
                inst->state.turret.cannonRotY += maxRot;
            } else if (angleDiffY < -maxRot) {
                inst->state.turret.cannonRotY -= maxRot;
            } else {
                inst->state.turret.cannonRotY = targetAngleY;
            }

            // Normalize cannon yaw to [0, 2*PI]
            while (inst->state.turret.cannonRotY < 0) inst->state.turret.cannonRotY += 6.28318530f;
            while (inst->state.turret.cannonRotY >= 6.28318530f) inst->state.turret.cannonRotY -= 6.28318530f;
        }

        // Calculate target pitch angle (vertical rotation)
        // Target player center mass (+18 units above feet)
        // Note: pitch is negated to match cannon model's local rotation axis
        float cannonY = inst->posY + TURRET_CANNON_HEIGHT * inst->scaleY;
        float targetY = py + 18.0f;  // Aim at center mass
        float heightDiff = targetY - cannonY;
        targetAngleX = -atan2f(heightDiff, horizontalDist);  // Negated for correct pitch direction

        // Clamp pitch to reasonable limits (-60° to +45°)
        // Note: limits are swapped due to negation
        float maxPitch = 0.785f;   // ~45 degrees (visually down, for targets below)
        float minPitch = -1.047f;  // ~60 degrees (visually up, for targets above)
        if (targetAngleX > maxPitch) targetAngleX = maxPitch;
        if (targetAngleX < minPitch) targetAngleX = minPitch;

        // Only track pitch if not in firing sequence
        if (!inst->state.turret.isFiring) {
            // Smoothly rotate cannon pitch toward target
            float maxRot = inst->state.turret.trackingSpeed * deltaTime;
            float angleDiffX = targetAngleX - inst->state.turret.cannonRotX;
            if (angleDiffX > maxRot) {
                inst->state.turret.cannonRotX += maxRot;
            } else if (angleDiffX < -maxRot) {
                inst->state.turret.cannonRotX -= maxRot;
            } else {
                inst->state.turret.cannonRotX = targetAngleX;
            }
        }

        // Check line of sight to player (both map geometry AND decorations)
        float cannonCenterY = inst->posY + TURRET_CANNON_HEIGHT * inst->scaleY;
        bool hasLineOfSight = true;

        // Only check line of sight when we need it (during lock-on or about to fire)
        // Skip expensive raycast checks during cooldown or when already fired
        // Also skip if using LOD (too far for expensive checks)
        bool needsLOSCheck = !useLOD && ((inst->state.turret.lockOnTimer >= 0 && !inst->state.turret.lockOnComplete) ||
                             (inst->state.turret.lockOnComplete && !inst->state.turret.zapPlayed));

        if (needsLOSCheck) {
            // Check map geometry
            if (map->mapLoader) {
                hasLineOfSight = !maploader_raycast_blocked(map->mapLoader,
                    inst->posX, cannonCenterY, inst->posZ,
                    px, py + 18.0f, pz);  // Target player center mass
            }

            // Also check decoration collision meshes (platforms, walls, etc.)
            // Only if map geometry didn't block and we have a valid cached index
            if (hasLineOfSight && inst->state.turret.cachedIndex >= 0) {
                hasLineOfSight = !map_raycast_blocked_by_decorations(map,
                    inst->posX, cannonCenterY, inst->posZ,
                    px, py + 18.0f, pz, inst->state.turret.cachedIndex);
            }
        }

        // SNIPER LOCK-ON STATE MACHINE
        // Phase 1: Lock-on (aim at player for TURRET_LOCKON_TIME seconds)
        // Phase 2: Wait until aiming directly at player, then fire rail
        // Phase 3: Wait TURRET_HITSCAN_DELAY, then perform hitscan
        // Phase 4: Cooldown for TURRET_COOLDOWN_TIME before next lock-on

        // Calculate how close cannon is to aiming at player (for firing check)
        float aimDiffY = fabsf(targetAngleY - inst->state.turret.cannonRotY);
        // Normalize to [0, PI]
        while (aimDiffY > 3.14159265f) aimDiffY -= 6.28318530f;
        aimDiffY = fabsf(aimDiffY);
        float aimDiffX = fabsf(targetAngleX - inst->state.turret.cannonRotX);
        bool aimingAtPlayer = (aimDiffY < TURRET_AIM_THRESHOLD && aimDiffX < TURRET_AIM_THRESHOLD);

        // STATE: Waiting for hitscan after projectile spawned
        if (inst->state.turret.railFired) {
            inst->state.turret.shotDelay -= deltaTime;

            if (inst->state.turret.shotDelay <= 0.0f) {
                // HITSCAN! Check if ray in locked direction hits player
                inst->state.turret.hitPlayer = false;

                float dirX = inst->state.turret.lockedAimX;
                float dirY = inst->state.turret.lockedAimY;
                float dirZ = inst->state.turret.lockedAimZ;
                float cannonY = inst->posY + TURRET_CANNON_HEIGHT * inst->scaleY;

                float toPx = px - inst->posX;
                float toPy = (py + 18.0f) - cannonY;
                float toPz = pz - inst->posZ;
                float rayDot = toPx * dirX + toPy * dirY + toPz * dirZ;

                if (rayDot > 0) {
                    float closestX = inst->posX + dirX * rayDot;
                    float closestY = cannonY + dirY * rayDot;
                    float closestZ = inst->posZ + dirZ * rayDot;

                    float missX = px - closestX;
                    float missY = (py + 18.0f) - closestY;
                    float missZ = pz - closestZ;
                    float missDist = sqrtf(missX*missX + missY*missY + missZ*missZ);

                    float hitRadius = 12.0f;
                    if (missDist < hitRadius) {
                        bool wallBlocks = false;
                        if (map->mapLoader) {
                            wallBlocks = maploader_raycast_blocked(map->mapLoader,
                                inst->posX, cannonY, inst->posZ,
                                closestX, closestY, closestZ);
                        }
                        if (!wallBlocks) {
                            inst->state.turret.hitPlayer = true;
                        }
                    }
                }

                // Reset for cooldown
                inst->state.turret.railFired = false;
                inst->state.turret.isFiring = false;
                inst->state.turret.lockOnTimer = -TURRET_COOLDOWN_TIME;
                inst->state.turret.lockOnComplete = false;
                inst->state.turret.zapPlayed = false;
                inst->state.turret.shotDelay = 0.0f;
                inst->state.turret.fireTimer = 0.0f;
            }
        }
        // STATE: Firing sequence - waiting to spawn projectile or do hitscan
        else if (inst->state.turret.isFiring) {
            inst->state.turret.fireTimer += deltaTime;
            
            // Phase 1: After FIRE_WARNING_TIME, spawn projectile
            if (inst->state.turret.fireTimer >= TURRET_FIRE_WARNING_TIME && !inst->state.turret.railFired) {
                float dirX = inst->state.turret.lockedAimX;
                float dirY = inst->state.turret.lockedAimY;
                float dirZ = inst->state.turret.lockedAimZ;
                float cannonY = inst->posY + TURRET_CANNON_HEIGHT * inst->scaleY;

                // Spawn rail projectile
                int freeSlot = -1;
                for (int i = 0; i < TURRET_MAX_PROJECTILES; i++) {
                    if (inst->state.turret.projLife[i] <= 0.0f) {
                        freeSlot = i;
                        break;
                    }
                }

                if (freeSlot >= 0) {
                    inst->state.turret.projPosX[freeSlot] = inst->posX;
                    inst->state.turret.projPosY[freeSlot] = cannonY;
                    inst->state.turret.projPosZ[freeSlot] = inst->posZ;
                    inst->state.turret.projVelX[freeSlot] = dirX * TURRET_PROJECTILE_SPEED;
                    inst->state.turret.projVelY[freeSlot] = dirY * TURRET_PROJECTILE_SPEED;
                    inst->state.turret.projVelZ[freeSlot] = dirZ * TURRET_PROJECTILE_SPEED;
                    inst->state.turret.projRotX[freeSlot] = inst->state.turret.lockedCannonRotX;
                    inst->state.turret.projRotY[freeSlot] = inst->state.turret.lockedCannonRotY;
                    inst->state.turret.projLife[freeSlot] = TURRET_PROJECTILE_LIFETIME;
                    inst->state.turret.activeProjectiles++;

                    float muzzleOffset = 50.0f * inst->scaleX;
                    game_spawn_spark_particles(
                        inst->posX + dirX * muzzleOffset,
                        cannonY + dirY * muzzleOffset,
                        inst->posZ + dirZ * muzzleOffset,
                        5
                    );
                }

                // Start fire animation
                if (inst->hasOwnSkeleton && inst->animCount > 0) {
                    T3DAnim* fireAnim = &inst->anims[0];
                    t3d_anim_set_playing(fireAnim, true);
                    t3d_anim_set_time(fireAnim, 0.001f);  // Reset to first frame (not bind pose)
                }

                // Set up hitscan delay
                inst->state.turret.railFired = true;
                inst->state.turret.shotDelay = TURRET_HITSCAN_DELAY;
            }
        }
        // STATE: Locked on and aiming at player - start firing sequence
        else if (inst->state.turret.lockOnComplete && !inst->state.turret.zapPlayed) {
            if (hasLineOfSight && aimingAtPlayer) {
                // FIRE TELEGRAPH! Play zap sound and lock aim direction
                debugf("TURRET: Fire telegraph! Playing fire sound on channel %d\n", sfxChannel);
                wav64_play(&sfxTurretFire, sfxChannel);
                sfxChannel = SFX_CHANNEL_START + ((sfxChannel - SFX_CHANNEL_START + 1) % SFX_CHANNEL_COUNT);
                
                // Lock current aim direction
                float cosPitch = cosf(inst->state.turret.cannonRotX);
                float sinPitch = sinf(inst->state.turret.cannonRotX);
                float cosYaw = cosf(inst->state.turret.cannonRotY);
                float sinYaw = sinf(inst->state.turret.cannonRotY);
                inst->state.turret.lockedAimX = -sinYaw * cosPitch;
                inst->state.turret.lockedAimY = -sinPitch;
                inst->state.turret.lockedAimZ = cosYaw * cosPitch;
                inst->state.turret.lockedCannonRotX = inst->state.turret.cannonRotX;
                inst->state.turret.lockedCannonRotY = inst->state.turret.cannonRotY;
                
                // Start firing sequence (freezes tracking)
                inst->state.turret.zapPlayed = true;
                inst->state.turret.isFiring = true;
                inst->state.turret.fireTimer = 0.0f;
            }
        }
        // STATE: Still locking on
        else if (hasLineOfSight && !inst->state.turret.lockOnComplete) {
            inst->state.turret.lockOnTimer += deltaTime;

            if (inst->state.turret.lockOnTimer >= TURRET_LOCKON_TIME) {
                inst->state.turret.lockOnComplete = true;
            }
        }
        // STATE: No line of sight - decay lock-on or continue cooldown
        else if (!hasLineOfSight) {
            if (inst->state.turret.lockOnTimer > 0) {
                inst->state.turret.lockOnTimer -= deltaTime * 0.5f;
            } else if (inst->state.turret.lockOnTimer < 0) {
                inst->state.turret.lockOnTimer += deltaTime;
            }
            // Reset lock if lost LOS before firing
            if (inst->state.turret.lockOnComplete && !inst->state.turret.zapPlayed) {
                inst->state.turret.lockOnComplete = false;
            }
        }
        // STATE: Cooldown (lockOnTimer < 0)
        else if (inst->state.turret.lockOnTimer < 0) {
            inst->state.turret.lockOnTimer += deltaTime;
        }
    } else {
        // Player out of range - but still complete the shot if in firing sequence
        if (inst->state.turret.isFiring || inst->state.turret.railFired) {
            // Continue firing sequence or hitscan even out of range
            if (inst->state.turret.railFired) {
                inst->state.turret.shotDelay -= deltaTime;
                if (inst->state.turret.shotDelay <= 0.0f) {
                    // Hitscan (will miss since player out of range, but complete the cycle)
                    inst->state.turret.railFired = false;
                    inst->state.turret.isFiring = false;
                    inst->state.turret.lockOnTimer = -TURRET_COOLDOWN_TIME;
                    inst->state.turret.lockOnComplete = false;
                    inst->state.turret.zapPlayed = false;
                    inst->state.turret.shotDelay = 0.0f;
                    inst->state.turret.fireTimer = 0.0f;
                }
            } else {
                // Still in fire warning phase - continue timer
                inst->state.turret.fireTimer += deltaTime;
            }
        } else {
            // Reset lock-on state
            if (inst->state.turret.lockOnTimer > 0) {
                inst->state.turret.lockOnTimer -= deltaTime;
            } else if (inst->state.turret.lockOnTimer < 0) {
                inst->state.turret.lockOnTimer += deltaTime;
            }
            inst->state.turret.lockOnComplete = false;
            inst->state.turret.zapPlayed = false;
            inst->state.turret.shotDelay = 0.0f;
        }
    }

    // Update active projectiles (visual only - no damage, just ground collision)
    // OPTIMIZATION: Stagger collision checks across frames for fast projectiles
    for (int i = 0; i < TURRET_MAX_PROJECTILES; i++) {
        if (inst->state.turret.projLife[i] <= 0.0f) continue;

        // Apply gravity to vertical velocity (minimal for sniper projectile)
        inst->state.turret.projVelY[i] -= TURRET_PROJECTILE_GRAVITY;

        // Update position (always)
        inst->state.turret.projPosX[i] += inst->state.turret.projVelX[i] * deltaTime;
        inst->state.turret.projPosY[i] += inst->state.turret.projVelY[i] * deltaTime;
        inst->state.turret.projPosZ[i] += inst->state.turret.projVelZ[i] * deltaTime;
        
        // Spawn vapor trail particles (1-2 per frame for visual effect)
        // Only spawn if close enough to player (performance)
        if (distSq < 250000.0f) {  // Within ~500 units
            game_spawn_trail_particles(
                inst->state.turret.projPosX[i],
                inst->state.turret.projPosY[i],
                inst->state.turret.projPosZ[i],
                1 + (rand() % 2)  // 1-2 particles per frame
            );
        }

        // Decrease lifetime
        inst->state.turret.projLife[i] -= deltaTime;

        // Check if lifetime expired (early exit - skip expensive collision checks)
        if (inst->state.turret.projLife[i] <= 0.0f) {
            inst->state.turret.activeProjectiles--;
            continue;
        }

        // OPTIMIZATION: Staggered collision checks (every Nth frame per projectile)
        // Fast projectiles can skip frames - position updates still happen
        if ((map->frameCounter + i) % PROJECTILE_CHECK_INTERVAL != 0) {
            continue;
        }

        // Check for ground collision only (simplified for fast projectiles)
        if (map->mapLoader) {
            float groundY = maploader_get_ground_height(map->mapLoader,
                inst->state.turret.projPosX[i],
                inst->state.turret.projPosY[i] + 50.0f,
                inst->state.turret.projPosZ[i]);

            if (groundY > -9000.0f && inst->state.turret.projPosY[i] <= groundY) {
                // Hit ground - spawn impact particles and deactivate
                game_spawn_spark_particles(
                    inst->state.turret.projPosX[i],
                    groundY + 2.0f,
                    inst->state.turret.projPosZ[i],
                    5
                );
                inst->state.turret.projLife[i] = 0.0f;
                inst->state.turret.activeProjectiles--;
            }
        }
    }
}

static void turret_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    // Touching the turret base doesn't do anything special
    // Projectile collision is checked separately in map_check_deco_collisions
}

// Helper function to check turret hitscan damage to player
// (Sniper turret uses hitscan - projectiles are visual only)
static inline bool turret_check_projectile_collision(DecoInstance* inst, float px, float py, float pz, float playerRadius, float playerHeight) {
    (void)px; (void)py; (void)pz; (void)playerRadius; (void)playerHeight;
    if (inst->type != DECO_TURRET) return false;

    // Check if turret hitscan hit the player this frame
    if (inst->state.turret.hitPlayer) {
        inst->state.turret.hitPlayer = false;  // Clear the flag
        return true;  // Player was hit by hitscan
    }
    return false;
}

// --- PULSE TURRET BEHAVIOR ---
// Fires slow homing projectiles that damage player on hit
#define PULSE_TURRET_DETECTION_RANGE 400.0f   // Range at which turret activates
#define PULSE_TURRET_TRACKING_SPEED 1.5f      // Radians per second for cannon rotation
#define PULSE_TURRET_FIRE_COOLDOWN 2.5f       // Seconds between shots
#define PULSE_TURRET_PROJECTILE_SPEED 80.0f   // Units per second (slow!)
#define PULSE_TURRET_PROJECTILE_HOMING 0.8f   // How much projectile steers toward player (radians/sec)
#define PULSE_TURRET_PROJECTILE_LIFETIME 5.0f // How long projectiles live
#define PULSE_TURRET_PROJECTILE_RADIUS 15.0f  // Collision radius for hitting player
#define PULSE_TURRET_CANNON_HEIGHT 20.0f      // Height offset for cannon pivot point
#define PULSE_TURRET_MAX_PROJECTILES 4        // Max active projectiles per turret
#define PULSE_TURRET_TARGET_LAG_SPEED 1.5f    // How fast lagged target catches up to player (lower = more lag)

static void turret_pulse_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.pulseTurret.cannonRotY = inst->rotY;  // Start facing same direction as base
    inst->state.pulseTurret.cannonRotX = 0.0f;        // Start level (no pitch)
    inst->state.pulseTurret.trackingSpeed = PULSE_TURRET_TRACKING_SPEED;
    inst->state.pulseTurret.isActive = true;
    inst->state.pulseTurret.health = 1;              // 1 hit to destroy
    inst->state.pulseTurret.isDead = false;
    inst->state.pulseTurret.animInitialized = false;  // Will be set on first update
    inst->state.pulseTurret.fireCooldown = 0.5f;  // Short initial delay
    inst->state.pulseTurret.activeProjectiles = 0;

    // Initialize projectile pool
    for (int i = 0; i < PULSE_TURRET_MAX_PROJECTILES; i++) {
        inst->state.pulseTurret.projLife[i] = 0.0f;  // All projectiles inactive
    }

    // Set up skeleton for animation (pulse turret cannon is primary model with animations)
    DecoTypeRuntime* pulseTurretType = map_get_deco_type(map, DECO_TURRET_PULSE);
    if (pulseTurretType && pulseTurretType->model && !inst->hasOwnSkeleton) {
        inst->skeleton = t3d_skeleton_create(pulseTurretType->model);
        inst->hasOwnSkeleton = true;
        inst->animCount = 0;
        
        // Load turret_fire animation
        uint32_t animCount = t3d_model_get_animation_count(pulseTurretType->model);
        if (animCount > 0) {
            // Look for turret_fire animation by name
            for (uint32_t i = 0; i < animCount && inst->animCount < MAX_DECO_ANIMS; i++) {
                T3DAnim anim = t3d_anim_create(pulseTurretType->model, "turret_fire");
                if (anim.animRef) {
                    inst->anims[inst->animCount] = anim;
                    t3d_anim_attach(&inst->anims[inst->animCount], &inst->skeleton);
                    t3d_anim_set_looping(&inst->anims[inst->animCount], false);
                    t3d_anim_set_playing(&inst->anims[inst->animCount], false);
                    // Set to first actual frame (0.001s past bind pose at frame 0)
                    t3d_anim_set_time(&inst->anims[inst->animCount], 0.001f);
                    // Update animation to calculate bone transforms, then update skeleton
                    t3d_anim_update(&inst->anims[inst->animCount], 0.0f);
                    t3d_skeleton_update(&inst->skeleton);
                    inst->animCount++;
                    debugf("PULSE TURRET: Loaded turret_fire animation\n");
                    break;  // Only need one animation
                }
            }
        }
    }
}

static void turret_pulse_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Skip if dead
    if (inst->state.pulseTurret.isDead) return;

    // First-frame animation initialization (fixes spawn in default pose)
    // IMPORTANT: Must happen before distance culling so it runs even when spawned far away
    if (!inst->state.pulseTurret.animInitialized && inst->hasOwnSkeleton && inst->animCount > 0) {
        T3DAnim* fireAnim = &inst->anims[0];
        t3d_anim_set_playing(fireAnim, true);     // Start playing
        t3d_anim_set_time(fireAnim, 0.001f);      // Set to first frame
        t3d_anim_update(fireAnim, 0.0f);          // Calculate transforms
        t3d_skeleton_update(&inst->skeleton);     // Apply to skeleton
        t3d_anim_set_playing(fireAnim, false);    // Pause on first frame
        inst->state.pulseTurret.animInitialized = true;
    }

    // Check activation system
    bool isActive = (inst->activationId == 0) || activation_get(inst->activationId);
    inst->state.pulseTurret.isActive = isActive;

    // Get player position
    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    // OPTIMIZATION: Calculate distance to player (horizontal)
    float dx = px - inst->posX;
    float dz = pz - inst->posZ;
    float distSq = dx * dx + dz * dz;
    
    // Far distance culling - completely skip update if too far
    if (distSq > COLLISION_FAR_RANGE_SQ) {
        return;
    }
    
    // Distance-based LOD flag
    bool useLOD = (distSq > COLLISION_UPDATE_RANGE_SQ);
    
    float horizontalDist = sqrtf(distSq);

    // Update fire animation (standard system handles playback)
    if (inst->hasOwnSkeleton && inst->animCount > 0) {
        T3DAnim* fireAnim = &inst->anims[0];  // turret_fire is first animation
        if (fireAnim->isPlaying) {
            t3d_anim_update(fireAnim, deltaTime);
            t3d_skeleton_update(&inst->skeleton);  // Update skeleton after animation
            // Check if animation finished
            if (fireAnim->time >= fireAnim->animRef->duration) {
                t3d_anim_set_playing(fireAnim, false);
                t3d_anim_set_time(fireAnim, 0.001f);  // Reset to first frame (not bind pose)
                t3d_anim_update(fireAnim, 0.0f);      // Force update to first frame
                t3d_skeleton_update(&inst->skeleton);  // Apply first frame pose
            }
        }
    }

    // Cannon tracking and firing (only if active and player in range)
    bool playerInRange = isActive && distSq < PULSE_TURRET_DETECTION_RANGE * PULSE_TURRET_DETECTION_RANGE;

    if (playerInRange) {
        // Calculate target yaw angle (horizontal rotation)
        float targetAngleY = atan2f(-dx, dz);

        // Smoothly rotate cannon yaw toward target
        float angleDiffY = targetAngleY - inst->state.pulseTurret.cannonRotY;

        // Normalize angle difference to [-PI, PI]
        while (angleDiffY > 3.14159265f) angleDiffY -= 6.28318530f;
        while (angleDiffY < -3.14159265f) angleDiffY += 6.28318530f;

        // Rotate toward target at tracking speed
        float maxRot = inst->state.pulseTurret.trackingSpeed * deltaTime;
        if (angleDiffY > maxRot) {
            inst->state.pulseTurret.cannonRotY += maxRot;
        } else if (angleDiffY < -maxRot) {
            inst->state.pulseTurret.cannonRotY -= maxRot;
        } else {
            inst->state.pulseTurret.cannonRotY = targetAngleY;
        }

        // Normalize cannon yaw to [0, 2*PI]
        while (inst->state.pulseTurret.cannonRotY < 0) inst->state.pulseTurret.cannonRotY += 6.28318530f;
        while (inst->state.pulseTurret.cannonRotY >= 6.28318530f) inst->state.pulseTurret.cannonRotY -= 6.28318530f;

        // Calculate target pitch angle (vertical rotation)
        float cannonY = inst->posY + PULSE_TURRET_CANNON_HEIGHT * inst->scaleY;
        float targetY = py + 18.0f;  // Aim at center mass
        float heightDiff = targetY - cannonY;
        float targetAngleX = -atan2f(heightDiff, horizontalDist);

        // Clamp pitch to reasonable limits (-60° to +45°)
        float maxPitch = 0.785f;   // ~45 degrees
        float minPitch = -1.047f;  // ~60 degrees
        if (targetAngleX > maxPitch) targetAngleX = maxPitch;
        if (targetAngleX < minPitch) targetAngleX = minPitch;

        // Smoothly rotate cannon pitch toward target
        float angleDiffX = targetAngleX - inst->state.pulseTurret.cannonRotX;
        if (angleDiffX > maxRot) {
            inst->state.pulseTurret.cannonRotX += maxRot;
        } else if (angleDiffX < -maxRot) {
            inst->state.pulseTurret.cannonRotX -= maxRot;
        } else {
            inst->state.pulseTurret.cannonRotX = targetAngleX;
        }

        // Check line of sight to player (only when about to fire)
        float cannonCenterY = inst->posY + PULSE_TURRET_CANNON_HEIGHT * inst->scaleY;
        bool hasLineOfSight = true;

        // OPTIMIZATION: Only check LOS when cooldown is nearly ready AND not using LOD
        // Skip expensive raycast during most of the cooldown period or when too far
        bool needsLOSCheck = !useLOD && (inst->state.pulseTurret.fireCooldown <= 0.2f);
        
        if (needsLOSCheck && map->mapLoader) {
            hasLineOfSight = !maploader_raycast_blocked(map->mapLoader,
                inst->posX, cannonCenterY, inst->posZ,
                px, py + 18.0f, pz);
        }

        // Countdown to fire
        if (inst->state.pulseTurret.fireCooldown > 0.0f) {
            inst->state.pulseTurret.fireCooldown -= deltaTime;
        }

        // Fire if cooldown ready and has line of sight
        if (inst->state.pulseTurret.fireCooldown <= 0.0f && hasLineOfSight) {
            // Find free slot
            int freeSlot = -1;
            for (int i = 0; i < PULSE_TURRET_MAX_PROJECTILES; i++) {
                if (inst->state.pulseTurret.projLife[i] <= 0.0f) {
                    freeSlot = i;
                    break;
                }
            }

            if (freeSlot >= 0) {
                // Spawn projectile at cannon center
                float projCannonY = inst->posY + PULSE_TURRET_CANNON_HEIGHT * inst->scaleY;
                inst->state.pulseTurret.projPosX[freeSlot] = inst->posX;
                inst->state.pulseTurret.projPosY[freeSlot] = projCannonY;
                inst->state.pulseTurret.projPosZ[freeSlot] = inst->posZ;

                // Calculate 3D direction from cannon pitch and yaw
                float cosPitch = cosf(inst->state.pulseTurret.cannonRotX);
                float sinPitch = sinf(inst->state.pulseTurret.cannonRotX);
                float cosYaw = cosf(inst->state.pulseTurret.cannonRotY);
                float sinYaw = sinf(inst->state.pulseTurret.cannonRotY);

                // Direction vector accounting for both yaw and pitch
                float dirX = -sinYaw * cosPitch;
                float dirY = -sinPitch;
                float dirZ = cosYaw * cosPitch;

                inst->state.pulseTurret.projVelX[freeSlot] = dirX * PULSE_TURRET_PROJECTILE_SPEED;
                inst->state.pulseTurret.projVelY[freeSlot] = dirY * PULSE_TURRET_PROJECTILE_SPEED;
                inst->state.pulseTurret.projVelZ[freeSlot] = dirZ * PULSE_TURRET_PROJECTILE_SPEED;

                // Initialize lagged target to player's current position
                // Projectile will chase this target, which slowly catches up to player
                inst->state.pulseTurret.projTargetX[freeSlot] = px;
                inst->state.pulseTurret.projTargetY[freeSlot] = py + 18.0f;
                inst->state.pulseTurret.projTargetZ[freeSlot] = pz;

                inst->state.pulseTurret.projLife[freeSlot] = PULSE_TURRET_PROJECTILE_LIFETIME;
                inst->state.pulseTurret.activeProjectiles++;

                // Spawn spark particles at muzzle
                float muzzleOffset = 40.0f * inst->scaleX;
                game_spawn_spark_particles(
                    inst->posX + dirX * muzzleOffset,
                    projCannonY + dirY * muzzleOffset,
                    inst->posZ + dirZ * muzzleOffset,
                    5
                );

                // Play sound
                static int sfxChannel = SFX_CHANNEL_START;
                wav64_play(&sfxTurretZap, sfxChannel);
                sfxChannel = SFX_CHANNEL_START + ((sfxChannel - SFX_CHANNEL_START + 1) % SFX_CHANNEL_COUNT);

                // Start fire animation
                if (inst->hasOwnSkeleton && inst->animCount > 0) {
                    T3DAnim* fireAnim = &inst->anims[0];
                    t3d_anim_set_playing(fireAnim, true);
                    t3d_anim_set_time(fireAnim, 0.001f);  // Reset to first frame (not bind pose)
                }

                // Reset cooldown
                inst->state.pulseTurret.fireCooldown = PULSE_TURRET_FIRE_COOLDOWN;
            }
        }
    }

    // Update active projectiles with homing behavior
    for (int i = 0; i < PULSE_TURRET_MAX_PROJECTILES; i++) {
        if (inst->state.pulseTurret.projLife[i] <= 0.0f) continue;

        // Store previous position for collision
        float prevX = inst->state.pulseTurret.projPosX[i];
        float prevY = inst->state.pulseTurret.projPosY[i];
        float prevZ = inst->state.pulseTurret.projPosZ[i];

        // Update lagged target - slowly catch up to player's current position
        // This makes projectiles chase where the player WAS, not where they ARE
        float lagAmount = PULSE_TURRET_TARGET_LAG_SPEED * deltaTime;
        inst->state.pulseTurret.projTargetX[i] += (px - inst->state.pulseTurret.projTargetX[i]) * lagAmount;
        inst->state.pulseTurret.projTargetY[i] += ((py + 18.0f) - inst->state.pulseTurret.projTargetY[i]) * lagAmount;
        inst->state.pulseTurret.projTargetZ[i] += (pz - inst->state.pulseTurret.projTargetZ[i]) * lagAmount;

        // OPTIMIZED: Simplified homing - blend velocity toward target without full normalization
        float toTargetX = inst->state.pulseTurret.projTargetX[i] - inst->state.pulseTurret.projPosX[i];
        float toTargetY = inst->state.pulseTurret.projTargetY[i] - inst->state.pulseTurret.projPosY[i];
        float toTargetZ = inst->state.pulseTurret.projTargetZ[i] - inst->state.pulseTurret.projPosZ[i];
        
        // Simple steering - blend velocity toward target direction
        float homingAmount = PULSE_TURRET_PROJECTILE_HOMING * deltaTime * 0.5f;  // Reduced for stability
        inst->state.pulseTurret.projVelX[i] += toTargetX * homingAmount;
        inst->state.pulseTurret.projVelY[i] += toTargetY * homingAmount;
        inst->state.pulseTurret.projVelZ[i] += toTargetZ * homingAmount;
        
        // Maintain constant speed (single sqrt instead of multiple)
        float velX = inst->state.pulseTurret.projVelX[i];
        float velY = inst->state.pulseTurret.projVelY[i];
        float velZ = inst->state.pulseTurret.projVelZ[i];
        float velMagSq = velX*velX + velY*velY + velZ*velZ;
        
        if (velMagSq > 0.01f) {
            float velMag = sqrtf(velMagSq);
            float speedScale = PULSE_TURRET_PROJECTILE_SPEED / velMag;
            inst->state.pulseTurret.projVelX[i] = velX * speedScale;
            inst->state.pulseTurret.projVelY[i] = velY * speedScale;
            inst->state.pulseTurret.projVelZ[i] = velZ * speedScale;
        }

        // Update position
        inst->state.pulseTurret.projPosX[i] += inst->state.pulseTurret.projVelX[i] * deltaTime;
        inst->state.pulseTurret.projPosY[i] += inst->state.pulseTurret.projVelY[i] * deltaTime;
        inst->state.pulseTurret.projPosZ[i] += inst->state.pulseTurret.projVelZ[i] * deltaTime;

        // Decrease lifetime
        inst->state.pulseTurret.projLife[i] -= deltaTime;
        
        // Check if lifetime expired (early exit - skip collision checks)
        if (inst->state.pulseTurret.projLife[i] <= 0.0f) {
            inst->state.pulseTurret.activeProjectiles--;
            continue;
        }

        // OPTIMIZATION: Staggered collision checks for slow projectiles
        // Still check every frame, but alternate between wall and ground
        bool checkWalls = ((map->frameCounter + i) % 2 == 0);
        
        if (checkWalls) {
            // Check for wall collision (important for slow projectiles - player can take cover)
            if (map->mapLoader) {
                if (maploader_raycast_blocked(map->mapLoader,
                    prevX, prevY, prevZ,
                inst->state.pulseTurret.projPosX[i],
                inst->state.pulseTurret.projPosY[i],
                inst->state.pulseTurret.projPosZ[i])) {
                // Hit wall - spawn impact particles and deactivate
                game_spawn_spark_particles(prevX, prevY, prevZ, 8);
                inst->state.pulseTurret.projLife[i] = 0.0f;
                inst->state.pulseTurret.activeProjectiles--;
                continue;
            }
        }
        } else {
            // Check for ground collision (on alternate frames)
            if (map->mapLoader) {
                float groundY = maploader_get_ground_height(map->mapLoader,
                    inst->state.pulseTurret.projPosX[i],
                    inst->state.pulseTurret.projPosY[i] + 50.0f,
                    inst->state.pulseTurret.projPosZ[i]);

                if (groundY > -9000.0f && inst->state.pulseTurret.projPosY[i] <= groundY) {
                    // Hit ground - spawn impact particles and deactivate
                    game_spawn_spark_particles(
                        inst->state.pulseTurret.projPosX[i],
                        groundY + 2.0f,
                        inst->state.pulseTurret.projPosZ[i],
                        8
                    );
                    inst->state.pulseTurret.projLife[i] = 0.0f;
                    inst->state.pulseTurret.activeProjectiles--;
                }
            }
        }
    }
}

static void turret_pulse_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    // Touching the turret base doesn't do anything special
    // Projectile collision is checked separately via turret_pulse_check_projectile_collision
}

// Helper function to check pulse turret projectile collision with player
// Returns true if player was hit by a projectile (and deactivates that projectile)
static inline bool turret_pulse_check_projectile_collision(DecoInstance* inst, float px, float py, float pz, float playerRadius, float playerHeight) {
    if (inst->type != DECO_TURRET_PULSE) return false;

    for (int i = 0; i < PULSE_TURRET_MAX_PROJECTILES; i++) {
        if (inst->state.pulseTurret.projLife[i] <= 0.0f) continue;

        // Check sphere-cylinder collision (simplified)
        float projX = inst->state.pulseTurret.projPosX[i];
        float projY = inst->state.pulseTurret.projPosY[i];
        float projZ = inst->state.pulseTurret.projPosZ[i];

        // Horizontal distance
        float dx = projX - px;
        float dz = projZ - pz;
        float horizDistSq = dx*dx + dz*dz;
        float combinedRadius = playerRadius + PULSE_TURRET_PROJECTILE_RADIUS;

        if (horizDistSq > combinedRadius * combinedRadius) continue;

        // Vertical check
        float playerBottom = py;
        float playerTop = py + playerHeight;
        float projBottom = projY - PULSE_TURRET_PROJECTILE_RADIUS;
        float projTop = projY + PULSE_TURRET_PROJECTILE_RADIUS;

        if (projBottom > playerTop || projTop < playerBottom) continue;

        // Hit! Deactivate projectile and spawn particles
        game_spawn_spark_particles(projX, projY, projZ, 10);
        inst->state.pulseTurret.projLife[i] = 0.0f;
        inst->state.pulseTurret.activeProjectiles--;

        return true;  // Player was hit
    }

    return false;
}

// --- SECURITY DROID BEHAVIOR ---
// Constants for droid behavior
#define DROID_MAX_HEALTH 3                 // HP before death
#define DROID_COMBAT_RANGE 300.0f          // Distance to enter combat mode
#define DROID_BLOCK_RANGE 100.0f           // Distance to use block stance in combat
#define DROID_SHOOT_COOLDOWN 1.5f          // Seconds between attacks (normal)
#define DROID_CLIFF_SHOOT_COOLDOWN 0.75f   // Halved cooldown when cliff-blocked
#define DROID_SHOOT_ANIM_DURATION 1.25f    // Duration of sec_shoot animation
#define DROID_SHOOT_PROJECTILE_TIME 0.625f // Fire projectile at animation midpoint
#define DROID_BULLET_SPEED 100.0f          // Projectile travel speed
#define DROID_BULLET_LIFETIME 3.0f         // Seconds before projectile despawns
#define DROID_BULLET_DAMAGE 1              // Damage dealt by projectile
#define DROID_BULLET_RADIUS 12.0f          // Collision radius for hitting player
#define DROID_MAX_PROJECTILES 4            // Max active projectiles per droid
#define DROID_WALK_SPEED 30.0f             // Movement speed (units/sec)
#define DROID_TURN_SPEED 2.0f              // Turn rate (radians/sec)
#define DROID_IDLE_PAUSE_MIN 1.0f          // Minimum idle pause duration
#define DROID_IDLE_PAUSE_MAX 2.0f          // Maximum idle pause duration
#define DROID_CLIFF_CHECK_DIST 25.0f       // Forward distance to check for cliffs

// Animation indices (sec_idle=0, sec_walk=1, sec_block=2, sec_shoot=3 from GLB)
#define DROID_ANIM_IDLE 0     // sec_idle (1.208s)
#define DROID_ANIM_WALK 1     // sec_walk (0.833s)
#define DROID_ANIM_BLOCK 2    // sec_block (0.375s, single frame hold)
#define DROID_ANIM_SHOOT 3    // sec_shoot (1.250s, vulnerable, fire at 0.625s)

static void droid_sec_init(DecoInstance* inst, MapRuntime* map) {
    inst->state.droid.health = DROID_MAX_HEALTH;
    inst->state.droid.isAggro = false;
    inst->state.droid.isDead = false;
    inst->state.droid.shootCooldown = 0.0f;
    inst->state.droid.shootAnimTimer = 0.0f;
    inst->state.droid.hasFiredProjectile = false;
    inst->state.droid.deathTimer = 0.0f;
    inst->state.droid.currentAnim = DROID_ANIM_IDLE;
    // Physics
    inst->state.droid.velY = 0.0f;
    inst->state.droid.isGrounded = false;
    // Store spawn position for respawn
    inst->state.droid.spawnX = inst->posX;
    inst->state.droid.spawnY = inst->posY;
    inst->state.droid.spawnZ = inst->posZ;
    inst->state.droid.spawnRotY = inst->rotY;
    // Patrol state
    inst->state.droid.moveDir = inst->rotY;  // Start facing initial direction
    inst->state.droid.walkSpeed = DROID_WALK_SPEED;
    inst->state.droid.idlePauseTimer = ((float)rand() / RAND_MAX) * (DROID_IDLE_PAUSE_MAX - DROID_IDLE_PAUSE_MIN) + DROID_IDLE_PAUSE_MIN;
    inst->state.droid.isIdling = true;  // Start with an idle pause
    inst->state.droid.cliffAhead = false;
    inst->state.droid.cliffShootCooldown = DROID_CLIFF_SHOOT_COOLDOWN;
    // Projectile pool
    inst->state.droid.activeProjectiles = 0;
    for (int i = 0; i < DROID_MAX_PROJECTILES; i++) {
        inst->state.droid.projLife[i] = 0.0f;  // All projectiles inactive
    }

    // Skip per-instance skeleton in demo/replay mode
    extern bool g_demoMode;
    extern bool g_replayMode;
    if (g_demoMode || g_replayMode) {
        inst->hasOwnSkeleton = false;
        inst->animCount = 0;
        memset(&inst->skeleton, 0, sizeof(T3DSkeleton));
        return;
    }

    // Create per-instance skeleton for independent animations
    DecoTypeRuntime* droidType = map_get_deco_type(map, DECO_DROID_SEC);
    if (droidType && droidType->model && droidType->hasSkeleton) {
        inst->skeleton = t3d_skeleton_create(droidType->model);
        inst->hasOwnSkeleton = true;
        inst->animCount = 0;

        // Load animations for this instance
        uint32_t animCount = t3d_model_get_animation_count(droidType->model);
        if (animCount > 0) {
            T3DChunkAnim* animChunks[MAX_DECO_ANIMS];
            uint32_t toLoad = animCount < MAX_DECO_ANIMS ? animCount : MAX_DECO_ANIMS;
            t3d_model_get_animations(droidType->model, animChunks);

            for (uint32_t i = 0; i < toLoad; i++) {
                T3DAnim anim = t3d_anim_create(droidType->model, animChunks[i]->name);
                t3d_anim_attach(&anim, &inst->skeleton);
                t3d_anim_set_looping(&anim, true);
                t3d_anim_set_playing(&anim, false);  // Start paused, will be activated on demand
                inst->anims[inst->animCount++] = anim;
            }
        }
    }
}

static void droid_sec_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // ==== UPDATE PROJECTILES FIRST (always runs regardless of droid state) ====
    for (int i = 0; i < DROID_MAX_PROJECTILES; i++) {
        if (inst->state.droid.projLife[i] > 0) {
            // Update position
            inst->state.droid.projPosX[i] += inst->state.droid.projVelX[i] * deltaTime;
            inst->state.droid.projPosY[i] += inst->state.droid.projVelY[i] * deltaTime;
            inst->state.droid.projPosZ[i] += inst->state.droid.projVelZ[i] * deltaTime;

            // Update lifetime
            inst->state.droid.projLife[i] -= deltaTime;
            if (inst->state.droid.projLife[i] <= 0) {
                inst->state.droid.activeProjectiles--;
                continue;
            }

            // Check collision with player
            float pdx = map->playerX - inst->state.droid.projPosX[i];
            float pdy = (map->playerY + 10.0f) - inst->state.droid.projPosY[i];  // Player center
            float pdz = map->playerZ - inst->state.droid.projPosZ[i];
            float distSq = pdx*pdx + pdy*pdy + pdz*pdz;

            if (distSq < DROID_BULLET_RADIUS * DROID_BULLET_RADIUS) {
                // Hit player!
                extern void player_take_damage(int damage);
                extern bool player_is_dead(void);

                if (!player_is_dead()) {
                    player_take_damage(DROID_BULLET_DAMAGE);
                    DECO_DEBUG("Droid projectile hit player!\n");
                }
                inst->state.droid.projLife[i] = 0.0f;
                inst->state.droid.activeProjectiles--;
            }
        }
    }

    // Handle death state
    if (inst->state.droid.isDead) {
        inst->state.droid.deathTimer += deltaTime;
        inst->state.droid.currentAnim = DROID_ANIM_IDLE;  // No death anim, just fade out

        // After 1.5s, deactivate and trigger activation
        if (inst->state.droid.deathTimer > 1.5f) {
            inst->active = false;

            // Activate linked objects (droid death = permanent activation, like killing a guard)
            if (inst->activationId > 0) {
                activation_set(inst->activationId, true);
                DECO_DEBUG("Droid died! Set activationId=%d to true\n", inst->activationId);
            }
        }
        goto update_animation;
    }

    // ==== GRAVITY AND GROUND COLLISION ====
    inst->state.droid.velY -= map->gravity;
    inst->posY += inst->state.droid.velY;

    if (map->mapLoader) {
        // Get ground height from map
        float groundY = maploader_get_ground_height(map->mapLoader, inst->posX, inst->posY + 50.0f, inst->posZ);

        // Also check decoration collision (platforms, etc.)
        int myIndex = (int)(inst - map->decorations);
        float decoGroundY = map_get_deco_ground_height_ex(map, inst->posX, inst->posY, inst->posZ, myIndex);
        if (decoGroundY > groundY) {
            groundY = decoGroundY;
        }

        // Snap to ground if falling and reached ground
        if (groundY > INVALID_GROUND_Y && inst->state.droid.velY <= 0 && inst->posY <= groundY + 1.0f) {
            inst->posY = groundY;
            inst->state.droid.velY = 0.0f;
            inst->state.droid.isGrounded = true;
        } else {
            inst->state.droid.isGrounded = false;
        }

        // Respawn at spawn position if fallen off map
        if (inst->posY < -500.0f) {
            inst->posX = inst->state.droid.spawnX;
            inst->posY = inst->state.droid.spawnY;
            inst->posZ = inst->state.droid.spawnZ;
            inst->rotY = inst->state.droid.spawnRotY;
            inst->state.droid.velY = 0.0f;
            inst->state.droid.isGrounded = false;
            inst->state.droid.moveDir = inst->state.droid.spawnRotY;
            inst->state.droid.isAggro = false;
            inst->state.droid.isIdling = true;
            inst->state.droid.idlePauseTimer = 1.0f;  // Brief pause after respawn
            DECO_DEBUG("Security droid respawned at (%.1f, %.1f, %.1f)\n", inst->posX, inst->posY, inst->posZ);
            return;
        }
    }

    // Calculate distance and direction to player
    float dx = map->playerX - inst->posX;
    float dz = map->playerZ - inst->posZ;
    float distToPlayer = sqrtf(dx * dx + dz * dz);

    // Update cooldowns
    if (inst->state.droid.shootCooldown > 0) {
        inst->state.droid.shootCooldown -= deltaTime;
    }

    // Combat range check
    bool inCombatRange = (distToPlayer < DROID_COMBAT_RANGE);
    bool inBlockRange = (distToPlayer < DROID_BLOCK_RANGE);
    inst->state.droid.isAggro = inCombatRange;

    // Cliff detection - check in direction toward player (combat) or moveDir (patrol)
    inst->state.droid.cliffAhead = false;
    float checkDir = inCombatRange ? atan2f(-dx, dz) : inst->state.droid.moveDir;
    float checkX = inst->posX - sinf(checkDir) * DROID_CLIFF_CHECK_DIST;
    float checkZ = inst->posZ + cosf(checkDir) * DROID_CLIFF_CHECK_DIST;
    float cliffY = maploader_get_ground_height(map->mapLoader, checkX, inst->posY + 50.0f, checkZ);
    if (cliffY < inst->posY - 50.0f || cliffY == INVALID_GROUND_Y) {
        inst->state.droid.cliffAhead = true;
    }

    // ==== SHOOTING ANIMATION STATE ====
    if (inst->state.droid.shootAnimTimer > 0) {
        inst->state.droid.shootAnimTimer -= deltaTime;
        inst->state.droid.currentAnim = DROID_ANIM_SHOOT;

        // Fire projectile at midpoint if not already fired
        if (!inst->state.droid.hasFiredProjectile && 
            inst->state.droid.shootAnimTimer <= (DROID_SHOOT_ANIM_DURATION - DROID_SHOOT_PROJECTILE_TIME)) {
            
            inst->state.droid.hasFiredProjectile = true;

            // Find free projectile slot
            int freeSlot = -1;
            for (int i = 0; i < DROID_MAX_PROJECTILES; i++) {
                if (inst->state.droid.projLife[i] <= 0) {
                    freeSlot = i;
                    break;
                }
            }

            if (freeSlot != -1) {
                // Calculate facing and right vectors
                float facingX = -sinf(inst->rotY);  // Inverted X
                float facingZ = cosf(inst->rotY);
                float rightX = cosf(inst->rotY);    // Right is perpendicular to facing
                float rightZ = sinf(inst->rotY);
                
                // Start projectile at droid's right arm position
                // Offset forward, to the right (negated for proper side), and at shoulder height
                inst->state.droid.projPosX[freeSlot] = inst->posX + facingX * 15.0f - rightX * 8.0f;
                inst->state.droid.projPosY[freeSlot] = inst->posY + 20.0f;  // Shoulder height
                inst->state.droid.projPosZ[freeSlot] = inst->posZ + facingZ * 15.0f - rightZ * 8.0f;

                // Calculate velocity toward player
                float bdx = map->playerX - inst->state.droid.projPosX[freeSlot];
                float bdy = (map->playerY + 10.0f) - inst->state.droid.projPosY[freeSlot];  // Aim at player center
                float bdz = map->playerZ - inst->state.droid.projPosZ[freeSlot];
                float bdist = sqrtf(bdx*bdx + bdy*bdy + bdz*bdz);
                if (bdist > 0.01f) {
                    inst->state.droid.projVelX[freeSlot] = (bdx / bdist) * DROID_BULLET_SPEED;
                    inst->state.droid.projVelY[freeSlot] = (bdy / bdist) * DROID_BULLET_SPEED;
                    inst->state.droid.projVelZ[freeSlot] = (bdz / bdist) * DROID_BULLET_SPEED;
                }
                inst->state.droid.projLife[freeSlot] = DROID_BULLET_LIFETIME;
                inst->state.droid.activeProjectiles++;

                // Play firing sound (without volume adjustment to avoid mixer conflicts)
                static int sfxChannel = SFX_CHANNEL_START;
                wav64_play(&sfxTurretZap, sfxChannel);
                sfxChannel = SFX_CHANNEL_START + ((sfxChannel - SFX_CHANNEL_START + 1) % SFX_CHANNEL_COUNT);

                DECO_DEBUG("Droid %d fired projectile %d!\n", inst->id, freeSlot);
            }
        }

        // Animation finished - start cooldown
        if (inst->state.droid.shootAnimTimer <= 0) {
            // Determine cooldown based on cliff state
            if (inst->state.droid.cliffAhead && inst->state.droid.isAggro) {
                inst->state.droid.shootCooldown = DROID_CLIFF_SHOOT_COOLDOWN;
            } else {
                inst->state.droid.shootCooldown = DROID_SHOOT_COOLDOWN;
            }
            inst->state.droid.hasFiredProjectile = false;
        }
        goto update_animation;
    }

    // ==== COMBAT MODE ====
    if (inst->state.droid.isAggro) {
        // Face player (negate dx for correct X-axis facing)
        float targetAngle = atan2f(-dx, dz);
        float angleDiff = targetAngle - inst->rotY;
        
        // Normalize angle difference to [-PI, PI]
        while (angleDiff > 3.14159265f) angleDiff -= 6.28318530f;
        while (angleDiff < -3.14159265f) angleDiff += 6.28318530f;
        
        // Rotate towards player
        float maxTurn = DROID_TURN_SPEED * deltaTime;
        if (angleDiff > maxTurn) angleDiff = maxTurn;
        else if (angleDiff < -maxTurn) angleDiff = -maxTurn;
        inst->rotY += angleDiff;
        
        // Normalize final angle
        while (inst->rotY > 3.14159265f) inst->rotY -= 6.28318530f;
        while (inst->rotY < -3.14159265f) inst->rotY += 6.28318530f;

        // Check if facing player (within 20 degrees = ~0.35 radians)
        bool facingPlayer = fabsf(angleDiff) < 0.35f;

        // Check if cooldown is done and facing player
        bool readyToAttack = (inst->state.droid.shootCooldown <= 0 && facingPlayer);

        // ATTACK: Start shoot animation
        if (readyToAttack) {
            inst->state.droid.shootAnimTimer = DROID_SHOOT_ANIM_DURATION;
            inst->state.droid.hasFiredProjectile = false;
            inst->state.droid.currentAnim = DROID_ANIM_SHOOT;
        }
        // BLOCKING: In close range and on cooldown
        else if (inBlockRange) {
            inst->state.droid.currentAnim = DROID_ANIM_BLOCK;
        }
        // WALKING: Approach player if no cliff, not in block range, and grounded
        else if (!inst->state.droid.cliffAhead && !inBlockRange && inst->state.droid.isGrounded) {
            // Move forward toward player
            float moveX = -sinf(inst->rotY) * inst->state.droid.walkSpeed * deltaTime;  // Inverted X
            float moveZ = cosf(inst->rotY) * inst->state.droid.walkSpeed * deltaTime;
            inst->posX += moveX;
            inst->posZ += moveZ;
            inst->state.droid.currentAnim = DROID_ANIM_WALK;
        }
        // IDLE: Cliff ahead, in block range, or not grounded - stay still
        else {
            inst->state.droid.currentAnim = DROID_ANIM_IDLE;
        }
    }
    // ==== PATROL MODE ====
    else {
        // Handle idle pause
        if (inst->state.droid.isIdling) {
            inst->state.droid.idlePauseTimer -= deltaTime;
            inst->state.droid.currentAnim = DROID_ANIM_IDLE;

            if (inst->state.droid.idlePauseTimer <= 0) {
                // End idle, pick new random direction
                inst->state.droid.isIdling = false;
                inst->state.droid.moveDir = ((float)rand() / RAND_MAX) * 6.28318530f;  // Random 0-2π
            }
        }
        // Walking patrol
        else {
            // Check for cliff ahead or not grounded - if so, go idle and pick new direction
            if (inst->state.droid.cliffAhead || !inst->state.droid.isGrounded) {
                inst->state.droid.isIdling = true;
                inst->state.droid.idlePauseTimer = ((float)rand() / RAND_MAX) * (DROID_IDLE_PAUSE_MAX - DROID_IDLE_PAUSE_MIN) + DROID_IDLE_PAUSE_MIN;
                inst->state.droid.currentAnim = DROID_ANIM_IDLE;
            } else {
                // Move forward in patrol direction
                float moveX = -sinf(inst->state.droid.moveDir) * inst->state.droid.walkSpeed * deltaTime;  // Inverted X
                float moveZ = cosf(inst->state.droid.moveDir) * inst->state.droid.walkSpeed * deltaTime;
                inst->posX += moveX;
                inst->posZ += moveZ;

                // Smoothly rotate toward patrol direction
                float angleDiff = inst->state.droid.moveDir - inst->rotY;
                while (angleDiff > 3.14159265f) angleDiff -= 6.28318530f;
                while (angleDiff < -3.14159265f) angleDiff += 6.28318530f;
                float maxTurn = DROID_TURN_SPEED * deltaTime;
                if (angleDiff > maxTurn) angleDiff = maxTurn;
                else if (angleDiff < -maxTurn) angleDiff = -maxTurn;
                inst->rotY += angleDiff;
                while (inst->rotY > 3.14159265f) inst->rotY -= 6.28318530f;
                while (inst->rotY < -3.14159265f) inst->rotY += 6.28318530f;

                inst->state.droid.currentAnim = DROID_ANIM_WALK;

                // Randomly decide to stop and idle
                if (((float)rand() / RAND_MAX) < 0.002f) {  // ~0.2% chance per frame (6% per second at 30fps)
                    inst->state.droid.isIdling = true;
                    inst->state.droid.idlePauseTimer = ((float)rand() / RAND_MAX) * (DROID_IDLE_PAUSE_MAX - DROID_IDLE_PAUSE_MIN) + DROID_IDLE_PAUSE_MIN;
                }
            }
        }
    }

update_animation:
    // Update skeleton animation
    if (inst->hasOwnSkeleton && skeleton_is_valid(&inst->skeleton) && inst->animCount > 0) {
        int animIdx = inst->state.droid.currentAnim;
        
        // Clamp animation index to valid range
        if (animIdx >= (int)inst->animCount) {
            animIdx = 0;  // Fall back to first animation
        }
        
        if (animIdx >= 0 && animIdx < (int)inst->animCount) {
            T3DAnim* anim = &inst->anims[animIdx];
            
            // Special handling for sec_block (single-frame hold)
            if (animIdx == DROID_ANIM_BLOCK) {
                t3d_anim_set_looping(anim, false);
                t3d_anim_set_time(anim, 0.0f);  // Hold first frame
                t3d_anim_set_playing(anim, false);
            } else {
                t3d_anim_set_looping(anim, true);
                t3d_anim_set_playing(anim, true);
            }
            
            t3d_anim_attach(anim, &inst->skeleton);
            t3d_anim_update(anim, deltaTime);
        }
        t3d_skeleton_update(&inst->skeleton);
    }
}

static void droid_sec_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map; (void)px; (void)py; (void)pz;
    // Droid doesn't damage player on touch - only bullets do
    // Damage from spin attack is handled in game.c spin attack section
}

// --- DROID BULLET BEHAVIOR ---
static void droid_bullet_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.bullet.velX = 0.0f;
    inst->state.bullet.velY = 0.0f;
    inst->state.bullet.velZ = 0.0f;
    inst->state.bullet.lifetime = DROID_BULLET_LIFETIME;
    inst->state.bullet.ownerIndex = -1;
}

static void droid_bullet_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Update position
    inst->posX += inst->state.bullet.velX * deltaTime;
    inst->posY += inst->state.bullet.velY * deltaTime;
    inst->posZ += inst->state.bullet.velZ * deltaTime;

    // Update lifetime
    inst->state.bullet.lifetime -= deltaTime;
    if (inst->state.bullet.lifetime <= 0) {
        inst->active = false;
        return;
    }

    // Check collision with player
    float dx = map->playerX - inst->posX;
    float dy = (map->playerY + 10.0f) - inst->posY;  // Player center
    float dz = map->playerZ - inst->posZ;
    float distSq = dx*dx + dy*dy + dz*dz;

    #define BULLET_HIT_RADIUS 12.0f
    if (distSq < BULLET_HIT_RADIUS * BULLET_HIT_RADIUS) {
        // Hit player! (player_take_damage handles invincibility internally)
        extern void player_take_damage(int damage);
        extern bool player_is_dead(void);

        if (!player_is_dead()) {
            player_take_damage(DROID_BULLET_DAMAGE);
            DECO_DEBUG("Bullet hit player!\n");
        }
        inst->active = false;
    }
}

static void droid_bullet_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    // Collision is handled in update for bullets
}

// --- ELEVATOR BEHAVIOR ---
static void elevator_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.elevator.elevatorY = inst->posY;  // Store original Y position
    inst->state.elevator.targetY = inst->posY;
    inst->state.elevator.speed = ELEVATOR_SPEED;
    inst->state.elevator.lastDelta = 0.0f;
    inst->state.elevator.moving = false;
}

static void elevator_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Get player position from map runtime
    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    // Use the elevator's collision mesh to detect if player is on the platform
    DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
    bool playerOnElevator = false;

    if (decoType && decoType->loaded && decoType->collision) {
        // Check if player is inside the elevator's collision volume
        // Uses cached AABB for performance (no per-frame recalculation)
        // Supports rotation via transforming player pos to local space
        playerOnElevator = deco_check_inside_volume_cached_rotated(decoType,
            px, py, pz, 5.0f, 0.0f,  // Small tolerance for foot placement, no radius expansion
            inst->posX, inst->posY, inst->posZ,
            inst->scaleX, inst->scaleY, inst->scaleZ,
            inst->rotY);
    }

    // Set target based on player presence
    if (playerOnElevator) {
        // Player is on elevator - move up
        inst->state.elevator.targetY = inst->state.elevator.elevatorY + ELEVATOR_RISE_HEIGHT;
        inst->state.elevator.moving = true;
    } else {
        // Player not on elevator - return to original position
        inst->state.elevator.targetY = inst->state.elevator.elevatorY;
        inst->state.elevator.moving = (inst->posY != inst->state.elevator.elevatorY);
    }

    // Move elevator toward target
    inst->state.elevator.lastDelta = 0.0f;  // Reset delta each frame

    if (inst->state.elevator.moving) {
        float prevY = inst->posY;
        float diff = inst->state.elevator.targetY - inst->posY;

        if (fabsf(diff) > ELEVATOR_STOP_THRESHOLD) {
            float movement = (diff > 0.0f ? 1.0f : -1.0f) * inst->state.elevator.speed * deltaTime;

            // Don't overshoot
            if (fabsf(movement) > fabsf(diff)) {
                inst->posY = inst->state.elevator.targetY;
            } else {
                inst->posY += movement;
            }

            // Store the movement delta for the player
            inst->state.elevator.lastDelta = inst->posY - prevY;
        } else {
            inst->posY = inst->state.elevator.targetY;
            inst->state.elevator.moving = false;
        }
    }
}

// --- TRANSITION COLLISION BEHAVIOR ---
static void transition_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Only reset the triggered flag - targetLevel and targetSpawn are set by level_load
    // Don't overwrite them here!
    inst->state.transition.triggered = false;
}

static void transition_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map; (void)px; (void)py; (void)pz;

    // Declare extern function and state from game.c
    extern void trigger_level_transition(int targetLevel, int targetSpawn);
    extern bool g_playerIsRespawning;

    // Don't trigger transitions while respawning (prevents checkpoint near transition bug)
    if (g_playerIsRespawning) {
        return;
    }

    // Trigger transition
    if (!inst->state.transition.triggered) {
        inst->state.transition.triggered = true;
        debugf("Transition triggered: Level %d, Spawn %d\n",
            inst->state.transition.targetLevel,
            inst->state.transition.targetSpawn);
        trigger_level_transition(inst->state.transition.targetLevel, inst->state.transition.targetSpawn);
    }
}

// --- IMMEDIATE LEVEL TRANSITION BEHAVIOR ---
// Simple fade to black and switch levels (no celebration sequence)
static void level_transition_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.transition.triggered = false;
}

static void level_transition_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map; (void)px; (void)py; (void)pz;

    // Declare extern function and state from game.c
    extern void trigger_immediate_level_transition(int targetLevel, int targetSpawn);
    extern bool g_playerIsRespawning;

    // Don't trigger transitions while respawning
    if (g_playerIsRespawning) {
        return;
    }

    // Trigger immediate transition (no celebration)
    if (!inst->state.transition.triggered) {
        inst->state.transition.triggered = true;
        debugf("Immediate level transition: Level %d, Spawn %d\n",
            inst->state.transition.targetLevel,
            inst->state.transition.targetSpawn);
        trigger_immediate_level_transition(inst->state.transition.targetLevel, inst->state.transition.targetSpawn);
    }
}

// --- SIGN BEHAVIOR ---
// Sign tilts when player stands on the sign board (not the pole)
// The sign board is roughly at Y offset +25 to +35 from base, extends ~15 units from pole

#define SIGN_BOARD_MIN_Y 0.0f       // Sign board starts at this height above base
#define SIGN_BOARD_RADIUS 35.0f     // How far sign board extends from center
#define SIGN_POLE_RADIUS 5.0f       // Pole is thin, don't tilt for pole
#define SIGN_BOARD_RADIUS_SQ (SIGN_BOARD_RADIUS * SIGN_BOARD_RADIUS)  // 1225
#define SIGN_POLE_RADIUS_SQ  (SIGN_POLE_RADIUS * SIGN_POLE_RADIUS)    // 25
#define SIGN_TILT_MAX 0.8f          // Max tilt in radians (~45 degrees)
#define SIGN_TILT_SPRING 12.0f      // Spring constant for returning to neutral
#define SIGN_TILT_DAMPING 5.0f      // Damping to prevent oscillation
#define SIGN_TILT_STRENGTH 0.8f     // How much player weight tilts the sign

static void sign_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.sign.tilt = 0.0f;
    inst->state.sign.tiltVel = 0.0f;
    inst->state.sign.baseRotY = inst->rotY;  // Store original rotation
    inst->state.sign.playerOnSign = false;
}

static void sign_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)deltaTime;
    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    // Check if player is on the sign board (not the pole)
    float dx = px - inst->posX;
    float dz = pz - inst->posZ;
    float distXZSq = dx * dx + dz * dz;  // Use squared distance (avoid sqrtf)
    float heightAboveSign = py - inst->posY;

    bool onSignBoard = false;
    if (heightAboveSign >= SIGN_BOARD_MIN_Y && heightAboveSign <= SIGN_BOARD_MIN_Y + 20.0f) {
        // Player is at sign board height
        if (distXZSq > SIGN_POLE_RADIUS_SQ && distXZSq < SIGN_BOARD_RADIUS_SQ) {
            // Player is on the sign board part (not the pole)
            onSignBoard = true;
        }
    }

    inst->state.sign.playerOnSign = onSignBoard;

    // Calculate target tilt - based on which side of the sign the player is on
    float targetTilt = 0.0f;

    if (onSignBoard && distXZSq > 0.0001f) {  // 0.01^2
        // Need actual distance for tilt ratio calculation
        float distXZ = sqrtf(distXZSq);
        // Tilt based on which side (X direction) of the sign player is standing
        float tiltAmount = (distXZ / SIGN_BOARD_RADIUS) * SIGN_TILT_STRENGTH;
        targetTilt = (dx > 0 ? -1.0f : 1.0f) * tiltAmount;
    }

    // Spring physics
    float force = (targetTilt - inst->state.sign.tilt) * SIGN_TILT_SPRING
                  - inst->state.sign.tiltVel * SIGN_TILT_DAMPING;

    inst->state.sign.tiltVel += force * deltaTime;
    inst->state.sign.tilt += inst->state.sign.tiltVel * deltaTime;

    // Clamp tilt to max
    if (inst->state.sign.tilt > SIGN_TILT_MAX) inst->state.sign.tilt = SIGN_TILT_MAX;
    if (inst->state.sign.tilt < -SIGN_TILT_MAX) inst->state.sign.tilt = -SIGN_TILT_MAX;

    // Apply tilt on global X axis - rotX applied BEFORE rotY in euler order
    inst->rotX = inst->state.sign.tilt;
    inst->rotY = inst->state.sign.baseRotY;
    inst->rotZ = 0.0f;
}

// ============================================================
// HANGING PLATFORM BEHAVIOR
// ============================================================
// Spinning animation - slowly rotates Y back and forth by 15 degrees

#define HANGING_SPIN_AMPLITUDE 0.2618f  // 15 degrees in radians
#define HANGING_SPIN_SPEED     1.5f     // Radians per second (~2.4 seconds per full cycle)

static void hanging_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Store base rotation to oscillate around
    inst->state.hanging.baseRotY = inst->rotY;
    // Random initial phase so multiple platforms don't sync
    inst->state.hanging.spinPhase = ((float)(inst->id % 100) / 100.0f) * 6.2831f;  // 0 to 2*PI
    inst->state.hanging.spinSpeed = HANGING_SPIN_SPEED;
    inst->state.hanging.spinAmplitude = HANGING_SPIN_AMPLITUDE;
    inst->state.hanging.prevRotY = inst->rotY;
    inst->state.hanging.playerDeltaX = 0.0f;
    inst->state.hanging.playerDeltaZ = 0.0f;
    inst->state.hanging.playerOnPlatform = false;
}

static void hanging_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Reset player movement deltas
    inst->state.hanging.playerDeltaX = 0.0f;
    inst->state.hanging.playerDeltaZ = 0.0f;
    inst->state.hanging.playerOnPlatform = false;

    // Store previous rotation for delta calculation
    float prevRotY = inst->rotY;

    // Advance phase
    inst->state.hanging.spinPhase += inst->state.hanging.spinSpeed * deltaTime;
    if (inst->state.hanging.spinPhase > 6.2831f) {
        inst->state.hanging.spinPhase -= 6.2831f;  // Keep in 0 to 2*PI range
    }

    // Apply sinusoidal spin to Y rotation (rotate back and forth around base)
    inst->rotY = inst->state.hanging.baseRotY +
                 sinf(inst->state.hanging.spinPhase) * inst->state.hanging.spinAmplitude;

    // Calculate rotation delta for player movement
    float deltaAngle = inst->rotY - prevRotY;

    // Get player position
    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    // Distance check for player physics (skip if too far)
    float dx = px - inst->posX;
    float dz = pz - inst->posZ;
    float distSq = dx*dx + dz*dz;
    if (distSq > 10000.0f) {  // ~100 units
        return;
    }

    // Check if player is standing on the platform using direct collision loop
    // (Same proven approach as moveplat_update)
    DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
    bool playerOn = false;

    if (decoType && decoType->collision) {
        CollisionMesh* mesh = decoType->collision;

        // Pre-calculate rotation values for transforming vertices
        float cosR = fm_cosf(inst->rotY);
        float sinR = fm_sinf(inst->rotY);

        for (int i = 0; i < mesh->count && !playerOn; i++) {
            CollisionTriangle* t = &mesh->triangles[i];

            // Transform triangle to world space: scale -> rotate Y -> translate
            // Vertex 0
            float sx0 = t->x0 * inst->scaleX;
            float sz0 = t->z0 * inst->scaleZ;
            float x0 = sx0 * cosR - sz0 * sinR + inst->posX;
            float y0 = t->y0 * inst->scaleY + inst->posY;
            float z0 = sx0 * sinR + sz0 * cosR + inst->posZ;
            // Vertex 1
            float sx1 = t->x1 * inst->scaleX;
            float sz1 = t->z1 * inst->scaleZ;
            float x1 = sx1 * cosR - sz1 * sinR + inst->posX;
            float y1 = t->y1 * inst->scaleY + inst->posY;
            float z1 = sx1 * sinR + sz1 * cosR + inst->posZ;
            // Vertex 2
            float sx2 = t->x2 * inst->scaleX;
            float sz2 = t->z2 * inst->scaleZ;
            float x2 = sx2 * cosR - sz2 * sinR + inst->posX;
            float y2 = t->y2 * inst->scaleY + inst->posY;
            float z2 = sx2 * sinR + sz2 * cosR + inst->posZ;

            // AABB culling
            float minX = fminf(fminf(x0, x1), x2);
            float maxX = fmaxf(fmaxf(x0, x1), x2);
            float minZ = fminf(fminf(z0, z1), z2);
            float maxZ = fmaxf(fmaxf(z0, z1), z2);
            if (px < minX - 5.0f || px > maxX + 5.0f || pz < minZ - 5.0f || pz > maxZ + 5.0f) continue;

            // Check if point is inside triangle (2D projection onto XZ plane)
            float v0x = x2 - x0, v0z = z2 - z0;
            float v1x = x1 - x0, v1z = z1 - z0;
            float v2x = px - x0, v2z = pz - z0;

            float dot00 = v0x * v0x + v0z * v0z;
            float dot01 = v0x * v1x + v0z * v1z;
            float dot02 = v0x * v2x + v0z * v2z;
            float dot11 = v1x * v1x + v1z * v1z;
            float dot12 = v1x * v2x + v1z * v2z;

            float denom = dot00 * dot11 - dot01 * dot01;
            if (fabsf(denom) < 0.0001f) continue;  // Degenerate triangle

            float invDenom = 1.0f / denom;
            float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
            float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

            if (u >= -0.1f && v >= -0.1f && (u + v) <= 1.1f) {
                float triY = y0 + u * (y2 - y0) + v * (y1 - y0);
                if (py >= triY - 5.0f && py <= triY + 25.0f) {
                    playerOn = true;
                }
            }
        }
    }

    if (!playerOn) {
        return;
    }

    inst->state.hanging.playerOnPlatform = true;

    // Rotate player position around platform center by the delta angle
    // For Y-axis rotation, we rotate in the XZ plane
    float relX = px - inst->posX;
    float relZ = pz - inst->posZ;

    float c = fast_cos(deltaAngle);
    float s = fast_sin(deltaAngle);
    float newRelX = relX * c - relZ * s;
    float newRelZ = relX * s + relZ * c;

    // Delta is the movement from rotation
    inst->state.hanging.playerDeltaX = newRelX - relX;
    inst->state.hanging.playerDeltaZ = newRelZ - relZ;
}

// ============================================================
// SLIME BEHAVIOR
// Spring-mass physics for jiggly jumping enemy
// ============================================================

// Slime physics constants
#define SLIME_JUMP_VELOCITY 10.0f       // Jump strength (higher = bouncier arc)
#define SLIME_JUMP_MIN_TIME 2.0f        // Minimum time between jumps
#define SLIME_JUMP_MAX_TIME 3.0f        // Maximum time between jumps
#define SLIME_MOVE_SPEED_BASE 40.0f     // Base horizontal movement speed (scaled by size)
#define SLIME_AGGRO_RANGE 120.0f        // Distance to detect player
#define SLIME_DEAGGRO_RANGE 200.0f      // Distance to stop chasing

// Spring constants for jiggle (F = -k*x - damping*v)
#define SLIME_JIGGLE_SPRING 50.0f       // Spring stiffness for XZ jiggle (lower = wobblier)
#define SLIME_JIGGLE_DAMPING 2.0f       // Damping for XZ jiggle (lower = longer wobble)
#define SLIME_JIGGLE_MAX 1.2f           // Maximum jiggle offset (higher = more extreme)

// Spring constants for vertical stretch
#define SLIME_STRETCH_SPRING 80.0f      // Spring stiffness for Y stretch
#define SLIME_STRETCH_DAMPING 4.5f      // Damping for Y stretch
#define SLIME_LAND_SQUASH 0.5f          // Squash amount on landing
#define SLIME_JUMP_STRETCH 1.4f         // Stretch amount when jumping

// Slime size thresholds (based on scaleX)
#define SLIME_SCALE_LARGE 1.5f          // >= 1.5 is large (3 HP, splits into 2 medium)
#define SLIME_SCALE_MEDIUM 1.0f         // >= 1.0 is medium (2 HP, splits into 2 small)
#define SLIME_SCALE_SMALL 0.5f          // >= 0.5 is small (1 HP, no split)
#define SLIME_CHILD_SCALE 0.65f         // Children are 65% of parent scale
#define SLIME_MERGE_RANGE 20.0f         // Distance for slimes to merge
#define SLIME_MERGE_COOLDOWN 3.0f       // Seconds before slime can merge after spawning

// Lava slime texture scrolling (shared by all lava slimes)
#define LAVASLIME_TEXTURE_SPEED 0.03f   // Slow lava flow speed (cycles per second)
static float g_lavaSlimeOffset = 0.0f;  // Global shared offset for all lava slime instances
static inline float lavaslime_get_offset(void) { return g_lavaSlimeOffset; }

static void slime_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.slime.velX = 0.0f;
    inst->state.slime.velY = 0.0f;
    inst->state.slime.velZ = 0.0f;
    inst->state.slime.isGrounded = false;
    inst->state.slime.jumpTimer = 0.5f + (rand() % 100) / 100.0f;  // Stagger initial jumps
    inst->state.slime.moveDir = ((float)(rand() % 628) / 100.0f) - 3.14f;
    inst->state.slime.windupTimer = 0.0f;

    // Initialize spring state
    inst->state.slime.jiggleX = 0.0f;
    inst->state.slime.jiggleZ = 0.0f;
    inst->state.slime.jiggleVelX = 0.0f;
    inst->state.slime.jiggleVelZ = 0.0f;

    inst->state.slime.stretchY = 1.0f;
    inst->state.slime.stretchVelY = 0.0f;

    inst->state.slime.isAggro = false;
    inst->state.slime.attackCooldown = 0.0f;

    // Set health based on scale (bigger = more HP)
    if (inst->scaleX >= SLIME_SCALE_LARGE) {
        inst->state.slime.health = 3;  // Large slime: 3 stomps to kill
    } else if (inst->scaleX >= SLIME_SCALE_MEDIUM) {
        inst->state.slime.health = 2;  // Medium slime: 2 stomps
    } else {
        inst->state.slime.health = 1;  // Small slime: 1 stomp
    }
    inst->state.slime.pendingSplit = false;
    inst->state.slime.mergeTimer = SLIME_MERGE_COOLDOWN;  // New slimes can't merge immediately
    inst->state.slime.shakeTimer = 0.0f;
    inst->state.slime.isDying = false;
    inst->state.slime.deathTimer = 0.0f;
    inst->state.slime.invincibleTimer = 0.0f;

    // Initialize oil trail decals
    inst->state.slime.decalHead = 0;
    inst->state.slime.decalCount = 0;
    inst->state.slime.decalScale = inst->scaleX * 15.0f;  // Scale decals with slime size (bigger)
    for (int i = 0; i < 5; i++) {
        inst->state.slime.decalAlpha[i] = 0.0f;
    }

    // Store spawn position for respawn on fall
    inst->state.slime.spawnX = inst->posX;
    inst->state.slime.spawnY = inst->posY;
    inst->state.slime.spawnZ = inst->posZ;
    inst->state.slime.spawnRotY = inst->rotY;
}

static void slime_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
#ifndef NDEBUG
    g_slimeUpdateCount++;
    uint32_t t0, t1;
#endif

    // Update lava slime texture offset (shared by all lava slimes)
    // This will be called multiple times per frame if multiple lava slimes exist,
    // but that just means the texture scrolls proportionally faster
    if (inst->type == DECO_SLIME_LAVA) {
        g_lavaSlimeOffset += LAVASLIME_TEXTURE_SPEED * deltaTime;
        if (g_lavaSlimeOffset >= 1.0f) {
            g_lavaSlimeOffset -= 1.0f;
        }
    }

    // Count down invincibility timer
    if (inst->state.slime.invincibleTimer > 0.0f) {
        inst->state.slime.invincibleTimer -= deltaTime;
    }

    // Handle death animation
    if (inst->state.slime.isDying) {
        inst->state.slime.deathTimer += deltaTime;

        // Jiggle violently while dying
        inst->state.slime.jiggleVelX += ((rand() % 100) - 50) / 10.0f * deltaTime;
        inst->state.slime.jiggleVelZ += ((rand() % 100) - 50) / 10.0f * deltaTime;

        // Flatten and shrink over 0.4 seconds
        float deathDuration = 0.4f;
        float progress = inst->state.slime.deathTimer / deathDuration;
        if (progress > 1.0f) progress = 1.0f;

        // Get original scale
        float originalScale = (inst->scaleX >= SLIME_SCALE_LARGE) ? SLIME_SCALE_LARGE :
                              (inst->scaleX >= SLIME_SCALE_MEDIUM) ? SLIME_SCALE_MEDIUM : SLIME_SCALE_SMALL;

        // Flatten vertically (Y shrinks faster) while XZ expands slightly then shrinks
        float yScale = originalScale * (1.0f - progress);  // Y goes to 0
        float xzScale = originalScale * (1.0f + 0.3f * progress) * (1.0f - progress * 0.8f);  // XZ expands then shrinks
        inst->scaleX = xzScale;
        inst->scaleY = yScale;
        inst->scaleZ = xzScale;

        // Force stretchY to be flat (override spring physics)
        inst->state.slime.stretchY = 0.3f * (1.0f - progress);

        // When fully flattened, spawn particles and either split or die
        if (inst->state.slime.deathTimer >= deathDuration) {
            // Spawn burst of oil particles
            game_spawn_splash_particles(inst->posX, inst->posY, inst->posZ, 20, 30, 20, 40);

            // Spawn death decal (bigger than normal, persists after slime dies)
            game_spawn_death_decal(inst->posX, inst->posY + 0.5f, inst->posZ, originalScale * 50.0f, inst->type == DECO_SLIME_LAVA);

            if (inst->state.slime.pendingSplit) {
                // Big slime: spawn 3 smaller ones
                inst->state.slime.pendingSplit = false;

                // Calculate spawn offset - spread them out more
                float spawnOffset = 25.0f;
                float angle = ((float)(rand() % 628) / 100.0f);

                // Spawn three small slimes (120 degrees apart)
                // SAFETY: Check we have room before spawning to prevent buffer overflow
                int slotsAvailable = 0;
                for (int j = 0; j < map->decoCount; j++) {
                    if (!map->decorations[j].active) slotsAvailable++;
                }
                slotsAvailable += (MAX_DECORATIONS - map->decoCount);  // Add remaining capacity
                int slimesToSpawn = (slotsAvailable < 3) ? slotsAvailable : 3;

                for (int i = 0; i < slimesToSpawn; i++) {
                    float offsetAngle = angle + (i * 2.094f);  // 120 degrees apart (2*PI/3)
                    float spawnX = inst->posX + cosf(offsetAngle) * spawnOffset;
                    float spawnZ = inst->posZ + sinf(offsetAngle) * spawnOffset;

                    // Find any inactive decoration slot to spawn into
                    // First check within decoCount, then expand if needed
                    int slotFound = -1;
                    for (int j = 0; j < map->decoCount; j++) {
                        if (!map->decorations[j].active) {
                            slotFound = j;
                            break;
                        }
                    }
                    // If no inactive slot found, use a new slot and expand decoCount
                    // Double-check bounds to prevent any race condition issues
                    if (slotFound < 0 && map->decoCount < MAX_DECORATIONS) {
                        slotFound = map->decoCount;
                        map->decoCount++;
                    }

                    if (slotFound >= 0) {
                        DecoInstance* newSlime = &map->decorations[slotFound];
                        newSlime->type = inst->type;  // Spawn same type (DECO_SLIME or DECO_SLIME_LAVA)
                        newSlime->active = true;
                        newSlime->initialized = true;  // Mark as initialized
                        newSlime->hasOwnSkeleton = false;  // Slimes don't have per-instance skeletons
                        newSlime->animCount = 0;
                        // CRITICAL: Zero skeleton to prevent garbage from reused slot
                        memset(&newSlime->skeleton, 0, sizeof(T3DSkeleton));
                        newSlime->id = map->nextDecoId++;
                        newSlime->posX = spawnX;
                        newSlime->posY = inst->posY + 15.0f;  // Spawn well above ground
                        newSlime->posZ = spawnZ;
                        newSlime->scaleX = SLIME_SCALE_SMALL;
                        newSlime->scaleY = SLIME_SCALE_SMALL;
                        newSlime->scaleZ = SLIME_SCALE_SMALL;
                        newSlime->rotY = 0.0f;
                        slime_init(newSlime, map);
                        // Pop up and outward when spawning
                        newSlime->state.slime.velY = 8.0f;
                        newSlime->state.slime.velX = cosf(offsetAngle) * 5.0f;
                        newSlime->state.slime.velZ = sinf(offsetAngle) * 5.0f;
                        newSlime->state.slime.isGrounded = false;  // Ensure not grounded
                        // Invincible for 1 second after spawning
                        newSlime->state.slime.invincibleTimer = 1.0f;
                    }
                }
            }

            // Deactivate this slime
            inst->active = false;
        }

        // Still update spring physics for jiggle while dying
        goto spring_physics;
    }

    // Track previous state for landing detection
    bool wasGrounded = inst->state.slime.isGrounded;
    float prevVelY = inst->state.slime.velY;

#ifndef NDEBUG
    t0 = TICKS_READ();
#endif
    // Calculate distance to player (squared to avoid expensive sqrtf)
    float dx = map->playerX - inst->posX;
    float dz = map->playerZ - inst->posZ;
    float distSq = dx * dx + dz * dz;

    // Aggro logic (use squared distance to avoid sqrt)
    if (distSq < SLIME_AGGRO_RANGE * SLIME_AGGRO_RANGE) {
        inst->state.slime.isAggro = true;
    } else if (distSq > SLIME_DEAGGRO_RANGE * SLIME_DEAGGRO_RANGE) {
        inst->state.slime.isAggro = false;
    }

    // Movement direction (use distSq > 1.0f instead of sqrt > 1.0f since sqrt(1) = 1)
    float targetDir = 0.0f;
    if (inst->state.slime.isAggro && distSq > 1.0f) {
        // Chase player
        targetDir = atan2f(dx, dz);
    } 
    // else {
    //     // Random patrol - change direction periodically
    //     inst->state.slime.jumpTimer -= deltaTime;  // Reuse jump timer
    //     if (inst->state.slime.jumpTimer <= 0.0f) {
    //         inst->state.slime.moveDir = ((float)(rand() % 628) / 100.0f) - 3.14f;
    //     }
    //     targetDir = inst->state.slime.moveDir;
    // }

    // Only initiate jump when grounded
    if (inst->state.slime.isGrounded && !inst->state.slime.isDying && inst->state.slime.isAggro) {
        // Handle windup phase
        const float WINDUP_DURATION = 0.5f;  // Half second anticipation
        const float WINDUP_SQUASH = 0.35f;   // Squash down to 35% height

        if (inst->state.slime.windupTimer > 0.0f) {
            inst->state.slime.windupTimer -= deltaTime;

            // Anticipation squash - smooth lerp to squashed state
            float windupProgress = 1.0f - (inst->state.slime.windupTimer / WINDUP_DURATION);
            // Ease-in curve for more dramatic squash at end
            float easedProgress = windupProgress * windupProgress;
            inst->state.slime.stretchY = 1.0f - easedProgress * (1.0f - WINDUP_SQUASH);
            inst->state.slime.stretchVelY = 0.0f;  // Hold the squash

            // Vibrate/tremble during windup (gets stronger near end)
            float trembleStrength = windupProgress * 0.4f;
            inst->state.slime.jiggleX = ((rand() % 100) - 50) / 100.0f * trembleStrength;
            inst->state.slime.jiggleZ = ((rand() % 100) - 50) / 100.0f * trembleStrength;

            // Actually jump when windup finishes
            if (inst->state.slime.windupTimer <= 0.0f) {
                // Speed scales inversely with size (smaller = faster, bigger = slower)
                // At scale 1.0 = 100% speed, 0.5 = 150% speed, 2.0 = 75% speed
                float sizeSpeedMult = 1.0f / sqrtf(inst->scaleX);
                float moveSpeed = SLIME_MOVE_SPEED_BASE * sizeSpeedMult;

                // Jump! Set initial velocities for arc
                inst->state.slime.velY = SLIME_JUMP_VELOCITY;
                inst->state.slime.velX = sinf(targetDir) * moveSpeed * 0.08f;
                inst->state.slime.velZ = cosf(targetDir) * moveSpeed * 0.08f;
                inst->state.slime.isGrounded = false;
                inst->state.slime.jumpTimer = SLIME_JUMP_MIN_TIME +
                    (rand() % (int)((SLIME_JUMP_MAX_TIME - SLIME_JUMP_MIN_TIME) * 100)) / 100.0f;

                // Big stretch on release (spring from deep squash)
                inst->state.slime.stretchY = SLIME_JUMP_STRETCH;
                inst->state.slime.stretchVelY = 5.0f;  // Strong upward for bounce feel

                // Jiggle impulse in movement direction
                float jiggleImpulse = 1.0f;
                inst->state.slime.jiggleVelX = sinf(targetDir) * jiggleImpulse;
                inst->state.slime.jiggleVelZ = cosf(targetDir) * jiggleImpulse;
            }
        } else {
            // Normal countdown to next jump
            inst->state.slime.jumpTimer -= deltaTime;

            if (inst->state.slime.jumpTimer <= 0.0f) {
                // Start windup phase instead of jumping immediately
                inst->state.slime.windupTimer = WINDUP_DURATION;
            }
        }
    }
#ifndef NDEBUG
    t1 = TICKS_READ();
    g_slimeMathTicks += (t1 - t0);
#endif

    // Apply gravity to vertical velocity
    inst->state.slime.velY -= map->gravity * deltaTime * 60.0f;

    // Apply all velocities to position (arc movement)
    inst->posX += inst->state.slime.velX;
    inst->posY += inst->state.slime.velY;
    inst->posZ += inst->state.slime.velZ;

    // Deactivate if fallen off map
    if (inst->posY < -500.0f) {
        inst->active = false;
        return;
    }

    // Collision detection (only if mapLoader is set)
    // OPTIMIZATION: Skip collision when grounded and not moving (waiting to jump)
    // Only check collision when:
    // 1. In the air (velY != 0 or velX/Z != 0)
    // 2. Was previously grounded but might have moved (wasGrounded && !isGrounded after check)
    bool needsCollision = !wasGrounded ||  // Was in air last frame
                          inst->state.slime.velY != 0.0f ||  // Has vertical velocity
                          inst->state.slime.velX != 0.0f ||  // Moving horizontally
                          inst->state.slime.velZ != 0.0f;

    if (map->mapLoader && needsCollision) {
        // Ground collision
        int myIndex = (int)(inst - map->decorations);

#ifndef NDEBUG
        t0 = TICKS_READ();
#endif
        float groundY = maploader_get_ground_height(map->mapLoader, inst->posX, inst->posY + 20.0f, inst->posZ);
#ifndef NDEBUG
        t1 = TICKS_READ();
        g_slimeGroundTicks += (t1 - t0);
        t0 = TICKS_READ();
#endif
        float decoGroundY = map_get_deco_ground_height_ex(map, inst->posX, inst->posY + 20.0f, inst->posZ, myIndex);
#ifndef NDEBUG
        t1 = TICKS_READ();
        g_slimeDecoGroundTicks += (t1 - t0);
#endif

        if (decoGroundY > groundY) groundY = decoGroundY;

        inst->state.slime.isGrounded = false;
        if (groundY > INVALID_GROUND_Y && inst->state.slime.velY <= 0 && inst->posY <= groundY + 1.0f) {
            inst->posY = groundY;
            inst->state.slime.isGrounded = true;

            // Landing impact - big squash and jiggle based on impact velocity
            if (!wasGrounded && prevVelY < -1.0f) {
                float impactStrength = -prevVelY / 8.0f;  // More sensitive to impact
                if (impactStrength > 1.0f) impactStrength = 1.0f;

                // Big squash on landing
                inst->state.slime.stretchY = 1.0f - (1.0f - SLIME_LAND_SQUASH) * impactStrength;
                inst->state.slime.stretchVelY = -2.0f * impactStrength;  // Downward for bounce back

                // Strong random jiggle on landing
                inst->state.slime.jiggleVelX += ((rand() % 200) - 100) / 100.0f * impactStrength;
                inst->state.slime.jiggleVelZ += ((rand() % 200) - 100) / 100.0f * impactStrength;

                // Add oil decal at landing position
                int slot = inst->state.slime.decalHead % 5;  // Bounds check for decal array
                inst->state.slime.decalX[slot] = inst->posX;
                inst->state.slime.decalY[slot] = groundY + 0.5f;  // Slightly above ground
                inst->state.slime.decalZ[slot] = inst->posZ;
                inst->state.slime.decalAlpha[slot] = 1.0f;  // Full opacity
                inst->state.slime.decalHead = (slot + 1) % 5;
                if (inst->state.slime.decalCount < 5) inst->state.slime.decalCount++;

                // Spawn oil splash particles! Dark purple/black color
                int numParticles = 4 + (int)(impactStrength * 4);  // 4-8 particles based on impact
                game_spawn_splash_particles(inst->posX, groundY, inst->posZ, numParticles, 30, 20, 40);
            }

            // Stop all velocity on landing
            inst->state.slime.velX = 0.0f;
            inst->state.slime.velY = 0.0f;
            inst->state.slime.velZ = 0.0f;
        }

        // Wall collision - only when moving horizontally
        if (inst->state.slime.velX != 0.0f || inst->state.slime.velZ != 0.0f) {
#ifndef NDEBUG
            t0 = TICKS_READ();
#endif
            float pushX = 0.0f, pushZ = 0.0f;
            if (maploader_check_walls(map->mapLoader, inst->posX, inst->posY, inst->posZ,
                    ENEMY_RADIUS, ENEMY_HEIGHT, &pushX, &pushZ)) {
                inst->posX += pushX;
                inst->posZ += pushZ;
                // Bounce jiggle off walls
                inst->state.slime.jiggleVelX -= pushX * 2.0f;
                inst->state.slime.jiggleVelZ -= pushZ * 2.0f;
            }
#ifndef NDEBUG
            t1 = TICKS_READ();
            g_slimeWallTicks += (t1 - t0);
#endif
        }
    }

    // Update face direction (toward movement/player)
    // Negate to flip model's facing to match movement direction
    inst->rotY = -targetDir;

    // ========================================
    // SPRING PHYSICS FOR JIGGLE
    // F = -k*x - damping*v
    // ========================================
spring_physics:
#ifndef NDEBUG
    t0 = TICKS_READ();
#endif

    // XZ jiggle spring (point mass oscillation)
    float forceX = -SLIME_JIGGLE_SPRING * inst->state.slime.jiggleX
                   - SLIME_JIGGLE_DAMPING * inst->state.slime.jiggleVelX;
    float forceZ = -SLIME_JIGGLE_SPRING * inst->state.slime.jiggleZ
                   - SLIME_JIGGLE_DAMPING * inst->state.slime.jiggleVelZ;

    inst->state.slime.jiggleVelX += forceX * deltaTime;
    inst->state.slime.jiggleVelZ += forceZ * deltaTime;
    inst->state.slime.jiggleX += inst->state.slime.jiggleVelX * deltaTime;
    inst->state.slime.jiggleZ += inst->state.slime.jiggleVelZ * deltaTime;

    // Clamp jiggle to max
    if (inst->state.slime.jiggleX > SLIME_JIGGLE_MAX) inst->state.slime.jiggleX = SLIME_JIGGLE_MAX;
    if (inst->state.slime.jiggleX < -SLIME_JIGGLE_MAX) inst->state.slime.jiggleX = -SLIME_JIGGLE_MAX;
    if (inst->state.slime.jiggleZ > SLIME_JIGGLE_MAX) inst->state.slime.jiggleZ = SLIME_JIGGLE_MAX;
    if (inst->state.slime.jiggleZ < -SLIME_JIGGLE_MAX) inst->state.slime.jiggleZ = -SLIME_JIGGLE_MAX;

    // Vertical stretch spring (squash/stretch)
    float stretchForce = -SLIME_STRETCH_SPRING * (inst->state.slime.stretchY - 1.0f)
                         - SLIME_STRETCH_DAMPING * inst->state.slime.stretchVelY;
    inst->state.slime.stretchVelY += stretchForce * deltaTime;
    inst->state.slime.stretchY += inst->state.slime.stretchVelY * deltaTime;

    // Clamp stretch - allow extreme squash/stretch for bouncy effect
    if (inst->state.slime.stretchY < 0.25f) inst->state.slime.stretchY = 0.25f;
    if (inst->state.slime.stretchY > 1.8f) inst->state.slime.stretchY = 1.8f;

#ifndef NDEBUG
    t1 = TICKS_READ();
    g_slimeSpringTicks += (t1 - t0);
#endif

    // Update attack cooldown
    if (inst->state.slime.attackCooldown > 0.0f) {
        inst->state.slime.attackCooldown -= deltaTime;
    }

    // Update merge cooldown
    if (inst->state.slime.mergeTimer > 0.0f) {
        inst->state.slime.mergeTimer -= deltaTime;
    }

    // Update shake timer
    if (inst->state.slime.shakeTimer > 0.0f) {
        inst->state.slime.shakeTimer -= deltaTime;
    }

    // NOTE: pendingSplit is now handled in the death animation code above

    // Slime merging: if two slimes are close, grounded, and neither on cooldown, merge
    if (inst->state.slime.isGrounded && inst->state.slime.mergeTimer <= 0.0f) {
        for (int i = 0; i < map->decoCount; i++) {
            DecoInstance* other = &map->decorations[i];

            // Skip self, inactive, different slime types, or slimes on merge cooldown
            // Only merge same type (regular slimes merge with regular, lava with lava)
            if (other == inst || !other->active || other->type != inst->type) continue;
            if (!other->state.slime.isGrounded || other->state.slime.mergeTimer > 0.0f) continue;
            if (other->state.slime.pendingSplit) continue;

            // Check distance
            float mdx = other->posX - inst->posX;
            float mdz = other->posZ - inst->posZ;
            float mergeDist = SLIME_MERGE_RANGE * (inst->scaleX + other->scaleX) * 0.5f;
            if (mdx * mdx + mdz * mdz < mergeDist * mergeDist) {
                // Merge! Only the smaller one (or same size, lower ID) merges into the other
                if (inst->scaleX < other->scaleX || (inst->scaleX == other->scaleX && inst->id < other->id)) {
                    // This slime merges into other
                    float newScale = sqrtf(inst->scaleX * inst->scaleX + other->scaleX * other->scaleX);
                    float newX = (inst->posX + other->posX) * 0.5f;
                    float newZ = (inst->posZ + other->posZ) * 0.5f;

                    other->scaleX = other->scaleY = other->scaleZ = newScale;
                    other->posX = newX;
                    other->posZ = newZ;

                    // Reset merged slime's health based on new size
                    if (newScale >= SLIME_SCALE_LARGE) {
                        other->state.slime.health = 3;
                    } else if (newScale >= SLIME_SCALE_MEDIUM) {
                        other->state.slime.health = 2;
                    } else {
                        other->state.slime.health = 1;
                    }
                    other->state.slime.mergeTimer = SLIME_MERGE_COOLDOWN;

                    // Big jiggle and shake from merge
                    other->state.slime.stretchY = 1.5f;
                    other->state.slime.stretchVelY = 2.0f;
                    other->state.slime.shakeTimer = 0.5f;  // Shake for half a second

                    DECO_DEBUG("Slime %d merged into %d, new scale %.2f\n", inst->id, other->id, newScale);
                    inst->active = false;
                    return;
                }
            }
        }
    }

    // Continuous player overlap check - if slime is inside player, push out and damage
    // This catches cases where slime lands directly on player
    if (inst->state.slime.attackCooldown <= 0.0f) {
        extern bool player_is_dead(void);
        extern void player_take_damage(int damage);
        extern void player_knockback(float fromX, float fromZ, float strength);
        extern void player_squash(float amount);

        if (!player_is_dead()) {
            float dx = map->playerX - inst->posX;
            float dz = map->playerZ - inst->posZ;
            float distSq = dx * dx + dz * dz;

            // Check XZ overlap (slime radius ~15, player radius ~10)
            float overlapRadius = 20.0f;
            if (distSq < overlapRadius * overlapRadius) {
                // Check Y overlap - player height ~40, slime height ~25
                float playerTop = map->playerY + 40.0f;
                float slimeTop = inst->posY + 25.0f;

                // Overlap if vertical ranges intersect
                if (map->playerY < slimeTop && playerTop > inst->posY) {
                    // Player is inside slime! Push out and damage
                    DECO_DEBUG("Slime %d overlapping player - pushing out!\n", inst->id);
                    player_take_damage(1);
                    player_knockback(inst->posX, inst->posZ, 10.0f);  // Stronger push
                    player_squash(0.07f);
                    inst->state.slime.attackCooldown = 1.0f;

                    // Slime also reacts
                    inst->state.slime.stretchY = 0.5f;
                    inst->state.slime.stretchVelY = -1.5f;
                }
            }
        }
    }

    // Fade oil decals over time (slower fade to see 3-4 at once)
    for (int i = 0; i < 5; i++) {
        if (inst->state.slime.decalAlpha[i] > 0.0f) {
            inst->state.slime.decalAlpha[i] -= deltaTime * 0.06f;  // Fade over ~16 seconds
            if (inst->state.slime.decalAlpha[i] < 0.0f) {
                inst->state.slime.decalAlpha[i] = 0.0f;
            }
        }
    }
}

static void slime_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map;

    // External functions
    extern bool player_is_dead(void);
    extern void player_take_damage(int damage);
    extern void player_knockback(float fromX, float fromZ, float strength);
    extern void player_squash(float amount);  // Squash player model

    if (player_is_dead()) return;

    // Calculate direction from slime to player
    float dx = px - inst->posX;
    float dz = pz - inst->posZ;
    float dist = sqrtf(dx * dx + dz * dz);

    if (dist > 0.01f) {
        // Normalize
        dx /= dist;
        dz /= dist;

        // Strong jiggle impulse away from player (big wobble effect)
        float impulseStrength = 1.5f;
        inst->state.slime.jiggleVelX -= dx * impulseStrength;
        inst->state.slime.jiggleVelZ -= dz * impulseStrength;

        // Squash from impact
        inst->state.slime.stretchY = 0.5f;
        inst->state.slime.stretchVelY = -1.5f;
    }

    // Check if player is above slime (stomping)
    float heightDiff = py - inst->posY;
    if (heightDiff > 5.0f && heightDiff < 25.0f) {
        // Player is stomping - bounce off slime's head
        DECO_DEBUG("Slime %d stomped by player! HP: %d\n", inst->id, inst->state.slime.health);

        // Damage slime
        inst->state.slime.health--;

        // Extreme squash from stomp
        inst->state.slime.stretchY = SLIME_LAND_SQUASH;
        inst->state.slime.stretchVelY = -3.0f;

        // Big wobble from stomp
        inst->state.slime.jiggleVelX += ((rand() % 200) - 100) / 50.0f;
        inst->state.slime.jiggleVelZ += ((rand() % 200) - 100) / 50.0f;

        // Bounce player up (reward stomp)
        extern void player_bounce(float strength);
        player_bounce(8.0f);

        // Check if slime is dead
        if (inst->state.slime.health <= 0) {
            // Hitstop on slime kill
            extern void game_trigger_hitstop(float duration);
            game_trigger_hitstop(0.12f);  // ~4 frames at 30FPS - noticeable pause

            // Check if slime is big enough to split
            if (inst->scaleX >= SLIME_SCALE_MEDIUM) {
                // Mark for splitting (will spawn children next update)
                inst->state.slime.pendingSplit = true;
                DECO_DEBUG("Slime %d will split!\n", inst->id);
            } else {
                // Too small to split - just die
                DECO_DEBUG("Slime %d killed!\n", inst->id);
                inst->active = false;
            }
        }
    } else {
        // Slime bounced into player - deal damage and knockback
        if (inst->state.slime.attackCooldown <= 0.0f) {
            DECO_DEBUG("Slime %d hit player!\n", inst->id);
            player_take_damage(1);
            player_knockback(inst->posX, inst->posZ, 8.0f);
            player_squash(0.07f);
            inst->state.slime.attackCooldown = 1.0f;  // 1 second cooldown
        }
    }
}

// ============================================================
// OIL PUDDLE BEHAVIOR
// ============================================================

static void oilpuddle_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Radius based on scaleX (default scale 1.0 = 20 unit radius)
    inst->state.oilpuddle.radius = inst->scaleX * 20.0f;
}

static void oilpuddle_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)deltaTime;

    // Check if player is within puddle radius (XZ only) and near ground level
    float dx = map->playerX - inst->posX;
    float dz = map->playerZ - inst->posZ;
    float distSq = dx * dx + dz * dz;
    float radius = inst->state.oilpuddle.radius;

    // Player must be within XZ radius and within ~10 units of puddle Y height
    float heightDiff = fabsf(map->playerY - inst->posY);

    if (distSq < radius * radius && heightDiff < 15.0f) {
        // Player is on the oil puddle! Set flag in map runtime (game.c copies to playerState)
        map->playerOnOil = true;
    }
}

// --- COG BEHAVIOR (rotating platform like a ferris wheel) ---
#define COG_ROTATION_SPEED 0.8f  // Radians per second (about 45 deg/sec)

static void cog_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.cog.rotSpeed = COG_ROTATION_SPEED;
    inst->state.cog.prevRotZ = inst->rotZ;
    inst->state.cog.playerDeltaX = 0.0f;
    inst->state.cog.playerDeltaY = 0.0f;
    inst->state.cog.playerDeltaZ = 0.0f;
    inst->state.cog.lastGroundY = INVALID_GROUND_Y;
    inst->state.cog.playerOnCog = false;
    inst->state.cog.wallPushX = 0.0f;
    inst->state.cog.wallPushZ = 0.0f;
    inst->state.cog.hitWall = false;
}

static void cog_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Reset player movement deltas
    inst->state.cog.playerDeltaX = 0.0f;
    inst->state.cog.playerDeltaY = 0.0f;
    inst->state.cog.playerDeltaZ = 0.0f;
    inst->state.cog.playerOnCog = false;
    inst->state.cog.wallPushX = 0.0f;
    inst->state.cog.wallPushZ = 0.0f;
    inst->state.cog.hitWall = false;

    // Get player position
    float px = map->playerX;
    float pz = map->playerZ;
    float py = map->playerY;

    // Distance check for rotation (matches render visibility)
    float dx = px - inst->posX;
    float dz = pz - inst->posZ;
    float distSq = dx*dx + dz*dz;

    // Calculate rotation delta (needed for both visual spin and player physics)
    const float TWO_PI = 6.28318530718f;
    float deltaAngle = inst->state.cog.rotSpeed * deltaTime;

    // Spin cog if within visibility range (700 units = 490000 squared)
    if (distSq <= 490000.0f) {
        inst->rotZ -= deltaAngle;
        while (inst->rotZ >= TWO_PI) inst->rotZ -= TWO_PI;
        while (inst->rotZ < 0.0f) inst->rotZ += TWO_PI;
    }

    // Quick distance check for player physics (tighter range)
    if (distSq > 10000.0f) {
        return;
    }

    // Get collision mesh
    DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
    if (!decoType || !collision_mesh_is_valid(decoType->collision)) {
        return;
    }

    // Add PI to rotZ to align collision with visual model
    const float PI = 3.14159265f;
    float collisionRotZ = inst->rotZ + PI;

    // =========================================================
    // WALL COLLISION - Always check, regardless of standing on cog
    // =========================================================
    float wallPushX = 0.0f, wallPushZ = 0.0f;
    bool hitWall = collision_check_walls_full_rotation(decoType->collision,
        px, py, pz, 8.0f, 30.0f,  // radius, playerHeight
        inst->posX, inst->posY, inst->posZ,
        inst->scaleX, inst->scaleY, inst->scaleZ,
        inst->rotX, inst->rotY, collisionRotZ,
        &wallPushX, &wallPushZ);

    if (hitWall) {
        inst->state.cog.wallPushX = wallPushX;
        inst->state.cog.wallPushZ = wallPushZ;
        inst->state.cog.hitWall = true;
    }

    // =========================================================
    // GROUND/SURFACE COLLISION - For standing on cog
    // =========================================================

    // Use the special "any surface" function that doesn't reject tilted surfaces
    // This prevents the player from becoming ungrounded when the pin tilts
    float surfaceY = collision_get_surface_height_full_rotation(decoType->collision,
        px, py, pz,
        inst->posX, inst->posY, inst->posZ,
        inst->scaleX, inst->scaleY, inst->scaleZ,
        inst->rotX, inst->rotY, collisionRotZ);

    // Check if player is standing on the cog
    // Don't consider player on platform if they have significant upward velocity (jumping)
    float playerVelY = map->playerVelY;  // Need to track this
    bool isJumping = (playerVelY > 2.0f);  // Upward velocity threshold

    // Ground detection - widen tolerance based on fall speed to prevent tunneling
    // Fast-falling players need a larger detection window
    float fallSpeed = (playerVelY < 0) ? -playerVelY : 0.0f;
    float aboveTolerance = 12.0f + fallSpeed * 0.5f;  // More margin when falling fast
    float belowTolerance = 8.0f + fallSpeed * 0.5f;   // Catch players who fell through

    bool onCog = (surfaceY > INVALID_GROUND_Y) &&
                 (py - surfaceY < aboveTolerance) &&  // Not too far above
                 (py - surfaceY > -belowTolerance) && // Not too far below
                 !isJumping;                           // Not jumping away

    if (!onCog) {
        return;
    }

    // Snap player to surface if they're below it (fell through)
    if (py < surfaceY) {
        inst->state.cog.playerDeltaY = (surfaceY + 2.0f) - py;
    }

    inst->state.cog.playerOnCog = true;

    // Get surface height AFTER rotation to push player up if needed
    // Use same PI offset for collision alignment (collisionRotZ already calculated above)
    float newSurfaceY = collision_get_surface_height_full_rotation(decoType->collision,
        px, py, pz,
        inst->posX, inst->posY, inst->posZ,
        inst->scaleX, inst->scaleY, inst->scaleZ,
        inst->rotX, inst->rotY, collisionRotZ);

    // Rotate player position around cog center by the delta angle
    // For Z-axis rotation, we rotate in the XY plane
    float relX = px - inst->posX;
    float relY = py - inst->posY;

    // Flip rotation direction to match visual cog movement
    float c = fast_cos(deltaAngle);
    float s = fast_sin(deltaAngle);
    float newRelX = relX * c - relY * s;
    float newRelY = relX * s + relY * c;

    // Delta is the movement from rotation
    inst->state.cog.playerDeltaX = newRelX - relX;
    inst->state.cog.playerDeltaY = newRelY - relY;

    // If player is below the surface after rotation, push them up
    if (newSurfaceY > INVALID_GROUND_Y && py < newSurfaceY + 2.0f) {
        inst->state.cog.playerDeltaY = (newSurfaceY + 2.0f) - py;
    }
}

// ============================================================
// ROUND BUTTON BEHAVIOR
// ============================================================
// Two-part button: bottom is static base, top moves down when pressed.
// Player standing on top triggers activation.

#define BUTTON_PRESS_DEPTH 8.0f      // How far the top moves down when fully pressed
#define BUTTON_PRESS_SPEED 15.0f     // Speed of press animation
#define BUTTON_RELEASE_SPEED 8.0f    // Speed of release animation
#define BUTTON_DETECT_RADIUS 15.0f   // Horizontal radius for player detection
#define BUTTON_DETECT_HEIGHT 20.0f   // How high above button to detect player

static void roundbutton_init(DecoInstance* inst, MapRuntime* map) {
    inst->state.button.pressed = false;
    inst->state.button.pressDepth = 0.0f;
    inst->state.button.pressVel = 0.0f;

    // Ensure button top model and collision are loaded (can't rely on lazy load
    // because button might be out of render range but still needs collision)
    if (!map->buttonTopLoaded) {
        debugf("roundbutton_init: Loading button top resources\n");
        map->buttonTopModel = t3d_model_load("rom:/RoundButtonTop.t3dm");
        map->buttonTopCollision = collision_find("RoundButtonTop");
        map->buttonTopLoaded = true;
        debugf("roundbutton_init: model=%p collision=%p\n",
            (void*)map->buttonTopModel, (void*)map->buttonTopCollision);
    }
}

static void roundbutton_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    // Debug: confirm update is running (print once per button per second)
    static int updateDbgFrame = 0;
    updateDbgFrame++;
    if ((updateDbgFrame % 30) == 0) {
        float dx = px - inst->posX;
        float dz = pz - inst->posZ;
        float distXZ = sqrtf(dx*dx + dz*dz);
        debugf("BTN_UPD[%d]: dist=%.0f btnY=%.0f playerY=%.0f\n",
            inst->activationId, distXZ, inst->posY, py);
    }

    // Check if player is standing on the button top using collision mesh
    bool playerOnButton = false;
    float dx = px - inst->posX;
    float dz = pz - inst->posZ;
    float distXZ = sqrtf(dx*dx + dz*dz);
    float topY = inst->posY - inst->state.button.pressDepth;

    if (map->buttonTopCollision) {
        float groundY = collision_get_ground_height_rotated(map->buttonTopCollision,
            px, py, pz,
            inst->posX, topY, inst->posZ,
            inst->scaleX, inst->scaleY, inst->scaleZ,
            inst->rotY);

        // Player is on button if collision found a surface near player's feet
        if (groundY > -9000.0f && py >= groundY - 2.0f && py <= groundY + BUTTON_DETECT_HEIGHT) {
            playerOnButton = true;
        }

        // Fallback: simple radius check if collision detection fails but player is close
        // This handles edge cases where collision mesh might miss due to rotation/scale
        if (!playerOnButton) {
            float scaledRadius = BUTTON_DETECT_RADIUS * inst->scaleX;
            if (distXZ < scaledRadius &&
                py >= topY - 2.0f &&
                py <= topY + BUTTON_DETECT_HEIGHT) {
                playerOnButton = true;
            }
        }
    } else {
        // No collision loaded - use simple radius fallback
        float scaledRadius = BUTTON_DETECT_RADIUS * inst->scaleX;
        if (distXZ < scaledRadius &&
            py >= topY - 2.0f &&
            py <= topY + BUTTON_DETECT_HEIGHT) {
            playerOnButton = true;
        }
    }

    // Update pressed state
    bool wasPressed = inst->state.button.pressed;
    inst->state.button.pressed = playerOnButton;

    // Animate press depth
    float targetDepth = playerOnButton ? BUTTON_PRESS_DEPTH : 0.0f;
    float speed = playerOnButton ? BUTTON_PRESS_SPEED : BUTTON_RELEASE_SPEED;

    if (inst->state.button.pressDepth < targetDepth) {
        inst->state.button.pressDepth += speed * deltaTime;
        if (inst->state.button.pressDepth > targetDepth) {
            inst->state.button.pressDepth = targetDepth;
        }
    } else if (inst->state.button.pressDepth > targetDepth) {
        inst->state.button.pressDepth -= speed * deltaTime;
        if (inst->state.button.pressDepth < targetDepth) {
            inst->state.button.pressDepth = targetDepth;
        }
    }

    // Toggle activation when button becomes fully pressed (not while held)
    if (inst->activationId > 0) {
        bool fullyPressed = (inst->state.button.pressDepth >= BUTTON_PRESS_DEPTH * 0.9f);

        // Toggle on rising edge (just became fully pressed)
        if (fullyPressed && !wasPressed) {
            bool currentState = activation_get(inst->activationId);
            activation_set(inst->activationId, !currentState);
            DECO_DEBUG("Button toggled! activationId=%d now=%d\n", inst->activationId, !currentState);
        }
    }
}

// ============================================================
// LASER WALL BEHAVIOR
// ============================================================
// Laser walls are ON by default and damage the player on contact.
// They can be deactivated via the activation system (linked to buttons).
// When ON: Shows full laser wall with beams, deals 3 damage (instant death)
// When OFF: Shows only the frame (no lasers), no damage

static void laserwall_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Laser is ON by default (deals damage when player touches)
    // When activationId is set and that ID is activated, laser turns OFF
    inst->state.laser.isOn = true;
}

static void laserwall_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;
    (void)deltaTime;

    // Update laser state based on activation system
    // Default: ON (isOn = true)
    // If activationId is set and that ID is activated (e.g., button pressed): OFF
    if (inst->activationId > 0) {
        inst->state.laser.isOn = !activation_get(inst->activationId);
    } else {
        inst->state.laser.isOn = true;  // No activation link = always on
    }
}

static void laserwall_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)px;
    (void)py;
    (void)pz;
    (void)map;

    // Only deal damage if laser is ON
    if (!inst->state.laser.isOn) {
        return;  // Laser is off, no damage
    }

    extern void player_take_damage(int damage);
    extern bool player_is_dead(void);

    if (!player_is_dead()) {
        // Deal 3 damage (which is instant death since max health is 3)
        player_take_damage(3);
    }
}

// ============================================================
// FAN BEHAVIOR
// ============================================================
// Fan bottom is static, fan top rotates when active.
// Blows player upward when standing above the fan.

#define FAN_SPIN_SPEED 15.0f           // Radians per second when active
#define FAN_BLOW_RADIUS 70.0f          // Horizontal radius of air stream (matches propeller size)
#define FAN_BLOW_HEIGHT 80.0f          // How high the air stream reaches (reduced)
#define FAN_BLOW_FORCE 75.0f           // Upward force on player (strong lift)
#define FAN2_BLOW_FORCE 50.0f          // Upward force for FAN2 (half of FAN)

static void fan_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    debugf("fan_init called for instance at (%.1f, %.1f, %.1f) activationId=%d\n",
           inst->posX, inst->posY, inst->posZ, inst->activationId);
    inst->state.fan.spinAngle = 0.0f;
    inst->state.fan.playerInStream = false;
    debugf("fan_init done\n");
}

static void fan_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Check if fan is active (default ON, activation turns it OFF)
    bool isActive = true;
    if (inst->activationId > 0 && inst->activationId < MAX_ACTIVATION_IDS) {
        if (activation_get(inst->activationId)) {
            isActive = false;  // Button pressed = fan off
        }
    }

    inst->state.fan.playerInStream = false;

    if (!isActive) {
        return;  // Fan is off
    }

    // Spin the fan top
    inst->state.fan.spinAngle += FAN_SPIN_SPEED * deltaTime;
    if (inst->state.fan.spinAngle > 6.283185f) {
        inst->state.fan.spinAngle -= 6.283185f;
    }

    // Check if player is in the air stream (above the fan within radius)
    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    float dx = px - inst->posX;
    float dz = pz - inst->posZ;
    float horizDistSq = dx * dx + dz * dz;
    float radiusSq = FAN_BLOW_RADIUS * FAN_BLOW_RADIUS * inst->scaleX * inst->scaleX;

    // Player must be within horizontal radius and above the fan (up to blow height)
    float fanTopY = inst->posY + 10.0f * inst->scaleY;  // Approximate top of fan
    if (horizDistSq < radiusSq && py >= fanTopY && py < fanTopY + FAN_BLOW_HEIGHT * inst->scaleY) {
        inst->state.fan.playerInStream = true;
    }
}

// ============================================================
// FAN2 BEHAVIOR (Single model fan with wind)
// ============================================================
// Same wind behavior as original fan, but single-model design.
// Model rotates around Y axis to create spinning blade effect.

#define FAN2_TEXTURE_SPEED 0.05f  // Texture scroll speed (cycles per second) - upward wind effect
static float g_fan2Offset = 0.0f;  // Global shared offset for all fan2 instances
static inline float fan2_get_offset(void) { return g_fan2Offset; }

static void fan2_init(DecoInstance* inst, MapRuntime* map) {
    debugf("fan2_init called for instance at (%.1f, %.1f, %.1f) activationId=%d\n",
           inst->posX, inst->posY, inst->posZ, inst->activationId);
    inst->state.fan2.spinAngle = 0.0f;
    inst->state.fan2.playerInStream = false;

    // Create per-instance skeleton and load "gust" animation
    DecoTypeRuntime* fan2Type = map_get_deco_type(map, DECO_FAN2);
    debugf("fan2_init: fan2Type=%p model=%p hasSkeleton=%d\n",
           fan2Type, fan2Type ? fan2Type->model : NULL,
           fan2Type ? fan2Type->hasSkeleton : 0);

    if (fan2Type && fan2Type->model) {
        // Check if model has skeleton
        const T3DChunkSkeleton* skelChunk = t3d_model_get_skeleton(fan2Type->model);
        debugf("fan2_init: skelChunk=%p\n", skelChunk);

        if (skelChunk) {
            inst->skeleton = t3d_skeleton_create(fan2Type->model);
            inst->hasOwnSkeleton = true;
            inst->animCount = 0;

            // Load the gust animation
            T3DAnim anim = t3d_anim_create(fan2Type->model, "gust");
            debugf("fan2_init: anim.animRef=%p\n", anim.animRef);
            if (anim.animRef) {
                t3d_anim_attach(&anim, &inst->skeleton);
                t3d_anim_set_looping(&anim, true);
                t3d_anim_set_playing(&anim, true);
                inst->anims[inst->animCount++] = anim;
                debugf("fan2_init: loaded gust animation successfully\n");
            } else {
                debugf("fan2_init: ERROR - gust animation not found in model\n");
            }
        } else {
            debugf("fan2_init: ERROR - model has no skeleton\n");
        }
    }
    debugf("fan2_init done\n");
}

static void fan2_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Check if fan is active (default ON, activation turns it OFF)
    bool isActive = true;
    if (inst->activationId > 0 && inst->activationId < MAX_ACTIVATION_IDS) {
        if (activation_get(inst->activationId)) {
            isActive = false;  // Button pressed = fan off
        }
    }

    inst->state.fan2.playerInStream = false;

    if (!isActive) {
        return;  // Fan is off
    }

    // Update animation
    if (inst->hasOwnSkeleton && inst->animCount > 0) {
        t3d_anim_update(&inst->anims[0], deltaTime);
        t3d_skeleton_update(&inst->skeleton);
    }

    // Check if player is in the air stream (at or above the fan within radius)
    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    float dx = px - inst->posX;
    float dz = pz - inst->posZ;
    float horizDistSq = dx * dx + dz * dz;
    float radiusSq = FAN_BLOW_RADIUS * FAN_BLOW_RADIUS * inst->scaleX * inst->scaleX;

    // Player must be within horizontal radius and near/above the fan (up to blow height)
    // Allow detection slightly below fan top so grounded players get lifted
    float fanTopY = inst->posY + 10.0f * inst->scaleY;  // Approximate top of fan
    float liftZone = 30.0f * inst->scaleY;  // How far below fan top to still lift player
    if (horizDistSq < radiusSq && py >= (fanTopY - liftZone) && py < fanTopY + FAN_BLOW_HEIGHT * inst->scaleY) {
        inst->state.fan2.playerInStream = true;
    }
}

// ============================================================
// CONVEYOR BELT BEHAVIOR
// ============================================================
// Pushes player along belt direction when activated.
// Texture scrolls to show belt movement.

#define CONVEYOR_SPEED 30.0f           // Units per second to push player
#define CONVEYOR_TEXTURE_SPEED 1.5f    // Texture scroll speed (cycles per second)
#define CONVEYOR_DETECT_RADIUS 40.0f   // How wide the belt is for player detection
#define CONVEYOR_DETECT_LENGTH 80.0f   // How long the belt is
#define CONVEYOR_DETECT_HEIGHT 15.0f   // Height above belt to detect player

static float g_conveyorOffset = 0.0f;  // Global shared offset for all conveyor belts
static inline float conveyor_get_offset(void) { return g_conveyorOffset; }

static void conveyor_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.conveyor.speed = CONVEYOR_SPEED;
    inst->state.conveyor.playerOnBelt = false;

    // Calculate push direction based on decoration's Y rotation
    // Models are rotated 90° on import, so local X becomes world Z
    float angle = inst->rotY;
    inst->state.conveyor.pushX = -cosf(angle);
    inst->state.conveyor.pushZ = -sinf(angle);
}

static void conveyor_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Check if conveyor is activated (via activation system)
    bool isActive = true;  // Default on if no activationId
    if (inst->activationId > 0) {
        isActive = activation_get(inst->activationId);
    }

    inst->state.conveyor.playerOnBelt = false;

    if (!isActive) {
        return;  // Conveyor is off - no movement, no texture scroll
    }

    // Scroll texture (global offset shared by all conveyors)
    g_conveyorOffset += CONVEYOR_TEXTURE_SPEED * deltaTime;
    if (g_conveyorOffset >= 1.0f) {
        g_conveyorOffset -= 1.0f;
    }

    // Check if player is on the conveyor - use belt collision mesh for ground check
    float px = map->playerX;
    float py = map->playerY;
    float pz = map->playerZ;

    // Use the belt collision (has the belt surface)
    if (!map->conveyorBeltCollision) return;

    // Check if player is standing on the conveyor using belt collision
    float groundY = collision_get_ground_height_rotated(map->conveyorBeltCollision,
        px, py, pz,
        inst->posX, inst->posY, inst->posZ,
        inst->scaleX, inst->scaleY, inst->scaleZ,
        inst->rotY);

    // Player is on belt if collision found a surface near player's feet
    if (groundY > -9000.0f && py >= groundY - 2.0f && py <= groundY + CONVEYOR_DETECT_HEIGHT) {
        inst->state.conveyor.playerOnBelt = true;
    }
}

// ============================================================
// DIALOGUE TRIGGER BEHAVIOR
// ============================================================
// Invisible trigger zone that activates a dialogue script when player enters.
// Uses scriptId to reference which script to run (scene must handle this).
// The triggered flag is set when player enters; scene checks and resets it.

#define DIALOGUE_TRIGGER_DEFAULT_RADIUS 50.0f

static void dialoguetrigger_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Use scaleX as radius multiplier, default radius if scale is ~1
    inst->state.dialogueTrigger.triggerRadius = DIALOGUE_TRIGGER_DEFAULT_RADIUS * inst->scaleX;
    // scriptId is set by level loader, don't overwrite here
    inst->state.dialogueTrigger.triggered = false;
    inst->state.dialogueTrigger.onceOnly = false;  // Set by level loader if needed
    inst->state.dialogueTrigger.hasTriggered = false;

    DECO_DEBUG("DialogueTrigger init at (%.1f, %.1f, %.1f) radius=%.1f scriptId=%d onceOnly=%d\n",
           inst->posX, inst->posY, inst->posZ,
           inst->state.dialogueTrigger.triggerRadius,
           inst->state.dialogueTrigger.scriptId,
           inst->state.dialogueTrigger.onceOnly);
}

static void dialoguetrigger_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)deltaTime;

    // Skip if already triggered and once-only
    if (inst->state.dialogueTrigger.onceOnly && inst->state.dialogueTrigger.hasTriggered) {
        return;
    }

    // Skip if currently in triggered state (waiting for scene to handle)
    if (inst->state.dialogueTrigger.triggered) {
        return;
    }

    // Check distance to player
    float dx = map->playerX - inst->posX;
    float dy = map->playerY - inst->posY;
    float dz = map->playerZ - inst->posZ;
    float distSq = dx * dx + dy * dy + dz * dz;
    float radiusSq = inst->state.dialogueTrigger.triggerRadius * inst->state.dialogueTrigger.triggerRadius;

    if (distSq < radiusSq) {
        inst->state.dialogueTrigger.triggered = true;
        if (inst->state.dialogueTrigger.onceOnly) {
            inst->state.dialogueTrigger.hasTriggered = true;
        }
        DECO_DEBUG("DialogueTrigger ACTIVATED scriptId=%d\n", inst->state.dialogueTrigger.scriptId);
    }
}

// ============================================================
// INTERACT TRIGGER BEHAVIOR
// ============================================================
// Optional dialogue trigger - shows A button prompt when player is in range.
// Player must press A to start dialogue. Player rotates to face lookAtAngle
// during dialogue, then rotates back to original angle when dialogue ends.

#define INTERACT_TRIGGER_DEFAULT_RADIUS 50.0f

static void interacttrigger_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Use scaleX as radius multiplier, default radius if scale is ~1
    inst->state.interactTrigger.triggerRadius = INTERACT_TRIGGER_DEFAULT_RADIUS * inst->scaleX;
    // scriptId is set by level loader, don't overwrite here
    // Use rotY as look direction (the angle player faces when interacting)
    inst->state.interactTrigger.lookAtAngle = inst->rotY;
    inst->state.interactTrigger.playerInRange = false;
    inst->state.interactTrigger.interacting = false;
    inst->state.interactTrigger.savedPlayerAngle = 0.0f;
    inst->state.interactTrigger.onceOnly = false;  // Set by level loader if needed
    inst->state.interactTrigger.hasTriggered = false;

    debugf("InteractTrigger init at (%.1f, %.1f, %.1f) radius=%.1f scriptId=%d lookAt=%.1f onceOnly=%d\n",
           inst->posX, inst->posY, inst->posZ,
           inst->state.interactTrigger.triggerRadius,
           inst->state.interactTrigger.scriptId,
           inst->state.interactTrigger.lookAtAngle,
           inst->state.interactTrigger.onceOnly);
}

static void interacttrigger_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)deltaTime;

    // Skip if already triggered and once-only
    if (inst->state.interactTrigger.onceOnly && inst->state.interactTrigger.hasTriggered) {
        inst->state.interactTrigger.playerInRange = false;
        return;
    }

    // Skip updating playerInRange if currently interacting (dialogue active)
    if (inst->state.interactTrigger.interacting) {
        return;
    }

    // Check distance to player
    float dx = map->playerX - inst->posX;
    float dy = map->playerY - inst->posY;
    float dz = map->playerZ - inst->posZ;
    float distSq = dx * dx + dy * dy + dz * dz;
    float radiusSq = inst->state.interactTrigger.triggerRadius * inst->state.interactTrigger.triggerRadius;

    inst->state.interactTrigger.playerInRange = (distSq < radiusSq);
}

// ============================================================
// TOXIC PIPE BEHAVIOR
// ============================================================
// Pipe with liquid that has a scrolling texture effect.
// The pipe is static, but the liquid texture scrolls.

#define TOXIC_PIPE_TEXTURE_SPEED 0.5f  // Texture scroll speed (cycles per second)

static void toxicpipe_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.toxicPipe.textureOffset = 0.0f;
    debugf("ToxicPipe init at (%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ);
}

static void toxicpipe_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;
    // Scroll texture
    inst->state.toxicPipe.textureOffset += TOXIC_PIPE_TEXTURE_SPEED * deltaTime;
    if (inst->state.toxicPipe.textureOffset >= 1.0f) {
        inst->state.toxicPipe.textureOffset -= 1.0f;
    }
}

// ============================================================
// TOXIC RUNNING BEHAVIOR
// ============================================================
// Toxic liquid with a scrolling texture effect.

#define TOXIC_RUNNING_TEXTURE_SPEED 0.5f  // Texture scroll speed (cycles per second)

static void toxicrunning_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.toxicRunning.textureOffset = 0.0f;
    debugf("ToxicRunning init at (%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ);
}

static void toxicrunning_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;
    // Scroll texture
    inst->state.toxicRunning.textureOffset += TOXIC_RUNNING_TEXTURE_SPEED * deltaTime;
    if (inst->state.toxicRunning.textureOffset >= 1.0f) {
        inst->state.toxicRunning.textureOffset -= 1.0f;
    }
}

// ============================================================
// LAVAFLOOR BEHAVIOR
// ============================================================
// Lava floor with scrolling texture effect (similar to toxic running).
// Uses global shared offset so all instances animate together.

#define LAVAFLOOR_TEXTURE_SPEED 0.025f  // Texture scroll speed (cycles per second)
static float g_lavaFloorOffset = 0.0f;  // Global shared offset for all lava floor instances
static inline float lavafloor_get_offset(void) { return g_lavaFloorOffset; }

static void lavafloor_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    (void)inst;
    // Uses global offset, no per-instance state needed
}

static void lavafloor_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;
    (void)inst;
    // Update global offset (will be called multiple times per frame but that's fine)
    g_lavaFloorOffset += LAVAFLOOR_TEXTURE_SPEED * deltaTime;
    if (g_lavaFloorOffset >= 1.0f) {
        g_lavaFloorOffset -= 1.0f;
    }
}

// ============================================================
// LAVAFALLS BEHAVIOR
// ============================================================
// Lava waterfall with scrolling texture effect.
// Uses global shared offset so all instances animate together.

#define LAVAFALLS_TEXTURE_SPEED 0.05f  // Texture scroll speed (cycles per second)
static float g_lavaFallsOffset = 0.0f;  // Global shared offset for all lava falls instances
static inline float lavafalls_get_offset(void) { return g_lavaFallsOffset; }

static void lavafalls_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    (void)inst;
    // Uses global offset, no per-instance state needed
}

static void lavafalls_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;
    (void)inst;
    // Update global offset (will be called multiple times per frame but that's fine)
    g_lavaFallsOffset += LAVAFALLS_TEXTURE_SPEED * deltaTime;
    if (g_lavaFallsOffset >= 1.0f) {
        g_lavaFallsOffset -= 1.0f;
    }
}

// ============================================================
// LEVEL 3 STREAM BEHAVIOR
// ============================================================
// Poison stream/waterfall with scrolling textures. Damages player on contact.
// Rendered at origin, always visible and collidable.

#define LVL3STREAM_TEXTURE_SPEED 0.08f  // Texture scroll speed (cycles per second) - downward flow
static float g_lvl3StreamOffset = 0.0f;  // Global shared offset for all instances
static inline float lvl3stream_get_offset(void) { return g_lvl3StreamOffset; }

static void lvl3stream_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    (void)inst;
    // Uses global offset, no per-instance state needed
}

static void lvl3stream_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;
    (void)inst;
    // Update global offset (will be called multiple times per frame but that's fine)
    g_lvl3StreamOffset += LVL3STREAM_TEXTURE_SPEED * deltaTime;
    if (g_lvl3StreamOffset >= 1.0f) {
        g_lvl3StreamOffset -= 1.0f;
    }
}

static void lvl3stream_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    extern void player_take_damage(int damage);
    extern bool player_is_dead(void);

    // Apply poison damage (1 damage)
    if (!player_is_dead()) {
        player_take_damage(1);
    }
}

// ============================================================
// GRINDER BEHAVIOR
// ============================================================
// Rotating grinder, spins around Z-axis like COG does.

#define GRINDER_ROTATION_SPEED 2.4f  // Radians per second (2x faster than cog)

static void grinder_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.grinder.rotSpeed = GRINDER_ROTATION_SPEED;
    debugf("Grinder init at (%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ);
}

static void grinder_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;

    // Spin grinder around Z axis (same as cog)
    const float TWO_PI = 6.28318530718f;
    float deltaAngle = inst->state.grinder.rotSpeed * deltaTime;

    inst->rotZ -= deltaAngle;
    while (inst->rotZ >= TWO_PI) inst->rotZ -= TWO_PI;
    while (inst->rotZ < 0.0f) inst->rotZ += TWO_PI;
}

static void grinder_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)inst; (void)map; (void)px; (void)py; (void)pz;
    extern void player_take_damage(int damage);
    extern bool player_is_dead(void);

    if (!player_is_dead()) {
        player_take_damage(1);
    }
}

// ============================================================
// CUTSCENE FALLOFF BEHAVIOR
// ============================================================
// Invisible trigger zone that starts a cutscene when player enters.
// The cutscene shows the robot walking and falling off a cliff, losing body parts.

#define CUTSCENE_FALLOFF_TRIGGER_RADIUS 80.0f  // Default trigger radius

// Global flag for game.c to check
static bool g_cutsceneFalloffTriggered = false;
static float g_cutsceneFalloffX = 0.0f;
static float g_cutsceneFalloffY = 0.0f;
static float g_cutsceneFalloffZ = 0.0f;
static float g_cutsceneFalloffRotY = 0.0f;

static void cutscene_falloff_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.cutsceneFalloff.triggerRadius = CUTSCENE_FALLOFF_TRIGGER_RADIUS * inst->scaleX;
    inst->state.cutsceneFalloff.triggered = false;
    inst->state.cutsceneFalloff.playing = false;
    inst->state.cutsceneFalloff.animTime = 0.0f;
    debugf("CutsceneFalloff init at (%.1f, %.1f, %.1f) radius=%.1f\n",
           inst->posX, inst->posY, inst->posZ,
           inst->state.cutsceneFalloff.triggerRadius);
}

static void cutscene_falloff_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)deltaTime;

    // Already triggered - nothing to do
    if (inst->state.cutsceneFalloff.triggered) {
        return;
    }

    // Check if player is within trigger radius
    float dx = map->playerX - inst->posX;
    float dy = map->playerY - inst->posY;
    float dz = map->playerZ - inst->posZ;
    float distSq = dx*dx + dy*dy + dz*dz;
    float radiusSq = inst->state.cutsceneFalloff.triggerRadius * inst->state.cutsceneFalloff.triggerRadius;

    if (distSq < radiusSq) {
        inst->state.cutsceneFalloff.triggered = true;
        inst->state.cutsceneFalloff.playing = true;

        // Set global flag for game.c to pick up
        g_cutsceneFalloffTriggered = true;
        g_cutsceneFalloffX = inst->posX;
        g_cutsceneFalloffY = inst->posY;
        g_cutsceneFalloffZ = inst->posZ;
        g_cutsceneFalloffRotY = inst->rotY;

        debugf("CUTSCENE FALLOFF TRIGGERED at (%.1f, %.1f, %.1f)!\n",
               inst->posX, inst->posY, inst->posZ);
    }
}

// ============================================================
// CUTSCENE 2 (SLIDESHOW) TRIGGER
// ============================================================
// Global flag for game.c to detect CS2 trigger (defined in game.c)
extern bool g_cs2Triggered;

static void cs2_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map; (void)px; (void)py; (void)pz;

    // Only trigger once
    if (g_cs2Triggered) return;

    g_cs2Triggered = true;
    inst->active = false;  // Deactivate trigger so it doesn't keep firing
    debugf("CUTSCENE 2 TRIGGERED!\n");
}

// ============================================================
// CUTSCENE 3 (ENDING SLIDESHOW) TRIGGER
// ============================================================
// Global flag for game.c to detect CS3 trigger (defined in game.c)
extern bool g_cs3Triggered;

static void cs3_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map; (void)px; (void)py; (void)pz;

    // Only trigger once
    if (g_cs3Triggered) return;

    g_cs3Triggered = true;
    inst->active = false;  // Deactivate trigger so it doesn't keep firing
    debugf("CUTSCENE 3 (ENDING) TRIGGERED!\n");
}

// ============================================================
// SINK PLATFORM BEHAVIOR
// ============================================================
// Platform that sinks when player stands on it, rises back up when they leave.

#define SINK_PLAT_DEFAULT_DEPTH 200.0f  // How far to sink (units) - deep enough to fall through
#define SINK_PLAT_SINK_SPEED 30.0f      // How fast to sink (units/sec)
#define SINK_PLAT_RISE_SPEED 30.0f      // How fast to rise back (units/sec)

static void sinkplat_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.sinkPlat.originalY = inst->posY;
    inst->state.sinkPlat.currentY = 0.0f;
    inst->state.sinkPlat.sinkDepth = SINK_PLAT_DEFAULT_DEPTH * inst->scaleY;
    inst->state.sinkPlat.sinkSpeed = SINK_PLAT_SINK_SPEED;
    inst->state.sinkPlat.riseSpeed = SINK_PLAT_RISE_SPEED;
    inst->state.sinkPlat.lastDelta = 0.0f;
    inst->state.sinkPlat.playerOnPlat = false;
    debugf("SinkPlat init at (%.1f, %.1f, %.1f) depth=%.1f\n",
           inst->posX, inst->posY, inst->posZ, inst->state.sinkPlat.sinkDepth);
}

static void sinkplat_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Check if player is standing on this platform by checking collision mesh triangles
    DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
    bool playerOn = false;

    if (decoType && decoType->collision) {
        CollisionMesh* mesh = decoType->collision;
        float px = map->playerX;
        float py = map->playerY;
        float pz = map->playerZ;

        // Pre-calculate rotation values for transforming vertices
        float cosR = fm_cosf(inst->rotY);
        float sinR = fm_sinf(inst->rotY);

        // Check each triangle to see if player is standing on it
        for (int i = 0; i < mesh->count && !playerOn; i++) {
            CollisionTriangle* t = &mesh->triangles[i];

            // Transform triangle to world space: scale -> rotate Y -> translate
            // Vertex 0
            float sx0 = t->x0 * inst->scaleX;
            float sz0 = t->z0 * inst->scaleZ;
            float x0 = sx0 * cosR - sz0 * sinR + inst->posX;
            float y0 = t->y0 * inst->scaleY + inst->posY;
            float z0 = sx0 * sinR + sz0 * cosR + inst->posZ;
            // Vertex 1
            float sx1 = t->x1 * inst->scaleX;
            float sz1 = t->z1 * inst->scaleZ;
            float x1 = sx1 * cosR - sz1 * sinR + inst->posX;
            float y1 = t->y1 * inst->scaleY + inst->posY;
            float z1 = sx1 * sinR + sz1 * cosR + inst->posZ;
            // Vertex 2
            float sx2 = t->x2 * inst->scaleX;
            float sz2 = t->z2 * inst->scaleZ;
            float x2 = sx2 * cosR - sz2 * sinR + inst->posX;
            float y2 = t->y2 * inst->scaleY + inst->posY;
            float z2 = sx2 * sinR + sz2 * cosR + inst->posZ;

            // AABB culling
            float minX = fminf(fminf(x0, x1), x2);
            float maxX = fmaxf(fmaxf(x0, x1), x2);
            float minZ = fminf(fminf(z0, z1), z2);
            float maxZ = fmaxf(fmaxf(z0, z1), z2);
            if (px < minX - 5.0f || px > maxX + 5.0f || pz < minZ - 5.0f || pz > maxZ + 5.0f) continue;

            // Check if point is inside triangle (2D projection onto XZ plane)
            float v0x = x2 - x0, v0z = z2 - z0;
            float v1x = x1 - x0, v1z = z1 - z0;
            float v2x = px - x0, v2z = pz - z0;

            float dot00 = v0x * v0x + v0z * v0z;
            float dot01 = v0x * v1x + v0z * v1z;
            float dot02 = v0x * v2x + v0z * v2z;
            float dot11 = v1x * v1x + v1z * v1z;
            float dot12 = v1x * v2x + v1z * v2z;

            float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
            float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
            float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

            if (u >= -0.1f && v >= -0.1f && (u + v) <= 1.1f) {
                // Inside triangle - interpolate Y
                float triY = y0 + u * (y2 - y0) + v * (y1 - y0);

                // Check if player is standing on this triangle
                if (py >= triY - 5.0f && py <= triY + 25.0f) {
                    playerOn = true;
                }
            }
        }
    }

    inst->state.sinkPlat.playerOnPlat = playerOn;

    float prevY = inst->state.sinkPlat.currentY;

    if (playerOn) {
        // Sink down
        inst->state.sinkPlat.currentY += inst->state.sinkPlat.sinkSpeed * deltaTime;
        if (inst->state.sinkPlat.currentY > inst->state.sinkPlat.sinkDepth) {
            inst->state.sinkPlat.currentY = inst->state.sinkPlat.sinkDepth;
        }
    } else {
        // Rise back up
        inst->state.sinkPlat.currentY -= inst->state.sinkPlat.riseSpeed * deltaTime;
        if (inst->state.sinkPlat.currentY < 0.0f) {
            inst->state.sinkPlat.currentY = 0.0f;
        }
    }

    // Calculate delta for carrying player
    inst->state.sinkPlat.lastDelta = -(inst->state.sinkPlat.currentY - prevY);

    // Update visual position
    inst->posY = inst->state.sinkPlat.originalY - inst->state.sinkPlat.currentY;
}

// ============================================================
// SPINNER BEHAVIOR
// ============================================================
// Spinning spinner decoration - rotates around Y axis.

#define SPINNER_ROTATION_SPEED 2.0f  // Radians per second (moderate spin)

static void spinner_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.spinner.rotSpeed = SPINNER_ROTATION_SPEED;
    debugf("Spinner init at (%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ);
}

static void spinner_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;

    // Spin spinner around Y axis (like a top)
    const float TWO_PI = 6.28318530718f;
    float deltaAngle = inst->state.spinner.rotSpeed * deltaTime;

    inst->rotY += deltaAngle;
    while (inst->rotY >= TWO_PI) inst->rotY -= TWO_PI;
    while (inst->rotY < 0.0f) inst->rotY += TWO_PI;
}

// ============================================================
// SPIN ROCK BEHAVIOR
// ============================================================
// Rock that spins slowly AND sinks when player stands on it.
// Combines spinner rotation with sink plat sinking.

#define SPIN_ROCK_ROTATION_SPEED 0.5f  // Slower than spinner (radians per second)

static void spinrock_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.spinRock.originalY = inst->posY;
    inst->state.spinRock.currentY = 0.0f;
    inst->state.spinRock.sinkDepth = SINK_PLAT_DEFAULT_DEPTH * inst->scaleY;
    inst->state.spinRock.sinkSpeed = SINK_PLAT_SINK_SPEED;
    inst->state.spinRock.riseSpeed = SINK_PLAT_RISE_SPEED;
    inst->state.spinRock.lastDelta = 0.0f;
    inst->state.spinRock.playerOnPlat = false;
    inst->state.spinRock.rotSpeed = SPIN_ROCK_ROTATION_SPEED;
    debugf("SpinRock init at (%.1f, %.1f, %.1f) depth=%.1f\n",
           inst->posX, inst->posY, inst->posZ, inst->state.spinRock.sinkDepth);
}

static void spinrock_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // === ROTATION (like spinner but slower) ===
    const float TWO_PI = 6.28318530718f;
    float deltaAngle = inst->state.spinRock.rotSpeed * deltaTime;
    inst->rotY += deltaAngle;
    while (inst->rotY >= TWO_PI) inst->rotY -= TWO_PI;
    while (inst->rotY < 0.0f) inst->rotY += TWO_PI;

    // === SINKING (like sink plat) ===
    // Check if player is standing on this platform by checking collision mesh triangles
    DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
    bool playerOn = false;

    if (decoType && decoType->collision) {
        CollisionMesh* mesh = decoType->collision;
        float px = map->playerX;
        float py = map->playerY;
        float pz = map->playerZ;

        // Pre-calculate rotation values for transforming vertices
        float cosR = fm_cosf(inst->rotY);
        float sinR = fm_sinf(inst->rotY);

        // Check each triangle to see if player is standing on it
        for (int i = 0; i < mesh->count && !playerOn; i++) {
            CollisionTriangle* t = &mesh->triangles[i];

            // Transform triangle to world space: scale -> rotate Y -> translate
            float sx0 = t->x0 * inst->scaleX;
            float sz0 = t->z0 * inst->scaleZ;
            float x0 = sx0 * cosR - sz0 * sinR + inst->posX;
            float y0 = t->y0 * inst->scaleY + inst->posY;
            float z0 = sx0 * sinR + sz0 * cosR + inst->posZ;
            float sx1 = t->x1 * inst->scaleX;
            float sz1 = t->z1 * inst->scaleZ;
            float x1 = sx1 * cosR - sz1 * sinR + inst->posX;
            float y1 = t->y1 * inst->scaleY + inst->posY;
            float z1 = sx1 * sinR + sz1 * cosR + inst->posZ;
            float sx2 = t->x2 * inst->scaleX;
            float sz2 = t->z2 * inst->scaleZ;
            float x2 = sx2 * cosR - sz2 * sinR + inst->posX;
            float y2 = t->y2 * inst->scaleY + inst->posY;
            float z2 = sx2 * sinR + sz2 * cosR + inst->posZ;

            // AABB culling
            float minX = fminf(fminf(x0, x1), x2);
            float maxX = fmaxf(fmaxf(x0, x1), x2);
            float minZ = fminf(fminf(z0, z1), z2);
            float maxZ = fmaxf(fmaxf(z0, z1), z2);
            if (px < minX - 5.0f || px > maxX + 5.0f || pz < minZ - 5.0f || pz > maxZ + 5.0f) continue;

            // Check if point is inside triangle (2D projection onto XZ plane)
            float v0x = x2 - x0, v0z = z2 - z0;
            float v1x = x1 - x0, v1z = z1 - z0;
            float v2x = px - x0, v2z = pz - z0;

            float dot00 = v0x * v0x + v0z * v0z;
            float dot01 = v0x * v1x + v0z * v1z;
            float dot02 = v0x * v2x + v0z * v2z;
            float dot11 = v1x * v1x + v1z * v1z;
            float dot12 = v1x * v2x + v1z * v2z;

            float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
            float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
            float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

            if (u >= -0.1f && v >= -0.1f && (u + v) <= 1.1f) {
                // Inside triangle - interpolate Y
                float triY = y0 + u * (y2 - y0) + v * (y1 - y0);

                // Check if player is standing on this triangle
                if (py >= triY - 5.0f && py <= triY + 25.0f) {
                    playerOn = true;
                }
            }
        }
    }

    inst->state.spinRock.playerOnPlat = playerOn;

    float prevY = inst->state.spinRock.currentY;

    if (playerOn) {
        // Sink down
        inst->state.spinRock.currentY += inst->state.spinRock.sinkSpeed * deltaTime;
        if (inst->state.spinRock.currentY > inst->state.spinRock.sinkDepth) {
            inst->state.spinRock.currentY = inst->state.spinRock.sinkDepth;
        }
    } else {
        // Rise back up
        inst->state.spinRock.currentY -= inst->state.spinRock.riseSpeed * deltaTime;
        if (inst->state.spinRock.currentY < 0.0f) {
            inst->state.spinRock.currentY = 0.0f;
        }
    }

    // Calculate delta for carrying player
    inst->state.spinRock.lastDelta = -(inst->state.spinRock.currentY - prevY);

    // Update visual position
    inst->posY = inst->state.spinRock.originalY - inst->state.spinRock.currentY;
}

// ============================================================
// MOVING PLATFORM BEHAVIOR
// ============================================================
// Platform that moves between start position and target position.
// Uses same triangle-based collision detection as sink plat.

#define MOVE_PLAT_DEFAULT_SPEED 60.0f  // Units per second

static void moveplat_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Store starting position
    inst->state.movePlat.startX = inst->posX;
    inst->state.movePlat.startY = inst->posY;
    inst->state.movePlat.startZ = inst->posZ;
    // DON'T overwrite target - it was already set by level_load from platformTarget fields
    // Only set defaults if target wasn't configured (all zeros means unconfigured)
    if (inst->state.movePlat.targetX == 0.0f &&
        inst->state.movePlat.targetY == 0.0f &&
        inst->state.movePlat.targetZ == 0.0f) {
        inst->state.movePlat.targetX = inst->posX;
        inst->state.movePlat.targetY = inst->posY;
        inst->state.movePlat.targetZ = inst->posZ;
    }
    inst->state.movePlat.progress = 0.0f;
    // Only set default speed if not already configured
    if (inst->state.movePlat.speed <= 0.0f) {
        inst->state.movePlat.speed = MOVE_PLAT_DEFAULT_SPEED;
    }
    inst->state.movePlat.direction = 1;  // Start moving toward target
    inst->state.movePlat.lastDeltaX = 0.0f;
    inst->state.movePlat.lastDeltaY = 0.0f;
    inst->state.movePlat.lastDeltaZ = 0.0f;
    inst->state.movePlat.playerOnPlat = false;
    // Only set activated = true if not already set to false (by startStationary in level_load)
    // Check: if activationId is set and activated is false, it was set by startStationary - don't overwrite
    if (inst->activationId == 0 || inst->state.movePlat.activated != false) {
        inst->state.movePlat.activated = true;
    }
    debugf("MovePlat init at (%.1f, %.1f, %.1f) -> target (%.1f, %.1f, %.1f) activated=%d\n",
           inst->posX, inst->posY, inst->posZ,
           inst->state.movePlat.targetX, inst->state.movePlat.targetY, inst->state.movePlat.targetZ,
           inst->state.movePlat.activated);
}

static void moveplat_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Distance culling - skip update if >2000 units away
    float dx = inst->posX - map->playerX;
    float dy = inst->posY - map->playerY;
    float dz = inst->posZ - map->playerZ;
    float distSq = dx*dx + dy*dy + dz*dz;
    
    if (distSq > 2000.0f * 2000.0f) {
        inst->state.movePlat.playerOnPlat = false;
        // Still update position if activated, but skip player checks
        goto skip_player_check;
    }

    // Staggered player-on-platform checks - only check every 3rd frame per platform
    // Use decoration index as offset to distribute checks across frames
    int decoIndex = inst - map->decorations;
    int checkInterval = 3;
    bool shouldCheck = (map->frameCounter + decoIndex) % checkInterval == 0;
    
    if (shouldCheck) {
        // Simplified AABB check instead of full triangle mesh
        // Platform bounds: use scale as rough approximation (coal carts are ~30 units wide)
        float halfWidth = 30.0f * inst->scaleX;
        float halfDepth = 30.0f * inst->scaleZ;
        float height = 20.0f * inst->scaleY;
        
        float px = map->playerX;
        float py = map->playerY;
        float pz = map->playerZ;
        
        // Simple AABB check (no rotation - conservative but much faster)
        bool inAABB = (px >= inst->posX - halfWidth && px <= inst->posX + halfWidth &&
                      pz >= inst->posZ - halfDepth && pz <= inst->posZ + halfDepth &&
                      py >= inst->posY - 5.0f && py <= inst->posY + height);
        
        inst->state.movePlat.playerOnPlat = inAABB;
        inst->state.movePlat.lastCheckFrame = map->frameCounter;
    }
    // Otherwise keep previous playerOnPlat state for 2 frames

skip_player_check:
    // Check activation system - if platform has an activationId and it's activated, start moving
    if (!inst->state.movePlat.activated && inst->activationId > 0) {
        if (activation_get(inst->activationId)) {
            inst->state.movePlat.activated = true;
            debugf("MovePlat activated via activationId=%d\n", inst->activationId);
        }
    }

    // Store previous position for delta calculation
    float prevX = inst->posX;
    float prevY = inst->posY;
    float prevZ = inst->posZ;

    // Only move if activated
    if (inst->state.movePlat.activated) {
        // Calculate total distance for speed normalization
        float dx = inst->state.movePlat.targetX - inst->state.movePlat.startX;
        float dy = inst->state.movePlat.targetY - inst->state.movePlat.startY;
        float dz = inst->state.movePlat.targetZ - inst->state.movePlat.startZ;
        float totalDist = sqrtf(dx * dx + dy * dy + dz * dz);

        if (totalDist > 0.1f) {
            // Calculate progress delta based on speed and total distance
            float progressDelta = (inst->state.movePlat.speed * deltaTime) / totalDist;
            inst->state.movePlat.progress += progressDelta * inst->state.movePlat.direction;

            // Reverse direction at endpoints
            if (inst->state.movePlat.progress >= 1.0f) {
                inst->state.movePlat.progress = 1.0f;
                inst->state.movePlat.direction = -1;
            } else if (inst->state.movePlat.progress <= 0.0f) {
                inst->state.movePlat.progress = 0.0f;
                inst->state.movePlat.direction = 1;
            }

            // Linear interpolation (no easing)
            float t = inst->state.movePlat.progress;

            // Interpolate position linearly
            inst->posX = inst->state.movePlat.startX + dx * t;
            inst->posY = inst->state.movePlat.startY + dy * t;
            inst->posZ = inst->state.movePlat.startZ + dz * t;
        }
    }

    // Calculate deltas for carrying player
    inst->state.movePlat.lastDeltaX = inst->posX - prevX;
    inst->state.movePlat.lastDeltaY = inst->posY - prevY;
    inst->state.movePlat.lastDeltaZ = inst->posZ - prevZ;
}

// ============================================================
// HIVE MOVING PLATFORM BEHAVIOR
// ============================================================
// Platform that moves to target when player stands on it.
// Returns to start when player gets off.

#define HIVE_MOVING_DEFAULT_SPEED 80.0f  // Units per second

static void hivemoving_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Store starting position
    inst->state.movePlat.startX = inst->posX;
    inst->state.movePlat.startY = inst->posY;
    inst->state.movePlat.startZ = inst->posZ;
    // Only set defaults if target wasn't configured (all zeros means unconfigured)
    if (inst->state.movePlat.targetX == 0.0f &&
        inst->state.movePlat.targetY == 0.0f &&
        inst->state.movePlat.targetZ == 0.0f) {
        inst->state.movePlat.targetX = inst->posX;
        inst->state.movePlat.targetY = inst->posY;
        inst->state.movePlat.targetZ = inst->posZ;
    }
    inst->state.movePlat.progress = 0.0f;
    // Only set default speed if not already configured
    if (inst->state.movePlat.speed <= 0.0f) {
        inst->state.movePlat.speed = HIVE_MOVING_DEFAULT_SPEED;
    }
    inst->state.movePlat.direction = 0;  // Not moving initially
    inst->state.movePlat.lastDeltaX = 0.0f;
    inst->state.movePlat.lastDeltaY = 0.0f;
    inst->state.movePlat.lastDeltaZ = 0.0f;
    inst->state.movePlat.playerOnPlat = false;
    inst->state.movePlat.activated = true;  // Always active
    inst->state.movePlat.returnDelayTimer = 0.0f;  // No delay initially
    debugf("HiveMoving init at (%.1f, %.1f, %.1f) -> target (%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ,
           inst->state.movePlat.targetX, inst->state.movePlat.targetY, inst->state.movePlat.targetZ);
}

static void hivemoving_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Check if player is standing on this platform by checking collision mesh triangles
    DecoTypeRuntime* decoType = map_get_deco_type(map, inst->type);
    bool playerOn = false;

    if (decoType && decoType->collision) {
        CollisionMesh* mesh = decoType->collision;
        float px = map->playerX;
        float py = map->playerY;
        float pz = map->playerZ;

        // Pre-calculate rotation values for transforming vertices
        float cosR = fm_cosf(inst->rotY);
        float sinR = fm_sinf(inst->rotY);

        for (int i = 0; i < mesh->count && !playerOn; i++) {
            CollisionTriangle* t = &mesh->triangles[i];

            // Transform triangle to world space: scale -> rotate Y -> translate
            // Vertex 0
            float sx0 = t->x0 * inst->scaleX;
            float sz0 = t->z0 * inst->scaleZ;
            float x0 = sx0 * cosR - sz0 * sinR + inst->posX;
            float y0 = t->y0 * inst->scaleY + inst->posY;
            float z0 = sx0 * sinR + sz0 * cosR + inst->posZ;
            // Vertex 1
            float sx1 = t->x1 * inst->scaleX;
            float sz1 = t->z1 * inst->scaleZ;
            float x1 = sx1 * cosR - sz1 * sinR + inst->posX;
            float y1 = t->y1 * inst->scaleY + inst->posY;
            float z1 = sx1 * sinR + sz1 * cosR + inst->posZ;
            // Vertex 2
            float sx2 = t->x2 * inst->scaleX;
            float sz2 = t->z2 * inst->scaleZ;
            float x2 = sx2 * cosR - sz2 * sinR + inst->posX;
            float y2 = t->y2 * inst->scaleY + inst->posY;
            float z2 = sx2 * sinR + sz2 * cosR + inst->posZ;

            // AABB culling
            float minX = fminf(fminf(x0, x1), x2);
            float maxX = fmaxf(fmaxf(x0, x1), x2);
            float minZ = fminf(fminf(z0, z1), z2);
            float maxZ = fmaxf(fmaxf(z0, z1), z2);
            if (px < minX - 5.0f || px > maxX + 5.0f || pz < minZ - 5.0f || pz > maxZ + 5.0f) continue;

            // Check if point is inside triangle (2D projection onto XZ plane)
            float v0x = x2 - x0, v0z = z2 - z0;
            float v1x = x1 - x0, v1z = z1 - z0;
            float v2x = px - x0, v2z = pz - z0;

            float dot00 = v0x * v0x + v0z * v0z;
            float dot01 = v0x * v1x + v0z * v1z;
            float dot02 = v0x * v2x + v0z * v2z;
            float dot11 = v1x * v1x + v1z * v1z;
            float dot12 = v1x * v2x + v1z * v2z;

            float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
            float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
            float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

            if (u >= -0.1f && v >= -0.1f && (u + v) <= 1.1f) {
                float triY = y0 + u * (y2 - y0) + v * (y1 - y0);
                if (py >= triY - 5.0f && py <= triY + 25.0f) {
                    playerOn = true;
                }
            }
        }
    }

    inst->state.movePlat.playerOnPlat = playerOn;

    // Set direction based on player presence:
    // Player on platform -> move toward target (direction = 1)
    // Player off platform -> wait 1 second, then move back to start (direction = -1)
    if (playerOn) {
        inst->state.movePlat.direction = 1;  // Move to target
        inst->state.movePlat.returnDelayTimer = 0.0f;  // Reset delay timer
    } else {
        // Player not on platform - wait 1 second before returning
        inst->state.movePlat.returnDelayTimer += deltaTime;
        if (inst->state.movePlat.returnDelayTimer >= 1.0f) {
            inst->state.movePlat.direction = -1;  // Return to start after delay
        } else {
            inst->state.movePlat.direction = 0;  // Stay in place during delay
        }
    }

    // Store previous position for delta calculation
    float prevX = inst->posX;
    float prevY = inst->posY;
    float prevZ = inst->posZ;

    // Calculate total distance for speed normalization
    float dx = inst->state.movePlat.targetX - inst->state.movePlat.startX;
    float dy = inst->state.movePlat.targetY - inst->state.movePlat.startY;
    float dz = inst->state.movePlat.targetZ - inst->state.movePlat.startZ;
    float totalDist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (totalDist > 0.1f) {
        // Calculate progress delta based on speed and total distance
        float progressDelta = (inst->state.movePlat.speed * deltaTime) / totalDist;
        inst->state.movePlat.progress += progressDelta * inst->state.movePlat.direction;

        // Clamp progress to [0, 1]
        if (inst->state.movePlat.progress >= 1.0f) {
            inst->state.movePlat.progress = 1.0f;
        } else if (inst->state.movePlat.progress <= 0.0f) {
            inst->state.movePlat.progress = 0.0f;
        }

        // Linear interpolation
        float t = inst->state.movePlat.progress;

        // Interpolate position linearly
        inst->posX = inst->state.movePlat.startX + dx * t;
        inst->posY = inst->state.movePlat.startY + dy * t;
        inst->posZ = inst->state.movePlat.startZ + dz * t;
    }

    // Calculate deltas for carrying player
    inst->state.movePlat.lastDeltaX = inst->posX - prevX;
    inst->state.movePlat.lastDeltaY = inst->posY - prevY;
    inst->state.movePlat.lastDeltaZ = inst->posZ - prevZ;
}

// ============================================================
// MOVING ROCK BEHAVIOR
// ============================================================
// Rock that moves between up to 4 waypoints in sequence.
// Player can ride on top. Moves forward through waypoints, then reverses.

#define MOVING_ROCK_DEFAULT_SPEED 60.0f  // Units per second

static void movingrock_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    // Starting position is waypoint 0
    inst->state.movingRock.waypoints[0][0] = inst->posX;
    inst->state.movingRock.waypoints[0][1] = inst->posY;
    inst->state.movingRock.waypoints[0][2] = inst->posZ;

    // If waypoints weren't set from level file, default to staying in place
    if (inst->state.movingRock.waypointCount < 2) {
        inst->state.movingRock.waypointCount = 1;
    }

    inst->state.movingRock.currentTarget = 1;  // Start moving to first target
    inst->state.movingRock.progress = 0.0f;

    // Only set default speed if not already configured
    if (inst->state.movingRock.speed <= 0.0f) {
        inst->state.movingRock.speed = MOVING_ROCK_DEFAULT_SPEED;
    }

    inst->state.movingRock.lastDeltaX = 0.0f;
    inst->state.movingRock.lastDeltaY = 0.0f;
    inst->state.movingRock.lastDeltaZ = 0.0f;
    inst->state.movingRock.playerOnPlat = false;

    // Only set activated = true if not already set to false (by startStationary in level_load)
    if (inst->activationId == 0 || inst->state.movingRock.activated != false) {
        inst->state.movingRock.activated = true;
    }

    debugf("MovingRock init at (%.1f, %.1f, %.1f) with %d waypoints, speed=%.1f\n",
           inst->posX, inst->posY, inst->posZ,
           inst->state.movingRock.waypointCount, inst->state.movingRock.speed);
}

static void movingrock_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Distance culling - skip update if >2000 units away
    float dx = inst->posX - map->playerX;
    float dy = inst->posY - map->playerY;
    float dz = inst->posZ - map->playerZ;
    float distSq = dx*dx + dy*dy + dz*dz;
    
    if (distSq > 2000.0f * 2000.0f) {
        inst->state.movingRock.playerOnPlat = false;
        // Still update position if activated, but skip player checks
        goto skip_player_check;
    }

    // Staggered player-on-platform checks - only check every 3rd frame per rock
    // Use decoration index as offset to distribute checks across frames
    int decoIndex = inst - map->decorations;
    int checkInterval = 3;
    bool shouldCheck = (map->frameCounter + decoIndex) % checkInterval == 0;
    
    if (shouldCheck) {
        // Simplified AABB check instead of full triangle mesh
        // Rock bounds: use scale as rough approximation (rocks are ~50 units wide)
        float halfWidth = 50.0f * inst->scaleX;
        float halfDepth = 50.0f * inst->scaleZ;
        float height = 30.0f * inst->scaleY;
        
        float px = map->playerX;
        float py = map->playerY;
        float pz = map->playerZ;
        
        // Simple AABB check (no rotation - conservative but much faster)
        bool inAABB = (px >= inst->posX - halfWidth && px <= inst->posX + halfWidth &&
                      pz >= inst->posZ - halfDepth && pz <= inst->posZ + halfDepth &&
                      py >= inst->posY - 5.0f && py <= inst->posY + height);
        
        inst->state.movingRock.playerOnPlat = inAABB;
        inst->state.movingRock.lastCheckFrame = map->frameCounter;
    }
    // Otherwise keep previous playerOnPlat state for 2 frames

skip_player_check:
    // Check activation system
    if (!inst->state.movingRock.activated && inst->activationId > 0) {
        if (activation_get(inst->activationId)) {
            inst->state.movingRock.activated = true;
            debugf("MovingRock activated via activationId=%d\n", inst->activationId);
        }
    }

    // Store previous position for delta calculation
    float prevX = inst->posX;
    float prevY = inst->posY;
    float prevZ = inst->posZ;

    // Only move if activated and we have at least 2 waypoints
    if (inst->state.movingRock.activated && inst->state.movingRock.waypointCount >= 2) {
        // Sequential pattern: 0 → 1 → 2 → 3 → snap back to 0 → 1 → ...
        int current = (inst->state.movingRock.currentTarget - 1 + inst->state.movingRock.waypointCount) % inst->state.movingRock.waypointCount;
        int target = inst->state.movingRock.currentTarget;

        // Get waypoint positions
        float startX = inst->state.movingRock.waypoints[current][0];
        float startY = inst->state.movingRock.waypoints[current][1];
        float startZ = inst->state.movingRock.waypoints[current][2];
        float targetX = inst->state.movingRock.waypoints[target][0];
        float targetY = inst->state.movingRock.waypoints[target][1];
        float targetZ = inst->state.movingRock.waypoints[target][2];

        // Calculate distance to target
        float dx = targetX - startX;
        float dy = targetY - startY;
        float dz = targetZ - startZ;
        float totalDist = sqrtf(dx * dx + dy * dy + dz * dz);

        if (totalDist > 0.1f) {
            // Speed multiplier: 3x faster when moving downward (dy < 0 means target is below start)
            float speedMult = (dy < -1.0f) ? 3.0f : 1.0f;

            // Calculate progress delta based on speed and total distance
            float progressDelta = (inst->state.movingRock.speed * speedMult * deltaTime) / totalDist;
            inst->state.movingRock.progress += progressDelta;

            // Check if we've reached the target waypoint
            if (inst->state.movingRock.progress >= 1.0f) {
                inst->state.movingRock.progress = 0.0f;

                // Move to next waypoint
                inst->state.movingRock.currentTarget++;

                // If we've reached the last waypoint, snap back to start
                if (inst->state.movingRock.currentTarget >= inst->state.movingRock.waypointCount) {
                    inst->state.movingRock.currentTarget = 1;  // Next target is waypoint 1
                    // Snap position back to waypoint 0
                    inst->posX = inst->state.movingRock.waypoints[0][0];
                    inst->posY = inst->state.movingRock.waypoints[0][1];
                    inst->posZ = inst->state.movingRock.waypoints[0][2];
                }
            }

            // Linear interpolation (no easing)
            float t = inst->state.movingRock.progress;

            // Interpolate position linearly
            inst->posX = startX + dx * t;
            inst->posY = startY + dy * t;
            inst->posZ = startZ + dz * t;
        }
    }

    // Calculate deltas for carrying player
    inst->state.movingRock.lastDeltaX = inst->posX - prevX;
    inst->state.movingRock.lastDeltaY = inst->posY - prevY;
    inst->state.movingRock.lastDeltaZ = inst->posZ - prevZ;
}

// ============================================================
// JUKEBOX BEHAVIOR
// ============================================================
// Jukebox with animated skeleton and scrolling FX texture overlay.
// The FX model is rendered on top with UV scrolling for visual effects.

#define JUKEBOX_FX_TEXTURE_SPEED 0.8f  // Texture scroll speed (cycles per second)

static void jukebox_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.jukebox.textureOffset = 0.0f;
    inst->state.jukebox.isPlaying = false;
    inst->state.jukebox.blendIn = 0.0f;
    debugf("Jukebox init at (%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ);
}

static void jukebox_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;
    // Blend in animation when playing
    if (inst->state.jukebox.isPlaying) {
        // Smoothly blend from idle (0) to full animation (1)
        if (inst->state.jukebox.blendIn < 1.0f) {
            inst->state.jukebox.blendIn += deltaTime * 0.5f;  // 2 second blend
            if (inst->state.jukebox.blendIn > 1.0f) {
                inst->state.jukebox.blendIn = 1.0f;
            }
        }
        // Scroll FX texture
        inst->state.jukebox.textureOffset += JUKEBOX_FX_TEXTURE_SPEED * deltaTime;
        if (inst->state.jukebox.textureOffset >= 1.0f) {
            inst->state.jukebox.textureOffset -= 1.0f;
        }
    }
}

// ============================================================
// MONITOR TABLE BEHAVIOR
// ============================================================
// Table with monitor screen that scrolls texture downward (like data display)

#define MONITOR_SCREEN_TEXTURE_SPEED 1.5f  // Texture scroll speed (cycles per second)

static void monitortable_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.monitorTable.textureOffset = 0.0f;
    debugf("MonitorTable init at (%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ);
}

static void monitortable_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    (void)map;
    // Scroll screen texture downward continuously (no wrap - UV tiling handles repeat)
    // Wrapping causes visible jump; continuous increment gives smooth scroll
    inst->state.monitorTable.textureOffset += MONITOR_SCREEN_TEXTURE_SPEED * deltaTime;
    // Prevent float overflow after very long play sessions (wrap at large value)
    if (inst->state.monitorTable.textureOffset > 1000.0f) {
        inst->state.monitorTable.textureOffset -= 1000.0f;
    }
}

// ============================================================
// DISCO BALL BEHAVIOR
// ============================================================
// Disco ball descends from ceiling and rotates when jukebox plays.
// Starts hidden up high, descends when activated, then spins to create light effect.

#define DISCOBALL_DESCENT_SPEED 30.0f    // Units per second descent
#define DISCOBALL_DESCENT_AMOUNT 80.0f   // How far to descend
#define DISCOBALL_ROTATION_SPEED 2.0f    // Radians per second

static void discoball_init(DecoInstance* inst, MapRuntime* map) {
    (void)map;
    inst->state.discoBall.startY = inst->posY;
    inst->state.discoBall.targetY = inst->posY - DISCOBALL_DESCENT_AMOUNT;
    inst->state.discoBall.currentY = 0.0f;  // Offset from original position
    inst->state.discoBall.rotation = 0.0f;
    inst->state.discoBall.isActive = false;
    inst->state.discoBall.isSpinning = false;
    debugf("DiscoBall init at (%.1f, %.1f, %.1f)\n",
           inst->posX, inst->posY, inst->posZ);
}

static void discoball_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Check if any jukebox is playing
    bool jukeboxPlaying = false;
    for (int i = 0; i < map->decoCount; i++) {
        DecoInstance* other = &map->decorations[i];
        if (other->active && other->type == DECO_JUKEBOX && other->state.jukebox.isPlaying) {
            jukeboxPlaying = true;
            break;
        }
    }

    // Activate when jukebox starts playing
    if (jukeboxPlaying && !inst->state.discoBall.isActive) {
        inst->state.discoBall.isActive = true;
        inst->state.discoBall.isSpinning = false;
    }

    if (inst->state.discoBall.isActive) {
        if (!inst->state.discoBall.isSpinning) {
            // Descending phase
            inst->state.discoBall.currentY -= DISCOBALL_DESCENT_SPEED * deltaTime;
            if (inst->state.discoBall.currentY <= -DISCOBALL_DESCENT_AMOUNT) {
                inst->state.discoBall.currentY = -DISCOBALL_DESCENT_AMOUNT;
                inst->state.discoBall.isSpinning = true;
                debugf("DiscoBall finished descending, now spinning!\n");
            }
        }

        // Rotate when spinning (or slowly during descent)
        if (inst->state.discoBall.isSpinning) {
            inst->state.discoBall.rotation += DISCOBALL_ROTATION_SPEED * deltaTime;
        } else {
            // Slow rotation during descent
            inst->state.discoBall.rotation += DISCOBALL_ROTATION_SPEED * 0.2f * deltaTime;
        }

        if (inst->state.discoBall.rotation > 6.28318f) {
            inst->state.discoBall.rotation -= 6.28318f;
        }
    }
}

// ============================================================
// BULLDOZER BEHAVIOR
// ============================================================

#define BULLDOZER_SPEED 40.0f
#define BULLDOZER_TURN_SPEED 0.5f    // Radians per second
#define BULLDOZER_PUSH_STRENGTH 15.0f
#define BULLDOZER_PUSH_COOLDOWN 0.3f
#define BULLDOZER_GRAVITY 400.0f
#define BULLDOZER_DETECTION_RANGE 200.0f
#define BULLDOZER_CLIFF_CHECK_DIST 70.0f  // How far ahead to check for cliffs (bulldozer is big)
#define BULLDOZER_MAX_STEP_DOWN 25.0f      // Max drop before refusing to move
#define BULLDOZER_CLIFF_PAUSE_TIME 0.5f    // Pause at cliff edge before reversing
#define BULLDOZER_CLIFF_REVERSE_TIME 1.0f  // Time to reverse away from cliff

static void bulldozer_init(DecoInstance* inst, MapRuntime* map) {
    inst->state.bulldozer.velX = 0.0f;
    inst->state.bulldozer.velZ = 0.0f;
    inst->state.bulldozer.velY = 0.0f;
    inst->state.bulldozer.isGrounded = false;
    inst->state.bulldozer.moveDir = 0.0f;
    inst->state.bulldozer.pushCooldown = 0.0f;
    inst->state.bulldozer.damageCooldown = 0.0f;
    inst->state.bulldozer.spawnX = inst->posX;
    inst->state.bulldozer.spawnY = inst->posY;
    inst->state.bulldozer.spawnZ = inst->posZ;
    inst->state.bulldozer.spawnRotY = inst->rotY;
    inst->state.bulldozer.speed = BULLDOZER_SPEED;
    // Patrol points initialized by map_add_decoration_patrol
    inst->state.bulldozer.patrolPoints = NULL;
    inst->state.bulldozer.patrolPointCount = 0;
    inst->state.bulldozer.currentPatrolIndex = 0;
    inst->state.bulldozer.pauseTimer = 0.0f;
    inst->state.bulldozer.chasingPlayer = false;
    inst->state.bulldozer.cliffPauseTimer = 0.0f;
    inst->state.bulldozer.reverseTimer = 0.0f;

    // Create per-instance skeleton for animation
    DecoTypeRuntime* dozerType = map_get_deco_type(map, DECO_BULLDOZER);
    if (dozerType && dozerType->model && dozerType->hasSkeleton) {
        inst->skeleton = t3d_skeleton_create(dozerType->model);
        inst->hasOwnSkeleton = true;
        inst->animCount = 0;

        // Load the dozer_loop animation
        T3DAnim anim = t3d_anim_create(dozerType->model, "dozer_loop");
        if (anim.animRef) {
            t3d_anim_attach(&anim, &inst->skeleton);
            t3d_anim_set_looping(&anim, true);
            t3d_anim_set_playing(&anim, true);
            inst->anims[inst->animCount++] = anim;
        }
    }
}

static void bulldozer_update(DecoInstance* inst, MapRuntime* map, float deltaTime) {
    // Decrease push cooldown
    if (inst->state.bulldozer.pushCooldown > 0.0f) {
        inst->state.bulldozer.pushCooldown -= deltaTime;
    }
    // Decrease damage cooldown
    if (inst->state.bulldozer.damageCooldown > 0.0f) {
        inst->state.bulldozer.damageCooldown -= deltaTime;
    }

    // === CLIFF AVOIDANCE STATE MACHINE ===
    // Priority: cliff pause -> reverse -> normal behavior
    if (inst->state.bulldozer.cliffPauseTimer > 0.0f) {
        // Pausing at cliff edge - stay still
        inst->state.bulldozer.cliffPauseTimer -= deltaTime;
        inst->state.bulldozer.velX = 0.0f;
        inst->state.bulldozer.velZ = 0.0f;

        // When pause ends, start reversing
        if (inst->state.bulldozer.cliffPauseTimer <= 0.0f) {
            inst->state.bulldozer.cliffPauseTimer = 0.0f;
            inst->state.bulldozer.reverseTimer = BULLDOZER_CLIFF_REVERSE_TIME;
        }
    }
    else if (inst->state.bulldozer.reverseTimer > 0.0f) {
        // Reversing away from cliff - move opposite to facing direction
        inst->state.bulldozer.reverseTimer -= deltaTime;
        float dirX = sinf(inst->state.bulldozer.moveDir);
        float dirZ = cosf(inst->state.bulldozer.moveDir);
        // Reverse direction (opposite of normal chase movement)
        inst->state.bulldozer.velX = dirX * inst->state.bulldozer.speed * 0.5f;  // Half speed reverse
        inst->state.bulldozer.velZ = -dirZ * inst->state.bulldozer.speed * 0.5f;

        if (inst->state.bulldozer.reverseTimer <= 0.0f) {
            inst->state.bulldozer.reverseTimer = 0.0f;
        }
    }
    // Decrease pause timer (waypoint pause, separate from cliff pause)
    else if (inst->state.bulldozer.pauseTimer > 0.0f) {
        inst->state.bulldozer.pauseTimer -= deltaTime;
        inst->state.bulldozer.velX = 0.0f;
        inst->state.bulldozer.velZ = 0.0f;
    } else {
        // Calculate direction to player
        float dx = map->playerX - inst->posX;
        float dz = map->playerZ - inst->posZ;
        float distSq = dx * dx + dz * dz;

        // Check if player is within detection range
        bool playerInRange = distSq < BULLDOZER_DETECTION_RANGE * BULLDOZER_DETECTION_RANGE;
        inst->state.bulldozer.chasingPlayer = playerInRange;

        if (playerInRange) {
            // Chase player
            float dist = sqrtf(distSq);
            float dirX = dx / dist;
            float dirZ = dz / dist;
            float targetDir = atan2f(-dirX, dirZ);
            while (targetDir < 0) targetDir += 6.283185f;
            while (inst->state.bulldozer.moveDir < 0) inst->state.bulldozer.moveDir += 6.283185f;
            if(inst->state.bulldozer.moveDir - targetDir > 0.6f || inst->state.bulldozer.moveDir - targetDir < -0.6f) { // if the player is more than 30 degrees away from the bulldozer's moveDir
                inst->state.bulldozer.velX = 0.0f;
                inst->state.bulldozer.velZ = 0.0f;
                // Smoothly turn toward player
                float dirDiff = targetDir - inst->state.bulldozer.moveDir;
                // Wrap to [-PI, PI]
                if (dirDiff > 3.141592f) dirDiff -= 6.283185f;
                if (dirDiff < -3.141592f) dirDiff += 6.283185f;

                float turnAmount = BULLDOZER_TURN_SPEED * deltaTime;
                if (fabsf(dirDiff) < turnAmount) {
                    inst->state.bulldozer.moveDir = targetDir;
                } else {
                    if (dirDiff > 0) {
                        inst->state.bulldozer.moveDir += turnAmount;
                    } else {
                        inst->state.bulldozer.moveDir -= turnAmount;
                    }
                }
            } else{
                dirX = sinf(inst->state.bulldozer.moveDir);
                dirZ = cosf(inst->state.bulldozer.moveDir);
                inst->state.bulldozer.velX = -dirX * inst->state.bulldozer.speed;
                inst->state.bulldozer.velZ = dirZ * inst->state.bulldozer.speed;
            }
        } else if (inst->state.bulldozer.patrolPoints != NULL && inst->state.bulldozer.patrolPointCount > 1) {
            // Follow patrol points
            T3DVec3* target = &inst->state.bulldozer.patrolPoints[inst->state.bulldozer.currentPatrolIndex];

            float pdx = target->v[0] - inst->posX;
            float pdz = target->v[2] - inst->posZ;
            float pDistSq = pdx * pdx + pdz * pdz;

            if (pDistSq < 100.0f) {
                // Reached waypoint - advance to next
                inst->state.bulldozer.currentPatrolIndex = (inst->state.bulldozer.currentPatrolIndex + 1) % inst->state.bulldozer.patrolPointCount;
                inst->state.bulldozer.pauseTimer = 0.5f;  // Brief pause at waypoint
                inst->state.bulldozer.velX = 0.0f;
                inst->state.bulldozer.velZ = 0.0f;
            } else {
                // Move toward patrol point
                float pDist = sqrtf(pDistSq);
                float pdirX = pdx / pDist;
                float pdirZ = pdz / pDist;

                inst->state.bulldozer.velX = pdirX * inst->state.bulldozer.speed * 0.6f;  // Slower patrol
                inst->state.bulldozer.velZ = pdirZ * inst->state.bulldozer.speed * 0.6f;
                inst->state.bulldozer.moveDir = atan2f(-pdirX, pdirZ);
            }
        } else {
            // No patrol - stay still
            inst->state.bulldozer.velX = 0.0f;
            inst->state.bulldozer.velZ = 0.0f;
        }
    }

    // Apply gravity
    if (!inst->state.bulldozer.isGrounded) {
        inst->state.bulldozer.velY -= BULLDOZER_GRAVITY * deltaTime;
    }

    // Cliff detection - check far ahead since bulldozer is large
    // Only check when moving forward (not during reverse or pause)
    bool isReversing = inst->state.bulldozer.reverseTimer > 0.0f;
    bool isCliffPausing = inst->state.bulldozer.cliffPauseTimer > 0.0f;
    if (!isReversing && !isCliffPausing &&
        (inst->state.bulldozer.velX != 0.0f || inst->state.bulldozer.velZ != 0.0f) && map->mapLoader) {
        float velLen = sqrtf(inst->state.bulldozer.velX * inst->state.bulldozer.velX +
                            inst->state.bulldozer.velZ * inst->state.bulldozer.velZ);
        if (velLen > 0.1f) {
            // Normalize direction and check BULLDOZER_CLIFF_CHECK_DIST units ahead
            float dirX = inst->state.bulldozer.velX / velLen;
            float dirZ = inst->state.bulldozer.velZ / velLen;
            float checkX = inst->posX + dirX * BULLDOZER_CLIFF_CHECK_DIST;
            float checkZ = inst->posZ + dirZ * BULLDOZER_CLIFF_CHECK_DIST;

            float currentGroundY = maploader_get_ground_height(map->mapLoader, inst->posX, inst->posY + 50.0f, inst->posZ);
            float aheadGroundY = maploader_get_ground_height(map->mapLoader, checkX, inst->posY + 50.0f, checkZ);

            // If ground ahead drops too much, start cliff avoidance sequence
            if (currentGroundY > INVALID_GROUND_Y && aheadGroundY > INVALID_GROUND_Y) {
                if (currentGroundY - aheadGroundY > BULLDOZER_MAX_STEP_DOWN) {
                    // Stop immediately and start pause at cliff edge
                    inst->state.bulldozer.velX = 0.0f;
                    inst->state.bulldozer.velZ = 0.0f;
                    inst->state.bulldozer.cliffPauseTimer = BULLDOZER_CLIFF_PAUSE_TIME;
                }
            }
        }
    }

    // Move position
    inst->posX += inst->state.bulldozer.velX * deltaTime;
    inst->posZ += inst->state.bulldozer.velZ * deltaTime;
    inst->posY += inst->state.bulldozer.velY * deltaTime;

    // Respawn at spawn position if fallen off map
    if (inst->posY < -500.0f) {
        inst->posX = inst->state.bulldozer.spawnX;
        inst->posY = inst->state.bulldozer.spawnY;
        inst->posZ = inst->state.bulldozer.spawnZ;
        inst->rotY = inst->state.bulldozer.spawnRotY;
        inst->state.bulldozer.velX = 0.0f;
        inst->state.bulldozer.velY = 0.0f;
        inst->state.bulldozer.velZ = 0.0f;
        inst->state.bulldozer.isGrounded = false;
        inst->state.bulldozer.moveDir = inst->state.bulldozer.spawnRotY;
        inst->state.bulldozer.chasingPlayer = false;
        inst->state.bulldozer.currentPatrolIndex = 0;
        inst->state.bulldozer.pauseTimer = 0.5f;
        inst->state.bulldozer.cliffPauseTimer = 0.0f;
        inst->state.bulldozer.reverseTimer = 0.0f;
        return;
    }

    // Ground collision
    if (map->mapLoader) {
        float groundY = maploader_get_ground_height(map->mapLoader, inst->posX, inst->posY + 50.0f, inst->posZ);
        inst->state.bulldozer.isGrounded = false;
        if (groundY > INVALID_GROUND_Y && inst->state.bulldozer.velY <= 0 && inst->posY <= groundY + 1.0f) {
            inst->posY = groundY;
            inst->state.bulldozer.velY = 0.0f;
            inst->state.bulldozer.isGrounded = true;
        }

        // Wall collision
        float pushX = 0.0f, pushZ = 0.0f;
        if (maploader_check_walls(map->mapLoader, inst->posX, inst->posY, inst->posZ, 15.0f, 30.0f, &pushX, &pushZ)) {
            inst->posX += pushX;
            inst->posZ += pushZ;
        }
    }

    // Update rotation to face movement direction
    inst->rotY = inst->state.bulldozer.moveDir;

    // Update animation
    if (inst->hasOwnSkeleton && inst->animCount > 0) {
        t3d_anim_update(&inst->anims[0], deltaTime);
        if (skeleton_is_valid(&inst->skeleton)) {
            t3d_skeleton_update(&inst->skeleton);
        }
    }
}

static void bulldozer_on_player_collide(DecoInstance* inst, MapRuntime* map, float px, float py, float pz) {
    (void)map;
    (void)px;
    (void)py;
    (void)pz;

    // External functions
    extern bool player_is_dead(void);
    extern void player_push(float fromX, float fromZ, float strength);
    extern void player_take_damage(int damage);

    if (player_is_dead()) return;

    // Continuously push player away from bulldozer (smooth push, no cooldown needed)
    player_push(inst->posX, inst->posZ, BULLDOZER_PUSH_STRENGTH);

    // Deal damage with cooldown (1 damage every 0.5 seconds)
    if (inst->state.bulldozer.damageCooldown <= 0.0f) {
        player_take_damage(1);
        inst->state.bulldozer.damageCooldown = 0.5f;
    }
}

#endif // MAP_DATA_H
