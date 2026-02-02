#include "player.h"
#include "mapLoader.h"
#include "mapData.h"
#include "scenes/game.h"
#include <malloc.h>
#include <stdlib.h>
#include <math.h>

// Moon jump cheat from game.c
extern bool reverseGravity;

// ============================================================
// LIFECYCLE FUNCTIONS
// ============================================================

void player_init(Player* p, int index, T3DModel* model, float x, float y, float z, PlayerPart bodyPart) {
    // Clear entire struct to zero
    memset(p, 0, sizeof(Player));

    // Basic info
    p->playerIndex = index;
    p->port = (index == 0) ? JOYPAD_PORT_1 : JOYPAD_PORT_2;
    p->tint = (index == 0) ? RGBA32(255, 255, 255, 255) : RGBA32(200, 150, 255, 255);

    // Position
    p->x = x;
    p->y = y;
    p->z = z;
    p->groundLevel = y;
    p->spawnX = x;
    p->spawnY = y;
    p->spawnZ = z;

    // Physics - use default config from controls.h
    controls_init(&p->config);
    memset(&p->physics, 0, sizeof(PlayerState));
    p->physics.canMove = true;
    p->physics.canRotate = true;
    p->physics.canJump = true;

    // Set body part mode from parameter
    p->currentPart = bodyPart;
    p->isArmsMode = (bodyPart == PLAYER_PART_ARMS);

    // Health
    p->health = 3;
    p->maxHealth = 3;

    // Squash/stretch defaults
    p->squashScale = 1.0f;

    // Iris effect defaults (prevent FPU crashes from uninitialized values)
    // These must be valid floats even if irisActive is false
    p->irisRadius = 400.0f;      // Fully open (larger than screen)
    p->irisCenterX = 160.0f;     // Screen center X
    p->irisCenterY = 120.0f;     // Screen center Y
    p->irisTargetX = 160.0f;
    p->irisTargetY = 120.0f;
    p->irisActive = false;
    p->irisPaused = false;
    p->irisPauseTimer = 0.0f;
    p->fadeAlpha = 0.0f;

    // Camera initial position
    p->camPos.v[0] = x;
    p->camPos.v[1] = y + 50.0f;
    p->camPos.v[2] = z + 150.0f;
    p->camTarget.v[0] = x;
    p->camTarget.v[1] = y + PLAYER_CAMERA_OFFSET_Y;
    p->camTarget.v[2] = z;
    p->smoothCamX = x;
    p->smoothCamY = y;

    // Store model reference
    p->model = model;

    // Allocate transform matrix
    p->matFP = malloc_uncached(sizeof(T3DMat4FP));

    // Load animations from model based on body part
    if (model) {
        player_load_animations(p, model, bodyPart);
    }
}

void player_load_animations(Player* p, T3DModel* model, PlayerPart bodyPart) {
    // Create skeleton
    p->skeleton = t3d_skeleton_create(model);
    p->skeletonBlend = t3d_skeleton_clone(&p->skeleton, false);

    // Initialize all animation flags to false
    p->torsoHasAnims = false;
    p->armsHasAnims = false;
    p->headHasAnims = false;

    // Load animations based on body part
    if (bodyPart == PLAYER_PART_HEAD || bodyPart == PLAYER_PART_TORSO) {
        // Torso model has torso_* and head_* animations
        p->animIdle = t3d_anim_create(model, "torso_idle");
        t3d_anim_set_looping(&p->animIdle, true);
        t3d_anim_attach(&p->animIdle, &p->skeleton);
        t3d_anim_set_playing(&p->animIdle, true);

        p->animWalk = t3d_anim_create(model, "torso_walk_fast");
        t3d_anim_attach(&p->animWalk, &p->skeletonBlend);
        t3d_anim_set_looping(&p->animWalk, true);
        t3d_anim_set_playing(&p->animWalk, true);

        p->animJumpCharge = t3d_anim_create(model, "torso_jump_charge");
        t3d_anim_set_looping(&p->animJumpCharge, false);

        p->animJumpLaunch = t3d_anim_create(model, "torso_jump_launch");
        t3d_anim_set_looping(&p->animJumpLaunch, false);

        p->animJumpLand = t3d_anim_create(model, "torso_jump_land");
        t3d_anim_set_looping(&p->animJumpLand, false);

        p->animWait = t3d_anim_create(model, "torso_wait");
        t3d_anim_set_looping(&p->animWait, false);

        p->animPain1 = t3d_anim_create(model, "torso_pain_1");
        t3d_anim_set_looping(&p->animPain1, false);

        p->animPain2 = t3d_anim_create(model, "torso_pain_2");
        t3d_anim_set_looping(&p->animPain2, false);

        p->animDeath = t3d_anim_create(model, "torso_death");
        t3d_anim_set_looping(&p->animDeath, false);

        p->animSlideFront = t3d_anim_create(model, "torso_slide_front");
        t3d_anim_set_looping(&p->animSlideFront, false);

        p->animSlideFrontRecover = t3d_anim_create(model, "torso_slide_front_recover");
        t3d_anim_set_looping(&p->animSlideFrontRecover, false);

        p->animSlideBack = t3d_anim_create(model, "torso_slide_back");
        t3d_anim_set_looping(&p->animSlideBack, false);

        p->animSlideBackRecover = t3d_anim_create(model, "torso_slide_back_recover");
        t3d_anim_set_looping(&p->animSlideBackRecover, false);

        p->torsoHasAnims = p->animIdle.animRef != NULL;

        // Torso model also has head animations
        p->animHeadIdle = t3d_anim_create(model, "head_idle");
        t3d_anim_set_looping(&p->animHeadIdle, true);

        p->animHeadWalk = t3d_anim_create(model, "head_walk");
        t3d_anim_set_looping(&p->animHeadWalk, true);

        p->headHasAnims = p->animHeadIdle.animRef && p->animHeadWalk.animRef;

        // Set initial animation
        p->currentAnim = &p->animIdle;
        p->attachedAnim = &p->animIdle;

        debugf("Player %d: Torso animations loaded (torso: %s, head: %s)\n",
            p->playerIndex, p->torsoHasAnims ? "OK" : "FAIL", p->headHasAnims ? "OK" : "FAIL");

    } else if (bodyPart == PLAYER_PART_ARMS) {
        // Arms model has arms_* animations
        p->animArmsIdle = t3d_anim_create(model, "arms_idle");
        t3d_anim_set_looping(&p->animArmsIdle, true);
        t3d_anim_attach(&p->animArmsIdle, &p->skeleton);
        t3d_anim_set_playing(&p->animArmsIdle, true);

        p->animArmsWalk1 = t3d_anim_create(model, "arms_walk_1");
        t3d_anim_attach(&p->animArmsWalk1, &p->skeletonBlend);
        t3d_anim_set_looping(&p->animArmsWalk1, true);
        t3d_anim_set_playing(&p->animArmsWalk1, true);

        p->animArmsWalk2 = t3d_anim_create(model, "arms_walk_2");
        t3d_anim_set_looping(&p->animArmsWalk2, true);

        p->animArmsJump = t3d_anim_create(model, "arms_jump");
        t3d_anim_set_looping(&p->animArmsJump, false);

        p->animArmsJumpLand = t3d_anim_create(model, "arms_jump_land");
        t3d_anim_set_looping(&p->animArmsJumpLand, false);

        p->animArmsSpin = t3d_anim_create(model, "arms_atk_spin");
        t3d_anim_set_looping(&p->animArmsSpin, false);

        p->animArmsWhip = t3d_anim_create(model, "arms_atk_whip");
        t3d_anim_set_looping(&p->animArmsWhip, false);

        p->animArmsDeath = t3d_anim_create(model, "arms_death");
        t3d_anim_set_looping(&p->animArmsDeath, false);

        p->animArmsPain1 = t3d_anim_create(model, "arms_pain_1");
        t3d_anim_set_looping(&p->animArmsPain1, false);

        p->animArmsPain2 = t3d_anim_create(model, "arms_pain_2");
        t3d_anim_set_looping(&p->animArmsPain2, false);

        p->animArmsSlide = t3d_anim_create(model, "arms_slide");
        t3d_anim_set_looping(&p->animArmsSlide, true);

        p->armsHasAnims = p->animArmsIdle.animRef && p->animArmsWalk1.animRef && p->animArmsSpin.animRef;

        // Set initial animation (use arms idle)
        p->currentAnim = &p->animArmsIdle;
        p->attachedAnim = &p->animArmsIdle;

        debugf("Player %d: Arms animations loaded (arms: %s)\n",
            p->playerIndex, p->armsHasAnims ? "OK" : "FAIL");

    } else if (bodyPart == PLAYER_PART_LEGS) {
        // Fullbody model has fb_* animations
        // Basic animations (using shared slots)
        p->animIdle = t3d_anim_create(model, "fb_idle");
        t3d_anim_set_looping(&p->animIdle, true);
        t3d_anim_attach(&p->animIdle, &p->skeleton);
        t3d_anim_set_playing(&p->animIdle, true);

        p->animWalk = t3d_anim_create(model, "fb_walk");
        t3d_anim_attach(&p->animWalk, &p->skeletonBlend);
        t3d_anim_set_looping(&p->animWalk, true);
        t3d_anim_set_playing(&p->animWalk, true);

        p->animJumpLaunch = t3d_anim_create(model, "fb_jump");
        t3d_anim_set_looping(&p->animJumpLaunch, false);

        p->animWait = t3d_anim_create(model, "fb_wait");
        t3d_anim_set_looping(&p->animWait, false);

        p->animPain1 = t3d_anim_create(model, "fb_pain_1");
        t3d_anim_set_looping(&p->animPain1, false);

        p->animPain2 = t3d_anim_create(model, "fb_pain_2");
        t3d_anim_set_looping(&p->animPain2, false);

        p->animDeath = t3d_anim_create(model, "fb_death");
        t3d_anim_set_looping(&p->animDeath, false);

        p->animSlideFront = t3d_anim_create(model, "fb_slide");
        t3d_anim_set_looping(&p->animSlideFront, true);

        // Fullbody-specific animations
        p->fbAnimRun = t3d_anim_create(model, "fb_run");
        t3d_anim_set_looping(&p->fbAnimRun, true);

        p->fbAnimCrouch = t3d_anim_create(model, "fb_crouch");
        t3d_anim_set_looping(&p->fbAnimCrouch, false);

        p->fbAnimCrouchJump = t3d_anim_create(model, "fb_crouch_jump");
        t3d_anim_set_looping(&p->fbAnimCrouchJump, false);

        p->fbAnimCrouchJumpHover = t3d_anim_create(model, "fb_crouch_jump_hover");
        t3d_anim_set_looping(&p->fbAnimCrouchJumpHover, true);

        p->fbAnimSpinAir = t3d_anim_create(model, "fb_spin_air");
        t3d_anim_set_looping(&p->fbAnimSpinAir, false);

        p->fbAnimSpinAtk = t3d_anim_create(model, "fb_spin_atk");
        t3d_anim_set_looping(&p->fbAnimSpinAtk, false);

        p->fbAnimSpinCharge = t3d_anim_create(model, "fb_spin_charge");
        t3d_anim_set_looping(&p->fbAnimSpinCharge, false);

        p->fbAnimRunNinja = t3d_anim_create(model, "fb_run_ninja");
        t3d_anim_set_looping(&p->fbAnimRunNinja, true);

        p->fbAnimCrouchAttack = t3d_anim_create(model, "fb_crouch_attack");
        t3d_anim_set_looping(&p->fbAnimCrouchAttack, false);

        p->fbHasAnims = p->animIdle.animRef != NULL;
        p->torsoHasAnims = p->fbHasAnims;  // For compatibility

        // Set initial animation
        p->currentAnim = &p->animIdle;
        p->attachedAnim = &p->animIdle;

        debugf("Player %d: Fullbody animations loaded (fb: %s)\n",
            p->playerIndex, p->fbHasAnims ? "OK" : "FAIL");
    }
}

