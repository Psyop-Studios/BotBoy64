// N64 Collision Data - Auto-chunked by chunk_collision.py
// Original Model: level6
// Chunk: 11
// Triangles: 2

#ifndef LEVEL6_CHUNK11_COLLISION_H
#define LEVEL6_CHUNK11_COLLISION_H

#include "../collision.h"

static CollisionTriangle level6_chunk11_collision_triangles[] = {
    { 5994.1f, 1689.3f, -558.3f,   6163.1f, 1689.3f, -902.7f,   6165.0f, 2222.3f, -903.9f },
    { 5994.1f, 1689.3f, -558.3f,   6165.0f, 2222.3f, -903.9f,   5994.1f, 2222.3f, -558.3f },
};

static CollisionMesh level6_chunk11_collision = {
    .triangles = level6_chunk11_collision_triangles,
    .count = 2,
};

#endif // LEVEL6_CHUNK11_COLLISION_H
