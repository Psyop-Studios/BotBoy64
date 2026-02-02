#ifndef COLLISION_H
#define COLLISION_H

#include "constants.h"

// Simple triangle collision format
// Each triangle is 9 floats: x0,y0,z0, x1,y1,z1, x2,y2,z2
// Coordinates are in world space (after any rotation)

typedef struct {
    float x0, y0, z0;
    float x1, y1, z1;
    float x2, y2, z2;
} CollisionTriangle;

// Fast inverse square root (Quake III style) - much faster than 1.0f / sqrtf(x) on N64
static inline float fast_inv_sqrt(float x) {
    float xhalf = 0.5f * x;
    union { float f; int i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);  // Magic constant
    u.f = u.f * (1.5f - xhalf * u.f * u.f);  // Newton iteration (one is enough for collision)
    return u.f;
}

// Spatial grid cell - stores triangle indices for fast lookup
// Reduced from 192 to 96 to save ~390KB RAM (half the spatial grid size)
#define MAX_TRIS_PER_CELL 96
typedef struct {
    int16_t triIndices[MAX_TRIS_PER_CELL];
    int16_t count;
    bool overflowed;  // True if cell had more triangles than could fit (triggers full scan)
} SpatialCell;

// Spatial grid for O(1) collision lookup using spatial hashing
#define SPATIAL_GRID_WIDTH 8       // 8x8 grid
#define SPATIAL_GRID_DEPTH 8
// Cell size is computed dynamically per-mesh to fit the mesh bounds
typedef struct {
    SpatialCell cells[SPATIAL_GRID_WIDTH * SPATIAL_GRID_DEPTH];
    float gridMinX, gridMinZ;  // World space origin
    float cellSizeX, cellSizeZ;  // Dynamic cell size based on mesh bounds
    float cosR, sinR;          // Precomputed rotation (for arbitrary Y rotation support)
    bool built;
} SpatialGrid;

typedef struct {
    CollisionTriangle* triangles;
    int count;
    SpatialGrid grid;  // Spatial acceleration structure
} CollisionMesh;

// Simple collision mesh validation - just check NULL and count
static inline bool collision_mesh_is_valid(const CollisionMesh* mesh) {
    return mesh != NULL && mesh->triangles != NULL && mesh->count > 0;
}

// To export collision from Blender:
// 1. Install the addon: Edit > Preferences > Add-ons > Install > select tools/export_collision.py
// 2. Select your collision mesh in Blender
// 3. File > Export > N64 Collision Data (.h)
// 4. Use preset "T3D + 90° Y rotation" to match rendered maps
// 5. Include the exported header in your game code

// Build spatial grid for fast collision queries - call once after loading mesh
// rotY: rotation angle in radians (e.g., T3D_DEG_TO_RAD(90.0f) for standard rotation)
static inline void collision_build_spatial_grid(CollisionMesh* mesh, float mapOffX, float mapOffZ, float scaleX, float scaleZ, float rotY) {
    // NULL check - mesh can be NULL during level transitions
    if (mesh == NULL) return;

    SpatialGrid* grid = &mesh->grid;

    // Store rotation for use in collision lookups
    grid->cosR = fast_cos(rotY);
    grid->sinR = fast_sin(rotY);

    // Clear grid
    for (int i = 0; i < SPATIAL_GRID_WIDTH * SPATIAL_GRID_DEPTH; i++) {
        grid->cells[i].count = 0;
        grid->cells[i].overflowed = false;
    }

    // Find mesh bounds to align grid (using the stored rotation)
    float minX = 99999.0f, minZ = 99999.0f, maxX = -99999.0f, maxZ = -99999.0f;
    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];
        float x0 = (t->x0 * grid->cosR - t->z0 * grid->sinR) * scaleX + mapOffX;
        float z0 = (t->x0 * grid->sinR + t->z0 * grid->cosR) * scaleZ + mapOffZ;
        float x1 = (t->x1 * grid->cosR - t->z1 * grid->sinR) * scaleX + mapOffX;
        float z1 = (t->x1 * grid->sinR + t->z1 * grid->cosR) * scaleZ + mapOffZ;
        float x2 = (t->x2 * grid->cosR - t->z2 * grid->sinR) * scaleX + mapOffX;
        float z2 = (t->x2 * grid->sinR + t->z2 * grid->cosR) * scaleZ + mapOffZ;
        if (x0 < minX) minX = x0;
        if (x0 > maxX) maxX = x0;
        if (x1 < minX) minX = x1;
        if (x1 > maxX) maxX = x1;
        if (x2 < minX) minX = x2;
        if (x2 > maxX) maxX = x2;
        if (z0 < minZ) minZ = z0;
        if (z0 > maxZ) maxZ = z0;
        if (z1 < minZ) minZ = z1;
        if (z1 > maxZ) maxZ = z1;
        if (z2 < minZ) minZ = z2;
        if (z2 > maxZ) maxZ = z2;
    }

    grid->gridMinX = minX;
    grid->gridMinZ = minZ;

    // Compute cell size to fit mesh bounds (with small padding)
    float meshWidth = maxX - minX + 1.0f;
    float meshDepth = maxZ - minZ + 1.0f;
    grid->cellSizeX = meshWidth / SPATIAL_GRID_WIDTH;
    grid->cellSizeZ = meshDepth / SPATIAL_GRID_DEPTH;
    // Minimum cell size to avoid precision issues
    if (grid->cellSizeX < 50.0f) grid->cellSizeX = 50.0f;
    if (grid->cellSizeZ < 50.0f) grid->cellSizeZ = 50.0f;

    debugf("Collision grid: bounds X[%.0f,%.0f] Z[%.0f,%.0f] cellSize=[%.0f,%.0f]\n",
        minX, maxX, minZ, maxZ, grid->cellSizeX, grid->cellSizeZ);

    // Insert triangles into grid cells (using stored rotation)
    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];
        float x0 = (t->x0 * grid->cosR - t->z0 * grid->sinR) * scaleX + mapOffX;
        float z0 = (t->x0 * grid->sinR + t->z0 * grid->cosR) * scaleZ + mapOffZ;
        float x1 = (t->x1 * grid->cosR - t->z1 * grid->sinR) * scaleX + mapOffX;
        float z1 = (t->x1 * grid->sinR + t->z1 * grid->cosR) * scaleZ + mapOffZ;
        float x2 = (t->x2 * grid->cosR - t->z2 * grid->sinR) * scaleX + mapOffX;
        float z2 = (t->x2 * grid->sinR + t->z2 * grid->cosR) * scaleZ + mapOffZ;

        // Triangle AABB
        float triMinX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float triMaxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float triMinZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
        float triMaxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);

        // Add triangle to all cells it overlaps
        int cellMinX = (int)((triMinX - grid->gridMinX) / grid->cellSizeX);
        int cellMaxX = (int)((triMaxX - grid->gridMinX) / grid->cellSizeX);
        int cellMinZ = (int)((triMinZ - grid->gridMinZ) / grid->cellSizeZ);
        int cellMaxZ = (int)((triMaxZ - grid->gridMinZ) / grid->cellSizeZ);

        if (cellMinX < 0) cellMinX = 0;
        if (cellMaxX >= SPATIAL_GRID_WIDTH) cellMaxX = SPATIAL_GRID_WIDTH - 1;
        if (cellMinZ < 0) cellMinZ = 0;
        if (cellMaxZ >= SPATIAL_GRID_DEPTH) cellMaxZ = SPATIAL_GRID_DEPTH - 1;

        for (int cz = cellMinZ; cz <= cellMaxZ; cz++) {
            for (int cx = cellMinX; cx <= cellMaxX; cx++) {
                SpatialCell* cell = &grid->cells[cz * SPATIAL_GRID_WIDTH + cx];
                if (cell->count < MAX_TRIS_PER_CELL) {
                    cell->triIndices[cell->count++] = i;
                } else {
                    // Mark cell as overflowed - collision functions will fall back to full scan
                    if (!cell->overflowed) {
                        debugf("Cell [%d,%d] overflowed, will use full scan fallback\n", cx, cz);
                        cell->overflowed = true;
                    }
                }
            }
        }
    }

    grid->built = true;
}

