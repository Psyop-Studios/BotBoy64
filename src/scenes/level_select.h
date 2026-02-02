#ifndef LEVEL_SELECT_H
#define LEVEL_SELECT_H

void init_level_select_scene(void);
void update_level_select_scene(void);
void draw_level_select_scene(void);
void cleanup_level_select_scene(void);

// Selected level to start (set by level select, read by game scene)
extern int selectedLevelID;

// If true, game scene starts with iris opening effect
extern bool startWithIrisOpen;

// If true, menu scene starts with iris opening effect (returning from game)
extern bool menuStartWithIrisOpen;

#endif // LEVEL_SELECT_H
