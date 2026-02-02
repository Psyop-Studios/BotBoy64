#ifndef MENU_H
#define MENU_H

// ============================================================
// MENU SCENE - Main Menu Level
// ============================================================

static const LevelData MENU_LEVEL_DATA = {
    .name = "Main Menu",

    // Map segments
    .segments = {
        "rom:/MenuScene.t3dm"
    },
    .segmentCount = 1,

    // Decorations - add menu objects here
    // Map bounds: X=[-147, 147], Z=[-371, 138], center X=0, Z=-117
    // Trigger 1: Door area - Load/New Game menu (script 0)
    // Trigger 2: Computer - Options menu (script 3)
    // Trigger 3: Jukebox - Sound test (script 4)
    .decorations = {
    { DECO_INTERACTTRIGGER, 0.0f, 0.1f, -124.0f, 3.14f, 1.00f, 1.00f, 1.00f, .scriptId = 0 },
    { DECO_INTERACTTRIGGER, -105.0f, -16.0f, -70.0f, 2.36f, 1.20f, 1.00f, 0.80f,  .scriptId = 3 },
    { DECO_INTERACTTRIGGER, 95.0f, -20.0f, -90.0f, 3.93f, 1.00f, 1.00f, 1.00f, .scriptId = 4 },
    { DECO_JUKEBOX, 96.0f, 0.0f, -86.5f, 0.65f, 1.40f, 1.40f, 1.40f },
    { DECO_MONITORTABLE, -96.0f, 0.0f, -43.8f, -2.45f, 1.00f, 1.00f, 1.00f },
    { DECO_DISCOBALL, 0.0f, 200.0f, -50.0f, 0.0f, 2.50f, 2.50f, 2.50f },
},
.decorationCount = 6,





    // Player start position (front and center)
    .playerStartX = 0.0f,
    .playerStartY = 0.0f,  // Start at ground level
    .playerStartZ = 50.0f,

    // Menu music
    .music = "rom:/scrap1-menu(full).wav64",

    // Body part (3 = fullbody)
    .bodyPart = 3,
};

#endif // MENU_H
