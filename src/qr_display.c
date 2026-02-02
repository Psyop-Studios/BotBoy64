#include "qr_display.h"
#include "qrcodegen.h"
#include <libdragon.h>
#include <rdpq.h>

// Use a smaller max version to save memory (version 6 = max 134 chars with medium ECC)
#define QR_MAX_VERSION 6
#define QR_BUFFER_SIZE qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)

// Static buffers for QR code generation
static uint8_t qrTempBuffer[QR_BUFFER_SIZE];
static uint8_t qrCodeBuffer[QR_BUFFER_SIZE];
static bool qrValid = false;
static int qrSize = 0;

void qr_display_init(void) {
    // Generate QR code from reward URL
    qrValid = qrcodegen_encodeText(
        QR_REWARD_URL,
        qrTempBuffer,
        qrCodeBuffer,
        qrcodegen_Ecc_MEDIUM,  // Medium error correction (~15%)
        1,                      // Min version
        QR_MAX_VERSION,         // Max version
        qrcodegen_Mask_AUTO,    // Auto mask selection
        true                    // Boost ECC if possible
    );

    if (qrValid) {
        qrSize = qrcodegen_getSize(qrCodeBuffer);
        debugf("QR code generated: %dx%d modules\n", qrSize, qrSize);
    } else {
        debugf("ERROR: Failed to generate QR code for reward URL\n");
    }
}

void qr_display_free(void) {
    qrValid = false;
    qrSize = 0;
}

bool qr_display_is_valid(void) {
    return qrValid;
}

int qr_display_get_size(void) {
    return qrSize;
}

void qr_display_draw(int x, int y, int pixelSize) {
    if (!qrValid || qrSize == 0) return;

    // Calculate total size with 1-module white border
    int totalSize = (qrSize + 2) * pixelSize;

    // Draw white background (quiet zone)
    rdpq_set_mode_fill(RGBA32(255, 255, 255, 255));
    rdpq_fill_rectangle(x, y, x + totalSize, y + totalSize);

    // Draw black modules
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));

    for (int qy = 0; qy < qrSize; qy++) {
        for (int qx = 0; qx < qrSize; qx++) {
            if (qrcodegen_getModule(qrCodeBuffer, qx, qy)) {
                // Offset by 1 module for quiet zone border
                int px = x + (qx + 1) * pixelSize;
                int py = y + (qy + 1) * pixelSize;
                rdpq_fill_rectangle(px, py, px + pixelSize, py + pixelSize);
            }
        }
    }
}
