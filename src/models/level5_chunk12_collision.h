// N64 Collision Data - Auto-chunked by chunk_collision.py
// Original Model: level5
// Chunk: 12
// Triangles: 10

#ifndef LEVEL5_CHUNK12_COLLISION_H
#define LEVEL5_CHUNK12_COLLISION_H

#include "../collision.h"

static CollisionTriangle level5_chunk12_collision_triangles[] = {
    { 6549.5f, 1977.4f, -530.6f,   6846.6f, 1914.0f, -530.6f,   6846.6f, 2131.3f, -530.6f },
    { 6352.3f, 1977.4f, -530.6f,   6549.5f, 1977.4f, -530.6f,   6846.6f, 2131.3f, -530.6f },
    { 6352.3f, 2131.3f, -530.6f,   6846.6f, 2131.3f, -530.6f,   6846.6f, 2287.6f, -530.6f },
    { 6257.2f, 2261.7f, -530.6f,   6846.6f, 2287.6f, -530.6f,   6846.6f, 2546.0f, -530.6f },
    { 6572.0f, 1732.9f, -1315.5f,   6605.3f, 1732.9f, -881.2f,   6605.3f, 1732.9f, -1315.5f },
    { 6605.3f, 1797.9f, -1315.5f,   6572.0f, 1732.9f, -1315.5f,   6605.3f, 1732.9f, -1315.5f },
    { 6605.3f, 1732.9f, -881.2f,   6605.3f, 1797.9f, -1315.5f,   6605.3f, 1732.9f, -1315.5f },
    { 6605.3f, 1797.9f, -881.2f,   6605.3f, 1850.3f, -1315.5f,   6605.3f, 1797.9f, -1315.5f },
    { 6572.0f, 1850.3f, -1315.5f,   6605.3f, 1797.9f, -1315.5f,   6605.3f, 1850.3f, -1315.5f },
    { 6572.0f, 1850.3f, -881.2f,   6572.0f, 1850.3f, -1315.5f,   6605.3f, 1850.3f, -1315.5f },
};

static CollisionMesh level5_chunk12_collision = {
    .triangles = level5_chunk12_collision_triangles,
    .count = 10,
};

#endif // LEVEL5_CHUNK12_COLLISION_H