// Check collision with a collision mesh at a given map offset and per-axis scale
// Returns the ground height at position (px, pz), or -9999 if not over any triangle
// mapOffX/Y/Z: offset to add to collision triangles (map world position)
// scaleX/Y/Z: per-axis multipliers for collision coordinates
static inline float collision_get_ground_height_scaled(CollisionMesh* mesh,
    float px, float py, float pz,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ) {
    // Validate mesh to prevent accessing corrupted/freed memory
    if (!collision_mesh_is_valid(mesh)) {
        return INVALID_GROUND_Y;
    }

    float bestY = INVALID_GROUND_Y;

    // Get rotation from grid (or use default 90°)
    float cosR = mesh->grid.built ? mesh->grid.cosR : 0.0f;
    float sinR = mesh->grid.built ? mesh->grid.sinR : 1.0f;

    // Check if we can use the spatial grid (grid built and player in bounds)
    bool useGrid = false;
    SpatialCell* cell = NULL;
    if (mesh->grid.built) {
        int cellX = (int)((px - mesh->grid.gridMinX) / mesh->grid.cellSizeX);
        int cellZ = (int)((pz - mesh->grid.gridMinZ) / mesh->grid.cellSizeZ);
        if (cellX >= 0 && cellX < SPATIAL_GRID_WIDTH && cellZ >= 0 && cellZ < SPATIAL_GRID_DEPTH) {
            cell = &mesh->grid.cells[cellZ * SPATIAL_GRID_WIDTH + cellX];
            // If cell overflowed, fall back to full scan
            if (!cell->overflowed) {
                useGrid = true;
            }
        }
    }

    // Determine how many triangles to check
    int triCount = useGrid ? cell->count : mesh->count;

    for (int idx = 0; idx < triCount; idx++) {
        int i = useGrid ? cell->triIndices[idx] : idx;
        // Bounds check to prevent accessing garbage indices during level transitions
        if (i < 0 || i >= mesh->count) continue;
        CollisionTriangle* t = &mesh->triangles[i];

        // Transform: scale -> rotate Y -> translate (correct order for non-uniform scale)
        float sx0 = t->x0 * scaleX, sz0 = t->z0 * scaleZ;
        float x0 = sx0 * cosR - sz0 * sinR + mapOffX, y0 = t->y0 * scaleY + mapOffY, z0 = sx0 * sinR + sz0 * cosR + mapOffZ;
        float sx1 = t->x1 * scaleX, sz1 = t->z1 * scaleZ;
        float x1 = sx1 * cosR - sz1 * sinR + mapOffX, y1 = t->y1 * scaleY + mapOffY, z1 = sx1 * sinR + sz1 * cosR + mapOffZ;
        float sx2 = t->x2 * scaleX, sz2 = t->z2 * scaleZ;
        float x2 = sx2 * cosR - sz2 * sinR + mapOffX, y2 = t->y2 * scaleY + mapOffY, z2 = sx2 * sinR + sz2 * cosR + mapOffZ;

        // Y-range early exit - skip if triangle is too far above/below player
        float minY = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
        float maxY = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
        if (maxY < py - 500.0f || minY > py + GROUND_SEARCH_MARGIN) continue;

        // Early AABB culling - skip triangles far from player (tight margin for speed)
        float minX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float maxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float minZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
        float maxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);
        if (px < minX - COLLISION_MARGIN || px > maxX + COLLISION_MARGIN ||
            pz < minZ - COLLISION_MARGIN || pz > maxZ + COLLISION_MARGIN) continue;

        // Calculate Y component of triangle normal (cross product)
        float ux = x1 - x0, uz = z1 - z0;
        float vx = x2 - x0, vz = z2 - z0;
        float ny = uz * vx - ux * vz;

        // Skip if not floor-like (normal not pointing up)
        if (ny < 0.0f) continue;

        // Barycentric coordinate check
        float denom = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
        if (fabsf(denom) < 0.001f) continue;

        float a = ((z1 - z2) * (px - x2) + (x2 - x1) * (pz - z2)) / denom;
        float b = ((z2 - z0) * (px - x2) + (x0 - x2) * (pz - z2)) / denom;
        float c = 1.0f - a - b;

        // Check if point is inside triangle
        if (a >= -0.01f && b >= -0.01f && c >= -0.01f) {
            // Interpolate Y height
            float groundY = a * y0 + b * y1 + c * y2;

            // Accept if below player and higher than current best
            if (groundY <= py + GROUND_SEARCH_MARGIN && groundY > bestY) {
                bestY = groundY;
            }
        }
    }

    return bestY;
}

// Version without scale (uses scale 1.0 on all axes)
static inline float collision_get_ground_height_offset(CollisionMesh* mesh,
    float px, float py, float pz,
    float mapOffX, float mapOffY, float mapOffZ) {
    return collision_get_ground_height_scaled(mesh, px, py, pz, mapOffX, mapOffY, mapOffZ, 1.0f, 1.0f, 1.0f);
}

