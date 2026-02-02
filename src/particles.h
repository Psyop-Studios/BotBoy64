#ifndef PARTICLES_H
#define PARTICLES_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================
// SIMPLE PARTICLE SYSTEM
// Screen-space particles with no collision
// ============================================================

#define PARTICLES_MAX 24

// Single particle
typedef struct {
    float x, y, z;           // World position
    float velX, velY, velZ;  // Velocity
    float life;              // Remaining lifetime (0 = dead)
    float maxLife;           // Initial lifetime (for fade calc)
    uint8_t r, g, b;         // Color
    float size;              // Particle size
    bool active;             // Is this slot in use?
} Particle;

// ============================================================
// DEATH DECALS - persistent ground splats
// ============================================================

#define DECALS_MAX 8

typedef struct {
    float x, y, z;
    float scale;
    float alpha;
    bool active;
} DeathDecal;

// ============================================================
// IMPACT STARS - cartoon stars orbiting player's head
// ============================================================

#define IMPACT_STAR_COUNT 4
#define IMPACT_STAR_RADIUS 12.0f   // Orbit radius around head
#define IMPACT_STAR_HEIGHT 25.0f   // Height above player base
#define IMPACT_STAR_DURATION 2.0f  // How long stars show

// ============================================================
// LIFECYCLE FUNCTIONS
// ============================================================

// Initialize particle system (call once at startup)
void particles_init(void);

// Update all particles and effects
void particles_update(float deltaTime);

// ============================================================
// SPAWN FUNCTIONS
// ============================================================

// Spawn particles in an arc (for slime splash)
void particles_spawn_splash(float x, float y, float z, int count,
                            uint8_t r, uint8_t g, uint8_t b);

// Spawn dust puffs (big, slow, floaty - for player landing)
void particles_spawn_dust(float x, float y, float z, int count);

// Spawn oil splash particles (brown, goopy - for slime hits)
void particles_spawn_oil(float x, float y, float z, int count);

// Spawn sparks (small, fast, arcing - for bolt collection)
void particles_spawn_sparks(float x, float y, float z, int count);

// Spawn a death decal at position
void particles_spawn_decal(float x, float y, float z, float scale);

// Trigger impact stars around player
void particles_spawn_impact_stars(void);

// ============================================================
// RENDER FUNCTIONS
// ============================================================

// Draw all particles (call after 3D rendering, before HUD)
// viewport: The T3DViewport for world-to-screen conversion
void particles_draw(T3DViewport* viewport);

// Draw impact stars around a player position
// playerX, playerY, playerZ: Player world position for star orbit center
void particles_draw_impact_stars(T3DViewport* viewport,
                                 float playerX, float playerY, float playerZ);

// Draw death decals (call during 3D rendering with decal model)
// decalModel: Model to render at each decal location
// decalMat: Transform matrix array (at least DECALS_MAX entries)
void particles_draw_decals(T3DModel* decalModel, T3DMat4FP* decalMat);

// ============================================================
// STATE ACCESSORS (for external systems)
// ============================================================

// Get impact stars timer (0 = not showing)
float particles_get_impact_stars_timer(void);

// Check if any particles are active
bool particles_any_active(void);

#endif // PARTICLES_H
