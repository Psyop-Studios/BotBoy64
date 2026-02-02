#ifndef SPLASH_H
#define SPLASH_H

#include <stdbool.h>

// Flag to tell menu scene to fade in from black (set when splash ends)
extern bool menuStartWithFadeIn;

void init_splash_scene(void);
void update_splash_scene(void);
void draw_splash_scene(void);
void deinit_splash_scene(void);

#endif
