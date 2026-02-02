// N64 Collision Data - JukeBox
// Simple box collision (scaled 64x from Blender units, centered)
// Jukebox visual dimensions approximately: width ~50, depth ~40, height ~220
// Collision centered on origin for proper placement

#ifndef JUKEBOX_COLLISION_H
#define JUKEBOX_COLLISION_H

#include "../collision.h"

// Jukebox collision box: centered, roughly matching visual shape
// Width: 50 units (-25 to +25 on X)
// Depth: 40 units (-20 to +20 on Z)
// Height: 220 units (0 to 220 on Y)
#define JB_X_MIN -25.0f
#define JB_X_MAX  25.0f
#define JB_Y_MIN   0.0f
#define JB_Y_MAX 220.0f
#define JB_Z_MIN -20.0f
#define JB_Z_MAX  20.0f

static CollisionTriangle JukeBox_collision_triangles[] = {
    // Bottom face (Y = 0)
    { JB_X_MIN, JB_Y_MIN, JB_Z_MIN,   JB_X_MAX, JB_Y_MIN, JB_Z_MIN,   JB_X_MAX, JB_Y_MIN, JB_Z_MAX },
    { JB_X_MIN, JB_Y_MIN, JB_Z_MIN,   JB_X_MAX, JB_Y_MIN, JB_Z_MAX,   JB_X_MIN, JB_Y_MIN, JB_Z_MAX },

    // Top face (Y = 220)
    { JB_X_MIN, JB_Y_MAX, JB_Z_MAX,   JB_X_MAX, JB_Y_MAX, JB_Z_MAX,   JB_X_MAX, JB_Y_MAX, JB_Z_MIN },
    { JB_X_MIN, JB_Y_MAX, JB_Z_MAX,   JB_X_MAX, JB_Y_MAX, JB_Z_MIN,   JB_X_MIN, JB_Y_MAX, JB_Z_MIN },

    // Front face (Z = +20)
    { JB_X_MIN, JB_Y_MIN, JB_Z_MAX,   JB_X_MAX, JB_Y_MIN, JB_Z_MAX,   JB_X_MAX, JB_Y_MAX, JB_Z_MAX },
    { JB_X_MIN, JB_Y_MIN, JB_Z_MAX,   JB_X_MAX, JB_Y_MAX, JB_Z_MAX,   JB_X_MIN, JB_Y_MAX, JB_Z_MAX },

    // Back face (Z = -20)
    { JB_X_MAX, JB_Y_MIN, JB_Z_MIN,   JB_X_MIN, JB_Y_MIN, JB_Z_MIN,   JB_X_MIN, JB_Y_MAX, JB_Z_MIN },
    { JB_X_MAX, JB_Y_MIN, JB_Z_MIN,   JB_X_MIN, JB_Y_MAX, JB_Z_MIN,   JB_X_MAX, JB_Y_MAX, JB_Z_MIN },

    // Left face (X = -25)
    { JB_X_MIN, JB_Y_MIN, JB_Z_MIN,   JB_X_MIN, JB_Y_MIN, JB_Z_MAX,   JB_X_MIN, JB_Y_MAX, JB_Z_MAX },
    { JB_X_MIN, JB_Y_MIN, JB_Z_MIN,   JB_X_MIN, JB_Y_MAX, JB_Z_MAX,   JB_X_MIN, JB_Y_MAX, JB_Z_MIN },

    // Right face (X = +25)
    { JB_X_MAX, JB_Y_MIN, JB_Z_MAX,   JB_X_MAX, JB_Y_MIN, JB_Z_MIN,   JB_X_MAX, JB_Y_MAX, JB_Z_MIN },
    { JB_X_MAX, JB_Y_MIN, JB_Z_MAX,   JB_X_MAX, JB_Y_MAX, JB_Z_MIN,   JB_X_MAX, JB_Y_MAX, JB_Z_MAX },
};

static CollisionMesh JukeBox_collision = {
    .triangles = JukeBox_collision_triangles,
    .count = 12
};

#endif // JUKEBOX_COLLISION_H