// Simple version without offset or scale
static inline float collision_get_ground_height(CollisionMesh* mesh, float px, float py, float pz) {
    return collision_get_ground_height_scaled(mesh, px, py, pz, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
}

// ============================================================
// CEILING COLLISION (Ray-Triangle Intersection)
// ============================================================
// Uses Möller–Trumbore ray-triangle intersection for proper angled ceiling detection.
// Casts a vertical ray upward from player position and finds intersections.

// Get ceiling height at position - returns height of LOWEST ceiling above player
// Returns INVALID_CEILING_Y (99999) if no ceiling found
// Uses ray-triangle intersection to properly handle angled surfaces
static inline float collision_get_ceiling_height_scaled(CollisionMesh* mesh,
    float px, float py, float pz,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ) {
    float bestY = INVALID_CEILING_Y;  // Start very high (no ceiling)

    if (!collision_mesh_is_valid(mesh)) return bestY;

    // Get rotation from grid (or use default 90°)
    float cosR = mesh->grid.built ? mesh->grid.cosR : 0.0f;
    float sinR = mesh->grid.built ? mesh->grid.sinR : 1.0f;

    // Ray origin is player position, direction is straight up (0, 1, 0)
    // For upward ray, we simplify the Möller–Trumbore algorithm

    int triCount = mesh->count;

    for (int idx = 0; idx < triCount; idx++) {
        CollisionTriangle* t = &mesh->triangles[idx];

        // Transform: scale -> rotate Y -> translate (correct order for non-uniform scale)
        float sx0 = t->x0 * scaleX, sz0 = t->z0 * scaleZ;
        float x0 = sx0 * cosR - sz0 * sinR + mapOffX;
        float y0 = t->y0 * scaleY + mapOffY;
        float z0 = sx0 * sinR + sz0 * cosR + mapOffZ;
        float sx1 = t->x1 * scaleX, sz1 = t->z1 * scaleZ;
        float x1 = sx1 * cosR - sz1 * sinR + mapOffX;
        float y1 = t->y1 * scaleY + mapOffY;
        float z1 = sx1 * sinR + sz1 * cosR + mapOffZ;
        float sx2 = t->x2 * scaleX, sz2 = t->z2 * scaleZ;
        float x2 = sx2 * cosR - sz2 * sinR + mapOffX;
        float y2 = t->y2 * scaleY + mapOffY;
        float z2 = sx2 * sinR + sz2 * cosR + mapOffZ;

        // Get min/max Y of triangle for early exit
        float minY = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
        float maxY = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);

        // Skip if triangle is entirely below player or too far above
        if (maxY < py || minY > py + CEILING_SEARCH_MARGIN) continue;

        // Early AABB culling in XZ (with generous margin for angled surfaces)
        float minX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float maxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float minZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
        float maxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);
        float margin = COLLISION_MARGIN + PLAYER_RADIUS;  // Extra margin for player width
        if (px < minX - margin || px > maxX + margin ||
            pz < minZ - margin || pz > maxZ + margin) continue;

        // Calculate triangle edges
        float e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
        float e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;

        // Calculate triangle normal to filter out horizontal floors
        // (We still want to hit angled surfaces that could block upward movement)
        float ny = e1z * e2x - e1x * e2z;  // Y component of cross(e1, e2)
        float nx = e1y * e2z - e1z * e2y;
        float nz = e1x * e2y - e1y * e2x;
        float normalLenSq = nx*nx + ny*ny + nz*nz;
        if (normalLenSq < 0.001f) continue;

        // Only skip very horizontal floors (normal pointing strongly up)
        // Allow angled surfaces on both sides - use fabsf for the normal check
        // Skip if |ny| > 0.85 * |normal| AND ny > 0 (floor, not ceiling)
        float normalLen = sqrtf(normalLenSq);
        if (ny > 0.85f * normalLen) continue;  // Skip horizontal floors only

        // Möller–Trumbore for upward ray (direction = 0, 1, 0)
        // We test BOTH sides of the triangle (double-sided collision)
        // h = cross(D, e2) = (e2z, 0, -e2x)
        float hx = e2z, hz = -e2x;

        // a = dot(e1, h)
        float a = e1x * hx + e1z * hz;

        // If a is near zero, ray is parallel to triangle
        float absA = fabsf(a);
        if (absA < 0.0001f) continue;

        float f = 1.0f / a;

        // s = ray_origin - v0
        float sx = px - x0, sy = py - y0, sz = pz - z0;

        // u = f * dot(s, h)
        float u = f * (sx * hx + sz * hz);
        if (u < -0.02f || u > 1.02f) continue;  // Tolerance for edges

        // q = cross(s, e1)
        float qx = sy * e1z - sz * e1y;
        float qy = sz * e1x - sx * e1z;
        float qz = sx * e1y - sy * e1x;

        // v = f * dot(D, q) = f * qy
        float v = f * qy;
        if (v < -0.02f || u + v > 1.02f) continue;

        // t = f * dot(e2, q) - distance along ray
        float rayT = f * (e2x * qx + e2y * qy + e2z * qz);

        // We want intersections ABOVE the player (t > 0)
        // Accept both frontface and backface hits
        if (rayT > 0.0f) {
            float ceilingY = py + rayT;
            if (ceilingY < bestY) {
                bestY = ceilingY;
            }
        }
    }

    return bestY;
}

// Simple version without offset or scale
static inline float collision_get_ceiling_height(CollisionMesh* mesh, float px, float py, float pz) {
    return collision_get_ceiling_height_scaled(mesh, px, py, pz, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
}

// Helper: Check single ground triangle and return height + normal if hit
static inline bool collision_check_ground_tri_normal(CollisionTriangle* t,
    float px, float py, float pz,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float cosR, float sinR,
    float* outY, float* outNX, float* outNY, float* outNZ) {

    // Transform: scale -> rotate Y -> translate (correct order for non-uniform scale)
    float sx0 = t->x0 * scaleX, sz0 = t->z0 * scaleZ;
    float x0 = sx0 * cosR - sz0 * sinR + mapOffX, y0 = t->y0 * scaleY + mapOffY, z0 = sx0 * sinR + sz0 * cosR + mapOffZ;
    float sx1 = t->x1 * scaleX, sz1 = t->z1 * scaleZ;
    float x1 = sx1 * cosR - sz1 * sinR + mapOffX, y1 = t->y1 * scaleY + mapOffY, z1 = sx1 * sinR + sz1 * cosR + mapOffZ;
    float sx2 = t->x2 * scaleX, sz2 = t->z2 * scaleZ;
    float x2 = sx2 * cosR - sz2 * sinR + mapOffX, y2 = t->y2 * scaleY + mapOffY, z2 = sx2 * sinR + sz2 * cosR + mapOffZ;

    // Early AABB culling
    float minX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    float maxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    float minZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
    float maxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);
    if (px < minX - COLLISION_MARGIN || px > maxX + COLLISION_MARGIN ||
        pz < minZ - COLLISION_MARGIN || pz > maxZ + COLLISION_MARGIN) return false;

    // Calculate full triangle normal
    float ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
    float vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;

    // Skip if not floor-like
    if (ny < 0.0f) return false;

    // Barycentric coordinate check
    float denom = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
    if (fabsf(denom) < 0.001f) return false;

    float a = ((z1 - z2) * (px - x2) + (x2 - x1) * (pz - z2)) / denom;
    float b = ((z2 - z0) * (px - x2) + (x0 - x2) * (pz - z2)) / denom;
    float c = 1.0f - a - b;

    if (a >= -0.01f && b >= -0.01f && c >= -0.01f) {
        float groundY = a * y0 + b * y1 + c * y2;
        if (groundY <= py + GROUND_SEARCH_MARGIN) {
            *outY = groundY;
            // Normalize normal
            float lenSq = nx*nx + ny*ny + nz*nz;
            if (lenSq > 0.001f) {
                float invLen = fast_inv_sqrt(lenSq);
                *outNX = nx * invLen;
                *outNY = ny * invLen;
                *outNZ = nz * invLen;
            }
            return true;
        }
    }
    return false;
}

