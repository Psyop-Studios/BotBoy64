// N64 Collision Data - Thin laser beam collision
// Model: DECO_MOVING_LASER
// Only the thin beam part (not the FX glow)
// Triangles: 12 (box shape)
// Long axis is Z, thin in X/Y (matches visual orientation)

#ifndef DECO_MOVING_LASER_COLLISION_H
#define DECO_MOVING_LASER_COLLISION_H

#include "../collision.h"

// Thin laser collision: 102 units half-length in Z, ~11.3 units radius in X/Y
// Rotated 90 degrees from previous to match visual rendering
static CollisionTriangle DECO_MOVING_LASER_collision_triangles[] = {
    // Front face (+Z)
    { 11.3f, 11.3f, 102.1f,   -11.3f, -11.3f, 102.1f,   -11.3f, 11.3f, 102.1f },
    { 11.3f, 11.3f, 102.1f,   11.3f, -11.3f, 102.1f,   -11.3f, -11.3f, 102.1f },
    // Back face (-Z)
    { -11.3f, 11.3f, -102.1f,   11.3f, -11.3f, -102.1f,   11.3f, 11.3f, -102.1f },
    { -11.3f, 11.3f, -102.1f,   -11.3f, -11.3f, -102.1f,   11.3f, -11.3f, -102.1f },
    // Top face (+Y)
    { -11.3f, 11.3f, -102.1f,   11.3f, 11.3f, 102.1f,   -11.3f, 11.3f, 102.1f },
    { -11.3f, 11.3f, -102.1f,   11.3f, 11.3f, -102.1f,   11.3f, 11.3f, 102.1f },
    // Bottom face (-Y)
    { -11.3f, -11.3f, 102.1f,   11.3f, -11.3f, -102.1f,   -11.3f, -11.3f, -102.1f },
    { -11.3f, -11.3f, 102.1f,   11.3f, -11.3f, 102.1f,   11.3f, -11.3f, -102.1f },
    // Right face (+X)
    { 11.3f, 11.3f, -102.1f,   11.3f, -11.3f, 102.1f,   11.3f, 11.3f, 102.1f },
    { 11.3f, 11.3f, -102.1f,   11.3f, -11.3f, -102.1f,   11.3f, -11.3f, 102.1f },
    // Left face (-X)
    { -11.3f, 11.3f, 102.1f,   -11.3f, -11.3f, -102.1f,   -11.3f, 11.3f, -102.1f },
    { -11.3f, 11.3f, 102.1f,   -11.3f, -11.3f, 102.1f,   -11.3f, -11.3f, -102.1f },
};

static CollisionMesh DECO_MOVING_LASER_collision = {
    .triangles = DECO_MOVING_LASER_collision_triangles,
    .count = 12
};

#endif // DECO_MOVING_LASER_COLLISION_H
