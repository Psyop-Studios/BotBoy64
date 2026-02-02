// ============================================================
// PARTICLES MODULE
// Simple particle system for visual effects
// ============================================================

#include "particles.h"
#include "constants.h"
#include <stdlib.h>
#include <math.h>

// Set FPU to flush denormals to zero and disable FPU exceptions
static void fpu_flush_denormals(void) {
    uint32_t fcr31;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcr31));
    fcr31 |= (1 << 24);     // FS bit: flush denormals to zero
    fcr31 &= ~(0x1F << 7);  // Clear exception enable bits
    fcr31 &= ~(0x3F << 2);  // Clear cause bits
    fcr31 &= ~(1 << 17);    // Clear unimplemented operation cause
    fcr31 &= ~(0x1F << 12); // Clear sticky FLAG bits
    __asm__ volatile("ctc1 %0, $31" : : "r"(fcr31));
}

// ============================================================
// STATIC DATA
// ============================================================

static Particle g_particles[PARTICLES_MAX];
static DeathDecal g_deathDecals[DECALS_MAX];
static float g_impactStarsTimer = 0.0f;
static float g_impactStarsAngle = 0.0f;

// ============================================================
// LIFECYCLE FUNCTIONS
// ============================================================

void particles_init(void) {
    for (int i = 0; i < PARTICLES_MAX; i++) {
        g_particles[i].active = false;
        g_particles[i].life = 0.0f;
    }
    for (int i = 0; i < DECALS_MAX; i++) {
        g_deathDecals[i].active = false;
    }
    g_impactStarsTimer = 0.0f;
    g_impactStarsAngle = 0.0f;
}

void particles_update(float deltaTime) {
    // Update particles
    for (int i = 0; i < PARTICLES_MAX; i++) {
        Particle* p = &g_particles[i];
        if (!p->active) continue;

        // Physics
        p->velY -= 0.4f;
        p->x += p->velX;
        p->y += p->velY;
        p->z += p->velZ;

        // Lifetime
        p->life -= deltaTime;

        // Deactivate dead particles
        if (p->life <= 0.0f) {
            p->active = false;
        }
    }

    // Update death decals (fade out)
    for (int i = 0; i < DECALS_MAX; i++) {
        if (g_deathDecals[i].active) {
            g_deathDecals[i].alpha -= deltaTime * 0.3f;  // Fade over ~3 seconds
            if (g_deathDecals[i].alpha <= 0.0f) {
                g_deathDecals[i].active = false;
            }
        }
    }

    // Update impact stars
    if (g_impactStarsTimer > 0.0f) {
        g_impactStarsTimer -= deltaTime;
        g_impactStarsAngle += 5.0f * deltaTime;  // Rotate around head
        // Wrap angle to prevent FPU overflow after long runtime
        if (g_impactStarsAngle > 6.283185f) {
            g_impactStarsAngle -= 6.283185f;
        }
    }
}

// ============================================================
// SPAWN FUNCTIONS
// ============================================================

void particles_spawn_splash(float x, float y, float z, int count,
                            uint8_t r, uint8_t g, uint8_t b) {
    fpu_flush_denormals();  // Prevent denormal exceptions from sinf/cosf
    int spawned = 0;
    for (int i = 0; i < PARTICLES_MAX && spawned < count; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            p->x = x;
            p->y = y + 2.0f;
            p->z = z;

            // Random outward velocity
            float angle = (float)(rand() % 628) / 100.0f;
            float speed = 1.5f + (float)(rand() % 100) / 50.0f;
            p->velX = cosf(angle) * speed;
            p->velZ = sinf(angle) * speed;
            p->velY = 3.0f + (float)(rand() % 100) / 50.0f;

            p->maxLife = 1.0f + (float)(rand() % 30) / 100.0f;
            p->life = p->maxLife;
            p->r = r;
            p->g = g;
            p->b = b;
            p->size = 2.0f + (float)(rand() % 15) / 10.0f;
            spawned++;
        }
    }
}

void particles_spawn_dust(float x, float y, float z, int count) {
    fpu_flush_denormals();  // Prevent denormal exceptions from sinf/cosf
    int spawned = 0;
    for (int i = 0; i < PARTICLES_MAX && spawned < count; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            p->x = x + ((rand() % 20) - 10);  // Slight random offset
            p->y = y + 3.0f;
            p->z = z + ((rand() % 20) - 10);

            // Slow outward and upward drift
            float angle = (float)(rand() % 628) / 100.0f;
            float speed = 0.3f + (float)(rand() % 50) / 100.0f;
            p->velX = cosf(angle) * speed;
            p->velZ = sinf(angle) * speed;
            p->velY = 0.8f + (float)(rand() % 50) / 100.0f;  // Gentle upward

            p->maxLife = 0.25f + (float)(rand() % 15) / 100.0f;
            p->life = p->maxLife;
            // Brownish-gray dust color
            p->r = 140 + (rand() % 30);
            p->g = 120 + (rand() % 30);
            p->b = 100 + (rand() % 30);
            p->size = 4.0f + (float)(rand() % 30) / 10.0f;  // Big puffs
            spawned++;
        }
    }
}