// Get ground height AND normal at a point (for slope detection and shadow projection)
// Returns ground Y, outputs normal via outNX, outNY, outNZ
// Finds the HIGHEST ground surface below (or slightly above) the player
static inline float collision_get_ground_height_normal(CollisionMesh* mesh,
    float px, float py, float pz,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float* outNX, float* outNY, float* outNZ) {

    float bestY = INVALID_GROUND_Y;
    *outNX = 0.0f; *outNY = 1.0f; *outNZ = 0.0f;  // Default: up

    if (!collision_mesh_is_valid(mesh)) return bestY;

    // Get rotation from grid (or use default 90°)
    float cosR = mesh->grid.built ? mesh->grid.cosR : 0.0f;
    float sinR = mesh->grid.built ? mesh->grid.sinR : 1.0f;

    // Check if we can use the spatial grid
    bool useGrid = false;
    SpatialCell* cell = NULL;
    if (mesh->grid.built) {
        int cellX = (int)((px - mesh->grid.gridMinX) / mesh->grid.cellSizeX);
        int cellZ = (int)((pz - mesh->grid.gridMinZ) / mesh->grid.cellSizeZ);
        if (cellX >= 0 && cellX < SPATIAL_GRID_WIDTH && cellZ >= 0 && cellZ < SPATIAL_GRID_DEPTH) {
            cell = &mesh->grid.cells[cellZ * SPATIAL_GRID_WIDTH + cellX];
            if (!cell->overflowed) {
                useGrid = true;
            }
        }
    }

    int triCount = useGrid ? cell->count : mesh->count;
    for (int idx = 0; idx < triCount; idx++) {
        int i = useGrid ? cell->triIndices[idx] : idx;
        // Bounds check to prevent accessing garbage indices during level transitions
        if (i < 0 || i >= mesh->count) continue;
        float hitY, nx, ny, nz;
        if (collision_check_ground_tri_normal(&mesh->triangles[i],
            px, py, pz, mapOffX, mapOffY, mapOffZ,
            scaleX, scaleY, scaleZ,
            cosR, sinR,
            &hitY, &nx, &ny, &nz)) {
            if (hitY > bestY) {
                bestY = hitY;
                *outNX = nx; *outNY = ny; *outNZ = nz;
            }
        }
    }

    return bestY;
}

// Helper: Check single wall triangle and accumulate push
static inline bool collision_check_wall_tri(CollisionTriangle* t,
    float px, float py, float pz, float radius, float playerHeight,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float cosR, float sinR,
    float* outPushX, float* outPushZ) {

    // Transform: scale -> rotate Y -> translate (correct order for non-uniform scale)
    float sx0 = t->x0 * scaleX, sz0 = t->z0 * scaleZ;
    float x0 = sx0 * cosR - sz0 * sinR + mapOffX, y0 = t->y0 * scaleY + mapOffY, z0 = sx0 * sinR + sz0 * cosR + mapOffZ;
    float sx1 = t->x1 * scaleX, sz1 = t->z1 * scaleZ;
    float x1 = sx1 * cosR - sz1 * sinR + mapOffX, y1 = t->y1 * scaleY + mapOffY, z1 = sx1 * sinR + sz1 * cosR + mapOffZ;
    float sx2 = t->x2 * scaleX, sz2 = t->z2 * scaleZ;
    float x2 = sx2 * cosR - sz2 * sinR + mapOffX, y2 = t->y2 * scaleY + mapOffY, z2 = sx2 * sinR + sz2 * cosR + mapOffZ;

    // Calculate triangle normal
    float ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
    float vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;

    // Normalize
    float lenSq = nx*nx + ny*ny + nz*nz;
    if (lenSq < 0.001f) return false;
    float invLen = fast_inv_sqrt(lenSq);
    nx *= invLen; ny *= invLen; nz *= invLen;

    // Skip floor/slope triangles (normal pointing mostly up) - we want walls only
    if (fabsf(ny) > 0.5f) return false;

    // Check if player Y overlaps with triangle Y range
    // Only check upper half of player body - allows ledge grabs and makes jumps feel better
    float minY = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    float maxY = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    float playerCheckY = py + playerHeight * 0.5f;  // Start checking from halfway up
    if (playerCheckY > maxY || py + playerHeight < minY) return false;

    // Calculate signed distance from player to triangle plane
    float dist = nx * (px - x0) + ny * (py - y0) + nz * (pz - z0);
    if (dist < 0 || dist > radius) return false;

    // Check if player XZ is near the triangle
    float triMinX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    float triMaxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    float triMinZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
    float triMaxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);

    if (px < triMinX - radius || px > triMaxX + radius) return false;
    if (pz < triMinZ - radius || pz > triMaxZ + radius) return false;

    // Push player out along the wall normal (XZ only)
    float pushDist = radius - dist + 0.1f;
    float hLenSq = nx*nx + nz*nz;
    if (hLenSq > 0.001f) {
        float hInvLen = fast_inv_sqrt(hLenSq);
        *outPushX += nx * hInvLen * pushDist;
        *outPushZ += nz * hInvLen * pushDist;
        return true;
    }
    return false;
}

// Check wall collision and return push-out vector (CLEAN REWRITE)
// Uses simple plane-distance approach like the original working version
// px,py,pz: player position, radius: collision radius, playerHeight: for Y overlap check
// outPushX/Z: filled with push direction if collision detected
// Returns true if wall collision occurred
static inline bool collision_check_walls_ex(CollisionMesh* mesh,
    float px, float py, float pz, float radius, float playerHeight,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float* outPushX, float* outPushZ,
    float playerGroundY) {

    (void)playerGroundY;  // Not used in clean version - Y overlap handles it

    *outPushX = 0.0f;
    *outPushZ = 0.0f;

    // Validate mesh to prevent accessing corrupted/freed memory
    if (!collision_mesh_is_valid(mesh)) return false;

    bool collided = false;

    // Get rotation from grid (or use default 90°)
    float cosR = mesh->grid.built ? mesh->grid.cosR : 0.0f;
    float sinR = mesh->grid.built ? mesh->grid.sinR : 1.0f;

    // Check if we can use the spatial grid
    bool useGrid = false;
    SpatialCell* cell = NULL;
    if (mesh->grid.built) {
        int cellX = (int)((px - mesh->grid.gridMinX) / mesh->grid.cellSizeX);
        int cellZ = (int)((pz - mesh->grid.gridMinZ) / mesh->grid.cellSizeZ);
        if (cellX >= 0 && cellX < SPATIAL_GRID_WIDTH && cellZ >= 0 && cellZ < SPATIAL_GRID_DEPTH) {
            cell = &mesh->grid.cells[cellZ * SPATIAL_GRID_WIDTH + cellX];
            if (!cell->overflowed) {
                useGrid = true;
            }
        }
    }

    int triCount = useGrid ? cell->count : mesh->count;
    for (int idx = 0; idx < triCount; idx++) {
        int i = useGrid ? cell->triIndices[idx] : idx;
        // Bounds check to prevent accessing garbage indices during level transitions
        if (i < 0 || i >= mesh->count) continue;
        if (collision_check_wall_tri(&mesh->triangles[i],
            px, py, pz, radius, playerHeight,
            mapOffX, mapOffY, mapOffZ,
            scaleX, scaleY, scaleZ,
            cosR, sinR,
            outPushX, outPushZ)) {
            collided = true;
        }
    }

    // Clamp total push to prevent getting shot out at corners
    float totalPushSq = (*outPushX) * (*outPushX) + (*outPushZ) * (*outPushZ);
    float maxPush = radius * 0.5f;
    if (totalPushSq > maxPush * maxPush) {
        float scale = maxPush * fast_inv_sqrt(totalPushSq);
        *outPushX *= scale;
        *outPushZ *= scale;
    }

    return collided;
}

