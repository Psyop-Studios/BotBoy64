// N64 Collision Data - Auto-chunked by chunk_collision.py
// Original Model: level5
// Chunk: 13
// Triangles: 5

#ifndef LEVEL5_CHUNK13_COLLISION_H
#define LEVEL5_CHUNK13_COLLISION_H

#include "../collision.h"

static CollisionTriangle level5_chunk13_collision_triangles[] = {
    { 6055.0f, 2261.7f, -822.4f,   6237.8f, 2520.2f, -822.4f,   6055.0f, 2520.2f, -765.0f },
    { 6055.0f, 2520.2f, -765.0f,   6237.8f, 2520.2f, -822.4f,   6237.8f, 2711.0f, -765.0f },
    { 6055.0f, 2520.2f, -765.0f,   6237.8f, 2711.0f, -765.0f,   6055.0f, 2711.0f, -765.0f },
    { 6237.8f, 2261.7f, -822.4f,   6257.2f, 2520.2f, -530.6f,   6237.8f, 2520.2f, -822.4f },
    { 6257.2f, 2261.7f, -530.6f,   6846.6f, 2546.0f, -530.6f,   6257.2f, 2520.2f, -530.6f },
};

static CollisionMesh level5_chunk13_collision = {
    .triangles = level5_chunk13_collision_triangles,
    .count = 5,
};

#endif // LEVEL5_CHUNK13_COLLISION_H