void player_deinit(Player* p) {
    // Free skeleton resources
    t3d_skeleton_destroy(&p->skeleton);
    t3d_skeleton_destroy(&p->skeletonBlend);

    // Free animations (all T3DAnim need to be destroyed)
    t3d_anim_destroy(&p->animIdle);
    t3d_anim_destroy(&p->animWalk);
    t3d_anim_destroy(&p->animJumpCharge);
    t3d_anim_destroy(&p->animJumpLaunch);
    t3d_anim_destroy(&p->animJumpLand);
    t3d_anim_destroy(&p->animWait);
    t3d_anim_destroy(&p->animPain1);
    t3d_anim_destroy(&p->animPain2);
    t3d_anim_destroy(&p->animDeath);
    t3d_anim_destroy(&p->animSlideFront);
    t3d_anim_destroy(&p->animSlideFrontRecover);
    t3d_anim_destroy(&p->animSlideBack);
    t3d_anim_destroy(&p->animSlideBackRecover);

    t3d_anim_destroy(&p->animArmsIdle);
    t3d_anim_destroy(&p->animArmsWalk1);
    t3d_anim_destroy(&p->animArmsWalk2);
    t3d_anim_destroy(&p->animArmsJump);
    t3d_anim_destroy(&p->animArmsJumpLand);
    t3d_anim_destroy(&p->animArmsSpin);
    t3d_anim_destroy(&p->animArmsWhip);
    t3d_anim_destroy(&p->animArmsDeath);
    t3d_anim_destroy(&p->animArmsPain1);
    t3d_anim_destroy(&p->animArmsPain2);
    t3d_anim_destroy(&p->animArmsSlide);

    t3d_anim_destroy(&p->animHeadIdle);
    t3d_anim_destroy(&p->animHeadWalk);

    // Free fullbody animations (LEGS mode)
    t3d_anim_destroy(&p->fbAnimRun);
    t3d_anim_destroy(&p->fbAnimCrouch);
    t3d_anim_destroy(&p->fbAnimCrouchJump);
    t3d_anim_destroy(&p->fbAnimCrouchJumpHover);
    t3d_anim_destroy(&p->fbAnimSpinAir);
    t3d_anim_destroy(&p->fbAnimSpinAtk);
    t3d_anim_destroy(&p->fbAnimSpinCharge);
    t3d_anim_destroy(&p->fbAnimRunNinja);
    t3d_anim_destroy(&p->fbAnimCrouchAttack);

    // Free matrix
    if (p->matFP) {
        free_uncached(p->matFP);
        p->matFP = NULL;
    }
}

void player_reset(Player* p) {
    // Reset position to spawn
    p->x = p->spawnX;
    p->y = p->spawnY;
    p->z = p->spawnZ;
    p->groundLevel = p->spawnY;
    p->angle = 0.0f;

    // Reset physics
    memset(&p->physics, 0, sizeof(PlayerState));
    p->physics.canMove = true;
    p->physics.canRotate = true;
    p->physics.canJump = true;

    // Reset jump charge state
    p->isCharging = false;
    p->isJumping = false;
    p->isLanding = false;
    p->jumpAnimPaused = false;
    p->jumpChargeTime = 0.0f;
    p->jumpAimX = 0.0f;
    p->jumpAimY = 0.0f;
    p->lastValidAimX = 0.0f;
    p->lastValidAimY = 0.0f;
    p->aimGraceTimer = 0.0f;
    p->jumpPeakY = p->y;

    // Reset arms mode state
    p->armsIsSpinning = false;
    p->armsIsWhipping = false;
    p->armsIsGliding = false;
    p->armsHasDoubleJumped = false;
    p->armsIsWallJumping = false;
    p->armsSpinTime = 0.0f;
    p->armsWhipTime = 0.0f;

    // Reset wall jump state
    p->torsoIsWallJumping = false;

    // Reset squash/stretch
    p->squashScale = 1.0f;
    p->squashVelocity = 0.0f;
    p->landingSquash = 0.0f;
    p->chargeSquash = 0.0f;

    // Reset coyote/buffer timers
    p->coyoteTimer = 0.0f;
    p->jumpBufferTimer = 0.0f;
    p->isBufferedCharge = false;

    // Reset triple jump combo
    p->jumpComboCount = 0;
    p->jumpComboTimer = 0.0f;

    // Reset idle/fidget
    p->idleFrames = 0;
    p->playingFidget = false;
    p->fidgetPlayTime = 0.0f;

    // Reset slide state
    p->wasSliding = false;
    p->isSlidingFront = true;
    p->isSlideRecovering = false;
    p->slideRecoverTime = 0.0f;

    // Reset health
    p->health = p->maxHealth;
    p->isDead = false;
    p->isHurt = false;
    p->hurtAnimTime = 0.0f;
    p->currentPainAnim = NULL;
    p->invincibilityTimer = 0.0f;
    p->invincibilityFlashFrame = 0;

    // Reset death/respawn
    p->deathTimer = 0.0f;
    p->isRespawning = false;
    p->respawnDelayTimer = 0.0f;

    // Reset animation to idle (use mode-appropriate idle)
    T3DAnim* idleAnim = p->isArmsMode ? &p->animArmsIdle : &p->animIdle;
    player_attach_anim(p, idleAnim);
    t3d_anim_set_time(idleAnim, 0.0f);
    t3d_anim_set_playing(idleAnim, true);
}

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

void player_attach_anim(Player* p, T3DAnim* anim) {
    // Safety check: don't attach NULL or invalid animations
    // This can happen when transitioning to a level with a different body part
    if (!anim || !anim->animRef) {
        return;
    }
    if (p->attachedAnim != anim) {
        t3d_anim_attach(anim, &p->skeleton);
        p->attachedAnim = anim;
    }
}

bool player_is_input_blocked(Player* p) {
    return p->isDead || p->isHurt || p->isRespawning;
}

float player_get_speed(Player* p) {
    if (p->isArmsMode) {
        return 100.0f;  // Arms mode is faster, more direct control
    } else {
        return 60.0f;   // Torso mode is slower, charge-jump focused
    }
}

// ============================================================
// COMBAT FUNCTIONS
// ============================================================

bool player_apply_damage(Player* p, int damage) {
    // Can't take damage if dead or invincible
    if (p->isDead || p->invincibilityTimer > 0.0f) {
        return false;
    }

    p->health -= damage;

    if (p->health <= 0) {
        p->health = 0;
        p->isDead = true;
        p->deathTimer = 0.0f;

        // Reset triple jump combo on death
        p->jumpComboCount = 0;
        p->jumpComboTimer = 0.0f;

        // Play death animation
        T3DAnim* deathAnim = p->isArmsMode ? &p->animArmsDeath : &p->animDeath;
        player_attach_anim(p, deathAnim);
        t3d_anim_set_time(deathAnim, 0.0f);
        t3d_anim_set_playing(deathAnim, true);
    } else {
        // Play pain animation
        p->isHurt = true;
        p->hurtAnimTime = HURT_ANIMATION_DURATION;
        p->invincibilityTimer = PLAYER_INVINCIBILITY_DURATION;

        // Randomly pick pain animation
        T3DAnim* painAnim;
        if (p->isArmsMode) {
            painAnim = (rand() & 1) ? &p->animArmsPain1 : &p->animArmsPain2;
        } else {
            painAnim = (rand() & 1) ? &p->animPain1 : &p->animPain2;
        }
        p->currentPainAnim = painAnim;
        player_attach_anim(p, painAnim);
        t3d_anim_set_time(painAnim, 0.0f);
        t3d_anim_set_playing(painAnim, true);
    }

    return true;
}

void player_apply_knockback(Player* p, float fromX, float fromZ, float strength) {
    // Calculate direction away from damage source
    float dx = p->x - fromX;
    float dz = p->z - fromZ;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.1f) {
        // Random direction if overlapping
        dx = (float)(rand() % 100 - 50);
        dz = (float)(rand() % 100 - 50);
        len = sqrtf(dx * dx + dz * dz);
    }

    // Normalize and apply strength
    if (len > 0.1f) {
        p->physics.velX = (dx / len) * strength;
        p->physics.velZ = (dz / len) * strength;
        p->physics.velY = strength * 0.5f;  // Small upward boost
    }
}

void player_respawn(Player* p) {
    p->isRespawning = true;
    p->respawnDelayTimer = 0.0f;
}

// ============================================================
// CAMERA UPDATE
// ============================================================

void player_update_camera(Player* p, float dt) {
    (void)dt;

    // Simple camera follow (can be enhanced later)
    float targetX = p->x;
    float targetY = p->y + PLAYER_CAMERA_OFFSET_Y;
    float targetZ = p->z;

    // Smooth follow
    float smoothSpeed = 0.15f;
    p->smoothCamX += (targetX - p->smoothCamX) * smoothSpeed;
    p->smoothCamY += (targetY - p->smoothCamY) * smoothSpeed;

    // If charging, shift camera toward arc endpoint
    if (p->isCharging) {
        p->smoothCamTargetX += (p->jumpArcEndX - p->smoothCamTargetX) * 0.05f;
    } else {
        p->smoothCamTargetX += (targetX - p->smoothCamTargetX) * 0.1f;
    }

    // Update camera position
    p->camTarget.v[0] = p->smoothCamX;
    p->camTarget.v[1] = p->smoothCamY;
    p->camTarget.v[2] = targetZ;

    // Camera stays behind and above player
    p->camPos.v[0] = (p->smoothCamX + p->smoothCamTargetX) * 0.5f;
    p->camPos.v[1] = p->smoothCamY + 50.0f;
    p->camPos.v[2] = targetZ + 150.0f;
}