// Backwards-compatible wrapper (doesn't know about ground height)
static inline bool collision_check_walls(CollisionMesh* mesh,
    float px, float py, float pz, float radius, float playerHeight,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float* outPushX, float* outPushZ) {
    return collision_check_walls_ex(mesh, px, py, pz, radius, playerHeight,
        mapOffX, mapOffY, mapOffZ, scaleX, scaleY, scaleZ,
        outPushX, outPushZ, INVALID_GROUND_Y);
}

// Check if point is inside collision mesh volume (for trigger zones like damage areas)
// Returns true if player is inside ANY triangle's bounding volume
static inline bool collision_check_inside_volume(CollisionMesh* mesh,
    float px, float py, float pz, float radius,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ) {

    // NULL check - mesh can be NULL during level transitions
    if (mesh == NULL || mesh->triangles == NULL || mesh->count == 0) return false;

    // Build AABB (axis-aligned bounding box) of the entire mesh with scale
    float minX = 9999.0f, minY = 9999.0f, minZ = 9999.0f;
    float maxX = INVALID_GROUND_Y, maxY = INVALID_GROUND_Y, maxZ = INVALID_GROUND_Y;

    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];

        // Apply 90° Y rotation and scale (same as other collision functions)
        float x0 = -t->z0 * scaleX + mapOffX, y0 = t->y0 * scaleY + mapOffY, z0 = t->x0 * scaleZ + mapOffZ;
        float x1 = -t->z1 * scaleX + mapOffX, y1 = t->y1 * scaleY + mapOffY, z1 = t->x1 * scaleZ + mapOffZ;
        float x2 = -t->z2 * scaleX + mapOffX, y2 = t->y2 * scaleY + mapOffY, z2 = t->x2 * scaleZ + mapOffZ;

        // Expand bounding box
        if (x0 < minX) minX = x0;
        if (x0 > maxX) maxX = x0;
        if (x1 < minX) minX = x1;
        if (x1 > maxX) maxX = x1;
        if (x2 < minX) minX = x2;
        if (x2 > maxX) maxX = x2;

        if (y0 < minY) minY = y0;
        if (y0 > maxY) maxY = y0;
        if (y1 < minY) minY = y1;
        if (y1 > maxY) maxY = y1;
        if (y2 < minY) minY = y2;
        if (y2 > maxY) maxY = y2;

        if (z0 < minZ) minZ = z0;
        if (z0 > maxZ) maxZ = z0;
        if (z1 < minZ) minZ = z1;
        if (z1 > maxZ) maxZ = z1;
        if (z2 < minZ) minZ = z2;
        if (z2 > maxZ) maxZ = z2;
    }

    // Check if point is inside AABB (ignore radius for trigger zones)
    // This gives accurate collision across the entire mesh bounds
    (void)radius;  // Unused for point-in-AABB check

    return (px >= minX && px <= maxX &&
            py >= minY && py <= maxY &&
            pz >= minZ && pz <= maxZ);
}

// ============================================================
// ROTATED COLLISION FUNCTIONS (for decorations with arbitrary Y rotation)
// These don't use the hardcoded 90° rotation - they use the provided rotY angle
// ============================================================

// Helper: Transform a point by rotation and scale for decoration collision
static inline void collision_transform_point_rotated(
    float inX, float inY, float inZ,
    float rotY, float scaleX, float scaleY, float scaleZ,
    float offX, float offY, float offZ,
    float* outX, float* outY, float* outZ) {

    float cosR = fast_cos(rotY);
    float sinR = fast_sin(rotY);
    *outX = (inX * cosR - inZ * sinR) * scaleX + offX;
    *outY = inY * scaleY + offY;
    *outZ = (inX * sinR + inZ * cosR) * scaleZ + offZ;
}

// Get ground height with arbitrary Y rotation (for decorations)
static inline float collision_get_ground_height_rotated(CollisionMesh* mesh,
    float px, float py, float pz,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float rotY) {

    float bestY = INVALID_GROUND_Y;
    if (!collision_mesh_is_valid(mesh)) return bestY;

    float cosR = fast_cos(rotY);
    float sinR = fast_sin(rotY);

    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];

        // Transform: scale -> rotate Y -> translate (correct order for non-uniform scale)
        // Vertex 0
        float sx0 = t->x0 * scaleX;
        float sz0 = t->z0 * scaleZ;
        float x0 = sx0 * cosR - sz0 * sinR + mapOffX;
        float y0 = t->y0 * scaleY + mapOffY;
        float z0 = sx0 * sinR + sz0 * cosR + mapOffZ;
        // Vertex 1
        float sx1 = t->x1 * scaleX;
        float sz1 = t->z1 * scaleZ;
        float x1 = sx1 * cosR - sz1 * sinR + mapOffX;
        float y1 = t->y1 * scaleY + mapOffY;
        float z1 = sx1 * sinR + sz1 * cosR + mapOffZ;
        // Vertex 2
        float sx2 = t->x2 * scaleX;
        float sz2 = t->z2 * scaleZ;
        float x2 = sx2 * cosR - sz2 * sinR + mapOffX;
        float y2 = t->y2 * scaleY + mapOffY;
        float z2 = sx2 * sinR + sz2 * cosR + mapOffZ;

        // Early AABB culling
        float minX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float maxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float minZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
        float maxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);

        if (px < minX - COLLISION_MARGIN || px > maxX + COLLISION_MARGIN) continue;
        if (pz < minZ - COLLISION_MARGIN || pz > maxZ + COLLISION_MARGIN) continue;

        // Calculate triangle normal Y component (to skip walls)
        float ux = x1 - x0, uz = z1 - z0;
        float vx = x2 - x0, vz = z2 - z0;
        float ny = uz * vx - ux * vz;

        // Skip walls (normal mostly horizontal)
        if (fabsf(ny) < 0.3f) continue;

        // Barycentric test
        float v0x = x2 - x0, v0z = z2 - z0;
        float v1x = x1 - x0, v1z = z1 - z0;
        float v2x = px - x0, v2z = pz - z0;

        float dot00 = v0x * v0x + v0z * v0z;
        float dot01 = v0x * v1x + v0z * v1z;
        float dot02 = v0x * v2x + v0z * v2z;
        float dot11 = v1x * v1x + v1z * v1z;
        float dot12 = v1x * v2x + v1z * v2z;

        // Protect against division by zero (degenerate triangle)
        float denom = dot00 * dot11 - dot01 * dot01;
        if (fabsf(denom) < 0.0001f) continue;

        float invDenom = 1.0f / denom;
        float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        if (u >= -0.01f && v >= -0.01f && (u + v) <= 1.01f) {
            float groundY = y0 + (y1 - y0) * v + (y2 - y0) * u;
            if (groundY > bestY && groundY <= py + GROUND_SEARCH_MARGIN) {
                bestY = groundY;
            }
        }
    }

    return bestY;
}

