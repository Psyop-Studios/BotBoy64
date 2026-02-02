#include "controls.h"
#include <math.h>
#include "scenes/game.h"
#include "constants.h"

// Set FPU to flush denormals to zero and disable FPU exceptions
static void fpu_flush_denormals(void) {
    uint32_t fcr31;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcr31));
    fcr31 |= (1 << 24);     // FS bit: flush denormals to zero
    fcr31 &= ~(0x1F << 7);  // Clear exception enable bits
    fcr31 &= ~(0x3F << 2);  // Clear cause bits
    fcr31 &= ~(1 << 17);    // Clear unimplemented operation cause
    fcr31 &= ~(0x1F << 12); // Clear sticky FLAG bits
    __asm__ volatile("ctc1 %0, $31" : : "r"(fcr31));
}

// Debug fly mode
bool debugFlyMode = false;

// Disable normal jump (for charge-jump-only modes like torso)
bool disableNormalJump = false;

// Ground physics constants (tuned for this game's scale)
#define GROUND_WALK_ACCEL 0.8f           // Acceleration per frame
#define GROUND_WALK_SPEED_CAP 6.0f       // Normal max walk speed
#define GROUND_FRICTION_NORMAL 0.50f     // Ground friction (lower = stops WAY faster)
#define GROUND_FRICTION_SLIPPERY 0.90f   // Oil/ice friction

void controls_init(ControlConfig* config) {
    config->moveSpeed = GROUND_WALK_SPEED_CAP;    // Max speed
    config->jumpForce = 7.0f * FPS_SCALE_SQRT;  // Scale for 30 FPS
    config->gravity = GRAVITY;                   // Use scaled constant
    config->totalJumps = 1;
    config->flySpeed = 5.0f * FPS_SCALE;        // Scale for 30 FPS
}

