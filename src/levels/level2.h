#ifndef LEVEL2_H
#define LEVEL2_H

// ============================================================
// LEVEL 2 - The Beginning
// ============================================================

static const LevelData LEVEL_2_DATA = {
    .name = "Getting A-Head",

    // Map segments - StartLevel is small enough (242 tris) to not need chunking
    .segments = {
        "rom:/level2_chunk0.t3dm",
        "rom:/level2_chunk1.t3dm",
        "rom:/level2_chunk2.t3dm",
        "rom:/level2_chunk3.t3dm",
        "rom:/level2_chunk4.t3dm",
        "rom:/level2_chunk5.t3dm",
        "rom:/level2_chunk6.t3dm",
        "rom:/level2_chunk7.t3dm",
        "rom:/level2_chunk8.t3dm",
        "rom:/level2_chunk9.t3dm",
        "rom:/level2_chunk10.t3dm",
        "rom:/level2_chunk11.t3dm",
        "rom:/level2_chunk12.t3dm"
    },
    .segmentCount = 13,

    // Extra rotation to match collision export (artist exported wrong orientation)
    .mapRotY = 90.0f,

    // Decorations
    .decorations = {
        { DECO_PLAYERSPAWN, 1121.0f, 751.0f, -21.8f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BOLT, -493.3f, 580.1f, -91.4f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_SCREWG, -2001.8f, 981.4f, -361.2f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BOLT, 9.2f, -394.4f, -794.1f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_CHARGEPAD, -540.0f, 617.4f, -17.0f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_MOVE_COAL, -1513.0f, 1174.0f, 47.8f, 3.14f, 0.50f, 0.50f, 0.50f, .platformTargetX = -1513.0f, .platformTargetY = 901.6f, .platformTargetZ = -47.5f, .platformSpeed = 60.0f, .activationId = 1, .startStationary = true },
{ DECO_MOVE_COAL, -1379.3f, 809.7f, -68.2f, 3.14f, 0.50f, 0.50f, 0.50f, .platformTargetX = -1379.3f, .platformTargetY = 1113.9f, .platformTargetZ = 13.8f, .platformSpeed = 60.0f, .activationId = 1, .startStationary = true },
{ DECO_TRANSITIONCOLLISION, -1928.9f, 1012.2f, -113.3f, -0.00f, 1.00f, 1.00f, 1.00f, .targetLevel = 2 },
{ DECO_ROUNDBUTTON, 83.4f, 770.8f, 143.6f, -0.00f, 0.58f, 0.58f, 0.58f, .activationId = 1 },
{ DECO_BOLT, -721.3f, 669.7f, -6.6f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_RAT, 21.4f, 662.2f, 57.0f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_RAT, -1450.3f, 783.0f, -88.1f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_CHARGEPAD, -240.6f, 771.4f, 50.7f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BOLT, 52.6f, 696.4f, -90.6f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_LIGHT_TRIGGER, -60.0f, 799.7f, 461.1f, -0.11f, 9.50f, 2.50f, -4.79f, .checkpointAmbientR = 14, .checkpointAmbientG = 13, .checkpointAmbientB = 20, .checkpointDirectionalR = 255, .checkpointDirectionalG = 73, .checkpointDirectionalB = 17, .lightTriggerDirX = 0.00f, .lightTriggerDirY = -1.00f, .lightTriggerDirZ = -1.00f },
    },
    .decorationCount = 15,

    // Player start position
    .playerStartX = 0.0f,
    .playerStartY = 50.0f,
    .playerStartZ = 0.0f,

    // Background music
    .music = "rom:/CalmJunglescrap.wav64",

    // Body part (1 = torso)
    .bodyPart = 1,

    // Point light (optional additional light source)
    .hasPointLight = false,
    .lightX = 9.2f,
    .lightY = -394.4f,
    .lightZ = -794.1f,

    // Directional light direction (0,0,0 = default 1,1,1 diagonal)
    // This is a vector pointing TOWARD the light source
    // Examples: (0,1,0) = noon sun, (1,0.5,0) = low side angle
    .lightDirX = -0.2f,
    .lightDirY = 0.5f,
    .lightDirZ = -1.0f,

    // Ambient light color RGB 0-255 (0,0,0 = default 80,80,80 gray)
    .ambientR = 102,
    .ambientG = 122,
    .ambientB = 149,

    // Directional light color RGB 0-255 (0,0,0 = default 255,255,255 white)
    .directionalR = 0,
    .directionalG = 0,
    .directionalB = 0,

    // Background/clear color RGB 0-255 (black default)
    .bgR = 0,
    .bgG = 0,
    .bgB = 0,

    // Fog settings - thin orange cheeto color fog
    .fogEnabled = true,
    .fogR = 255,
    .fogG = 140,
    .fogB = 40,
    .fogNear = 100.0f,
    .fogFar = 600.0f,
};

#endif // LEVEL2_H
