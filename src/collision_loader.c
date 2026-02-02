/**
 * Collision loader globals - single instance shared across all translation units
 */

#include "collision_loader.h"

// Global collision storage - single definition
LoadedCollision g_loadedCollision[MAX_LOADED_COLLISION];
int g_loadedCollisionCount = 0;
bool g_collisionLoaderInitialized = false;