void particles_spawn_oil(float x, float y, float z, int count) {
    fpu_flush_denormals();  // Prevent denormal exceptions from sinf/cosf
    int spawned = 0;
    for (int i = 0; i < PARTICLES_MAX && spawned < count; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            p->x = x + ((rand() % 16) - 8);
            p->y = y + 5.0f;
            p->z = z + ((rand() % 16) - 8);

            // Fast outward splatter
            float angle = (float)(rand() % 628) / 100.0f;
            float speed = 2.5f + (float)(rand() % 150) / 50.0f;
            p->velX = cosf(angle) * speed;
            p->velZ = sinf(angle) * speed;
            p->velY = 4.0f + (float)(rand() % 200) / 50.0f;

            p->maxLife = 0.6f + (float)(rand() % 30) / 100.0f;
            p->life = p->maxLife;
            // Brown/dark oil colors
            p->r = 60 + (rand() % 40);
            p->g = 40 + (rand() % 30);
            p->b = 20 + (rand() % 20);
            p->size = 3.0f + (float)(rand() % 20) / 10.0f;
            spawned++;
        }
    }
}

void particles_spawn_sparks(float x, float y, float z, int count) {
    fpu_flush_denormals();  // Prevent denormal exceptions from sinf/cosf
    int spawned = 0;
    for (int i = 0; i < PARTICLES_MAX && spawned < count; i++) {
        if (!g_particles[i].active) {
            Particle* p = &g_particles[i];
            p->active = true;
            p->x = x;
            p->y = y;
            p->z = z;

            // Fast outward velocity in all directions
            float angle = (float)(rand() % 628) / 100.0f;
            float speed = 2.0f + (float)(rand() % 150) / 50.0f;
            p->velX = cosf(angle) * speed;
            p->velZ = sinf(angle) * speed;
            p->velY = 2.0f + (float)(rand() % 200) / 50.0f;  // Upward arc

            p->maxLife = 0.4f + (float)(rand() % 20) / 100.0f;  // Short lived
            p->life = p->maxLife;
            // Yellow/orange spark colors
            p->r = 255;
            p->g = 200 + (rand() % 55);
            p->b = 50 + (rand() % 100);
            p->size = 1.5f + (float)(rand() % 10) / 10.0f;  // Small sparks
            spawned++;
        }
    }
}

void particles_spawn_decal(float x, float y, float z, float scale) {
    // Find oldest or inactive slot
    int slot = 0;
    float lowestAlpha = 999.0f;
    for (int i = 0; i < DECALS_MAX; i++) {
        if (!g_deathDecals[i].active) {
            slot = i;
            break;
        }
        if (g_deathDecals[i].alpha < lowestAlpha) {
            lowestAlpha = g_deathDecals[i].alpha;
            slot = i;
        }
    }
    g_deathDecals[slot].x = x;
    g_deathDecals[slot].y = y;
    g_deathDecals[slot].z = z;
    g_deathDecals[slot].scale = scale;
    g_deathDecals[slot].alpha = 1.0f;
    g_deathDecals[slot].active = true;
}

void particles_spawn_impact_stars(void) {
    g_impactStarsTimer = IMPACT_STAR_DURATION;
    g_impactStarsAngle = 0.0f;
}

// ============================================================
// RENDER FUNCTIONS
// ============================================================

