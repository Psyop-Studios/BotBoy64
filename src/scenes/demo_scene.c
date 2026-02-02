// ============================================================
// DEMO SCENE - Plays pre-recorded gameplay demos from ROM
// ============================================================
// This scene wraps the game scene to play back demo data.
// It loads a random demo from DEMO_LIST and feeds input to game.c.
// Press any button to go to the menu.
// ============================================================

#include <libdragon.h>
#include <math.h>
#include "demo_scene.h"
#include "game.h"
#include "level_select.h"
#include "../scene.h"
#include "../demo_data.h"
#include "../ui.h"
#include "../constants.h"
#include "../mapData.h"

// Demo mode flag - when true, game.c reads from ROM demo data
bool g_demoMode = false;

// Demo playback state (defined here, declared extern in demo_data.h)
DemoPlaybackState g_demoState = {0};

// DEMO_LIST definition - MUST be in exactly one .c file to avoid duplicate symbols
// (The frame arrays are static in demo_data.h, so this file's copies are used)
const DemoData DEMO_LIST[DEMO_COUNT] = {
    // { levelId, frameCount, frames, startX, startY, startZ }
    // startX/Y/Z = 0 means use default level spawn position
    { 1, DEMO_LEVEL_1_FRAMES, demo_level_1_frames, 167.6f, 661.7f, -16.6f },
    { 2, DEMO_LEVEL_2_FRAMES, demo_level_2_frames, -146.0f, 35.1f, -57.2f },
    { 5, DEMO_LEVEL_5_FRAMES, demo_level_5_frames, 904.7f, 593.6f, -61.7f },
};

// Demo overlay callbacks (called by game.c during draw/update phases)
DemoOverlayDrawFunc g_demoOverlayDraw = NULL;
DemoOverlayUpdateFunc g_demoOverlayUpdate = NULL;
DemoIrisDrawFunc g_demoIrisDraw = NULL;

// Flag set by game.c when goal is reached in demo mode
bool g_demoGoalReached = false;

// Tutorial support: set g_requestedDemoIndex before changing to DEMO_SCENE
// -1 means use random selection (default/title screen behavior)
int g_requestedDemoIndex = -1;

// Return to a specific scene after demo finishes (for tutorial flow)
// -1 or DEMO_SCENE means loop through demos (default behavior)
int g_demoReturnScene = -1;

// Body part tutorial state
TutorialType g_tutorialType = TUTORIAL_NONE;
float g_tutorialReturnX = 0.0f;
float g_tutorialReturnY = 0.0f;
float g_tutorialReturnZ = 0.0f;
int g_tutorialReturnLevel = -1;

// Tutorial dialogue box
static DialogueBox tutorialDialogue;
static bool tutorialDialogueActive = false;
static bool tutorialDialogueShown = false;  // Has dialogue been shown yet this tutorial?

// Track current demo for cycling
static int currentDemoIndex = 0;

// Track demo cycle count - return to menu after MAX_DEMO_CYCLES to prevent crashes
// With minimal animation loading fix, should handle 5+ cycles
#define MAX_DEMO_CYCLES 5
static int demoCycleCount = 0;

// Track if we're in single-demo mode (tutorial)
static bool tutorialMode = false;

// Demo timeout (20 seconds max per demo)
#define DEMO_MAX_TIME 20.0f
static float demoTimer = 0.0f;

// Press Start sprite and bounce animation
static sprite_t* pressStartSprite = NULL;
static float pressStartTimer = 0.0f;

// Iris transition state
static bool exitingToMenu = false;
static bool switchingDemo = false;  // Transitioning between demos
static float exitIrisRadius = 336.0f;  // Will be set properly in init
static bool irisOpening = false;  // true = opening, false = closing

// CRITICAL FIX: Defer level switching to next frame to prevent race condition
// When this is true, the NEXT update_demo_scene() call will do deinit/init
// This ensures draw_game_scene() completes before resources are freed
static bool pendingLevelSwitch = false;
static bool pendingTutorialLoop = false;  // For tutorial demo looping

// CRITICAL FIX: Skip update_game_scene on the frame when we just switched levels
// This prevents NULL pointer dereference in platform_get_displacement when
// decoration data hasn't fully initialized after init_game_scene()
static bool justSwitchedLevel = false;

