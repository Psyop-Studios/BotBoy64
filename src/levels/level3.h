#ifndef LEVEL3_H
#define LEVEL3_H

// ============================================================
// LEVEL 3 - Learning to Crawl (moved from level 2)
// ============================================================

static const LevelData LEVEL_3_DATA = {
    .name = "Learning to Crawl",

    // Map segments (auto-aligned left to right)
    .segments = {
        "rom:/level3_chunk0.t3dm",
        "rom:/level3_chunk1.t3dm",
        "rom:/level3_chunk2.t3dm",
        "rom:/level3_chunk3.t3dm",
        "rom:/level3_chunk4.t3dm",
        "rom:/level3_chunk5.t3dm",
        "rom:/level3_chunk6.t3dm",
        "rom:/level3_chunk7.t3dm"
    },
    .segmentCount = 8,

    // Extra rotation to orient level correctly
    .mapRotY = 90.0f,

    // Decorations
    .decorations = {
       { DECO_CONVEYERLARGE, -1526.4f, 189.6f, -71.9f, 1.57f, 1.10f, 1.69f, 1.10f },
{ DECO_CONVEYERLARGE, -1401.8f, 189.6f, -71.9f, 1.57f, 1.10f, 1.69f, 1.10f },
{ DECO_CONVEYERLARGE, -1275.7f, 189.6f, -71.9f, 1.57f, 1.10f, 1.69f, 1.10f },
{ DECO_COG, -1028.1f, 115.5f, 56.7f, -0.03f, 1.22f, 1.22f, 2.51f },
{ DECO_RAT, -3849.0f, 338.8f, 1024.4f, 3.14f, 1.94f, 1.94f, 1.94f },
{ DECO_RAT, -4340.8f, 292.7f, 995.2f, 3.14f, 1.94f, 1.94f, 1.94f },
{ DECO_RAT, -4152.3f, 299.5f, 895.6f, 1.52f, 1.00f, 1.00f, 1.00f },
{ DECO_PLAYERSPAWN, -36.3f, 68.2f, 135.2f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_LASERWALL, -801.5f, 35.7f, -181.5f, -0.09f, 1.37f, 0.69f, 1.05f, .activationId = 1 },
{ DECO_BOLT, -140.7f, 181.3f, 148.0f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BOLT, -365.9f, 13.3f, -306.1f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BOLT, -1401.0f, 229.9f, -225.4f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BOLT, -2404.8f, 136.1f, 414.0f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_TOXICPIPE, -661.0f, 20.7f, 84.8f, 0.64f, 1.38f, 1.38f, 1.38f },
{ DECO_TOXICPIPE, -329.2f, 11.3f, 99.0f, -0.51f, 1.00f, 1.00f, 1.00f },
{ DECO_TOXICPIPE, -2505.2f, 217.1f, 752.7f, -1.38f, 1.47f, 1.47f, 1.47f },
{ DECO_TOXICPIPE, -3117.7f, 325.9f, 1025.6f, 0.21f, 3.09f, 3.09f, 3.09f },
{ DECO_BULLDOZER, -1099.5f, 60.2f, -90.8f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_SLIME, -2941.6f, 281.6f, 912.6f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_ROUNDBUTTON, -500.0f, 24.4f, 159.9f, -0.00f, 0.52f, 0.52f, 0.52f, .activationId = 1 },
{ DECO_BOLT, 148.8f, 156.0f, 232.5f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_SLIME, -502.8f, 66.3f, -74.8f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_CHARGEPAD, -1397.3f, 204.2f, 158.3f, -0.00f, 1.50f, 1.50f, 1.50f },
{ DECO_BOLT, -4118.7f, 252.0f, 634.4f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_TRANSITIONCOLLISION, -4927.6f, 207.9f, 898.8f, -0.00f, 1.00f, 1.00f, 1.00f, .targetLevel = 3 },
{ DECO_MOVING_ROCK, -1274.9f, 904.9f, 71.1f, 3.14f, 0.65f, 0.65f, 0.65f, .waypoint1X = -1274.9f, .waypoint1Y = 225.7f, .waypoint1Z = 64.2f, .waypoint2X = -1274.9f, .waypoint2Y = 238.6f, .waypoint2Z = -281.4f, .waypoint3X = -1274.9f, .waypoint3Y = -305.3f, .waypoint3Z = -281.4f, .waypointCount = 4, .platformSpeed = 60.0f },
{ DECO_MOVING_ROCK, -1397.1f, 904.9f, 71.1f, 0.94f, 0.65f, 0.65f, 0.65f, .waypoint1X = -1397.1f, .waypoint1Y = 225.7f, .waypoint1Z = 64.2f, .waypoint2X = -1397.1f, .waypoint2Y = 238.6f, .waypoint2Z = -281.4f, .waypoint3X = -1397.1f, .waypoint3Y = -305.3f, .waypoint3Z = -281.4f, .waypointCount = 4, .platformSpeed = 70.0f },
{ DECO_MOVING_ROCK, -1527.4f, 904.9f, 71.1f, -0.80f, 0.65f, 0.65f, 0.65f, .waypoint1X = -1527.4f, .waypoint1Y = 225.7f, .waypoint1Z = 64.2f, .waypoint2X = -1527.4f, .waypoint2Y = 238.6f, .waypoint2Z = -281.4f, .waypoint3X = -1527.4f, .waypoint3Y = -305.3f, .waypoint3Z = -281.4f, .waypointCount = 4, .platformSpeed = 40.0f },
{ DECO_LEVEL3_STREAM, 0.0f, 0.0f, 0.0f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_BULLDOZER, -2082.8f, 60.2f, -12.3f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_SCREWG, -2323.8f, 232.9f, 125.7f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_RAT, -3920.8f, 299.5f, 674.1f, 1.52f, 1.00f, 1.00f, 1.00f },
    },
    .decorationCount = 32,

    // Player start position
    .playerStartX = -50.0f,
    .playerStartY = 100.0f,
    .playerStartZ = 0.0f,

    // Background music
    .music = "rom:/androidjungle1.wav64",

    // Body part (1 = torso)
    .bodyPart = 1,

    // Point light (optional, set hasPointLight = true to enable)
    .hasPointLight = false,
    .lightX = 0.0f,
    .lightY = 0.0f,
    .lightZ = 0.0f,

    // Directional light direction (0,0,0 = default 1,1,1 diagonal)
    // This is a vector pointing TOWARD the light source
    // Examples: (0,1,0) = noon sun, (1,0.5,0) = low side angle
    .lightDirX = -0.2f,
    .lightDirY = 0.5f,
    .lightDirZ = -1.0f,

    // Ambient light color RGB 0-255 (0,0,0 = default 80,80,80 gray)
    .ambientR = 0,
    .ambientG = 0,
    .ambientB = 0,

    // Directional light color RGB 0-255 (0,0,0 = default 255,255,255 white)
    .directionalR = 0,
    .directionalG = 0,
    .directionalB = 0,

    // Background/clear color RGB 0-255 (purple to match fog)
    .bgR = 40,
    .bgG = 15,
    .bgB = 55,

    // Fog settings - thin purple poison fog
    .fogEnabled = true,
    .fogR = 90,
    .fogG = 30,
    .fogB = 120,
    .fogNear = 100.0f,
    .fogFar = 600.0f,
};

#endif // LEVEL3_H
