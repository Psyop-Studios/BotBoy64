#ifndef QR_DISPLAY_H
#define QR_DISPLAY_H

#include <stdbool.h>

// Initialize QR display system (generates QR code for reward URL)
void qr_display_init(void);

// Free QR display resources
void qr_display_free(void);

// Draw QR code at specified position
// x, y = top-left corner position
// pixelSize = size of each QR module in screen pixels (2-4 recommended)
void qr_display_draw(int x, int y, int pixelSize);

// Check if QR code was successfully generated
bool qr_display_is_valid(void);

// Get the size of the QR code (modules per side)
int qr_display_get_size(void);

// The reward URL
#define QR_REWARD_URL "https://psyops.studio/games/botboy64/reward/"

#endif // QR_DISPLAY_H