bool controls_update(PlayerState* state, ControlConfig* config, joypad_port_t port) {
    // Prevent FPU denormal exceptions from sqrtf/atan2f with joypad input
    fpu_flush_denormals();

    joypad_poll();
    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);
    joypad_buttons_t held = joypad_get_buttons_held(port);
    joypad_inputs_t inputs = joypad_get_inputs(port);

    // Toggle debug fly mode with Z button
    if (pressed.z) {
        //debugFlyMode = !debugFlyMode;
    }

    // 2-axis movement: X (horizontal) and Z (depth)
    // Apply deadzone to prevent drift from stick not centering perfectly
    float stickX = apply_deadzone(inputs.stick_x / 128.0f);
    float stickY = apply_deadzone(inputs.stick_y / 128.0f);

    if (debugFlyMode) {
        // Fly mode: free movement, no gravity
        state->velX = -stickX * config->flySpeed;
        state->velZ = stickY * config->flySpeed;
        state->velY = 0.0f;

        // C-up/C-down for vertical movement
        if (held.c_up) {
            state->velY = config->flySpeed;
        } else if (held.c_down) {
            state->velY = -config->flySpeed;
        }

        // A/B for faster up/down
        if (held.a) {
            state->velY = config->flySpeed * 2.0f;
        } else if (held.b) {
            state->velY = -config->flySpeed * 2.0f;
        }

        return false; // No animation in fly mode
    }

    // =================================================================
    // GROUND MOVEMENT
    // Uses acceleration toward target velocity + friction when stopping
    // =================================================================

    // Calculate input magnitude and direction
    float inputMag = sqrtf(stickX * stickX + stickY * stickY);
    if (inputMag > 1.0f) inputMag = 1.0f;  // Clamp to unit circle

    // Target velocity based on input (inverted X for camera facing -Z)
    // Chargepad speed buff doubles movement speed (legs mode)
    extern float buff_get_speed_timer(void);
    float speedMult = buff_get_speed_timer() > 0.0f ? 2.0f : 1.0f;
    float targetVelX = -stickX * config->moveSpeed * speedMult;
    float targetVelZ = stickY * config->moveSpeed * speedMult;

    // If movement is allowed and not sliding
    // Ground movement OR air control (if enabled for current part)
    bool hasAirControl = state->hasAirControl && !state->isGrounded;
    if (state->canMove && !state->isSliding && (state->isGrounded || hasAirControl)) {
        // Choose friction based on surface
        float friction = state->onOilPuddle ? GROUND_FRICTION_SLIPPERY : GROUND_FRICTION_NORMAL;

        if (inputMag > 0.1f) {
            // Player is pressing a direction - accelerate toward target
            // Acceleration is speed-dependent (faster = less accel)
            float currentSpeed = sqrtf(state->velX * state->velX + state->velZ * state->velZ);
            float accel = GROUND_WALK_ACCEL - currentSpeed / 43.0f;
            if (accel < 0.1f) accel = 0.1f;

            // Apply slope influence (slower uphill, faster downhill)
            if (state->isOnSlope && state->slopeNormalY < 0.99f) {
                float steepness = sqrtf(state->slopeNormalX * state->slopeNormalX +
                                        state->slopeNormalZ * state->slopeNormalZ);

                if (steepness > 0.01f) {
                    // Movement direction
                    float targetLen = sqrtf(targetVelX * targetVelX + targetVelZ * targetVelZ);
                    if (targetLen > 0.1f) {
                        float moveDirX = targetVelX / targetLen;
                        float moveDirZ = targetVelZ / targetLen;

                        // Dot product with downhill direction (nx, nz normalized)
                        float uphillDot = (moveDirX * state->slopeNormalX + moveDirZ * state->slopeNormalZ) / steepness;

                        if (uphillDot > 0.0f) {
                            // Going uphill - reduce target speed
                            float uphillPenalty = 1.0f - steepness * uphillDot * 0.7f;
                            if (uphillPenalty < 0.3f) uphillPenalty = 0.3f;
                            targetVelX *= uphillPenalty;
                            targetVelZ *= uphillPenalty;
                        } else {
                            // Going downhill - slight speed boost
                            float downhillBoost = 1.0f - uphillDot * steepness * 0.3f;
                            if (downhillBoost > 1.5f) downhillBoost = 1.5f;
                            targetVelX *= downhillBoost;
                            targetVelZ *= downhillBoost;
                        }
                    }
                }
            }

            // Lerp toward target velocity (acceleration)
            // Oil makes steering sluggish, normal is snappy
            float steerRate = state->onOilPuddle ? 0.05f : 0.5f;
            state->velX += (targetVelX - state->velX) * steerRate;
            state->velZ += (targetVelZ - state->velZ) * steerRate;

            // Also accelerate magnitude toward target
            float targetSpeed = sqrtf(targetVelX * targetVelX + targetVelZ * targetVelZ);
            if (currentSpeed < targetSpeed) {
                currentSpeed += accel;
                if (currentSpeed > targetSpeed) currentSpeed = targetSpeed;
            }

            // Apply new speed in current direction
            if (currentSpeed > 0.1f) {
                float velLen = sqrtf(state->velX * state->velX + state->velZ * state->velZ);
                if (velLen > 0.1f) {
                    state->velX = (state->velX / velLen) * currentSpeed;
                    state->velZ = (state->velZ / velLen) * currentSpeed;
                }
            }
        } else {
            // No input - apply friction to slow down
            state->velX *= friction;
            state->velZ *= friction;

            // Zero out tiny velocities
            if (state->velX * state->velX + state->velZ * state->velZ < 0.5f) {
                state->velX = 0.0f;
                state->velZ = 0.0f;
            }
        }

        // Hard speed cap - prevent velocity from exceeding max speed (prevents infinite speed exploits)
        float maxSpeed = config->moveSpeed * speedMult;
        float speedSq = state->velX * state->velX + state->velZ * state->velZ;
        if (speedSq > maxSpeed * maxSpeed) {
            float scale = maxSpeed / sqrtf(speedSq);
            state->velX *= scale;
            state->velZ *= scale;
        }
    }

    // Update facing direction based on input when rotation is allowed.
    if (state->canRotate) {
        if (fabsf(stickX) > 0.1f || fabsf(stickY) > 0.1f) {
            state->playerAngle = atan2f(-targetVelX, targetVelZ);
        }
    }
    if(pressed.l){
        partsObtained--;
    }
    if(pressed.r){
        partsObtained++;
    }
    // Apply gravity (reverse if reverseGravity is true)
    // Don't apply gravity when grounded - ground collision handles Y position
    // Sliding only skips gravity when ALSO grounded (airborne sliding still falls)
    if (!state->isGrounded) {
        // Reduced gravity when gliding (arms spin in air)
        // Chargepad glide buff reduces gravity even further (2x distance = 0.5x gravity)
        extern bool buff_get_glide_active(void);
        float gravityMult = 1.0f;
        if (state->isGliding) {
            gravityMult = buff_get_glide_active() ? 0.125f : 0.25f;  // 0.125 = 2x distance
        }
        if (reverseGravity) {
            state->velY += config->gravity * gravityMult;  // Reverse gravity
        } else {
            state->velY -= config->gravity * gravityMult;  // Normal gravity
        }
    }
    if (partsObtained == 3 && !disableNormalJump)
    {
        // Jump with A button (supports multi-jump)
        if (pressed.a && state->currentJumps < config->totalJumps)
        {
            state->velY = config->jumpForce;
            state->currentJumps++;
            state->isGrounded = false;
        }
    }

    bool isMoving = (fabsf(state->velX) > 0.1f || fabsf(state->velZ) > 0.1f) && state->canMove;
    return isMoving;
}