// ============================================================
// PLAYER UPDATE - Full game.c-compatible physics
// This function implements the exact same physics as game.c
// for torso mode charge jumping, coyote time, jump buffering, etc.
// ============================================================

void player_update(Player* p, MapLoader* map, struct MapRuntime* runtime,
                   float dt, bool inputEnabled) {
    // runtime used for decoration wall collision

    // Skip update if dead
    if (p->isDead) {
        p->deathTimer += dt;
        return;
    }

    // Handle hurt state
    if (p->isHurt) {
        p->hurtAnimTime -= dt;
        if (p->hurtAnimTime <= 0.0f) {
            p->isHurt = false;
            player_attach_anim(p, p->isArmsMode ? &p->animArmsIdle : &p->animIdle);
        }
        // During hurt, skip input processing but continue physics
        goto physics;
    }

    // Update invincibility
    if (p->invincibilityTimer > 0.0f) {
        p->invincibilityTimer -= dt;
        p->invincibilityFlashFrame++;
    }

    // Skip input if blocked
    if (!inputEnabled || player_is_input_blocked(p)) {
        goto physics;
    }

    // Get input for this player's port
    joypad_inputs_t inputs = joypad_get_inputs(p->port);
    joypad_buttons_t held = joypad_get_buttons_held(p->port);
    joypad_buttons_t pressed = joypad_get_buttons_pressed(p->port);

    // Calculate released buttons
    joypad_buttons_t released;
    released.raw = p->prevHeld.raw & ~held.raw;

    // Store current held for next frame's released detection (do this early)
    p->prevHeld = held;

    // Apply deadzone (match game.c: inverted X, Y forward)
    float rawStickX = player_apply_deadzone((float)inputs.stick_x / 128.0f);
    float rawStickY = player_apply_deadzone((float)inputs.stick_y / 128.0f);

    // Check if stick is moved
    bool isMoving = (fabsf(rawStickX) > 0.01f || fabsf(rawStickY) > 0.01f);

    // ============================================================
    // COYOTE TIME - track time since last grounded
    // ============================================================
    if (p->physics.isGrounded) {
        p->coyoteTimer = PLAYER_COYOTE_TIME;
    } else if (p->coyoteTimer > 0.0f) {
        p->coyoteTimer -= dt;
    }

    // ============================================================
    // JUMP BUFFER - track A held in air (for chain jumps)
    // ============================================================
    if (held.a && !p->physics.isGrounded && !p->isCharging) {
        p->jumpBufferTimer = PLAYER_JUMP_BUFFER_TIME;
    }
    if (p->jumpBufferTimer > 0.0f) {
        p->jumpBufferTimer -= dt;
    }

    // ============================================================
    // SQUASH/STRETCH SPRING PHYSICS
    // ============================================================
    if (!p->isCharging) {
        float targetSquash = 1.0f;
        float springForce = (targetSquash - p->squashScale) * PLAYER_SQUASH_SPRING_K;
        p->squashVelocity += springForce * dt;
        p->squashVelocity *= (1.0f - PLAYER_SQUASH_DAMPING * dt);
        p->squashScale += p->squashVelocity * dt;

        // Clamp squash scale
        if (p->squashScale < 0.6f) p->squashScale = 0.6f;
        if (p->squashScale > 1.4f) p->squashScale = 1.4f;
    }

    // ============================================================
    // TORSO/HEAD MODE - CHARGE JUMP PHYSICS
    // ============================================================
    if (p->currentPart == PLAYER_PART_TORSO || p->currentPart == PLAYER_PART_HEAD) {
        bool canStartJump = p->physics.isGrounded || p->coyoteTimer > 0.0f;
        bool wantsJump = pressed.a || p->jumpBufferTimer > 0.0f;

        // === START CHARGING ===
        if (wantsJump && canStartJump && !p->isCharging &&
            (p->physics.canJump || p->isLanding) && !p->physics.isSliding) {
            p->isCharging = true;
            p->isBufferedCharge = (p->jumpBufferTimer > 0.0f);
            p->coyoteTimer = 0.0f;
            p->jumpBufferTimer = 0.0f;

            // Triple jump combo: buffered jump (held A before landing) continues combo,
            // or pressing A within the combo window after releasing also continues it
            if (p->jumpComboCount < 2 && (p->isBufferedCharge || p->jumpComboTimer > 0.0f)) {
                p->jumpComboCount++;
            } else if (!p->isBufferedCharge && p->jumpComboTimer <= 0.0f) {
                p->jumpComboCount = 0;
            }
            p->jumpComboTimer = 0.0f;  // Consume timer when starting charge
            p->isJumping = false;
            p->isLanding = false;
            p->jumpChargeTime = 0.0f;
            p->lastValidAimX = 0.0f;
            p->lastValidAimY = 0.0f;
            p->aimGraceTimer = 0.0f;

            // Stop movement immediately
            p->physics.velX = 0.0f;
            p->physics.velZ = 0.0f;
            p->physics.canMove = false;
            p->physics.canRotate = true;
            p->physics.canJump = true;

            // Reset idle state
            p->idleFrames = 0;
            p->playingFidget = false;
            p->fidgetPlayTime = 0.0f;

            // Start with idle animation (switches to charge after hop threshold)
            if (p->animIdle.animRef) {
                player_attach_anim(p, &p->animIdle);
                t3d_anim_set_time(&p->animIdle, 0.0f);
                t3d_anim_set_playing(&p->animIdle, true);
            }
        }

        // === WHILE CHARGING ===
        if (p->isCharging) {
            // Reset idle state
            p->idleFrames = 0;
            p->playingFidget = false;
            p->fidgetPlayTime = 0.0f;

            // Read aim input (X inverted like game.c)
            float rawAimX = -player_apply_deadzone(rawStickX);
            float rawAimY = player_apply_deadzone(rawStickY);
            float rawMag = sqrtf(rawAimX * rawAimX + rawAimY * rawAimY);

            // Aim grace period tracking
            if (rawMag > PLAYER_AIM_GRACE_THRESHOLD) {
                p->lastValidAimX = rawAimX;
                p->lastValidAimY = rawAimY;
                p->aimGraceTimer = PLAYER_AIM_GRACE_DURATION;
            } else {
                p->aimGraceTimer -= dt;
                if (p->aimGraceTimer <= 0.0f) {
                    p->lastValidAimX = 0.0f;
                    p->lastValidAimY = 0.0f;
                    p->aimGraceTimer = 0.0f;
                }
            }

            // Current aim (for arc display)
            p->jumpAimX = rawAimX;
            p->jumpAimY = rawAimY;
            float aimMag = sqrtf(p->jumpAimX * p->jumpAimX + p->jumpAimY * p->jumpAimY);
            if (aimMag > 1.0f) aimMag = 1.0f;

            // Face aim direction if stick pushed
            if (aimMag > 0.3f) {
                p->angle = atan2f(-p->jumpAimX, p->jumpAimY);
                p->physics.playerAngle = p->angle;
            }

            // Update charge time with triple jump combo multiplier
            // Faster charge rate on subsequent jumps means reaching max power quicker
            float prevChargeTime = p->jumpChargeTime;
            float chargeRateMult = (p->jumpComboCount == 0) ? PLAYER_JUMP_COMBO_CHARGE_MULT_1 :
                                   (p->jumpComboCount == 1) ? PLAYER_JUMP_COMBO_CHARGE_MULT_2 :
                                                              PLAYER_JUMP_COMBO_CHARGE_MULT_3;
            float chargeRate = dt * chargeRateMult;
            p->jumpChargeTime += chargeRate;

            // Squash down while charging (same max charge time for all jumps)
            float chargeRatio = p->jumpChargeTime / PLAYER_MAX_CHARGE_TIME;
            if (chargeRatio > 1.0f) chargeRatio = 1.0f;
            p->chargeSquash = chargeRatio * 0.25f;
            p->squashScale = 1.0f - p->landingSquash - p->chargeSquash;
            p->squashVelocity = 0.0f;

            // Transition to charge animation after hop threshold
            if (prevChargeTime < PLAYER_HOP_THRESHOLD && p->jumpChargeTime >= PLAYER_HOP_THRESHOLD) {
                if (p->animJumpCharge.animRef) {
                    player_attach_anim(p, &p->animJumpCharge);
                    t3d_anim_set_time(&p->animJumpCharge, 0.0f);
                    t3d_anim_set_playing(&p->animJumpCharge, true);
                }
            }

            // Update animation
            if (p->jumpChargeTime >= PLAYER_HOP_THRESHOLD) {
                if (p->animJumpCharge.animRef && p->animJumpCharge.isPlaying) {
                    t3d_anim_update(&p->animJumpCharge, dt);
                }
            } else {
                if (p->animIdle.animRef && p->animIdle.isPlaying) {
                    t3d_anim_update(&p->animIdle, dt);
                }
            }

            // Clamp charge time to max
            if (p->jumpChargeTime > PLAYER_MAX_CHARGE_TIME) {
                p->jumpChargeTime = PLAYER_MAX_CHARGE_TIME;
                p->chargeSquash = 0.25f;
                p->squashScale = 1.0f - p->landingSquash - p->chargeSquash;
            }

            // === RELEASE CHARGE - JUMP! ===
            if (released.a) {
                p->isCharging = false;
                p->isJumping = true;
                p->isLanding = false;
                p->physics.canJump = false;
                p->physics.canMove = false;
                p->physics.canRotate = false;
                p->jumpAnimPaused = false;

                // Use grace period aim if stick is neutral
                float execAimX = p->jumpAimX;
                float execAimY = p->jumpAimY;
                float execMag = aimMag;
                if (execMag < PLAYER_AIM_GRACE_THRESHOLD) {
                    execAimX = p->lastValidAimX;
                    execAimY = p->lastValidAimY;
                    execMag = sqrtf(execAimX * execAimX + execAimY * execAimY);
                    if (execMag > 1.0f) execMag = 1.0f;
                }

                if (p->jumpChargeTime < PLAYER_HOP_THRESHOLD) {
                    // Quick tap = small hop
                    p->physics.velY = PLAYER_HOP_VELOCITY_Y;
                    p->landingSquash = 0.0f;
                    p->chargeSquash = 0.0f;
                    p->squashScale = 1.1f;
                    p->squashVelocity = 1.0f;

                    if (execMag > 0.1f) {
                        p->physics.velX = (execAimX / execMag) * PLAYER_HOP_FORWARD_SPEED * execMag * PLAYER_HORIZONTAL_SCALE;
                        p->physics.velZ = (execAimY / execMag) * PLAYER_HOP_FORWARD_SPEED * execMag * PLAYER_HORIZONTAL_SCALE;
                    } else {
                        p->physics.velX = -sinf(p->angle) * PLAYER_HOP_FORWARD_SPEED * PLAYER_HORIZONTAL_SCALE;
                        p->physics.velZ = cosf(p->angle) * PLAYER_HOP_FORWARD_SPEED * PLAYER_HORIZONTAL_SCALE;
                    }
                } else {
                    // Full charge jump with triple jump power multiplier
                    float powerMult = (p->jumpComboCount == 0) ? PLAYER_JUMP_COMBO_POWER_MULT_1 :
                                      (p->jumpComboCount == 1) ? PLAYER_JUMP_COMBO_POWER_MULT_2 :
                                                                 PLAYER_JUMP_COMBO_POWER_MULT_3;
                    p->physics.velY = (PLAYER_CHARGE_JUMP_EARLY_BASE + p->jumpChargeTime * PLAYER_CHARGE_JUMP_EARLY_MULT) * powerMult;
                    float forward = (3.0f + 2.0f * p->jumpChargeTime) * FPS_SCALE * execMag * powerMult;

                    if (execMag > 0.1f) {
                        p->physics.velX = (execAimX / execMag) * forward * PLAYER_HORIZONTAL_SCALE;
                        p->physics.velZ = (execAimY / execMag) * forward * PLAYER_HORIZONTAL_SCALE;
                    } else {
                        p->physics.velX = 0.0f;
                        p->physics.velZ = 0.0f;
                    }

                    // Stretch based on charge
                    p->landingSquash = 0.0f;
                    p->chargeSquash = 0.0f;
                    float stretchRatio = p->jumpChargeTime / PLAYER_MAX_CHARGE_TIME;
                    p->squashScale = 1.1f + stretchRatio * 0.15f;
                    p->squashVelocity = 1.0f + stretchRatio * 1.0f;
                }

                p->physics.isGrounded = false;
                p->jumpPeakY = p->y;

                // Start launch animation
                if (p->animJumpLaunch.animRef) {
                    player_attach_anim(p, &p->animJumpLaunch);
                    t3d_anim_set_time(&p->animJumpLaunch, 0.0f);
                    t3d_anim_set_playing(&p->animJumpLaunch, true);
                }
            }

            // Cancel charge with B
            if (p->isCharging && pressed.b) {
                p->isCharging = false;
                p->jumpChargeTime = 0.0f;
                p->landingSquash = 0.0f;
                p->chargeSquash = 0.0f;
                p->squashScale = 1.0f;
                p->squashVelocity = 0.0f;
                p->jumpAimX = 0.0f;
                p->jumpAimY = 0.0f;
                if (p->animIdle.animRef) {
                    player_attach_anim(p, &p->animIdle);
                    t3d_anim_set_time(&p->animIdle, 0.0f);
                    t3d_anim_set_playing(&p->animIdle, true);
                }
                p->physics.canMove = true;
                p->physics.canRotate = true;
                p->physics.canJump = true;
            }
        }
        // === IN AIR / LANDING ===
        else if (p->isJumping) {
            p->idleFrames = 0;
            p->playingFidget = false;
            p->fidgetPlayTime = 0.0f;

            if (p->isLanding) {
                // Update landing animation
                if (p->animJumpLand.animRef) {
                    if (p->animJumpLand.isPlaying) {
                        p->physics.canJump = true;
                        t3d_anim_update(&p->animJumpLand, dt);
                        p->physics.velX *= 0.8f;
                        p->physics.velZ *= 0.8f;
                    }
                    if (!p->animJumpLand.isPlaying) {
                        t3d_anim_set_time(&p->animJumpLand, p->animJumpLand.animRef->duration);
                        p->isJumping = false;
                        p->isLanding = false;
                        p->physics.canMove = true;
                        p->physics.canRotate = true;
                    }
                } else {
                    p->isJumping = false;
                    p->isLanding = false;
                    p->physics.canMove = true;
                    p->physics.canRotate = true;
                }
            } else if (!p->jumpAnimPaused) {
                // Launch phase
                if (p->animJumpLaunch.animRef) {
                    t3d_anim_update(&p->animJumpLaunch, dt);
                    if (p->animJumpLaunch.animRef->duration > 0.0f) {
                        float animProgress = p->animJumpLaunch.time / p->animJumpLaunch.animRef->duration;
                        if (animProgress > 0.5f && !p->physics.isGrounded) {
                            p->jumpAnimPaused = true;
                            t3d_anim_set_playing(&p->animJumpLaunch, false);
                        }
                    }
                } else {
                    if (p->physics.velY < 0.0f && !p->physics.isGrounded) {
                        p->jumpAnimPaused = true;
                    }
                }

                // Land while in launch animation
                if (p->physics.isGrounded && !p->physics.isSliding) {
                    p->physics.velX *= 0.8f;
                    p->physics.velZ *= 0.8f;
                    p->jumpAnimPaused = true;
                }
            }
        }
        // === GROUNDED MOVEMENT ===
        else if (!p->isCharging && !p->isJumping) {
            p->physics.canMove = true;
            p->physics.canRotate = true;
            p->physics.canJump = true;

            // Triple jump combo timer countdown - reset combo when expired
            if (p->jumpComboTimer > 0.0f) {
                p->jumpComboTimer -= dt;
                if (p->jumpComboTimer <= 0.0f) {
                    p->jumpComboCount = 0;
                    p->jumpComboTimer = 0.0f;
                }
            }

            // Ground movement physics (matches controls_update() in controls.c)
            #define GROUND_WALK_ACCEL 0.8f
            #define GROUND_WALK_SPEED_CAP 6.0f
            #define GROUND_FRICTION_NORMAL 0.50f

            // Calculate input magnitude
            float inputMag = sqrtf(rawStickX * rawStickX + rawStickY * rawStickY);
            if (inputMag > 1.0f) inputMag = 1.0f;

            // Target velocity (inverted X for camera facing, Z scaled for isometric view)
            float targetVelX = -rawStickX * GROUND_WALK_SPEED_CAP;
            float targetVelZ = rawStickY * GROUND_WALK_SPEED_CAP * 0.7f;

            if (inputMag > 0.1f && p->physics.canMove) {
                // Accelerate toward target velocity
                float currentSpeed = sqrtf(p->physics.velX * p->physics.velX + p->physics.velZ * p->physics.velZ);
                float accel = GROUND_WALK_ACCEL - currentSpeed / 43.0f;
                if (accel < 0.1f) accel = 0.1f;

                // Lerp toward target (steer rate 0.5 for snappy response)
                float steerRate = 0.5f;
                p->physics.velX += (targetVelX - p->physics.velX) * steerRate;
                p->physics.velZ += (targetVelZ - p->physics.velZ) * steerRate;

                // Also accelerate magnitude toward target
                float targetSpeed = sqrtf(targetVelX * targetVelX + targetVelZ * targetVelZ);
                if (currentSpeed < targetSpeed) {
                    currentSpeed += accel;
                    if (currentSpeed > targetSpeed) currentSpeed = targetSpeed;
                }

                // Apply new speed in current direction
                if (currentSpeed > 0.1f) {
                    float velLen = sqrtf(p->physics.velX * p->physics.velX + p->physics.velZ * p->physics.velZ);
                    if (velLen > 0.1f) {
                        p->physics.velX = (p->physics.velX / velLen) * currentSpeed;
                        p->physics.velZ = (p->physics.velZ / velLen) * currentSpeed;
                    }
                }

                // Face movement direction
                if (fabsf(rawStickX) > 0.1f || fabsf(rawStickY) > 0.1f) {
                    p->angle = atan2f(-targetVelX, targetVelZ);
                    p->physics.playerAngle = p->angle;
                }

                // Walk animation
                player_attach_anim(p, &p->animWalk);
                t3d_anim_update(&p->animWalk, dt);
                p->idleFrames = 0;
            } else {
                // No input - apply friction
                p->physics.velX *= GROUND_FRICTION_NORMAL;
                p->physics.velZ *= GROUND_FRICTION_NORMAL;

                // Zero out tiny velocities
                if (p->physics.velX * p->physics.velX + p->physics.velZ * p->physics.velZ < 0.5f) {
                    p->physics.velX = 0.0f;
                    p->physics.velZ = 0.0f;
                }

                // Idle animation
                player_attach_anim(p, &p->animIdle);
                t3d_anim_update(&p->animIdle, dt);
                p->idleFrames++;
            }
        }
    }
    // ============================================================
    // FULLBODY/LEGS MODE - Classic 3D platformer crouch mechanics (matches game.c)
    // ============================================================
    else if (p->currentPart == PLAYER_PART_LEGS && p->fbHasAnims) {
        // Crouch movement constants
        const float LONG_JUMP_SPEED = 8.0f;
        const float LONG_JUMP_HEIGHT = 9.0f;
        const float BACKFLIP_HEIGHT = 14.0f;
        const float BACKFLIP_BACK_SPEED = 2.0f;
        const float CROUCH_MOVE_MULT = 0.4f;

        // Hover constants
        const float HOVER_LAUNCH_VELOCITY = 8.0f;
        const float HOVER_RISE_GRAVITY = 0.15f;
        const float HOVER_FALL_SPEED = -1.5f;
        const float HOVER_AIR_CONTROL = 0.15f;

        // Jump buffer - don't trigger during special jump states (long jump/hover already use the jump)
        if (pressed.a && !p->physics.isGrounded && !p->isJumping && !p->fbIsLongJumping && !p->fbIsBackflipping && !p->fbIsHovering) {
            p->jumpBufferTimer = PLAYER_JUMP_BUFFER_TIME;
        }
        if (p->jumpBufferTimer > 0.0f) {
            p->jumpBufferTimer -= dt;
        }

        // Wall jump grace period
        #define FB_WALL_JUMP_GRACE_FRAMES 5
        if (p->physics.isGrounded) {
            p->fbFramesSinceGrounded = 0;
        } else if (p->fbFramesSinceGrounded < 100) {
            p->fbFramesSinceGrounded++;
        }

        bool wantsCrouch = held.z;
        float currentSpeed = sqrtf(p->physics.velX * p->physics.velX + p->physics.velZ * p->physics.velZ);

        // === DEATH STATE ===
        if (p->isDead) {
            p->fbIsCrouching = false;
            p->fbIsLongJumping = false;
            p->fbIsBackflipping = false;
            p->fbIsHovering = false;
            p->fbHoverTime = 0.0f;
            p->fbIsSpinning = false;
            p->fbIsSpinningAir = false;
            p->fbSpinTime = 0.0f;
            p->fbIsCharging = false;
            p->fbChargeTime = 0.0f;
            p->fbIsCrouchAttacking = false;
            p->fbCrouchAttackTime = 0.0f;
            p->fbIsWallJumping = false;
            if (p->animDeath.animRef != NULL) {
                player_attach_anim(p, &p->animDeath);
                t3d_anim_update(&p->animDeath, dt);
            }
        }
        // === HURT STATE ===
        else if (p->isHurt) {
            p->fbIsCrouching = false;
            // Pain anim handled by player_apply_damage
        }
        // === GROUND SPIN ATTACK ===
        else if (p->fbIsSpinning && p->physics.isGrounded) {
            p->fbSpinTime += dt;
            float spinDuration = p->fbAnimSpinAtk.animRef ? p->fbAnimSpinAtk.animRef->duration : 0.5f;

            if (p->fbAnimSpinAtk.animRef != NULL) {
                player_attach_anim(p, &p->fbAnimSpinAtk);
                t3d_anim_update(&p->fbAnimSpinAtk, dt);
            }

            // No movement during spin attack
            p->physics.canMove = false;
            p->physics.canRotate = false;
            p->physics.velX *= 0.8f;
            p->physics.velZ *= 0.8f;

            if (p->fbSpinTime >= spinDuration) {
                p->fbIsSpinning = false;
                p->fbSpinTime = 0.0f;
                p->physics.canMove = true;
                p->physics.canRotate = true;
                p->physics.velX = 0.0f;
                p->physics.velZ = 0.0f;
            }
        }
        // === AIR SPIN ATTACK ===
        else if (p->fbIsSpinningAir && !p->physics.isGrounded) {
            p->fbSpinTime += dt;
            float airSpinDuration = p->fbAnimSpinAir.animRef ? p->fbAnimSpinAir.animRef->duration : 0.6f;

            const float FB_AIR_SPIN_MAX_FALL = -3.0f;
            if (p->physics.velY < FB_AIR_SPIN_MAX_FALL) {
                p->physics.velY = FB_AIR_SPIN_MAX_FALL;
            }

            if (p->fbAnimSpinAir.animRef != NULL) {
                player_attach_anim(p, &p->fbAnimSpinAir);
                t3d_anim_update(&p->fbAnimSpinAir, dt);
            }

            if (p->physics.isGrounded) {
                p->fbIsSpinningAir = false;
                p->fbSpinTime = 0.0f;
            } else if (p->fbSpinTime >= airSpinDuration) {
                p->fbIsSpinningAir = false;
                p->fbSpinTime = 0.0f;
            }
        }
        // === SPIN CHARGE (Z + B) - cancel if left ground ===
        else if (p->fbIsCharging && !p->physics.isGrounded) {
            p->fbIsCharging = false;
            p->fbChargeTime = 0.0f;
            p->physics.canMove = true;
            p->physics.canRotate = true;
        }
        // === SPIN CHARGE (Z + B) - grounded ===
        else if (p->fbIsCharging && p->physics.isGrounded) {
            p->fbChargeTime += dt;
            if (p->fbAnimSpinCharge.animRef != NULL) {
                player_attach_anim(p, &p->fbAnimSpinCharge);
                float animTime = t3d_anim_get_time(&p->fbAnimSpinCharge);
                if (animTime < p->fbAnimSpinCharge.animRef->duration) {
                    t3d_anim_update(&p->fbAnimSpinCharge, dt);
                }
            }
            p->physics.velX = 0.0f;
            p->physics.velZ = 0.0f;

            // Release B = spin attack
            if (!held.b) {
                p->fbIsCharging = false;
                p->fbChargeTime = 0.0f;
                p->fbIsSpinning = true;
                p->fbSpinTime = 0.0f;
                p->fbIsCrouching = false;
                p->physics.canMove = true;
                p->physics.canRotate = true;
                if (p->fbAnimSpinAtk.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimSpinAtk);
                    t3d_anim_set_time(&p->fbAnimSpinAtk, 0.0f);
                    t3d_anim_set_playing(&p->fbAnimSpinAtk, true);
                }
            }
        }
        // === CROUCH ATTACK (crouch + C-down) ===
        else if (p->fbIsCrouchAttacking) {
            p->fbCrouchAttackTime += dt;
            if (p->fbAnimCrouchAttack.animRef != NULL) {
                player_attach_anim(p, &p->fbAnimCrouchAttack);
                t3d_anim_update(&p->fbAnimCrouchAttack, dt);
                if (p->fbCrouchAttackTime >= p->fbAnimCrouchAttack.animRef->duration) {
                    p->fbIsCrouchAttacking = false;
                    p->fbCrouchAttackTime = 0.0f;
                    p->fbIsCrouching = false;
                    p->physics.canMove = true;
                    p->physics.canRotate = true;
                }
            } else {
                p->fbIsCrouchAttacking = false;
                p->fbCrouchAttackTime = 0.0f;
                p->fbIsCrouching = false;
                p->physics.canMove = true;
                p->physics.canRotate = true;
            }
            p->physics.velX *= 0.5f;
            p->physics.velZ *= 0.5f;
        }
        // === SLIDING ON SLOPE ===
        else if (p->physics.isSliding && p->physics.isGrounded) {
            p->fbIsCrouching = false;
            p->physics.canRotate = false;

            float slopeDirX = p->physics.slopeNormalX;
            float slopeDirZ = p->physics.slopeNormalZ;
            float targetAngle = atan2f(-slopeDirX, slopeDirZ);

            float angleDiff = targetAngle - p->physics.playerAngle;
            while (angleDiff > 3.14159265f) angleDiff -= 6.28318530f;
            while (angleDiff < -3.14159265f) angleDiff += 6.28318530f;

            const float SLIDE_TURN_SPEED = 8.0f;
            float maxTurn = SLIDE_TURN_SPEED * dt;
            if (angleDiff > maxTurn) angleDiff = maxTurn;
            else if (angleDiff < -maxTurn) angleDiff = -maxTurn;

            p->physics.playerAngle += angleDiff;
            p->angle = p->physics.playerAngle;

            if (p->animSlideFront.animRef != NULL) {
                player_attach_anim(p, &p->animSlideFront);
                t3d_anim_update(&p->animSlideFront, dt);
            } else if (p->fbAnimCrouch.animRef != NULL) {
                player_attach_anim(p, &p->fbAnimCrouch);
                t3d_anim_update(&p->fbAnimCrouch, dt);
            }
        }
        // === LONG JUMP / BACKFLIP IN AIR ===
        else if ((p->fbIsLongJumping || p->fbIsBackflipping) && !p->physics.isGrounded) {
            // B button during long jump = cancel into spin/glide!
            if (pressed.b && p->fbIsLongJumping && !p->fbIsSpinningAir) {
                p->fbIsLongJumping = false;
                p->fbLongJumpSpeed = 0.0f;
                p->fbIsSpinningAir = true;
                p->fbSpinTime = 0.0f;
                p->physics.canMove = true;
                p->physics.canRotate = true;
                if (p->fbAnimSpinAir.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimSpinAir);
                    t3d_anim_set_time(&p->fbAnimSpinAir, 0.0f);
                    t3d_anim_set_playing(&p->fbAnimSpinAir, true);
                }
            }
            else if (p->fbAnimCrouchJumpHover.animRef != NULL) {
                player_attach_anim(p, &p->fbAnimCrouchJumpHover);
                t3d_anim_update(&p->fbAnimCrouchJumpHover, dt);
            } else if (p->animJumpLaunch.animRef != NULL) {
                player_attach_anim(p, &p->animJumpLaunch);
                t3d_anim_update(&p->animJumpLaunch, dt);
            }

            // No wall jump during long jump - must commit or cancel with B
            if (p->fbIsLongJumping) {
                float angle = p->physics.playerAngle;
                p->physics.velX = -sinf(angle) * p->fbLongJumpSpeed;
                p->physics.velZ = cosf(angle) * p->fbLongJumpSpeed;
            }
        }
        // === CROUCH JUMP WIND-UP ===
        else if (p->fbCrouchJumpWindup) {
            #define CROUCH_JUMP_WINDUP_FRAMES 6
            p->fbCrouchJumpWindupTime += dt;

            if (p->fbAnimCrouchJump.animRef != NULL) {
                player_attach_anim(p, &p->fbAnimCrouchJump);
                t3d_anim_update(&p->fbAnimCrouchJump, dt);
            }

            if (p->fbCrouchJumpWindupTime >= (CROUCH_JUMP_WINDUP_FRAMES / 30.0f)) {
                p->fbCrouchJumpWindup = false;
                p->fbCrouchJumpWindupTime = 0.0f;
                p->fbCrouchJumpRising = true;
                p->fbIsHovering = true;
                p->fbHoverTime = 0.01f;
                p->fbIsCrouching = false;
                p->isJumping = true;
                p->physics.canMove = true;
                p->physics.canRotate = true;
                p->physics.velX = 0.0f;
                p->physics.velZ = 0.0f;
                p->physics.velY = HOVER_LAUNCH_VELOCITY;
                p->physics.isGrounded = false;
            }
        }
        // === HOVER STATE ===
        else if (p->fbIsHovering && !p->physics.isGrounded) {
            if (p->physics.velY <= 0.0f) {
                // Falling - air control and slow descent
                float stickX = rawStickX;
                float stickY = rawStickY;
                p->physics.velX += stickX * HOVER_AIR_CONTROL;
                p->physics.velZ -= stickY * HOVER_AIR_CONTROL;

                // Apply gravity during hover fall, then clamp to slow fall speed
                p->physics.velY -= GRAVITY;
                if (p->physics.velY < HOVER_FALL_SPEED) {
                    p->physics.velY = HOVER_FALL_SPEED;
                }
            } else {
                // Rising - no horizontal control, reduced gravity
                p->physics.velX = 0.0f;
                p->physics.velZ = 0.0f;
                // Apply reduced gravity during rise (normal gravity minus rise reduction)
                p->physics.velY -= HOVER_RISE_GRAVITY;
            }

            if (p->fbCrouchJumpRising && p->physics.velY > 0.0f) {
                if (p->fbAnimCrouchJump.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimCrouchJump);
                    float animTime = t3d_anim_get_time(&p->fbAnimCrouchJump);
                    if (animTime < p->fbAnimCrouchJump.animRef->duration) {
                        t3d_anim_update(&p->fbAnimCrouchJump, dt);
                    } else {
                        p->fbCrouchJumpRising = false;
                    }
                } else {
                    p->fbCrouchJumpRising = false;
                }
            } else {
                p->fbCrouchJumpRising = false;
                if (p->fbAnimCrouchJumpHover.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimCrouchJumpHover);
                    t3d_anim_update(&p->fbAnimCrouchJumpHover, dt);
                } else if (p->animJumpLaunch.animRef != NULL) {
                    player_attach_anim(p, &p->animJumpLaunch);
                    t3d_anim_update(&p->animJumpLaunch, dt);
                }
            }
        }
        // === NORMAL JUMPING ===
        else if (!p->physics.isGrounded && !wantsCrouch) {
            if (!p->fbIsLongJumping && !p->fbIsBackflipping) {
                p->fbIsCrouching = false;
            }

            // Wall kick check
            bool fbPastGracePeriod = p->fbFramesSinceGrounded >= FB_WALL_JUMP_GRACE_FRAMES;
            if (p->physics.wallHitTimer > 0 && pressed.a && fbPastGracePeriod) {
                wav64_play(&sfxJumpSound, 2);  // Play jump sound on channel 2
                #define FB_WALL_KICK_VEL_Y 8.0f
                #define FB_WALL_KICK_VEL_XZ 4.0f

                float awayX = p->physics.wallNormalX;
                float awayZ = p->physics.wallNormalZ;

                float facingX = -sinf(p->physics.playerAngle);
                float facingZ = cosf(p->physics.playerAngle);
                float dot = facingX * awayX + facingZ * awayZ;
                float reflectedX = facingX - 2.0f * dot * awayX;
                float reflectedZ = facingZ - 2.0f * dot * awayZ;

                float len = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
                if (len > 0.01f) {
                    reflectedX /= len;
                    reflectedZ /= len;
                    reflectedX = reflectedX * 0.5f + awayX * 0.5f;
                    reflectedZ = reflectedZ * 0.5f + awayZ * 0.5f;
                    len = sqrtf(reflectedX * reflectedX + reflectedZ * reflectedZ);
                    if (len > 0.01f) {
                        reflectedX /= len;
                        reflectedZ /= len;
                    }
                    p->fbWallJumpAngle = atan2f(-reflectedX, reflectedZ);
                } else {
                    p->fbWallJumpAngle = atan2f(-awayX, awayZ);
                }

                p->physics.velY = FB_WALL_KICK_VEL_Y;
                p->physics.velX = reflectedX * FB_WALL_KICK_VEL_XZ;
                p->physics.velZ = reflectedZ * FB_WALL_KICK_VEL_XZ;
                p->physics.playerAngle = p->fbWallJumpAngle;
                p->angle = p->fbWallJumpAngle;
                p->fbIsWallJumping = true;
                p->physics.canMove = false;
                p->physics.canRotate = false;
                p->physics.wallHitTimer = 0;
            }

            // Air control (fullbody has air steering like game.c)
            if (isMoving && p->physics.canMove) {
                p->physics.velX += -rawStickX * 0.25f;
                p->physics.velZ += rawStickY * 0.25f;
            }

            // B while airborne = air spin
            if (pressed.b && !p->fbIsSpinningAir && !p->fbIsHovering) {
                p->fbIsSpinningAir = true;
                p->fbSpinTime = 0.0f;
                if (p->fbAnimSpinAir.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimSpinAir);
                    t3d_anim_set_time(&p->fbAnimSpinAir, 0.0f);
                    t3d_anim_set_playing(&p->fbAnimSpinAir, true);
                }
            }
            // Normal jump animation
            else if (!p->fbIsSpinningAir && p->animJumpLaunch.animRef != NULL) {
                player_attach_anim(p, &p->animJumpLaunch);
                if (!p->animJumpLaunch.isPlaying) {
                    t3d_anim_set_time(&p->animJumpLaunch, 0.0f);
                    t3d_anim_set_playing(&p->animJumpLaunch, true);
                }
                float animTime = t3d_anim_get_time(&p->animJumpLaunch);
                float animDuration = p->animJumpLaunch.animRef->duration;
                if (animTime < animDuration) {
                    t3d_anim_update(&p->animJumpLaunch, dt);
                }
            }
        }
        // === CROUCHING (grounded, Z held) ===
        else if (wantsCrouch && (p->physics.isGrounded || p->fbIsCrouching)) {
            if (!p->fbWasCrouchingPrev && p->fbAnimCrouch.animRef != NULL) {
                t3d_anim_set_time(&p->fbAnimCrouch, 0.0f);
                t3d_anim_set_playing(&p->fbAnimCrouch, true);
            }
            p->fbIsCrouching = true;
            p->idleFrames = 0;

            // A while crouching
            if (pressed.a && !p->fbCrouchJumpWindup && !p->fbIsLongJumping) {
                if (currentSpeed > 0.1f) {
                    // Long jump
                    p->fbIsLongJumping = true;
                    p->fbIsCrouching = false;
                    p->isJumping = true;
                    p->physics.isGrounded = false;
                    p->physics.velY = LONG_JUMP_HEIGHT;

                    const float LONG_JUMP_LAUNCH_SPEED = 6.0f;
                    p->fbLongJumpSpeed = LONG_JUMP_LAUNCH_SPEED;
                    float angle = p->physics.playerAngle;
                    p->physics.velX = -sinf(angle) * LONG_JUMP_LAUNCH_SPEED;
                    p->physics.velZ = cosf(angle) * LONG_JUMP_LAUNCH_SPEED;

                    if (p->fbAnimCrouchJump.animRef != NULL) {
                        player_attach_anim(p, &p->fbAnimCrouchJump);
                        t3d_anim_set_time(&p->fbAnimCrouchJump, 0.0f);
                        t3d_anim_set_playing(&p->fbAnimCrouchJump, true);
                    }
                } else {
                    // Hover wind-up
                    p->fbCrouchJumpWindup = true;
                    p->fbCrouchJumpWindupTime = 0.0f;
                    p->physics.canMove = false;
                    p->physics.canRotate = false;
                    if (p->fbAnimCrouchJump.animRef != NULL) {
                        player_attach_anim(p, &p->fbAnimCrouchJump);
                        t3d_anim_set_time(&p->fbAnimCrouchJump, 0.0f);
                        t3d_anim_set_playing(&p->fbAnimCrouchJump, true);
                    }
                }
            }
            // C-down while crouching = crouch attack
            else if (pressed.c_down && !p->fbIsCrouchAttacking) {
                p->fbIsCrouchAttacking = true;
                p->fbCrouchAttackTime = 0.0f;
                p->physics.canMove = false;
                p->physics.canRotate = false;
                if (p->fbAnimCrouchAttack.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimCrouchAttack);
                    t3d_anim_set_time(&p->fbAnimCrouchAttack, 0.0f);
                    t3d_anim_set_playing(&p->fbAnimCrouchAttack, true);
                }
            }
            // Z + B = spin charge
            else if (held.b && !p->fbIsCharging && !p->fbIsCrouchAttacking) {
                p->fbIsCharging = true;
                p->fbChargeTime = 0.0f;
                p->physics.canMove = false;
                p->physics.canRotate = false;
                if (p->fbAnimSpinCharge.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimSpinCharge);
                    t3d_anim_set_time(&p->fbAnimSpinCharge, 0.0f);
                    t3d_anim_set_playing(&p->fbAnimSpinCharge, true);
                }
            }
            else {
                p->physics.canMove = false;
                p->physics.velX *= 0.95f;
                p->physics.velZ *= 0.95f;

                if (p->fbAnimCrouch.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimCrouch);
                    float animTime = t3d_anim_get_time(&p->fbAnimCrouch);
                    float animDuration = p->fbAnimCrouch.animRef->duration;
                    if (animTime < animDuration) {
                        t3d_anim_update(&p->fbAnimCrouch, dt);
                    }
                }
            }
        }
        // === WALKING/RUNNING ===
        else if (isMoving) {
            p->fbIsCrouching = false;
            p->idleFrames = 0;

            // Ground movement physics (fullbody has air control)
            #define FB_WALK_SPEED_CAP 6.0f
            #define FB_WALK_ACCEL 0.8f
            #define FB_FRICTION 0.5f

            // Calculate input magnitude
            float inputMag = sqrtf(rawStickX * rawStickX + rawStickY * rawStickY);
            if (inputMag > 1.0f) inputMag = 1.0f;

            // Target velocity
            float targetVelX = -rawStickX * FB_WALK_SPEED_CAP;
            float targetVelZ = rawStickY * FB_WALK_SPEED_CAP * 0.7f;

            // Lerp toward target
            float steerRate = 0.5f;
            p->physics.velX += (targetVelX - p->physics.velX) * steerRate;
            p->physics.velZ += (targetVelZ - p->physics.velZ) * steerRate;

            // Face movement direction
            if (fabsf(rawStickX) > 0.1f || fabsf(rawStickY) > 0.1f) {
                p->angle = atan2f(-targetVelX, targetVelZ);
                p->physics.playerAngle = p->angle;
            }

            // B while running = ground spin
            if (pressed.b && !p->fbIsSpinning) {
                p->fbIsSpinning = true;
                p->fbSpinTime = 0.0f;
                if (p->fbAnimSpinAtk.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimSpinAtk);
                    t3d_anim_set_time(&p->fbAnimSpinAtk, 0.0f);
                    t3d_anim_set_playing(&p->fbAnimSpinAtk, true);
                }
            }
            // Run/walk animations (update currentSpeed after velocity changes)
            else {
                float newSpeed = sqrtf(p->physics.velX * p->physics.velX + p->physics.velZ * p->physics.velZ);
                if (newSpeed > 3.0f && p->fbAnimRun.animRef != NULL) {
                    player_attach_anim(p, &p->fbAnimRun);
                    t3d_anim_update(&p->fbAnimRun, dt);
                } else if (p->animWalk.animRef != NULL) {
                    player_attach_anim(p, &p->animWalk);
                    t3d_anim_update(&p->animWalk, dt);
                }
            }
        }
        // === IDLE ===
        else {
            p->fbIsCrouching = false;
            p->idleFrames++;

            // Apply friction when idle
            p->physics.velX *= 0.5f;
            p->physics.velZ *= 0.5f;
            if (p->physics.velX * p->physics.velX + p->physics.velZ * p->physics.velZ < 0.5f) {
                p->physics.velX = 0.0f;
                p->physics.velZ = 0.0f;
            }

            if (p->animIdle.animRef != NULL) {
                player_attach_anim(p, &p->animIdle);
                t3d_anim_update(&p->animIdle, dt);
            }
        }

        // Track jump peak
        if (!p->physics.isGrounded && p->physics.velY > 0) {
            if (p->y > p->jumpPeakY) p->jumpPeakY = p->y;
        }

        // Landing detection
        if (!p->physics.isGrounded) {
            p->fbWasAirborne = true;
            p->fbJustLandedFromSpecial = false;  // Clear when airborne
        } else if (p->fbWasAirborne) {
            p->fbWasAirborne = false;
            p->isJumping = false;
            // Track if landing from special jump state (before clearing them)
            p->fbJustLandedFromSpecial = p->fbIsLongJumping || p->fbIsBackflipping || p->fbIsHovering || p->fbIsSpinningAir;
            if (p->fbJustLandedFromSpecial) {
                p->jumpBufferTimer = 0.0f;  // Clear jump buffer to prevent buffered jumps
            }
            p->fbIsLongJumping = false;
            p->fbIsBackflipping = false;
            p->fbLongJumpSpeed = 0.0f;
            p->fbIsHovering = false;
            p->fbHoverTime = 0.0f;
            p->fbCrouchJumpWindup = false;
            p->fbCrouchJumpWindupTime = 0.0f;
            p->fbCrouchJumpRising = false;
            p->fbIsSpinningAir = false;
            p->fbSpinTime = 0.0f;
            p->fbIsWallJumping = false;
            p->physics.canMove = true;
            p->physics.canRotate = true;
            p->jumpPeakY = p->y;
        }

        // Jump input (when not in special states)
        // Also skip on the frame we land from special jumps (prevents "double jump" from held A)
        bool wantsJump = pressed.a || p->jumpBufferTimer > 0.0f;
        if (wantsJump && p->physics.isGrounded && !p->isJumping && !p->fbIsLongJumping && !p->fbIsBackflipping && !p->fbJustLandedFromSpecial) {
            p->jumpBufferTimer = 0.0f;
            p->jumpPeakY = p->y;
            wav64_play(&sfxJumpSound, 2);  // Play jump sound on channel 2

            if (p->fbIsCrouching && !p->fbIsHovering) {
                if (currentSpeed > 0.1f) {
                    // Long jump
                    p->fbIsLongJumping = true;
                    p->fbIsCrouching = false;
                    p->isJumping = true;
                    p->physics.isGrounded = false;
                    p->physics.velY = LONG_JUMP_HEIGHT;
                    p->fbLongJumpSpeed = LONG_JUMP_SPEED;
                    float angle = p->physics.playerAngle;
                    p->physics.velX = -sinf(angle) * LONG_JUMP_SPEED;
                    p->physics.velZ = cosf(angle) * LONG_JUMP_SPEED;

                    if (p->fbAnimCrouchJump.animRef != NULL) {
                        player_attach_anim(p, &p->fbAnimCrouchJump);
                        t3d_anim_set_time(&p->fbAnimCrouchJump, 0.0f);
                        t3d_anim_set_playing(&p->fbAnimCrouchJump, true);
                    }
                } else {
                    // Backflip
                    p->fbIsBackflipping = true;
                    p->fbIsCrouching = false;
                    p->isJumping = true;
                    p->physics.isGrounded = false;
                    p->physics.velY = BACKFLIP_HEIGHT;

                    float angle = p->physics.playerAngle;
                    p->physics.velX = -sinf(angle) * BACKFLIP_BACK_SPEED;
                    p->physics.velZ = cosf(angle) * BACKFLIP_BACK_SPEED;

                    if (p->fbAnimCrouchJump.animRef != NULL) {
                        player_attach_anim(p, &p->fbAnimCrouchJump);
                        t3d_anim_set_time(&p->fbAnimCrouchJump, 0.0f);
                        t3d_anim_set_playing(&p->fbAnimCrouchJump, true);
                    }
                }
            } else {
                // Normal jump
                p->isJumping = true;
                p->physics.isGrounded = false;
                p->physics.velY = 10.0f;  // FB jump velocity
            }
        }

        // Update crouch tracking
        bool isCurrentlyCrouching = wantsCrouch && p->physics.isGrounded;
        if (p->fbWasCrouchingPrev && !isCurrentlyCrouching) {
            p->physics.canMove = true;
        }
        p->fbWasCrouchingPrev = isCurrentlyCrouching;

        // Wall jump facing
        if (p->fbIsWallJumping && !p->physics.isGrounded) {
            p->physics.playerAngle = p->fbWallJumpAngle;
            p->angle = p->fbWallJumpAngle;
        }
    }
    // ============================================================
    // ARMS MODE - Fast movement with spin/whip attacks
    // ============================================================
    else if (p->currentPart == PLAYER_PART_ARMS) {
        const float ARMS_JUMP_VELOCITY = 12.0f;
        const float ARMS_SPEED = 5.0f * FPS_SCALE;
        const float ARMS_SPIN_DURATION = 0.4f;

        // === SPINNING ATTACK ===
        if (p->armsIsSpinning) {
            p->armsSpinTime += dt;

            if (p->animArmsSpin.animRef != NULL) {
                player_attach_anim(p, &p->animArmsSpin);
                t3d_anim_update(&p->animArmsSpin, dt);
            }

            // Allow movement during spin
            if (isMoving) {
                p->physics.velX = -rawStickX * ARMS_SPEED * 0.7f;
                p->physics.velZ = rawStickY * ARMS_SPEED * 0.7f;
            }

            if (p->armsSpinTime >= ARMS_SPIN_DURATION) {
                p->armsIsSpinning = false;
                p->armsSpinTime = 0.0f;
            }
        }
        // === IN AIR ===
        else if (!p->physics.isGrounded) {
            if (p->animArmsJump.animRef != NULL) {
                player_attach_anim(p, &p->animArmsJump);
                t3d_anim_update(&p->animArmsJump, dt);
            }

            // Double jump
            if (pressed.a && !p->armsHasDoubleJumped) {
                p->armsHasDoubleJumped = true;
                p->physics.velY = ARMS_JUMP_VELOCITY * 0.8f;
                wav64_play(&sfxJumpSound, 2);  // Play jump sound on channel 2
            }

            // Air spin attack (B)
            if (pressed.b && !p->armsIsSpinning) {
                p->armsIsSpinning = true;
                p->armsSpinTime = 0.0f;
            }

            // Wall kick
            if (p->physics.wallHitTimer > 0 && pressed.a) {
                wav64_play(&sfxJumpSound, 2);  // Play jump sound on channel 2
                float awayX = p->physics.wallNormalX;
                float awayZ = p->physics.wallNormalZ;

                p->physics.velY = 10.0f;
                p->physics.velX = awayX * 5.0f;
                p->physics.velZ = awayZ * 5.0f;
                p->angle = atan2f(-awayX, awayZ);
                p->physics.playerAngle = p->angle;
                p->armsHasDoubleJumped = false;  // Reset double jump after wall kick
            }

            // Air control
            if (isMoving) {
                p->physics.velX += -rawStickX * 0.3f;
                p->physics.velZ += rawStickY * 0.3f;
            }
        }
        // === GROUNDED ===
        else {
            p->armsHasDoubleJumped = false;  // Reset double jump on landing
            p->physics.canMove = true;
            p->physics.canRotate = true;

            // Jump
            if (pressed.a) {
                p->physics.velY = ARMS_JUMP_VELOCITY;
                p->physics.isGrounded = false;
                wav64_play(&sfxJumpSound, 2);  // Play jump sound on channel 2

                if (p->animArmsJump.animRef != NULL) {
                    player_attach_anim(p, &p->animArmsJump);
                    t3d_anim_set_time(&p->animArmsJump, 0.0f);
                    t3d_anim_set_playing(&p->animArmsJump, true);
                }
            }
            // Spin attack (B)
            else if (pressed.b && !p->armsIsSpinning) {
                p->armsIsSpinning = true;
                p->armsSpinTime = 0.0f;

                if (p->animArmsSpin.animRef != NULL) {
                    player_attach_anim(p, &p->animArmsSpin);
                    t3d_anim_set_time(&p->animArmsSpin, 0.0f);
                    t3d_anim_set_playing(&p->animArmsSpin, true);
                }
            }
            // Movement physics
            else if (isMoving) {
                // Calculate input magnitude
                float inputMag = sqrtf(rawStickX * rawStickX + rawStickY * rawStickY);
                if (inputMag > 1.0f) inputMag = 1.0f;

                // Target velocity
                float targetVelX = -rawStickX * ARMS_SPEED;
                float targetVelZ = rawStickY * ARMS_SPEED * 0.7f;

                // Lerp toward target (arms mode is snappier)
                float steerRate = 0.6f;
                p->physics.velX += (targetVelX - p->physics.velX) * steerRate;
                p->physics.velZ += (targetVelZ - p->physics.velZ) * steerRate;

                if (fabsf(rawStickX) > 0.1f || fabsf(rawStickY) > 0.1f) {
                    p->angle = atan2f(-targetVelX, targetVelZ);
                    p->physics.playerAngle = p->angle;
                }

                player_attach_anim(p, &p->animArmsWalk1);
                t3d_anim_update(&p->animArmsWalk1, dt);
            } else {
                // Friction
                p->physics.velX *= 0.5f;
                p->physics.velZ *= 0.5f;

                // Zero out tiny velocities
                if (p->physics.velX * p->physics.velX + p->physics.velZ * p->physics.velZ < 0.5f) {
                    p->physics.velX = 0.0f;
                    p->physics.velZ = 0.0f;
                }

                player_attach_anim(p, &p->animArmsIdle);
                t3d_anim_update(&p->animArmsIdle, dt);
            }
        }
    }

