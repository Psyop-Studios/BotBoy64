#ifndef LEVEL7_H
#define LEVEL7_H

// ============================================================
// LEVEL 7
// ============================================================

static const LevelData LEVEL_7_DATA = {
    .name = "Closing Beta",

    // Map segments (add your chunks here)
    .segments = {
        "rom:/level7_chunk0.t3dm",
        "rom:/level7_chunk1.t3dm",
        "rom:/level7_chunk2.t3dm",
        "rom:/level7_chunk3.t3dm",
        "rom:/level7_chunk4.t3dm",
        "rom:/level7_chunk5.t3dm",
        "rom:/level7_chunk6.t3dm",
        "rom:/level7_chunk7.t3dm"
    },
    .segmentCount = 8,

    // Extra rotation to orient level correctly
    .mapRotY = 90.0f,

    // Decorations - add your decorations here
    .decorations = {
       { DECO_PLAYERSPAWN, 1788.3f, 2486.7f, 79.7f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BOLT, -371.8f, 1702.9f, -89.5f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_HIVE_MOVING, -3231.3f, 1696.3f, 17.7f, 3.14f, 1.25f, 1.25f, 1.25f, .platformTargetX = -3231.3f, .platformTargetY = 2578.2f, .platformTargetZ = 17.7f, .platformSpeed = 45.0f },
{ DECO_MOVING_LASER, -1602.7f, 1723.0f, -25.6f, 3.14f, 1.00f, 1.00f, 1.47f, .platformTargetX = -909.7f, .platformTargetY = 1723.0f, .platformTargetZ = -25.6f, .platformSpeed = 80.0f },
{ DECO_MOVING_LASER, -370.0f, 1689.9f, -47.4f, -1.57f, 1.00f, 1.00f, 1.31f, .platformTargetX = -370.0f, .platformTargetY = 1689.9f, .platformTargetZ = -147.9f, .platformSpeed = 50.0f },
{ DECO_MOVING_LASER, -909.7f, 1821.9f, -25.6f, 3.14f, 1.00f, 1.00f, 1.47f, .platformTargetX = -1602.7f, .platformTargetY = 1824.1f, .platformTargetZ = -25.6f, .platformSpeed = 80.0f },
{ DECO_FAN2, -3598.0f, 2641.1f, -22.6f, 3.14f, 0.60f, 0.60f, 0.70f },
{ DECO_STAGE7, 0.0f, 0.0f, 0.0f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_TURRET_PULSE, -2050.8f, 1892.7f, 181.1f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_FOGCOLOR, -376.8f, 1942.8f, 55.6f, -0.00f, 3.57f, 1.38f, 3.00f, .fogColorR = 5, .fogColorG = 125, .fogColorB = 248 },
{ DECO_FOGCOLOR, -3256.6f, 2376.3f, 55.6f, -0.00f, 6.19f, 1.38f, 6.33f, .fogColorR = 248, .fogColorG = 248, .fogColorB = 248 },
{ DECO_FOGCOLOR, -2435.2f, 2205.3f, 55.6f, 0.00f, -29.22f, 1.38f, 8.01f, .fogColorR = 5, .fogColorG = 125, .fogColorB = 248 },
{ DECO_FAN2, -3820.1f, 2641.1f, -86.3f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_FAN2, -3824.0f, 2641.1f, 117.0f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_FAN2, -4078.0f, 2641.1f, 113.4f, 3.14f, 1.00f, 0.75f, 0.70f },
{ DECO_FAN2, -4271.7f, 2641.1f, 91.1f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_FAN2, -4526.0f, 2641.1f, 38.2f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_FAN2, -4718.0f, 2641.1f, 92.6f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_MOVING_LASER, -3092.1f, 2021.0f, 22.8f, 3.14f, 1.00f, 1.00f, 1.00f, .platformTargetX = -3299.5f, .platformTargetY = 2021.0f, .platformTargetZ = 22.8f, .platformSpeed = 80.0f },
{ DECO_MOVING_LASER, -3214.3f, 2295.5f, 120.8f, -1.57f, 1.00f, 1.00f, 1.24f, .platformTargetX = -3214.3f, .platformTargetY = 2295.5f, .platformTargetZ = -86.0f, .platformSpeed = 80.0f },
{ DECO_MOVING_LASER, -3152.3f, 2480.9f, -46.5f, 2.36f, 0.71f, 1.00f, 0.86f, .platformTargetX = -3275.9f, .platformTargetY = 2480.9f, .platformTargetZ = 55.0f, .platformSpeed = 80.0f },
{ DECO_FAN2, -3598.0f, 2641.1f, 54.1f, 3.14f, 0.60f, 0.60f, 0.70f },
{ DECO_FAN2, -3898.4f, 2641.1f, -81.3f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_FAN2, -3895.7f, 2641.1f, 120.8f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_FAN2, -4269.4f, 2641.1f, -13.3f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_FAN2, -4526.0f, 2641.1f, -90.4f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_FAN2, -4718.0f, 2641.1f, -11.0f, 3.14f, 0.80f, 1.00f, 0.70f },
{ DECO_FAN2, 1059.7f, 2341.1f, 24.7f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_FAN2, 1059.7f, 2341.1f, 80.5f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_CHECKPOINT, -3456.1f, 2719.3f, 25.3f, -0.00f, 3.43f, 3.43f, 3.43f },
{ DECO_DAMAGECOLLISION, 642.6f, 2324.4f, 56.1f, -0.00f, 22.31f, 1.00f, 2.76f },
{ DECO_DAMAGECUBE_LIGHT, -352.2f, 2483.0f, 56.1f, -0.00f, 1.98f, 0.09f, 2.47f },
{ DECO_DAMAGECUBE_LIGHT, -433.6f, 2337.1f, 56.1f, -0.00f, 1.98f, 0.09f, 2.47f },
{ DECO_DAMAGECUBE_LIGHT, -359.0f, 2144.2f, 56.1f, -0.00f, 2.51f, 0.09f, 2.47f },
{ DECO_HIVE_MOVING, -5326.3f, 2631.7f, 52.8f, 3.14f, 0.88f, 0.88f, 0.88f, .platformTargetX = -5326.3f, .platformTargetY = 2278.6f, .platformTargetZ = 52.8f, .platformSpeed = 75.0f },
{ DECO_HIVE_MOVING, -5386.8f, 2278.6f, -47.8f, 3.14f, 0.88f, 0.88f, 0.88f, .platformTargetX = -5882.4f, .platformTargetY = 2278.6f, .platformTargetZ = -47.8f, .platformSpeed = 75.0f },
{ DECO_HIVE_MOVING, -5944.0f, 2277.7f, 52.8f, 3.14f, 0.88f, 0.88f, 0.88f, .platformTargetX = -5944.0f, .platformTargetY = 1874.9f, .platformTargetZ = 52.8f, .platformSpeed = 75.0f },
{ DECO_MOVING_LASER, -5498.2f, 2142.8f, -39.2f, 3.14f, 1.00f, 1.00f, 0.73f, .platformTargetX = -5498.2f, .platformTargetY = 2142.8f, .platformTargetZ = -39.2f, .platformSpeed = 80.0f },
{ DECO_MOVING_LASER, -5773.2f, 2142.8f, -39.2f, 3.14f, 1.00f, 1.00f, 0.73f, .platformTargetX = -5773.2f, .platformTargetY = 2142.8f, .platformTargetZ = -39.2f, .platformSpeed = 80.0f },
{ DECO_MOVING_LASER, -5634.7f, 2492.7f, -39.2f, 3.14f, 1.00f, 1.00f, 0.73f, .platformTargetX = -5634.7f, .platformTargetY = 2492.7f, .platformTargetZ = -39.2f, .platformSpeed = 80.0f },
{ DECO_TURRET_PULSE, -5635.2f, 2330.8f, 84.0f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_DAMAGECOLLISION, -2149.0f, 1564.0f, 110.0f, -0.00f, 61.49f, 1.57f, 8.50f },
{ DECO_DAMAGECOLLISION, -5656.1f, 1617.3f, -42.5f, -0.00f, 19.87f, 3.24f, 8.50f },
{ DECO_DAMAGECUBE_LIGHT, -3884.7f, 2827.1f, 21.9f, -0.00f, 3.38f, 0.09f, 5.21f },
{ DECO_DAMAGECUBE_LIGHT, -4523.4f, 2827.1f, 21.9f, -0.00f, 10.50f, 0.09f, 5.21f },
{ DECO_BOLT, 1806.6f, 2781.4f, 48.4f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BOLT, -690.6f, 2388.7f, 48.4f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BOLT, -4083.4f, 3024.3f, 269.0f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_TURRET_PULSE, -615.7f, 2391.5f, 52.7f, 3.14f, 1.00f, 1.00f, 1.00f },
{ DECO_SCREWG, -2050.0f, 1688.8f, 228.4f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BULLDOZER, -190.9f, 2486.6f, 49.0f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BULLDOZER, 743.6f, 2510.6f, 49.0f, -0.00f, 1.00f, 1.00f, 1.00f },
{ DECO_BULLDOZER, -4083.1f, 2650.7f, -44.3f, -0.00f, 2.21f, 2.21f, 2.21f },
{ DECO_CS_3, -6147.6f, 1914.9f, 52.4f, 3.14f, 1.00f, 1.00f, 1.00f },
    },
    .decorationCount = 54,

    // Player start position
    .playerStartX = 0.0f,
    .playerStartY = 100.0f,
    .playerStartZ = 0.0f,

    // Background music
    .music = "rom:/scrap1.wav64",

    // Body part (1 = torso, 2 = arms, 3 = fullbody/legs)
    .bodyPart = 3,

    // Point light (optional, set hasPointLight = true to enable)
    .hasPointLight = false,
    .lightX = 0.0f,
    .lightY = 0.0f,
    .lightZ = 0.0f,

    // Directional light direction (0,0,0 = default 1,1,1 diagonal)
    .lightDirX = 0.0f,
    .lightDirY = 0.0f,
    .lightDirZ = 0.0f,

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

    // Fog settings
    .fogEnabled = false,
    .fogR = 128,
    .fogG = 128,
    .fogB = 128,
    .fogNear = 200,
    .fogFar = 800,
};

#endif // LEVEL7_H