// Check wall collision with arbitrary Y rotation (for decorations)
static inline bool collision_check_walls_rotated(CollisionMesh* mesh,
    float px, float py, float pz, float radius, float playerHeight,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float rotY,
    float* outPushX, float* outPushZ) {

    *outPushX = 0.0f;
    *outPushZ = 0.0f;
    bool collided = false;

    // Use robust validation to catch corrupted/freed mesh pointers
    if (!collision_mesh_is_valid(mesh)) return false;

    float cosR = fast_cos(rotY);
    float sinR = fast_sin(rotY);

    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];

        // Transform: scale -> rotate Y -> translate (correct order for non-uniform scale)
        float sx0 = t->x0 * scaleX, sz0 = t->z0 * scaleZ;
        float x0 = sx0 * cosR - sz0 * sinR + mapOffX;
        float y0 = t->y0 * scaleY + mapOffY;
        float z0 = sx0 * sinR + sz0 * cosR + mapOffZ;
        float sx1 = t->x1 * scaleX, sz1 = t->z1 * scaleZ;
        float x1 = sx1 * cosR - sz1 * sinR + mapOffX;
        float y1 = t->y1 * scaleY + mapOffY;
        float z1 = sx1 * sinR + sz1 * cosR + mapOffZ;
        float sx2 = t->x2 * scaleX, sz2 = t->z2 * scaleZ;
        float x2 = sx2 * cosR - sz2 * sinR + mapOffX;
        float y2 = t->y2 * scaleY + mapOffY;
        float z2 = sx2 * sinR + sz2 * cosR + mapOffZ;

        // Calculate triangle normal
        float ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
        float vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;

        // Normalize
        float lenSq = nx*nx + ny*ny + nz*nz;
        if (lenSq < 0.001f) continue;
        float invLen = fast_inv_sqrt(lenSq);
        nx *= invLen; ny *= invLen; nz *= invLen;

        // Skip floor/slope triangles (normal pointing mostly up) - we want walls only
        if (fabsf(ny) > 0.5f) continue;

        // Check if player Y overlaps with triangle Y range
        float minY = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
        float maxY = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
        if (py > maxY || py + playerHeight < minY) continue;

        // Calculate signed distance from player to triangle plane
        float dist = nx * (px - x0) + ny * (py - y0) + nz * (pz - z0);
        if (dist < 0 || dist > radius) continue;

        // Check if player XZ is near the triangle
        float triMinX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float triMaxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float triMinZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
        float triMaxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);

        if (px < triMinX - radius || px > triMaxX + radius) continue;
        if (pz < triMinZ - radius || pz > triMaxZ + radius) continue;

        // Push player out along the wall normal (XZ only)
        float pushDist = radius - dist + 0.1f;
        float hLenSq = nx*nx + nz*nz;
        if (hLenSq > 0.001f) {
            float hInvLen = fast_inv_sqrt(hLenSq);
            *outPushX += nx * hInvLen * pushDist;
            *outPushZ += nz * hInvLen * pushDist;
            collided = true;
        }
    }

    // Clamp total push
    float totalPushSq = (*outPushX) * (*outPushX) + (*outPushZ) * (*outPushZ);
    float maxPush = radius * 0.5f;
    if (totalPushSq > maxPush * maxPush) {
        float scale = maxPush * fast_inv_sqrt(totalPushSq);
        *outPushX *= scale;
        *outPushZ *= scale;
    }

    return collided;
}

// ============================================================
// FULL 3D ROTATION COLLISION (for cogs and other rotating objects)
// Supports rotation around all three axes (X, Y, Z)
// ============================================================

// Helper: Transform a point by full XYZ rotation and scale
// Rotation order: Z * Y * X (standard Euler)
// Note: Z rotation uses -sz to match T3D's rotation convention
static inline void collision_transform_point_full(
    float inX, float inY, float inZ,
    float rotX, float rotY, float rotZ,
    float scaleX, float scaleY, float scaleZ,
    float offX, float offY, float offZ,
    float* outX, float* outY, float* outZ) {

    float cx = fast_cos(rotX), sx = fast_sin(rotX);
    float cy = fast_cos(rotY), sy = fast_sin(rotY);
    float cz = fast_cos(rotZ), sz = fast_sin(rotZ);

    // Apply rotation: first X, then Y, then Z
    // X rotation
    float y1 = inY * cx - inZ * sx;
    float z1 = inY * sx + inZ * cx;
    float x1 = inX;

    // Y rotation
    float x2 = x1 * cy + z1 * sy;
    float z2 = -x1 * sy + z1 * cy;
    float y2 = y1;

    // Z rotation (negate sz to match T3D convention)
    float x3 = x2 * cz + y2 * sz;
    float y3 = -x2 * sz + y2 * cz;
    float z3 = z2;

    // Apply scale and offset
    *outX = x3 * scaleX + offX;
    *outY = y3 * scaleY + offY;
    *outZ = z3 * scaleZ + offZ;
}

// Get ground height with full XYZ rotation (for cogs)
static inline float collision_get_ground_height_full_rotation(CollisionMesh* mesh,
    float px, float py, float pz,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float rotX, float rotY, float rotZ) {

    float bestY = INVALID_GROUND_Y;
    if (!collision_mesh_is_valid(mesh)) return bestY;

    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];

        // Transform all three vertices with full rotation
        float x0, y0, z0, x1, y1, z1, x2, y2, z2;
        collision_transform_point_full(t->x0, t->y0, t->z0, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x0, &y0, &z0);
        collision_transform_point_full(t->x1, t->y1, t->z1, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x1, &y1, &z1);
        collision_transform_point_full(t->x2, t->y2, t->z2, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x2, &y2, &z2);

        // Early AABB culling
        float minX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float maxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float minZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
        float maxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);

        if (px < minX - COLLISION_MARGIN || px > maxX + COLLISION_MARGIN) continue;
        if (pz < minZ - COLLISION_MARGIN || pz > maxZ + COLLISION_MARGIN) continue;

        // Calculate triangle normal (Y component only - for ground detection)
        float ux = x1 - x0, uz = z1 - z0;
        float vx = x2 - x0, vz = z2 - z0;
        float ny = uz * vx - ux * vz;

        // Skip walls/ceilings (normal not pointing up enough)
        if (ny < 0.3f) continue;

        // Barycentric test
        float v0x = x2 - x0, v0z = z2 - z0;
        float v1x = x1 - x0, v1z = z1 - z0;
        float v2x = px - x0, v2z = pz - z0;

        float dot00 = v0x * v0x + v0z * v0z;
        float dot01 = v0x * v1x + v0z * v1z;
        float dot02 = v0x * v2x + v0z * v2z;
        float dot11 = v1x * v1x + v1z * v1z;
        float dot12 = v1x * v2x + v1z * v2z;

        float denom = dot00 * dot11 - dot01 * dot01;
        if (fabsf(denom) < 0.0001f) continue;

        float invDenom = 1.0f / denom;
        float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        if (u >= -0.01f && v >= -0.01f && (u + v) <= 1.01f) {
            float groundY = y0 + (y1 - y0) * v + (y2 - y0) * u;
            if (groundY > bestY && groundY <= py + GROUND_SEARCH_MARGIN) {
                bestY = groundY;
            }
        }
    }

    return bestY;
}

