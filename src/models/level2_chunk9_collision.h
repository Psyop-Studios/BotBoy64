// N64 Collision Data - Auto-chunked by chunk_collision.py
// Original Model: level2
// Chunk: 9
// Triangles: 11

#ifndef LEVEL2_CHUNK9_COLLISION_H
#define LEVEL2_CHUNK9_COLLISION_H

#include "../collision.h"

static CollisionTriangle level2_chunk9_collision_triangles[] = {
    { 1189.2f, 1500.5f, -547.1f,   1557.4f, 1619.1f, -560.9f,   1619.9f, 2360.3f, -736.7f },
    { 1189.2f, 1500.5f, -547.1f,   1619.9f, 2360.3f, -736.7f,   1069.3f, 1921.8f, -722.4f },
    { 1557.4f, 1619.1f, -560.9f,   1883.8f, 2352.3f, -691.5f,   1619.9f, 2360.3f, -736.7f },
    { 960.9f, 1505.0f, -543.8f,   1189.2f, 1500.5f, -547.1f,   1069.3f, 1921.8f, -722.4f },
    { 775.5f, 1499.7f, -547.6f,   960.9f, 1505.0f, -543.8f,   1069.3f, 1921.8f, -722.4f },
    { 775.5f, 1499.7f, -547.6f,   1069.3f, 1921.8f, -722.4f,   775.5f, 1901.6f, -718.9f },
    { 430.5f, 1541.1f, -608.9f,   645.5f, 1496.3f, -598.0f,   570.7f, 1898.2f, -764.4f },
    { 645.5f, 1496.3f, -598.0f,   775.5f, 1499.7f, -547.6f,   775.5f, 1901.6f, -718.9f },
    { 645.5f, 1496.3f, -598.0f,   775.5f, 1901.6f, -718.9f,   570.7f, 1898.2f, -764.4f },
    { 176.7f, 1525.4f, -618.8f,   430.5f, 1541.1f, -608.9f,   354.2f, 1939.3f, -806.9f },
    { 430.5f, 1541.1f, -608.9f,   570.7f, 1898.2f, -764.4f,   354.2f, 1939.3f, -806.9f },
};

static CollisionMesh level2_chunk9_collision = {
    .triangles = level2_chunk9_collision_triangles,
    .count = 11,
};

#endif // LEVEL2_CHUNK9_COLLISION_H
