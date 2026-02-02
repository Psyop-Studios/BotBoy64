#ifndef N64JAM_SCRIPTSYSTEM_H
#define N64JAM_SCRIPTSYSTEM_H

#include <stdbool.h>

// Start a cutscene (resets to step 0)
void cutscene_start(void);

// Update cutscene - call every frame
// Returns: true if still running, false when done
bool cutscene_update(void);

// Check if cutscene is active
bool cutscene_is_active(void);

// Skip/cancel the current cutscene
void cutscene_skip(void);

#endif