// Get ANY surface height with full XYZ rotation (for cogs - no angle threshold)
// This finds surfaces regardless of tilt angle, for "sticky" cog riding
static inline float collision_get_surface_height_full_rotation(CollisionMesh* mesh,
    float px, float py, float pz,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float rotX, float rotY, float rotZ) {

    float bestY = INVALID_GROUND_Y;
    if (!collision_mesh_is_valid(mesh)) return bestY;

    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];

        // Transform all three vertices with full rotation
        float x0, y0, z0, x1, y1, z1, x2, y2, z2;
        collision_transform_point_full(t->x0, t->y0, t->z0, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x0, &y0, &z0);
        collision_transform_point_full(t->x1, t->y1, t->z1, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x1, &y1, &z1);
        collision_transform_point_full(t->x2, t->y2, t->z2, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x2, &y2, &z2);

        // Early AABB culling (with larger margin for tilted surfaces)
        float minX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float maxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float minZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
        float maxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);

        float margin = COLLISION_MARGIN * 2.0f;
        if (px < minX - margin || px > maxX + margin) continue;
        if (pz < minZ - margin || pz > maxZ + margin) continue;

        // For rotating platforms like cogs, we DON'T skip any surfaces based on normal
        // What's a "ceiling" now could be a "floor" as the cog rotates
        // The player should collide with whichever surface is highest beneath them

        // Barycentric test
        float v0x = x2 - x0, v0z = z2 - z0;
        float v1x = x1 - x0, v1z = z1 - z0;
        float v2x = px - x0, v2z = pz - z0;

        float dot00 = v0x * v0x + v0z * v0z;
        float dot01 = v0x * v1x + v0z * v1z;
        float dot02 = v0x * v2x + v0z * v2z;
        float dot11 = v1x * v1x + v1z * v1z;
        float dot12 = v1x * v2x + v1z * v2z;

        float denom = dot00 * dot11 - dot01 * dot01;
        if (fabsf(denom) < 0.0001f) continue;

        float invDenom = 1.0f / denom;
        float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        // Slightly more lenient bounds for tilted surfaces
        if (u >= -0.05f && v >= -0.05f && (u + v) <= 1.05f) {
            float surfaceY = y0 + (y1 - y0) * v + (y2 - y0) * u;
            // Accept surfaces below player (with generous margin for cog riding)
            if (surfaceY > bestY && surfaceY <= py + 30.0f) {
                bestY = surfaceY;
            }
        }
    }

    return bestY;
}

// Get ground height AND normal with full XYZ rotation (for cogs - needed for slope push)
static inline float collision_get_ground_height_normal_full_rotation(CollisionMesh* mesh,
    float px, float py, float pz,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float rotX, float rotY, float rotZ,
    float* outNX, float* outNY, float* outNZ) {

    float bestY = INVALID_GROUND_Y;
    *outNX = 0.0f; *outNY = 1.0f; *outNZ = 0.0f;  // Default: up

    if (!collision_mesh_is_valid(mesh)) return bestY;

    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];

        // Transform all three vertices with full rotation
        float x0, y0, z0, x1, y1, z1, x2, y2, z2;
        collision_transform_point_full(t->x0, t->y0, t->z0, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x0, &y0, &z0);
        collision_transform_point_full(t->x1, t->y1, t->z1, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x1, &y1, &z1);
        collision_transform_point_full(t->x2, t->y2, t->z2, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x2, &y2, &z2);

        // Early AABB culling
        float minX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float maxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float minZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
        float maxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);

        if (px < minX - COLLISION_MARGIN || px > maxX + COLLISION_MARGIN) continue;
        if (pz < minZ - COLLISION_MARGIN || pz > maxZ + COLLISION_MARGIN) continue;

        // Calculate triangle normal
        float ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
        float vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;

        // Normalize
        float lenSq = nx*nx + ny*ny + nz*nz;
        if (lenSq < 0.001f) continue;
        float invLen = fast_inv_sqrt(lenSq);
        nx *= invLen; ny *= invLen; nz *= invLen;

        // Skip walls/ceilings (normal not pointing up enough)
        if (ny < 0.3f) continue;

        // Barycentric test
        float v0x = x2 - x0, v0z = z2 - z0;
        float v1x = x1 - x0, v1z = z1 - z0;
        float v2x = px - x0, v2z = pz - z0;

        float dot00 = v0x * v0x + v0z * v0z;
        float dot01 = v0x * v1x + v0z * v1z;
        float dot02 = v0x * v2x + v0z * v2z;
        float dot11 = v1x * v1x + v1z * v1z;
        float dot12 = v1x * v2x + v1z * v2z;

        float denom = dot00 * dot11 - dot01 * dot01;
        if (fabsf(denom) < 0.0001f) continue;

        float invDenom = 1.0f / denom;
        float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        if (u >= -0.01f && v >= -0.01f && (u + v) <= 1.01f) {
            float groundY = y0 + (y1 - y0) * v + (y2 - y0) * u;
            if (groundY > bestY && groundY <= py + GROUND_SEARCH_MARGIN) {
                bestY = groundY;
                *outNX = nx;
                *outNY = ny;
                *outNZ = nz;
            }
        }
    }

    return bestY;
}

// Check wall collision with full XYZ rotation (for cogs)
static inline bool collision_check_walls_full_rotation(CollisionMesh* mesh,
    float px, float py, float pz, float radius, float playerHeight,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ,
    float rotX, float rotY, float rotZ,
    float* outPushX, float* outPushZ) {

    *outPushX = 0.0f;
    *outPushZ = 0.0f;
    bool collided = false;

    if (!collision_mesh_is_valid(mesh)) return false;

    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];

        // Transform all three vertices with full rotation
        float x0, y0, z0, x1, y1, z1, x2, y2, z2;
        collision_transform_point_full(t->x0, t->y0, t->z0, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x0, &y0, &z0);
        collision_transform_point_full(t->x1, t->y1, t->z1, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x1, &y1, &z1);
        collision_transform_point_full(t->x2, t->y2, t->z2, rotX, rotY, rotZ,
            scaleX, scaleY, scaleZ, mapOffX, mapOffY, mapOffZ, &x2, &y2, &z2);

        // Calculate triangle normal
        float ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
        float vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;

        // Normalize
        float lenSq = nx*nx + ny*ny + nz*nz;
        if (lenSq < 0.001f) continue;
        float invLen = fast_inv_sqrt(lenSq);
        nx *= invLen; ny *= invLen; nz *= invLen;

        // Skip floor/ceiling triangles (normal pointing mostly up/down) - we want walls only
        if (fabsf(ny) > 0.7f) continue;

        // Check if player Y overlaps with triangle Y range
        float minY = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
        float maxY = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
        if (py > maxY || py + playerHeight < minY) continue;

        // Calculate signed distance from player to triangle plane
        float dist = nx * (px - x0) + ny * (py - y0) + nz * (pz - z0);
        if (dist < 0 || dist > radius) continue;

        // Check if player XZ is near the triangle
        float triMinX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float triMaxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float triMinZ = z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2);
        float triMaxZ = z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2);

        if (px < triMinX - radius || px > triMaxX + radius) continue;
        if (pz < triMinZ - radius || pz > triMaxZ + radius) continue;

        // Push player out along the wall normal (XZ only)
        float pushDist = radius - dist + 0.1f;
        float hLenSq = nx*nx + nz*nz;
        if (hLenSq > 0.001f) {
            float hInvLen = fast_inv_sqrt(hLenSq);
            *outPushX += nx * hInvLen * pushDist;
            *outPushZ += nz * hInvLen * pushDist;
            collided = true;
        }
    }

    // Clamp total push
    float totalPushSq = (*outPushX) * (*outPushX) + (*outPushZ) * (*outPushZ);
    float maxPush = radius * 0.5f;
    if (totalPushSq > maxPush * maxPush) {
        float scale = maxPush * fast_inv_sqrt(totalPushSq);
        *outPushX *= scale;
        *outPushZ *= scale;
    }

    return collided;
}