physics:
    // ============================================================
    // PHYSICS - Gravity, position, collisions
    // ============================================================

    // Constants for collision (match game.c)
    #define MAX_STEP_UP 15.0f
    #define NUM_SUBSTEPS 4

    // Get ground height from map (use MAX_STEP_UP like game.c)
    if (map) {
        float searchFromY = p->y + MAX_STEP_UP;
        float groundY = maploader_get_ground_height(map, p->x, searchFromY, p->z);
        if (groundY > INVALID_GROUND_Y) {
            p->groundLevel = groundY;
        }
    }

    // Also check decoration ground height (barrels, platforms, buttons, etc.)
    if (runtime) {
        float searchFromY = p->y + MAX_STEP_UP;
        float decoGroundY = map_get_deco_ground_height(runtime, p->x, searchFromY, p->z);
        if (decoGroundY > p->groundLevel) {
            p->groundLevel = decoGroundY;
        }
    }

    // =================================================================
    // QUARTER STEPS (prevents tunneling through walls)
    // Move in 4 substeps, checking walls after each one
    // =================================================================
    if (map) {
        float stepVelX = p->physics.velX / NUM_SUBSTEPS;
        float stepVelZ = p->physics.velZ / NUM_SUBSTEPS;

        for (int step = 0; step < NUM_SUBSTEPS; step++) {
            // Move one substep
            float nextX = p->x + stepVelX;
            float nextZ = p->z + stepVelZ;

            // Check wall collision at new position
            float pushX = 0.0f, pushZ = 0.0f;
            bool hitWall = maploader_check_walls_ex(map, nextX, p->y, nextZ,
                PLAYER_RADIUS, PLAYER_HEIGHT, &pushX, &pushZ, p->groundLevel);

            // Also check decoration walls (barrels, etc.)
            float decoPushX = 0.0f, decoPushZ = 0.0f;
            bool hitDecoWall = false;
            if (runtime) {
                hitDecoWall = map_check_deco_walls(runtime, nextX, p->y, nextZ,
                    PLAYER_RADIUS, PLAYER_HEIGHT, &decoPushX, &decoPushZ);
            }

            if (hitWall || hitDecoWall) {
                // Apply push and slide along wall
                nextX += pushX + decoPushX;
                nextZ += pushZ + decoPushZ;

                // Calculate wall normal from push direction
                float totalPushX = pushX + decoPushX;
                float totalPushZ = pushZ + decoPushZ;
                float pushLen = sqrtf(totalPushX * totalPushX + totalPushZ * totalPushZ);
                if (pushLen > 0.001f) {
                    float wallNX = totalPushX / pushLen;
                    float wallNZ = totalPushZ / pushLen;

                    // Project velocity onto wall (slide along it)
                    float dot = p->physics.velX * wallNX + p->physics.velZ * wallNZ;
                    if (dot < 0) {  // Only if moving into wall
                        p->physics.velX -= dot * wallNX;
                        p->physics.velZ -= dot * wallNZ;
                        // Update substep velocity for remaining steps
                        stepVelX = p->physics.velX / NUM_SUBSTEPS;
                        stepVelZ = p->physics.velZ / NUM_SUBSTEPS;
                    }
                }
            }

            p->x = nextX;
            p->z = nextZ;
        }
    } else {
        // No map - just apply velocity directly
        p->x += p->physics.velX;
        p->z += p->physics.velZ;
    }

    // Apply gravity (skip during fullbody hover - hover handles its own gravity)
    // Moon jump reverses gravity direction
    if (!p->physics.isGrounded && !p->fbIsHovering) {
        if (reverseGravity) {
            p->physics.velY += GRAVITY;  // Moon jump - go up!
            if (p->physics.velY > -TERMINAL_VELOCITY) {
                p->physics.velY = -TERMINAL_VELOCITY;
            }
        } else {
            p->physics.velY -= GRAVITY;
            if (p->physics.velY < TERMINAL_VELOCITY) {
                p->physics.velY = TERMINAL_VELOCITY;
            }
        }
    }

    // =================================================================
    // CEILING COLLISION - check before moving up to prevent tunneling
    // =================================================================
    if (map && p->physics.velY > 0) {
        float headY = p->y + PLAYER_HEIGHT;
        float ceilingY = maploader_get_ceiling_height(map, p->x, headY, p->z);

        if (ceilingY < INVALID_CEILING_Y - 10.0f) {
            // Found a ceiling - check if moving would hit it
            float newHeadY = headY + p->physics.velY;
            if (newHeadY >= ceilingY) {
                // Bonk! Clamp position to just below ceiling
                p->y = ceilingY - PLAYER_HEIGHT - 0.5f;
                p->physics.velY = 0;
            } else {
                p->y += p->physics.velY;
            }
        } else {
            p->y += p->physics.velY;
        }
    } else {
        p->y += p->physics.velY;
    }

    // Track peak height for landing squash
    if (p->y > p->jumpPeakY) {
        p->jumpPeakY = p->y;
    }

    // Ground collision
    bool wasGrounded = p->physics.isGrounded;
    if (p->y <= p->groundLevel) {
        p->y = p->groundLevel;

        // Landing detection
        if (!wasGrounded && p->physics.velY < -1.0f) {
            float fallHeight = p->jumpPeakY - p->groundLevel;
            float landSquash = fallHeight * 0.005f;
            if (landSquash > 0.3f) landSquash = 0.3f;
            p->squashScale = 1.0f - landSquash;
            p->squashVelocity = -landSquash * 8.0f;
            p->landingSquash = landSquash;

            // Transition to landing animation
            if (p->isJumping && p->animJumpLand.animRef) {
                p->isLanding = true;
                // Start triple jump combo timer on landing
                p->jumpComboTimer = PLAYER_JUMP_COMBO_WINDOW;
                player_attach_anim(p, &p->animJumpLand);
                t3d_anim_set_time(&p->animJumpLand, 0.0f);
                t3d_anim_set_playing(&p->animJumpLand, true);

                // ============================================================
                // THIRD JUMP SPECIAL LANDING (COMMENTED OUT - FUTURE FEATURE)
                // ============================================================
                // When landing the third jump of a triple-jump combo, disable
                // player controls and play the full landing animation before
                // returning control. This creates a "commitment" feel for the
                // powerful third jump.
                //
                // To enable this behavior, uncomment the following code:
                //
                // if (p->jumpComboCount == 2) {
                //     // Third jump landing - disable controls until animation finishes
                //     p->physics.canMove = false;
                //     p->physics.canRotate = false;
                //     p->physics.canJump = false;
                //
                //     // Play landing animation at normal speed (don't allow early cancel)
                //     // The animation will play fully before controls are restored.
                //     // When the landing animation finishes (checked in the isLanding
                //     // handling above around line ~780), controls will be restored
                //     // via physics.canMove = true, etc.
                //     //
                //     // Optional: Add a longer/special landing animation for third jump
                //     // player_attach_anim(p, &p->animJumpLandHeavy);  // if available
                //     //
                //     // Reset combo counter after third jump lands
                //     p->jumpComboCount = 0;
                //     p->jumpComboTimer = 0.0f;  // Don't allow immediate combo restart
                // }
                // ============================================================
            } else {
                p->isJumping = false;
                // Start triple jump combo timer on landing
                p->jumpComboTimer = PLAYER_JUMP_COMBO_WINDOW;
                p->isLanding = false;
            }
        }

        p->physics.velY = 0.0f;
        p->physics.isGrounded = true;
        p->physics.currentJumps = 0;
        p->jumpPeakY = p->groundLevel;
    } else if (p->y > p->groundLevel + 5.0f) {
        p->physics.isGrounded = false;
        // Walk off edge - enter falling
        if (wasGrounded && !p->isJumping && !p->isCharging) {
            p->isJumping = true;
            p->jumpAnimPaused = true;
            if (p->animJumpLand.animRef) {
                player_attach_anim(p, &p->animJumpLand);
                t3d_anim_set_time(&p->animJumpLand, 0.0f);
                t3d_anim_set_playing(&p->animJumpLand, false);
            }
        }
    }

    // Death from falling
    if (p->y < -500.0f) {
        p->isDead = true;
        p->deathTimer = 0.0f;
        // Reset triple jump combo on fall death
        p->jumpComboCount = 0;
        p->jumpComboTimer = 0.0f;
    }

    // Update skeleton
    t3d_skeleton_update(&p->skeleton);

    // Update camera
    player_update_camera(p, dt);
}