void particles_draw(T3DViewport* viewport) {
    bool hasActiveParticles = false;
    for (int i = 0; i < PARTICLES_MAX; i++) {
        if (g_particles[i].active) {
            hasActiveParticles = true;
            break;
        }
    }

    if (!hasActiveParticles) return;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    for (int i = 0; i < PARTICLES_MAX; i++) {
        Particle* p = &g_particles[i];
        if (!p->active) continue;

        // Convert world position to screen position
        T3DVec3 worldPos = {{p->x, p->y, p->z}};
        T3DVec3 screenPos;
        t3d_viewport_calc_viewspace_pos(viewport, &screenPos, &worldPos);

        // Skip if behind camera
        if (screenPos.v[2] <= 0) continue;

        // Calculate alpha based on remaining life
        float lifeRatio = p->life / p->maxLife;
        uint8_t alpha = (uint8_t)(180.0f * lifeRatio);

        // Draw as a small filled rectangle
        float halfSize = p->size;
        float sx = screenPos.v[0];
        float sy = screenPos.v[1];

        // Skip if off screen
        if (sx < -halfSize || sx > SCREEN_WIDTH + halfSize || sy < -halfSize || sy > SCREEN_HEIGHT + halfSize) continue;

        rdpq_set_prim_color(RGBA32(p->r, p->g, p->b, alpha));

        // Draw as two triangles forming a quad
        float v0[] = {sx - halfSize, sy - halfSize};
        float v1[] = {sx + halfSize, sy - halfSize};
        float v2[] = {sx - halfSize, sy + halfSize};
        float v3[] = {sx + halfSize, sy + halfSize};

        rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
        rdpq_triangle(&TRIFMT_FILL, v2, v1, v3);
    }
}

void particles_draw_impact_stars(T3DViewport* viewport,
                                 float playerX, float playerY, float playerZ) {
    if (g_impactStarsTimer <= 0.0f) return;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    // Calculate alpha based on remaining time (fade out in last 0.5 seconds)
    float fadeRatio = g_impactStarsTimer < 0.5f ? (g_impactStarsTimer / 0.5f) : 1.0f;
    uint8_t alpha = (uint8_t)(220.0f * fadeRatio);

    // Draw 4 stars orbiting around the player's head position
    for (int i = 0; i < IMPACT_STAR_COUNT; i++) {
        float angle = g_impactStarsAngle + (float)i * (6.28318f / IMPACT_STAR_COUNT);
        float offsetX = cosf(angle) * IMPACT_STAR_RADIUS;
        float offsetZ = sinf(angle) * IMPACT_STAR_RADIUS;

        T3DVec3 starWorld = {{playerX + offsetX, playerY + IMPACT_STAR_HEIGHT, playerZ + offsetZ}};
        T3DVec3 starScreen;
        t3d_viewport_calc_viewspace_pos(viewport, &starScreen, &starWorld);

        // Skip if behind camera
        if (starScreen.v[2] <= 0) continue;

        float sx = starScreen.v[0];
        float sy = starScreen.v[1];

        // Skip if off screen
        if (sx < -10 || sx > 330 || sy < -10 || sy > 250) continue;

        rdpq_set_prim_color(RGBA32(255, 255, 100, alpha));  // Yellow stars

        // Draw a simple 4-pointed star shape using triangles
        float starSize = 3.0f;
        float v_top[] = {sx, sy - starSize * 2};
        float v_bot[] = {sx, sy + starSize * 2};
        float v_left[] = {sx - starSize * 2, sy};
        float v_right[] = {sx + starSize * 2, sy};
        float v_center[] = {sx, sy};

        // 4 triangles for the star
        rdpq_triangle(&TRIFMT_FILL, v_center, v_top, v_right);
        rdpq_triangle(&TRIFMT_FILL, v_center, v_right, v_bot);
        rdpq_triangle(&TRIFMT_FILL, v_center, v_bot, v_left);
        rdpq_triangle(&TRIFMT_FILL, v_center, v_left, v_top);
    }
}

void particles_draw_decals(T3DModel* decalModel, T3DMat4FP* decalMat) {
    if (!decalModel || !decalMat) return;

    for (int i = 0; i < DECALS_MAX; i++) {
        DeathDecal* d = &g_deathDecals[i];
        if (!d->active) continue;

        // Set up transform
        t3d_mat4fp_from_srt_euler(&decalMat[i],
            (float[3]){d->scale, 0.05f, d->scale},  // Flat on ground
            (float[3]){0, 0, 0},
            (float[3]){d->x, d->y + 0.5f, d->z}  // Slightly above ground
        );

        t3d_matrix_push(&decalMat[i]);

        // Set alpha based on fade
        uint8_t alpha = (uint8_t)(d->alpha * 180.0f);
        rdpq_set_prim_color(RGBA32(40, 30, 20, alpha));  // Dark oil color

        t3d_model_draw(decalModel);
        t3d_matrix_pop(1);
    }
}

// ============================================================
// STATE ACCESSORS
// ============================================================

float particles_get_impact_stars_timer(void) {
    return g_impactStarsTimer;
}

bool particles_any_active(void) {
    for (int i = 0; i < PARTICLES_MAX; i++) {
        if (g_particles[i].active) return true;
    }
    return false;
}