// ============================================================
// RAYCAST - Check if line of sight is blocked by geometry
// Uses Möller–Trumbore ray-triangle intersection
// Returns true if ray hits something before reaching target
// ============================================================
static inline bool collision_raycast_blocked(CollisionMesh* mesh,
    float fromX, float fromY, float fromZ,
    float toX, float toY, float toZ,
    float mapOffX, float mapOffY, float mapOffZ,
    float scaleX, float scaleY, float scaleZ) {

    // Use robust validation to catch corrupted/freed mesh pointers
    if (!collision_mesh_is_valid(mesh)) return false;

    // Ray direction
    float dirX = toX - fromX;
    float dirY = toY - fromY;
    float dirZ = toZ - fromZ;
    float rayLenSq = dirX*dirX + dirY*dirY + dirZ*dirZ;
    // Skip very short rays to avoid FPU denormal issues from 1/rayLen
    if (rayLenSq < 1.0f) return false;
    float rayLen = sqrtf(rayLenSq);

    // Normalize direction
    float invLen = 1.0f / rayLen;
    dirX *= invLen;
    dirY *= invLen;
    dirZ *= invLen;

    const float EPSILON = 0.0001f;

    // Get rotation from grid (or use default 90° = cos(90)=0, sin(90)=1)
    float cosR = mesh->grid.built ? mesh->grid.cosR : 0.0f;
    float sinR = mesh->grid.built ? mesh->grid.sinR : 1.0f;

    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];

        // Transform: scale -> rotate Y -> translate (same as wall collision)
        float sx0 = t->x0 * scaleX, sz0 = t->z0 * scaleZ;
        float x0 = sx0 * cosR - sz0 * sinR + mapOffX;
        float y0 = t->y0 * scaleY + mapOffY;
        float z0 = sx0 * sinR + sz0 * cosR + mapOffZ;
        float sx1 = t->x1 * scaleX, sz1 = t->z1 * scaleZ;
        float x1 = sx1 * cosR - sz1 * sinR + mapOffX;
        float y1 = t->y1 * scaleY + mapOffY;
        float z1 = sx1 * sinR + sz1 * cosR + mapOffZ;
        float sx2 = t->x2 * scaleX, sz2 = t->z2 * scaleZ;
        float x2 = sx2 * cosR - sz2 * sinR + mapOffX;
        float y2 = t->y2 * scaleY + mapOffY;
        float z2 = sx2 * sinR + sz2 * cosR + mapOffZ;

        // Edge vectors
        float e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
        float e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;

        // Cross product: h = dir x e2
        float hx = dirY * e2z - dirZ * e2y;
        float hy = dirZ * e2x - dirX * e2z;
        float hz = dirX * e2y - dirY * e2x;

        float a = e1x * hx + e1y * hy + e1z * hz;
        if (a > -EPSILON && a < EPSILON) continue;  // Parallel

        float f = 1.0f / a;

        // Vector from v0 to ray origin
        float sx = fromX - x0;
        float sy = fromY - y0;
        float sz = fromZ - z0;

        float u = f * (sx * hx + sy * hy + sz * hz);
        if (u < 0.0f || u > 1.0f) continue;

        // Cross product: q = s x e1
        float qx = sy * e1z - sz * e1y;
        float qy = sz * e1x - sx * e1z;
        float qz = sx * e1y - sy * e1x;

        float v = f * (dirX * qx + dirY * qy + dirZ * qz);
        if (v < 0.0f || u + v > 1.0f) continue;

        // Distance along ray
        float hitT = f * (e2x * qx + e2y * qy + e2z * qz);

        // Hit if between start and end (with small margin)
        if (hitT > 0.1f && hitT < rayLen - 0.1f) {
            return true;  // Blocked!
        }
    }

    return false;  // Clear line of sight
}

// Check if line of sight is blocked - with arbitrary Y rotation support (for decorations)
static inline bool collision_raycast_blocked_rotated(CollisionMesh* mesh,
    float fromX, float fromY, float fromZ,
    float toX, float toY, float toZ,
    float posX, float posY, float posZ,
    float scaleX, float scaleY, float scaleZ,
    float rotY) {

    if (!collision_mesh_is_valid(mesh)) return false;

    // Ray direction
    float dirX = toX - fromX;
    float dirY = toY - fromY;
    float dirZ = toZ - fromZ;
    float rayLenSq = dirX*dirX + dirY*dirY + dirZ*dirZ;
    if (rayLenSq < 1.0f) return false;
    float rayLen = sqrtf(rayLenSq);

    // Normalize direction
    float invLen = 1.0f / rayLen;
    dirX *= invLen;
    dirY *= invLen;
    dirZ *= invLen;

    const float EPSILON = 0.0001f;

    float cosR = fast_cos(rotY);
    float sinR = fast_sin(rotY);

    for (int i = 0; i < mesh->count; i++) {
        CollisionTriangle* t = &mesh->triangles[i];

        // Transform: scale -> rotate Y -> translate (same as ground collision)
        float sx0 = t->x0 * scaleX;
        float sz0 = t->z0 * scaleZ;
        float x0 = sx0 * cosR - sz0 * sinR + posX;
        float y0 = t->y0 * scaleY + posY;
        float z0 = sx0 * sinR + sz0 * cosR + posZ;

        float sx1 = t->x1 * scaleX;
        float sz1 = t->z1 * scaleZ;
        float x1 = sx1 * cosR - sz1 * sinR + posX;
        float y1 = t->y1 * scaleY + posY;
        float z1 = sx1 * sinR + sz1 * cosR + posZ;

        float sx2 = t->x2 * scaleX;
        float sz2 = t->z2 * scaleZ;
        float x2 = sx2 * cosR - sz2 * sinR + posX;
        float y2 = t->y2 * scaleY + posY;
        float z2 = sx2 * sinR + sz2 * cosR + posZ;

        // Edge vectors
        float e1x = x1 - x0, e1y = y1 - y0, e1z = z1 - z0;
        float e2x = x2 - x0, e2y = y2 - y0, e2z = z2 - z0;

        // Cross product: h = dir x e2
        float hx = dirY * e2z - dirZ * e2y;
        float hy = dirZ * e2x - dirX * e2z;
        float hz = dirX * e2y - dirY * e2x;

        float a = e1x * hx + e1y * hy + e1z * hz;
        if (a > -EPSILON && a < EPSILON) continue;

        float f = 1.0f / a;

        float sx = fromX - x0;
        float sy = fromY - y0;
        float sz = fromZ - z0;

        float u = f * (sx * hx + sy * hy + sz * hz);
        if (u < 0.0f || u > 1.0f) continue;

        float qx = sy * e1z - sz * e1y;
        float qy = sz * e1x - sx * e1z;
        float qz = sx * e1y - sy * e1x;

        float v = f * (dirX * qx + dirY * qy + dirZ * qz);
        if (v < 0.0f || u + v > 1.0f) continue;

        float hitT = f * (e2x * qx + e2y * qy + e2z * qz);

        if (hitT > 0.1f && hitT < rayLen - 0.1f) {
            return true;
        }
    }

    return false;
}

#endif // COLLISION_H