// CRITICAL FIX: Skip draw_game_scene on the frame after level switch
// This is set when justSwitchedLevel is processed in update and cleared after draw skips
static bool skipNextDraw = false;

// Forward declarations
static void demo_draw_iris_callback(void);

// Bouncy elastic ease-out function (same as countdown)
static float press_start_ease_out_elastic(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    float p = 0.3f;
    return powf(2.0f, -10.0f * t) * sinf((t - p / 4.0f) * (2.0f * 3.14159f) / p) + 1.0f;
}

// Draw callback for Press Start overlay (called by game.c)
static void demo_draw_press_start(void) {
    if (!pressStartSprite) return;

    // CRITICAL: Flush FPU denormals before any sprite math
    // Decoration updates (rat animation, etc.) can leave denormals that crash rdpq_sprite_blit
    fpu_flush_denormals();

    // Get sprite dimensions
    int spriteW = pressStartSprite->width;
    int spriteH = pressStartSprite->height;

    // Screen center, positioned near bottom
    int centerX = SCREEN_CENTER_X;
    int baseY = 200;  // Near bottom of screen

    // Continuous bounce animation using a looping pattern
    // Bounce every 1.5 seconds
    float bouncePhase = fmodf(pressStartTimer, 1.5f);
    float scale = 1.0f;

    if (bouncePhase < 0.4f) {
        // Pop in with elastic bounce (first 0.4 seconds of cycle)
        float popProgress = bouncePhase / 0.4f;
        scale = press_start_ease_out_elastic(popProgress) * 1.2f;
    } else if (bouncePhase < 0.8f) {
        // Hold at slightly larger size
        scale = 1.2f;
    } else {
        // Ease back to normal
        float easeProgress = (bouncePhase - 0.8f) / 0.7f;
        scale = 1.2f - (0.2f * easeProgress);
    }

    // Add subtle wobble
    float wobble = sinf(pressStartTimer * 15.0f) * 0.02f * scale;
    scale += wobble;

    // Clamp scale to safe range to prevent FPU issues in rdpq_sprite_blit
    // Scale of 0 or negative causes division by zero in texture coordinate math
    if (scale < 0.1f) scale = 0.1f;
    if (scale > 3.0f) scale = 3.0f;

    // Set up for scaled sprite drawing - reset render state completely
    // CRITICAL: When switching from T3D 3D rendering to rdpq 2D operations,
    // we must sync T3D triangles first to avoid RDP command buffer corruption
    t3d_tri_sync();
    rdpq_sync_pipe();  // Sync before 2D mode switch
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, PRIM, 0), (TEX0, 0, PRIM, 0)));

    // Use rdpq_sprite_blit with cx/cy anchoring to ensure consistent centering
    rdpq_blitparms_t parms = {
        .scale_x = scale,
        .scale_y = scale,
        .cx = spriteW / 2,
        .cy = spriteH / 2
    };
    rdpq_sprite_blit(pressStartSprite, centerX, baseY, &parms);
}

// Draw tutorial dialogue overlay (called instead of Press Start for tutorials)
static void demo_draw_tutorial_dialogue(void) {
    if (tutorialDialogueActive) {
        dialogue_draw(&tutorialDialogue);
    }
}

// Start a body part tutorial - called from game.c when pickup is collected
void demo_start_tutorial(TutorialType type, float returnX, float returnY, float returnZ, int returnLevel) {
    debugf("Starting tutorial type %d, return to (%.1f, %.1f, %.1f) level %d\n",
           type, returnX, returnY, returnZ, returnLevel);

    // Store return state
    g_tutorialType = type;
    g_tutorialReturnX = returnX;
    g_tutorialReturnY = returnY;
    g_tutorialReturnZ = returnZ;
    g_tutorialReturnLevel = returnLevel;

    // Set up demo to play - use first available demo for tutorials
    g_requestedDemoIndex = 0;
    g_demoReturnScene = GAME;  // Return to game scene

    // Change to demo scene
    change_scene(DEMO_SCENE);
}

