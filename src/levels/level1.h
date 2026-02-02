#ifndef LEVEL1_H
#define LEVEL1_H

// ============================================================
// LEVEL 1 - Playground (test level)
// ============================================================

static const LevelData LEVEL_1_DATA = {
    .name = "Welcome to BotBoy",

    // Map segments (auto-aligned left to right)
    .segments = {
        "rom:/level1.t3dm"
    },
    .segmentCount = 1,

    // Decorations (X coordinates adjusted by -514.4 for non-chunked map alignment)
    .decorations = {
    { DECO_TRANSITIONCOLLISION, 153.3f, 52.0f, 478.5f, -0.00f, 0.25f, 0.25f, 0.25f, .targetLevel = 1 },
{ DECO_PLAYERSPAWN, 107.5f, 52.2f, 13.6f, -0.00f, 0.17f, 0.17f, 0.17f },
{ DECO_TRANSITIONCOLLISION, 76.8f, 52.0f, 478.5f, -0.00f, 0.25f, 0.25f, 0.25f, .targetLevel = 2 },
{ DECO_TRANSITIONCOLLISION, -9.9f, 52.0f, 478.5f, -0.00f, 0.25f, 0.25f, 0.25f, .targetLevel = 3 },
{ DECO_TRANSITIONCOLLISION, -91.7f, 52.0f, 478.5f, -0.00f, 0.25f, 0.25f, 0.25f, .targetLevel = 4 },
{ DECO_TRANSITIONCOLLISION, -185.9f, 52.0f, 478.5f, -0.00f, 0.25f, 0.25f, 0.25f, .targetLevel = 5 },
{ DECO_TRANSITIONCOLLISION, -275.2f, 52.0f, 478.5f, -0.00f, 0.25f, 0.25f, 0.25f, .targetLevel = 6 },
{ DECO_LEVEL_TRANSITION, -1929.5f, 109.9f, 13.6f, -0.00f, 2.93f, 2.93f, 2.93f, .targetLevel = 1 },


},
.decorationCount = 8,

    // Player start position (map spans X=-2022 to X=320, center around -851)
    .playerStartX = 0.0f,
    .playerStartY = 0.0f,
    .playerStartZ = 0.0f,

    // Background music
    .music = "rom:/scrap1.wav64",

    // Body part (3 = fullbody/legs)
    .bodyPart = 3,

    // Collision rotation (90° to align collision with visuals)
    .mapRotY = 90.0f,

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

    // Background/clear color RGB 0-255 (black default)
    .bgR = 0,
    .bgG = 0,
    .bgB = 0,

    // Fog settings (disabled by default)
    .fogEnabled = false,
    .fogR = 0,
    .fogG = 0,
    .fogB = 0,
    .fogNear = 0,
    .fogFar = 0,
};

#endif // LEVEL1_H
