#ifndef N64_LEVEL_COMPLETE_H
#define N64_LEVEL_COMPLETE_H

// Level completion data (set before transitioning to this scene)
typedef struct {
    int levelId;           // Which level was completed
    int boltsCollected;    // Bolts collected in this level
    int totalBoltsInLevel; // Total bolts in this level
    int deathCount;        // Deaths during this level attempt
    float levelTime;       // Time taken to complete
} LevelCompleteData;

// Set the completion data before showing the screen
void level_complete_set_data(int levelId, int boltsCollected, int totalBolts, int deaths, float time);

void init_level_complete_scene(void);
void deinit_level_complete_scene(void);
void update_level_complete_scene(void);
void draw_level_complete_scene(void);

#endif