// Start a specific demo by index
static void demo_start_specific(int demoIndex) {
    if (demoIndex < 0 || demoIndex >= demo_get_count()) {
        debugf("Demo: Invalid demo index %d, falling back to 0\n", demoIndex);
        demoIndex = 0;
    }
    currentDemoIndex = demoIndex;

    const DemoData* demo = demo_get(currentDemoIndex);
    if (demo) {
        // Set up the game scene to load the demo's level
        selectedLevelID = demo->levelId;
        startWithIrisOpen = true;  // Enable iris opening effect
        g_demoMode = true;
        extern bool g_replayMode;
        g_replayMode = true;  // Also set replay mode to skip animation updates that can crash

        // Start demo playback
        demo_start_playback(demo);

        // Reset demo timer
        demoTimer = 0.0f;

        debugf("Demo: Starting demo %d for level %d (%d frames)\n",
               currentDemoIndex, demo->levelId, demo->frameCount);
    }
}

// Start the next demo (randomly selected, or use requested index)
static void demo_start_next(void) {
    // Always ensure demo mode is set - critical for init_game_scene behavior
    g_demoMode = true;
    extern bool g_replayMode;
    g_replayMode = true;  // Also set replay mode to skip animation updates that can crash

    // Check if a specific demo was requested (tutorial mode)
    if (g_requestedDemoIndex >= 0) {
        tutorialMode = true;
        demo_start_specific(g_requestedDemoIndex);
        g_requestedDemoIndex = -1;  // Clear the request after using it
        return;
    }

    tutorialMode = false;

    // Pick a random demo
    if (demo_get_count() > 1) {
        // Avoid repeating the same demo
        int newIndex;
        do {
            newIndex = (int)(TICKS_READ() % demo_get_count());
        } while (newIndex == currentDemoIndex && demo_get_count() > 1);
        currentDemoIndex = newIndex;
    } else {
        currentDemoIndex = 0;
    }

    const DemoData* demo = demo_get(currentDemoIndex);
    if (demo) {
        // Set up the game scene to load the demo's level
        selectedLevelID = demo->levelId;
        startWithIrisOpen = true;  // Enable iris opening effect

        // Start demo playback
        demo_start_playback(demo);

        // Reset demo timer
        demoTimer = 0.0f;

        debugf("Demo: Starting demo %d for level %d (%d frames)\n",
               currentDemoIndex, demo->levelId, demo->frameCount);
    }
}

// Note: fpu_flush_denormals() is now defined in mapData.h (included via levels.h)

void init_demo_scene(void) {
    // Ensure FPU flushes denormals to zero
    fpu_flush_denormals();

    debugf("Demo scene init\n");

    // Load Press Start sprite
    pressStartSprite = sprite_load("rom:/PressStart.sprite");
    pressStartTimer = 0.0f;
    demoTimer = 0.0f;

    // Reset exit state
    exitingToMenu = false;
    switchingDemo = false;
    exitIrisRadius = (float)SCREEN_WIDTH + 80.0f;
    irisOpening = false;

    // Reset pending operation flags
    pendingLevelSwitch = false;
    pendingTutorialLoop = false;
    justSwitchedLevel = false;
    skipNextDraw = false;

    // Reset demo cycle counter
    demoCycleCount = 0;

    // Reset goal reached flag
    g_demoGoalReached = false;

    // Initialize tutorial dialogue
    // Zero first to prevent garbage float reads during init
    memset(&tutorialDialogue, 0, sizeof(tutorialDialogue));
    dialogue_init(&tutorialDialogue);
    tutorialDialogueActive = false;
    tutorialDialogueShown = false;

    // If this is a body part tutorial, set up tutorial dialogue overlay
    if (g_tutorialType != TUTORIAL_NONE) {
        debugf("Demo scene: Setting up tutorial mode for type %d\n", g_tutorialType);
        g_demoOverlayDraw = demo_draw_tutorial_dialogue;

        // Queue up tutorial dialogue based on type
        dialogue_queue_clear(&tutorialDialogue);
        if (g_tutorialType == TUTORIAL_TORSO) {
            dialogue_queue_add(&tutorialDialogue, "You found your TORSO!", "SYSTEM");
            dialogue_queue_add(&tutorialDialogue, "Hold A to CHARGE your jump. The longer you charge, the farther you'll go!", "TORSO");
            dialogue_queue_add(&tutorialDialogue, "Use the analog stick while charging to AIM your jump direction.", "TORSO");
            dialogue_queue_add(&tutorialDialogue, "Release A to LAUNCH! Watch the demo to see how it works.", "TORSO");
        } else if (g_tutorialType == TUTORIAL_ARMS) {
            dialogue_queue_add(&tutorialDialogue, "You found your ARMS!", "SYSTEM");
            dialogue_queue_add(&tutorialDialogue, "Press B to SPIN ATTACK! Great for defeating enemies.", "ARMS");
            dialogue_queue_add(&tutorialDialogue, "Hold A in the air to GLIDE. You can also DOUBLE JUMP!", "ARMS");
        }
        // Start showing dialogue
        dialogue_queue_start(&tutorialDialogue);
        tutorialDialogueActive = true;
        tutorialDialogueShown = true;
    } else {
        // Normal demo mode - show Press Start
        g_demoOverlayDraw = demo_draw_press_start;
    }

    // Set the iris draw callback so game.c can draw it before rdpq_detach_show
    g_demoIrisDraw = demo_draw_iris_callback;

    // Start the first demo
    demo_start_next();

    // Initialize the game scene in demo mode
    init_game_scene();
}

