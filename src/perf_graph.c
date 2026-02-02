// ============================================================
// PERFORMANCE GRAPH MODULE
// Debug visualization for frame timing
// ============================================================

#include "perf_graph.h"
#include <libdragon.h>

// State
static int g_perfGraphData[PERF_GRAPH_WIDTH] = {0};
static int g_perfGraphHead = 0;
static bool g_perfGraphEnabled = false;
static bool g_toggleCooldown = false;

void perf_graph_init(void) {
    g_perfGraphHead = 0;
    g_perfGraphEnabled = false;
    g_toggleCooldown = false;
    for (int i = 0; i < PERF_GRAPH_WIDTH; i++) {
        g_perfGraphData[i] = 0;
    }
}

void perf_graph_record(uint32_t frameTimeUs) {
    g_perfGraphData[g_perfGraphHead] = (int)frameTimeUs;
    g_perfGraphHead = (g_perfGraphHead + 1) % PERF_GRAPH_WIDTH;
}

void perf_graph_toggle(void) {
    g_perfGraphEnabled = !g_perfGraphEnabled;
}

bool perf_graph_enabled(void) {
    return g_perfGraphEnabled;
}

void perf_graph_update(bool cLeftHeld, uint32_t frameTimeUs) {
    // Handle toggle with cooldown
    if (cLeftHeld) {
        if (!g_toggleCooldown) {
            perf_graph_toggle();
            g_toggleCooldown = true;
        }
    } else {
        g_toggleCooldown = false;
    }

    // Record frame time
    perf_graph_record(frameTimeUs);
}

void perf_graph_draw(int graphX, int graphY) {
    if (!g_perfGraphEnabled) return;

    // Draw background
    rdpq_sync_pipe();  // Sync before switching to fill mode
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 200));
    rdpq_fill_rectangle(graphX - 2, graphY - PERF_GRAPH_HEIGHT - 12,
                       graphX + PERF_GRAPH_WIDTH + 2, graphY + 2);

    // Draw target line (30 FPS)
    rdpq_set_mode_fill(RGBA32(100, 100, 100, 255));
    int targetY = graphY - (PERF_GRAPH_TARGET_US * PERF_GRAPH_HEIGHT / 66666);
    rdpq_fill_rectangle(graphX, targetY, graphX + PERF_GRAPH_WIDTH, targetY + 1);

    // Draw bars
    for (int i = 0; i < PERF_GRAPH_WIDTH; i++) {
        int idx = (g_perfGraphHead + i) % PERF_GRAPH_WIDTH;
        int frameUs = g_perfGraphData[idx];

        // Scale: 0-66666us maps to 0-PERF_GRAPH_HEIGHT
        int barHeight = (frameUs * PERF_GRAPH_HEIGHT) / 66666;
        if (barHeight > PERF_GRAPH_HEIGHT) barHeight = PERF_GRAPH_HEIGHT;
        if (barHeight < 1) barHeight = 1;

        // Color based on performance (green = good, red = bad)
        if (frameUs < PERF_GRAPH_TARGET_US) {
            rdpq_set_mode_fill(RGBA32(50, 200, 50, 255));  // Green - under budget
        } else if (frameUs < 50000) {
            rdpq_set_mode_fill(RGBA32(200, 200, 50, 255)); // Yellow - slightly over
        } else {
            rdpq_set_mode_fill(RGBA32(200, 50, 50, 255));  // Red - way over
        }

        rdpq_fill_rectangle(graphX + i, graphY - barHeight, graphX + i + 1, graphY);
    }

    // Draw label
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, graphX, graphY - PERF_GRAPH_HEIGHT - 8, "Frame ms");
}
