#ifndef N64_GAME_H
#define N64_GAME_H

#include <libdragon.h>

void init_game_scene(void);
void deinit_game_scene(void);
void update_game_scene(void);
void draw_game_scene(void);
bool is_game_scene_initialized(void);

// Level complete functionality
void game_show_level_complete(void);
void game_restart_level(void);

extern int partsObtained;

// Sound effects
extern wav64_t sfxBoltCollect;
extern wav64_t sfxJumpSound;

// Reverse gravity flag
extern bool reverseGravity;

// Replay mode flag (set true before init_game_scene to watch a replay)
extern bool g_replayMode;

// Player scale cheat (1.0 = normal, 2.0 = giant, 0.5 = tiny)
extern float g_playerScaleCheat;

#endif
