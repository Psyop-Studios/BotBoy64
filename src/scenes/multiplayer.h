#ifndef N64_MULTIPLAYER_H
#define N64_MULTIPLAYER_H

#include <libdragon.h>

void init_multiplayer_scene(void);
void deinit_multiplayer_scene(void);
void update_multiplayer_scene(void);
void draw_multiplayer_scene(void);

// Selected level for multiplayer (set before init)
extern int multiplayerLevelID;

#endif
