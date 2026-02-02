#ifndef DEMO_SCENE_H
#define DEMO_SCENE_H

#include <stdbool.h>

void init_demo_scene(void);
void deinit_demo_scene(void);
void update_demo_scene(void);
void draw_demo_scene(void);

// Demo mode flag (set true before init_game_scene to play from ROM demo data)
extern bool g_demoMode;

// Demo overlay draw callback (called by game.c during 2D draw phase)
// Set this to a function that draws the Press Start sprite, etc.
typedef void (*DemoOverlayDrawFunc)(void);
extern DemoOverlayDrawFunc g_demoOverlayDraw;

// Demo overlay update callback (for animation timing)
typedef void (*DemoOverlayUpdateFunc)(float dt);
extern DemoOverlayUpdateFunc g_demoOverlayUpdate;

// Demo iris draw callback (called by game.c after all UI, before rdpq_detach_show)
// Set this to a function that draws the iris transition overlay
typedef void (*DemoIrisDrawFunc)(void);
extern DemoIrisDrawFunc g_demoIrisDraw;

// Flag set by game.c when goal is reached in demo mode
// demo_scene checks this to skip level complete and start next demo
extern bool g_demoGoalReached;

// ============================================================
// TUTORIAL DEMO SUPPORT
// ============================================================
// To play a specific demo (for tutorials), set this before changing to DEMO_SCENE:
//   g_requestedDemoIndex = 2;  // Play demo index 2
//   change_scene(DEMO_SCENE);
//
// Set to -1 to use random demo selection (normal title screen behavior)
extern int g_requestedDemoIndex;

// Play a specific demo and return to a scene when finished
// Set this before changing to DEMO_SCENE to return to a specific scene after demo
// Set to DEMO_SCENE or -1 for normal looping behavior (cycles through demos)
extern int g_demoReturnScene;

// ============================================================
// BODY PART TUTORIAL SUPPORT
// ============================================================
// For body part pickups, we need to:
// 1. Play a demo showing the moves
// 2. Show dialogue explaining controls
// 3. Return player to exact position with new part equipped

// Tutorial type enum
typedef enum {
    TUTORIAL_NONE = 0,
    TUTORIAL_TORSO,
    TUTORIAL_ARMS,
    TUTORIAL_HEAD,
    TUTORIAL_LEGS,
} TutorialType;

// Set these before changing to DEMO_SCENE for a body part tutorial
extern TutorialType g_tutorialType;
extern float g_tutorialReturnX;
extern float g_tutorialReturnY;
extern float g_tutorialReturnZ;
extern int g_tutorialReturnLevel;

// Helper function to start a body part tutorial
void demo_start_tutorial(TutorialType type, float returnX, float returnY, float returnZ, int returnLevel);

#endif // DEMO_SCENE_H