void deinit_demo_scene(void) {
    debugf("Demo scene deinit\n");

    // Free lazy-loaded UI sounds to reduce open wav64 file count
    ui_free_key_sounds();

    // Stop demo playback
    demo_stop_playback();
    g_demoMode = false;
    extern bool g_replayMode;
    g_replayMode = false;

    // Clear the overlay callbacks BEFORE freeing sprites to prevent use-after-free
    g_demoOverlayDraw = NULL;
    g_demoOverlayUpdate = NULL;
    g_demoIrisDraw = NULL;

    // CRITICAL: Wait for RSP/RDP to finish before freeing sprites
    // The overlay may have just drawn these sprites and RDP could still be using them
    rspq_wait();

    // Free Press Start sprite (now safe since RDP is done with it)
    if (pressStartSprite) {
        sprite_free(pressStartSprite);
        pressStartSprite = NULL;
    }

    // Clean up game scene (also calls rspq_wait internally)
    deinit_game_scene();
}

void update_demo_scene(void) {
    fpu_flush_denormals();  // Prevent FPU denormal exceptions

    // =========================================================================
    // CRITICAL FIX: Process pending level switch at START of frame
    // This ensures the previous frame's draw_game_scene() has completed
    // before we free any resources. The race condition was: update frees
    // resources, then draw tries to use them on the same frame.
    // =========================================================================
    if (pendingLevelSwitch) {
        pendingLevelSwitch = false;

        // Clear iris state BEFORE any deinit/init to prevent drawing during transition
        switchingDemo = false;
        exitIrisRadius = (float)SCREEN_WIDTH + 80.0f;  // Fully open (prevents iris draw during transition)

        debugf("Demo: Processing pending level switch (deferred from last frame)\n");

        // Flush denormals BEFORE deinit to prevent FPU exceptions during cleanup
        fpu_flush_denormals();

        // Stop ALL audio channels before level transition
        for (int ch = 0; ch < 8; ch++) {
            mixer_ch_stop(ch);
        }
        rspq_highpri_sync();  // Sync high-priority RSP queue (audio) before deinit

        // CRITICAL: Wait for RSP/RDP to fully complete before deinit
        // deinit_game_scene() also calls rspq_wait(), but we call it explicitly here
        // to ensure the queue is fully drained BEFORE any callbacks can fire
        rspq_wait();

        deinit_game_scene();

        // Start the next demo (this sets selectedLevelID and starts playback)
        demo_start_next();

        // Ensure FPU is in correct state before reinit
        fpu_flush_denormals();

        // Reinit game scene with new level
        init_game_scene();

        // Sync RDP after level load to ensure clean state before drawing
        rspq_wait();
        rdpq_sync_full(NULL, NULL);

        // Start opening the iris
        irisOpening = true;
        justSwitchedLevel = true;  // Skip update_game_scene this frame
        debugf("Demo: New level loaded, opening iris\n");
    }

    // Process pending tutorial loop (deferred from last frame)
    if (pendingTutorialLoop) {
        pendingTutorialLoop = false;
        debugf("Demo: Processing pending tutorial loop (deferred from last frame)\n");

        const DemoData* demo = demo_get(currentDemoIndex);
        if (demo) {
            fpu_flush_denormals();
            // CRITICAL: Wait for RSP/RDP to fully complete before deinit
            rspq_wait();
            deinit_game_scene();
            demo_start_playback(demo);
            demoTimer = 0.0f;
            fpu_flush_denormals();
            init_game_scene();

            // Sync RDP after level load to ensure clean state before drawing
            rspq_wait();
            rdpq_sync_full(NULL, NULL);

            justSwitchedLevel = true;  // Skip update_game_scene this frame
        }
    }

    // NOTE: Audio mixer is handled by update_game_scene() (game.c line 3354)
    // Do NOT call mixer_poll() here - double-polling causes RDP buffer overflow crash
    // on heavy levels like Level 3 (6 map chunks + many decorations)

    float dt = 1.0f / 30.0f;  // Assuming 30fps

    // Handle iris animation (either for exiting to menu or switching demos)
    if (exitingToMenu || switchingDemo) {
        float maxIrisRadius = (float)SCREEN_WIDTH + 80.0f;
        if (irisOpening) {
            // Opening iris (after switching demo)
            float expandSpeed = (maxIrisRadius - exitIrisRadius) * 0.12f;
            if (expandSpeed < 20.0f) expandSpeed = 20.0f;
            exitIrisRadius += expandSpeed;

            if (exitIrisRadius >= maxIrisRadius) {
                exitIrisRadius = maxIrisRadius;
                irisOpening = false;
                switchingDemo = false;
                debugf("Demo: Iris fully open, demo playing\n");
            }
        } else {
            // Closing iris - use linear speed for smoother animation
            float shrinkSpeed = 30.0f;  // Constant speed for visible closing
            exitIrisRadius -= shrinkSpeed;

            if (exitIrisRadius <= 0.0f) {
                exitIrisRadius = 0.0f;

                if (exitingToMenu) {
                    // Iris fully closed, go to menu
                    debugf("Demo: Iris closed, going to menu\n");
                    demo_stop_playback();
                    g_demoMode = false;
                    menuStartWithIrisOpen = true;  // Menu opens with iris effect
                    change_scene(MENU_SCENE);
                    return;
                } else if (switchingDemo) {
                    // In tutorial mode, return to specified scene after demo finishes
                    if (tutorialMode && g_demoReturnScene >= 0) {
                        debugf("Demo: Tutorial finished, returning to scene %d\n", g_demoReturnScene);
                        demo_stop_playback();
                        g_demoMode = false;

                        // Set up the return level and position for body part tutorials
                        if (g_tutorialType != TUTORIAL_NONE && g_tutorialReturnLevel >= 0) {
                            selectedLevelID = g_tutorialReturnLevel;
                            debugf("Tutorial return: level %d, pos (%.1f, %.1f, %.1f)\n",
                                   g_tutorialReturnLevel, g_tutorialReturnX, g_tutorialReturnY, g_tutorialReturnZ);
                        }

                        int returnScene = g_demoReturnScene;
                        g_demoReturnScene = -1;  // Reset for next time
                        tutorialMode = false;
                        // Note: g_tutorialType and return position are preserved for init_game_scene to use
                        change_scene(returnScene);
                        return;
                    }

                    // Iris fully closed - DEFER level switch to next frame
                    // This is the CRITICAL FIX: don't do deinit/init here because
                    // draw_game_scene() hasn't happened yet this frame. Setting the
                    // pending flag means the switch happens at the START of next frame,
                    // after the current frame's draw is complete.
                    debugf("Demo: Iris closed, deferring level switch to next frame\n");
                    pendingLevelSwitch = true;
                    // Don't call deinit/init here! It will happen next frame.
                }
            }
        }

        // Still update game scene for visual continuity if we have one
        // CRITICAL: Skip update on the frame when we just switched levels to avoid
        // NULL pointer dereference in platform_get_displacement. The game scene needs
        // one frame to fully stabilize after init before update can safely run.
        if (is_game_scene_initialized() && (!irisOpening || exitIrisRadius > 0.0f)) {
            if (justSwitchedLevel) {
                justSwitchedLevel = false;  // Clear flag, next frame is safe
                skipNextDraw = true;  // Also skip the draw this frame
                debugf("Demo: Skipping update_game_scene on level switch frame\n");
            } else {
                update_game_scene();
            }
        }
        return;
    }

    // Update Press Start animation timer (wrap to prevent FPU overflow)
    pressStartTimer += dt;
    if (pressStartTimer > 1000.0f) {
        pressStartTimer -= 1000.0f;
    }

    // Update demo timer
    demoTimer += dt;

    // Poll joypad for input
    joypad_poll();

    // Handle tutorial dialogue updates
    if (g_tutorialType != TUTORIAL_NONE && tutorialDialogueActive) {
        // Update dialogue - returns false when dialogue is completely closed
        bool dialogueStillActive = dialogue_update(&tutorialDialogue, JOYPAD_PORT_1);

        // Dialogue is done when update returns false (closing animation completed)
        if (!dialogueStillActive) {
            tutorialDialogueActive = false;
            debugf("Tutorial dialogue finished!\n");
            debugf("  g_tutorialReturnLevel = %d\n", g_tutorialReturnLevel);
            debugf("  g_demoReturnScene = %d\n", g_demoReturnScene);
            debugf("  selectedLevelID before = %d\n", selectedLevelID);

            // Immediately return to game
            demo_stop_playback();
            g_demoMode = false;

            if (g_tutorialType != TUTORIAL_NONE && g_tutorialReturnLevel >= 0) {
                selectedLevelID = g_tutorialReturnLevel;
                debugf("  selectedLevelID after = %d\n", selectedLevelID);
            }

            tutorialMode = false;
            int returnScene = g_demoReturnScene;
            g_demoReturnScene = -1;
            debugf("  Calling change_scene(%d)\n", returnScene);
            change_scene(returnScene);
            return;
        }

        // Check if demo finished - loop it while dialogue is still active
        // DEFER to next frame to prevent race condition with draw
        if (!demo_is_playing() || demoTimer >= DEMO_MAX_TIME) {
            debugf("Demo: Tutorial demo finished, deferring loop to next frame\n");
            pendingTutorialLoop = true;
            return;
        }

        // Update the game scene for demo playback
        if (is_game_scene_initialized()) {
            update_game_scene();
        }
        return;
    }

    // Check for any button press to exit demo to menu (only in non-tutorial mode)
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (!tutorialMode && g_tutorialType == TUTORIAL_NONE) {
        // Normal demo mode - any button exits to menu
        if (pressed.start || pressed.a || pressed.b || pressed.z ||
            pressed.l || pressed.r || pressed.c_up || pressed.c_down ||
            pressed.c_left || pressed.c_right || pressed.d_up ||
            pressed.d_down || pressed.d_left || pressed.d_right) {
            debugf("Demo: Button pressed, starting iris close to menu\n");
            exitingToMenu = true;
            irisOpening = false;
            exitIrisRadius = 399.0f;  // Start just below 400 so draw condition triggers immediately
            return;
        }
    }

    // In tutorial mode, check if dialogue is done - if so, return to game immediately
    if (tutorialMode && !tutorialDialogueActive) {
        debugf("Demo: Tutorial dialogue complete, returning to game scene %d, level %d\n",
               g_demoReturnScene, g_tutorialReturnLevel);

        // Stop demo and return directly to game
        demo_stop_playback();
        g_demoMode = false;

        // Set up the return level
        if (g_tutorialType != TUTORIAL_NONE && g_tutorialReturnLevel >= 0) {
            selectedLevelID = g_tutorialReturnLevel;
        }

        // Clean up and change scene
        tutorialMode = false;
        int returnScene = g_demoReturnScene;
        g_demoReturnScene = -1;
        // Note: g_tutorialType and return position preserved for init_game_scene

        change_scene(returnScene);
        return;
    }

    // Check if demo finished, goal reached, OR hit 20-second timeout
    if (!demo_is_playing() || demoTimer >= DEMO_MAX_TIME || g_demoGoalReached) {
        // In tutorial mode, loop the demo until dialogue is finished
        // DEFER to next frame to prevent race condition with draw
        if (tutorialMode && tutorialDialogueActive) {
            debugf("Demo: Tutorial demo finished, deferring loop to next frame\n");
            pendingTutorialLoop = true;
            return;
        }

        // Increment cycle count
        demoCycleCount++;
        debugf("Demo: Cycle %d/%d completed\n", demoCycleCount, MAX_DEMO_CYCLES);

        // Check if we've hit the cycle limit - exit to menu to avoid NaN crash
        if (demoCycleCount >= MAX_DEMO_CYCLES) {
            debugf("Demo: Max cycles reached (%d), returning to menu\n", MAX_DEMO_CYCLES);
            exitingToMenu = true;
            irisOpening = false;
            exitIrisRadius = 399.0f;
            pressStartTimer = 0.0f;
            return;
        }

        // Normal demo mode - switch to next demo
        if (g_demoGoalReached) {
            debugf("Demo: Goal reached, starting iris close for next demo\n");
            g_demoGoalReached = false;  // Reset the flag
        } else if (demoTimer >= DEMO_MAX_TIME) {
            debugf("Demo: 20-second timeout, starting iris close for next demo\n");
        } else {
            debugf("Demo: Playback finished, starting iris close for next demo\n");
        }

        // Start the iris close transition to switch demos
        debugf("Demo: Setting switchingDemo=true, exitIrisRadius=399\n");
        switchingDemo = true;
        irisOpening = false;
        exitIrisRadius = 399.0f;  // Start just below 400 so draw condition triggers immediately

        // Reset the Press Start animation timer for a fresh bounce
        pressStartTimer = 0.0f;
        return;
    }

    // Update game scene normally (it will read from demo data)
    if (is_game_scene_initialized()) {
        update_game_scene();
    }
}

