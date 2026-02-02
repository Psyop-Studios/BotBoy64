// ============================================================
// PERFORMANCE GRAPH MODULE
// Debug visualization for frame timing
// Toggle with C-Left button
// ============================================================

#ifndef PERF_GRAPH_H
#define PERF_GRAPH_H

#include <libdragon.h>
#include <stdbool.h>
#include <stdint.h>

// Constants
#define PERF_GRAPH_WIDTH 64       // Number of samples to display
#define PERF_GRAPH_HEIGHT 40      // Height in pixels
#define PERF_GRAPH_TARGET_US 33333 // Target frame time (30 FPS = 33.3ms)

// Initialize performance graph
void perf_graph_init(void);

// Record a frame time (call once per frame with get_ticks() difference)
void perf_graph_record(uint32_t frameTimeUs);

// Toggle graph visibility
void perf_graph_toggle(void);

// Check if graph is enabled
bool perf_graph_enabled(void);

// Draw the performance graph
// graphX, graphY: Bottom-left corner of graph (default: 10, 234)
void perf_graph_draw(int graphX, int graphY);

// Convenience function for full update cycle
// Call with C-Left held state for toggle, frame time for recording
void perf_graph_update(bool cLeftHeld, uint32_t frameTimeUs);

#endif // PERF_GRAPH_H