// Button mask constants for replay (matching game.c encoding)
#define REPLAY_BTN_A       0x0001
#define REPLAY_BTN_B       0x0002
#define REPLAY_BTN_Z       0x0004
#define REPLAY_BTN_START   0x0008
#define REPLAY_BTN_DU      0x0010
#define REPLAY_BTN_DD      0x0020
#define REPLAY_BTN_DL      0x0040
#define REPLAY_BTN_DR      0x0080
#define REPLAY_BTN_L       0x0100
#define REPLAY_BTN_R       0x0200
#define REPLAY_BTN_CU      0x0400
#define REPLAY_BTN_CD      0x0800
#define REPLAY_BTN_CL      0x1000
#define REPLAY_BTN_CR      0x2000

// State tracking for replay "pressed" detection
static uint16_t prevReplayButtons = 0;

bool controls_update_replay(PlayerState* state, ControlConfig* config, int8_t stickX, int8_t stickY, uint16_t buttons) {
    // Validate pointers - can be corrupted during level transitions
    if (state == NULL || config == NULL) {
        return false;
    }
    // Check if pointers are valid RDRAM addresses (0x80000000 - 0x80800000)
    uintptr_t statePtr = (uintptr_t)state;
    uintptr_t configPtr = (uintptr_t)config;
    if (statePtr < 0x80000000 || statePtr >= 0x80800000 ||
        configPtr < 0x80000000 || configPtr >= 0x80800000) {
        return false;
    }

    // Prevent FPU denormal exceptions from sqrtf/atan2f with replay input
    fpu_flush_denormals();

    // Simulate joypad held state from bitmask
    bool heldA = (buttons & REPLAY_BTN_A) != 0;
    bool heldB = (buttons & REPLAY_BTN_B) != 0;
    bool heldL = (buttons & REPLAY_BTN_L) != 0;
    bool heldR = (buttons & REPLAY_BTN_R) != 0;
    bool heldCU = (buttons & REPLAY_BTN_CU) != 0;
    bool heldCD = (buttons & REPLAY_BTN_CD) != 0;

    // Simulate "pressed" (just pressed this frame, not last frame)
    bool pressedA = heldA && !(prevReplayButtons & REPLAY_BTN_A);
    bool pressedL = heldL && !(prevReplayButtons & REPLAY_BTN_L);
    bool pressedR = heldR && !(prevReplayButtons & REPLAY_BTN_R);

    prevReplayButtons = buttons;

    // Apply deadzone to stick values
    float stickXf = apply_deadzone(stickX / 128.0f);
    float stickYf = apply_deadzone(stickY / 128.0f);

    if (debugFlyMode) {
        // Fly mode: free movement, no gravity
        state->velX = -stickXf * config->flySpeed;
        state->velZ = stickYf * config->flySpeed;
        state->velY = 0.0f;

        if (heldCU) {
            state->velY = config->flySpeed;
        } else if (heldCD) {
            state->velY = -config->flySpeed;
        }

        if (heldA) {
            state->velY = config->flySpeed * 2.0f;
        } else if (heldB) {
            state->velY = -config->flySpeed * 2.0f;
        }

        return false;
    }

    // Calculate input magnitude and direction
    float inputMag = sqrtf(stickXf * stickXf + stickYf * stickYf);
    if (inputMag > 1.0f) inputMag = 1.0f;

    float targetVelX = -stickXf * config->moveSpeed;
    float targetVelZ = stickYf * config->moveSpeed;

    bool hasAirControl = state->hasAirControl && !state->isGrounded;
    if (state->canMove && !state->isSliding && (state->isGrounded || hasAirControl)) {
        float friction = state->onOilPuddle ? GROUND_FRICTION_SLIPPERY : GROUND_FRICTION_NORMAL;

        if (inputMag > 0.1f) {
            float currentSpeed = sqrtf(state->velX * state->velX + state->velZ * state->velZ);
            float accel = GROUND_WALK_ACCEL - currentSpeed / 43.0f;
            if (accel < 0.1f) accel = 0.1f;

            if (state->isOnSlope && state->slopeNormalY < 0.99f) {
                float steepness = sqrtf(state->slopeNormalX * state->slopeNormalX +
                                        state->slopeNormalZ * state->slopeNormalZ);

                if (steepness > 0.01f) {
                    float targetLen = sqrtf(targetVelX * targetVelX + targetVelZ * targetVelZ);
                    if (targetLen > 0.1f) {
                        float moveDirX = targetVelX / targetLen;
                        float moveDirZ = targetVelZ / targetLen;
                        float uphillDot = (moveDirX * state->slopeNormalX + moveDirZ * state->slopeNormalZ) / steepness;

                        if (uphillDot > 0.0f) {
                            float uphillPenalty = 1.0f - steepness * uphillDot * 0.7f;
                            if (uphillPenalty < 0.3f) uphillPenalty = 0.3f;
                            targetVelX *= uphillPenalty;
                            targetVelZ *= uphillPenalty;
                        } else {
                            float downhillBoost = 1.0f - uphillDot * steepness * 0.3f;
                            if (downhillBoost > 1.5f) downhillBoost = 1.5f;
                            targetVelX *= downhillBoost;
                            targetVelZ *= downhillBoost;
                        }
                    }
                }
            }

            float steerRate = state->onOilPuddle ? 0.05f : 0.5f;
            state->velX += (targetVelX - state->velX) * steerRate;
            state->velZ += (targetVelZ - state->velZ) * steerRate;

            float targetSpeed = sqrtf(targetVelX * targetVelX + targetVelZ * targetVelZ);
            if (currentSpeed < targetSpeed) {
                currentSpeed += accel;
                if (currentSpeed > targetSpeed) currentSpeed = targetSpeed;
            }

            if (currentSpeed > 0.1f) {
                float velLen = sqrtf(state->velX * state->velX + state->velZ * state->velZ);
                if (velLen > 0.1f) {
                    state->velX = (state->velX / velLen) * currentSpeed;
                    state->velZ = (state->velZ / velLen) * currentSpeed;
                }
            }
        } else {
            state->velX *= friction;
            state->velZ *= friction;

            if (state->velX * state->velX + state->velZ * state->velZ < 0.5f) {
                state->velX = 0.0f;
                state->velZ = 0.0f;
            }
        }

        // Hard speed cap - prevent velocity from exceeding max speed (prevents infinite speed exploits)
        float maxSpeed = config->moveSpeed;
        float speedSq = state->velX * state->velX + state->velZ * state->velZ;
        if (speedSq > maxSpeed * maxSpeed) {
            float scale = maxSpeed / sqrtf(speedSq);
            state->velX *= scale;
            state->velZ *= scale;
        }
    }

    if (state->canRotate) {
        if (fabsf(stickXf) > 0.1f || fabsf(stickYf) > 0.1f) {
            state->playerAngle = atan2f(-targetVelX, targetVelZ);
        }
    }

    if (pressedL) {
        partsObtained--;
    }
    if (pressedR) {
        partsObtained++;
    }

    if (!state->isGrounded) {
        // Chargepad glide buff reduces gravity even further (2x distance = 0.5x gravity)
        extern bool buff_get_glide_active(void);
        float gravityMult = 1.0f;
        if (state->isGliding) {
            gravityMult = buff_get_glide_active() ? 0.125f : 0.25f;
        }
        if (reverseGravity) {
            state->velY += config->gravity * gravityMult;
        } else {
            state->velY -= config->gravity * gravityMult;
        }
    }

    if (partsObtained == 3 && !disableNormalJump) {
        if (pressedA && state->currentJumps < config->totalJumps) {
            state->velY = config->jumpForce;
            state->currentJumps++;
            state->isGrounded = false;
        }
    }

    bool isMoving = (fabsf(state->velX) > 0.1f || fabsf(state->velZ) > 0.1f) && state->canMove;
    return isMoving;
}