// Draw an iris wipe overlay (circle that shrinks to center)
// Uses triangle-based approach like menu scene for better N64 compatibility
static void draw_iris_overlay(float radius) {
    float maxIrisRadius = (float)SCREEN_WIDTH + 80.0f;
    if (radius >= maxIrisRadius) return;  // Fully open, nothing to draw

    // Note: RSP sync is done in game.c before calling this callback
    // Don't double-sync as it can cause display timing issues

    // Prevent FPU exceptions during cosf/sinf operations
    fpu_flush_denormals();

    // IRIS EFFECT DISABLED - rdpq_triangle causes RDP hardware crashes on real N64
    // Using simple alpha fade instead for stability
    float r = radius;

    // CRITICAL: Sync T3D triangles before rdpq 2D operations
    t3d_tri_sync();

    // Validate radius
    if (isnan(r) || isinf(r) || r < 0.0f) {
        r = 0.0f;  // Treat as fully closed
    }

    // Max radius is SCREEN_WIDTH + 80 (about 400)
    float maxRadius = (float)SCREEN_WIDTH + 80.0f;
    if (r > maxRadius) {
        r = maxRadius;
    }

    // Convert radius to alpha (inverted - smaller radius = more black)
    // r = maxRadius means fully open (alpha = 0), r = 0 means fully closed (alpha = 255)
    int alpha = (int)((1.0f - (r / maxRadius)) * 255.0f);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    // Only draw if there's something to show
    if (alpha > 0) {
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0, 0, 0, (uint8_t)alpha));
        rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    }
}

// Callback for game.c to draw iris (called just before rdpq_detach_show)
static void demo_draw_iris_callback(void) {
    // Draw iris overlay during any transition
    float maxIrisRadius = (float)SCREEN_WIDTH + 80.0f;
    if ((exitingToMenu || switchingDemo) && exitIrisRadius < maxIrisRadius) {
        // Validate iris radius before drawing
        if (isnan(exitIrisRadius) || isinf(exitIrisRadius) || exitIrisRadius < 0.0f) {
            debugf("ERROR: demo_draw_iris_callback: invalid radius %f\n", exitIrisRadius);
            return;
        }
        draw_iris_overlay(exitIrisRadius);
    }
}

void draw_demo_scene(void) {
    // CRITICAL FIX: Skip drawing when level just switched or switch is pending
    // This prevents drawing with stale/corrupted model/texture data
    if (skipNextDraw || pendingLevelSwitch) {
        if (skipNextDraw) skipNextDraw = false;
        debugf("Demo: Skipping draw_game_scene on level switch frame\n");
        return;
    }

    // Draw game scene (this will call g_demoOverlayDraw and g_demoIrisDraw via callbacks)
    if (is_game_scene_initialized()) {
        draw_game_scene();
    }
}