// ============================================================
// PLAYER DRAW
// ============================================================

void player_draw(Player* p, T3DViewport* vp) {
    (void)vp;

    if (!p->model || !p->matFP) return;

    // Skip if flashing during invincibility
    if (p->invincibilityTimer > 0.0f) {
        if ((p->invincibilityFlashFrame / PLAYER_INVINCIBILITY_FLASH_RATE) & 1) {
            return;  // Skip draw on flash frames
        }
    }

    // Calculate scale with squash/stretch
    float scaleX = 1.0f / p->squashScale;
    float scaleY = p->squashScale;
    float scaleZ = 1.0f / p->squashScale;

    // Build transform matrix
    t3d_mat4fp_from_srt_euler(p->matFP,
        (float[3]){scaleX, scaleY, scaleZ},
        (float[3]){0.0f, p->angle, 0.0f},
        (float[3]){p->x, p->y, p->z}
    );

    // Draw with skeleton
    t3d_matrix_push(p->matFP);
    t3d_model_draw_skinned(p->model, &p->skeleton);
    t3d_matrix_pop(1);
}

void player_draw_arc(Player* p, MapLoader* map, T3DModel* arcModel, T3DMat4FP* arcMat) {
    (void)map;

    if (!p->isCharging || !arcModel || !arcMat) return;

    // Draw arc preview dots (simplified version)
    // Full version calculates trajectory based on charge power

    float chargePower = fminf(p->jumpChargeTime / 0.8f, 1.0f);
    float velY = JUMP_VELOCITY * (0.5f + 0.5f * chargePower);
    float velX = p->jumpAimX * 80.0f * chargePower;
    float velZ = -p->jumpAimY * 80.0f * chargePower;

    float dt = 0.05f;  // Step size
    float px = p->x;
    float py = p->y;
    float pz = p->z;

    for (int i = 0; i < 8; i++) {
        velY -= GRAVITY * dt * 60.0f;
        px += velX * dt * 60.0f;
        py += velY * dt * 60.0f;
        pz += velZ * dt * 60.0f;

        if (py < p->groundLevel - 10.0f) break;

        // Draw dot
        t3d_mat4fp_from_srt_euler(arcMat,
            (float[3]){0.2f, 0.2f, 0.2f},
            (float[3]){0.0f, 0.0f, 0.0f},
            (float[3]){px, py, pz}
        );
        t3d_matrix_push(arcMat);
        t3d_model_draw(arcModel);
        t3d_matrix_pop(1);
    }

    // Store arc end for camera
    p->jumpArcEndX = px;
}
