// ============================================================
// DECORATION RENDERING MODULE
// Shared decoration rendering for game.c and multiplayer.c
// ============================================================

#ifndef DECO_RENDER_H
#define DECO_RENDER_H

#include <libdragon.h>
#include <fgeom.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include "mapData.h"

// ============================================================
// UV SCROLLING STATE
// Only included in files that #define DECO_RENDER_OWNER before including
// This prevents duplicate static variables across translation units
// ============================================================

#ifdef DECO_RENDER_OWNER

// Conveyor belt
static int16_t* g_conveyorBaseUVs = NULL;
static int g_conveyorVertCount = 0;
static T3DVertPacked* g_conveyorVerts = NULL;

// Toxic pipe liquid
static int16_t* g_toxicPipeLiquidBaseUVs = NULL;
static int g_toxicPipeLiquidVertCount = 0;
static T3DVertPacked* g_toxicPipeLiquidVerts = NULL;

// Toxic running
static int16_t* g_toxicRunningBaseUVs = NULL;
static int g_toxicRunningVertCount = 0;
static T3DVertPacked* g_toxicRunningVerts = NULL;

// Lava floor
static int16_t* g_lavaFloorBaseUVs = NULL;
static int g_lavaFloorVertCount = 0;
static T3DVertPacked* g_lavaFloorVerts = NULL;

// Lava falls
static int16_t* g_lavaFallsBaseUVs = NULL;
static int g_lavaFallsVertCount = 0;
static T3DVertPacked* g_lavaFallsVerts = NULL;

// Jukebox FX
#define MAX_JUKEBOX_FX_PARTS 8
static int16_t* g_jukeboxFxBaseUVs[MAX_JUKEBOX_FX_PARTS] = {NULL};
static int g_jukeboxFxVertCount[MAX_JUKEBOX_FX_PARTS] = {0};
static T3DVertPacked* g_jukeboxFxVerts[MAX_JUKEBOX_FX_PARTS] = {NULL};
static int g_jukeboxFxPartCount = 0;

// Monitor screen
static int16_t* g_monitorScreenBaseUVs = NULL;
static int g_monitorScreenVertCount = 0;
static T3DVertPacked* g_monitorScreenVerts = NULL;

// Fan2 (DECO_FAN)
static int16_t* g_fan2BaseUVs = NULL;
static int g_fan2VertCount = 0;
static T3DVertPacked* g_fan2Verts = NULL;

// Lava slime (DECO_SLIME_LAVA)
static int16_t* g_lavaSlimeBaseUVs = NULL;
static int g_lavaSlimeVertCount = 0;
static T3DVertPacked* g_lavaSlimeVerts = NULL;

// Level 3 stream (DECO_LEVEL3_STREAM) - defined in functions section below

// ============================================================
// UV SCROLLING FUNCTIONS
// ============================================================

static inline void conveyor_belt_init_uvs(T3DModel* beltModel) {
    // Always refresh vertex pointer from model (may have been reloaded)
    T3DModelIter iter = t3d_model_iter_create(beltModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        if (!obj->numParts) continue;

        T3DObjectPart* part = &obj->parts[0];
        g_conveyorVerts = part->vert;
        g_conveyorVertCount = part->vertLoadCount;

        // Only allocate base UV buffer once
        if (g_conveyorBaseUVs == NULL) {
            g_conveyorBaseUVs = malloc(g_conveyorVertCount * 2 * sizeof(int16_t));
            if (!g_conveyorBaseUVs) {
                debugf("ERROR: Failed to allocate conveyor UV buffer\n");
                g_conveyorVerts = NULL;
                g_conveyorVertCount = 0;
                return;
            }

            for (int i = 0; i < g_conveyorVertCount; i++) {
                int16_t* uv = t3d_vertbuffer_get_uv(g_conveyorVerts, i);
                g_conveyorBaseUVs[i * 2] = uv[0];
                g_conveyorBaseUVs[i * 2 + 1] = uv[1];
            }
        }
        break;
    }
}

static inline void conveyor_belt_scroll_uvs(float offset) {
    if (!g_conveyorBaseUVs || !g_conveyorVerts) return;

    int16_t scrollAmount = (int16_t)(offset * 32.0f * 18.0f);

    for (int i = 0; i < g_conveyorVertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_conveyorVerts, i);
        uv[1] = g_conveyorBaseUVs[i * 2 + 1] - scrollAmount;
    }

    int packedCount = (g_conveyorVertCount + 1) / 2;
    data_cache_hit_writeback(g_conveyorVerts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

static inline void toxic_pipe_liquid_init_uvs(T3DModel* liquidModel) {
    // Always refresh vertex pointer from model (may have been reloaded)
    T3DModelIter iter = t3d_model_iter_create(liquidModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        if (!obj->numParts) continue;

        T3DObjectPart* part = &obj->parts[0];
        g_toxicPipeLiquidVerts = part->vert;
        g_toxicPipeLiquidVertCount = part->vertLoadCount;

        // Only allocate base UV buffer once
        if (g_toxicPipeLiquidBaseUVs == NULL) {
            g_toxicPipeLiquidBaseUVs = malloc(g_toxicPipeLiquidVertCount * 2 * sizeof(int16_t));
            if (!g_toxicPipeLiquidBaseUVs) {
                debugf("ERROR: Failed to allocate toxic pipe liquid UV buffer\n");
                g_toxicPipeLiquidVerts = NULL;
                g_toxicPipeLiquidVertCount = 0;
                return;
            }

            for (int i = 0; i < g_toxicPipeLiquidVertCount; i++) {
                int16_t* uv = t3d_vertbuffer_get_uv(g_toxicPipeLiquidVerts, i);
                g_toxicPipeLiquidBaseUVs[i * 2] = uv[0];
                g_toxicPipeLiquidBaseUVs[i * 2 + 1] = uv[1];
            }
        }
        break;
    }
}

static inline void toxic_pipe_liquid_scroll_uvs(float offset) {
    if (!g_toxicPipeLiquidBaseUVs || !g_toxicPipeLiquidVerts) return;

    int16_t scrollAmount = (int16_t)(offset * 32.0f * 32.0f);

    for (int i = 0; i < g_toxicPipeLiquidVertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_toxicPipeLiquidVerts, i);
        uv[1] = g_toxicPipeLiquidBaseUVs[i * 2 + 1] - scrollAmount;
    }

    int packedCount = (g_toxicPipeLiquidVertCount + 1) / 2;
    data_cache_hit_writeback(g_toxicPipeLiquidVerts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

static inline void toxic_running_init_uvs(T3DModel* toxicModel) {
    // Always refresh vertex pointer from model (may have been reloaded)
    T3DModelIter iter = t3d_model_iter_create(toxicModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        if (!obj->numParts) continue;

        T3DObjectPart* part = &obj->parts[0];
        g_toxicRunningVerts = part->vert;
        g_toxicRunningVertCount = part->vertLoadCount;

        // Only allocate base UV buffer once
        if (g_toxicRunningBaseUVs == NULL) {
            g_toxicRunningBaseUVs = malloc(g_toxicRunningVertCount * 2 * sizeof(int16_t));
            if (!g_toxicRunningBaseUVs) {
                debugf("ERROR: Failed to allocate toxic running UV buffer\n");
                g_toxicRunningVerts = NULL;
                g_toxicRunningVertCount = 0;
                return;
            }

            for (int i = 0; i < g_toxicRunningVertCount; i++) {
                int16_t* uv = t3d_vertbuffer_get_uv(g_toxicRunningVerts, i);
                g_toxicRunningBaseUVs[i * 2] = uv[0];
                g_toxicRunningBaseUVs[i * 2 + 1] = uv[1];
            }
        }
        break;
    }
}

static inline void toxic_running_scroll_uvs(float offset) {
    if (!g_toxicRunningBaseUVs || !g_toxicRunningVerts) return;

    int16_t scrollAmount = (int16_t)(offset * 32.0f * 32.0f);

    for (int i = 0; i < g_toxicRunningVertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_toxicRunningVerts, i);
        uv[1] = g_toxicRunningBaseUVs[i * 2 + 1] - scrollAmount;
    }

    int packedCount = (g_toxicRunningVertCount + 1) / 2;
    data_cache_hit_writeback(g_toxicRunningVerts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

static inline void lavafloor_init_uvs(T3DModel* lavaModel) {
    // Always refresh vertex pointer from model (may have been reloaded)
    T3DModelIter iter = t3d_model_iter_create(lavaModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        if (!obj->numParts) continue;

        T3DObjectPart* part = &obj->parts[0];
        g_lavaFloorVerts = part->vert;
        g_lavaFloorVertCount = part->vertLoadCount;

        // Only allocate base UV buffer once
        if (g_lavaFloorBaseUVs == NULL) {
            g_lavaFloorBaseUVs = malloc(g_lavaFloorVertCount * 2 * sizeof(int16_t));
            if (!g_lavaFloorBaseUVs) {
                debugf("ERROR: Failed to allocate lava floor UV buffer\n");
                g_lavaFloorVerts = NULL;
                g_lavaFloorVertCount = 0;
                return;
            }

            for (int i = 0; i < g_lavaFloorVertCount; i++) {
                int16_t* uv = t3d_vertbuffer_get_uv(g_lavaFloorVerts, i);
                g_lavaFloorBaseUVs[i * 2] = uv[0];
                g_lavaFloorBaseUVs[i * 2 + 1] = uv[1];
            }
        }
        break;
    }
}

static inline void lavafloor_scroll_uvs(float offset) {
    if (!g_lavaFloorBaseUVs || !g_lavaFloorVerts) return;

    int16_t scrollAmount = (int16_t)(offset * 32.0f * 32.0f);

    for (int i = 0; i < g_lavaFloorVertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_lavaFloorVerts, i);
        uv[1] = g_lavaFloorBaseUVs[i * 2 + 1] - scrollAmount;
    }

    int packedCount = (g_lavaFloorVertCount + 1) / 2;
    data_cache_hit_writeback(g_lavaFloorVerts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

static inline void lavafalls_init_uvs(T3DModel* lavaModel) {
    // Always refresh vertex pointer from model (may have been reloaded)
    T3DModelIter iter = t3d_model_iter_create(lavaModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        if (!obj->numParts) continue;

        T3DObjectPart* part = &obj->parts[0];
        g_lavaFallsVerts = part->vert;
        g_lavaFallsVertCount = part->vertLoadCount;

        // Only allocate base UV buffer once
        if (g_lavaFallsBaseUVs == NULL) {
            g_lavaFallsBaseUVs = malloc(g_lavaFallsVertCount * 2 * sizeof(int16_t));
            if (!g_lavaFallsBaseUVs) {
                debugf("ERROR: Failed to allocate lava falls UV buffer\n");
                g_lavaFallsVerts = NULL;
                g_lavaFallsVertCount = 0;
                return;
            }

            for (int i = 0; i < g_lavaFallsVertCount; i++) {
                int16_t* uv = t3d_vertbuffer_get_uv(g_lavaFallsVerts, i);
                g_lavaFallsBaseUVs[i * 2] = uv[0];
                g_lavaFallsBaseUVs[i * 2 + 1] = uv[1];
            }
        }
        break;
    }
}

static inline void lavafalls_scroll_uvs(float offset) {
    if (!g_lavaFallsBaseUVs || !g_lavaFallsVerts) return;

    int16_t scrollAmount = (int16_t)(offset * 32.0f * 32.0f);

    for (int i = 0; i < g_lavaFallsVertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_lavaFallsVerts, i);
        uv[1] = g_lavaFallsBaseUVs[i * 2 + 1] + scrollAmount;  // ADD to scroll opposite direction
    }

    int packedCount = (g_lavaFallsVertCount + 1) / 2;
    data_cache_hit_writeback(g_lavaFallsVerts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

// Level 3 stream can have multiple objects/parts - store info for each
#define LVL3STREAM_MAX_PARTS 8
static int16_t* g_lvl3StreamBaseUVsArr[LVL3STREAM_MAX_PARTS] = {0};
static int g_lvl3StreamVertCountArr[LVL3STREAM_MAX_PARTS] = {0};
static T3DVertPacked* g_lvl3StreamVertsArr[LVL3STREAM_MAX_PARTS] = {0};
static int g_lvl3StreamPartCount = 0;

static inline void lvl3stream_init_uvs(T3DModel* streamModel) {
    // Always refresh vertex pointers from model (may have been reloaded)
    int partIdx = 0;
    T3DModelIter iter = t3d_model_iter_create(streamModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter) && partIdx < LVL3STREAM_MAX_PARTS) {
        T3DObject* obj = iter.object;
        for (int p = 0; p < obj->numParts && partIdx < LVL3STREAM_MAX_PARTS; p++) {
            T3DObjectPart* part = &obj->parts[p];
            if (part->vertLoadCount == 0) continue;

            g_lvl3StreamVertsArr[partIdx] = part->vert;
            g_lvl3StreamVertCountArr[partIdx] = part->vertLoadCount;

            // Only allocate base UV buffer once per part
            if (g_lvl3StreamBaseUVsArr[partIdx] == NULL) {
                g_lvl3StreamBaseUVsArr[partIdx] = malloc(part->vertLoadCount * 2 * sizeof(int16_t));
                if (!g_lvl3StreamBaseUVsArr[partIdx]) {
                    debugf("ERROR: Failed to allocate lvl3 stream UV buffer for part %d\n", partIdx);
                    continue;
                }

                for (int i = 0; i < part->vertLoadCount; i++) {
                    int16_t* uv = t3d_vertbuffer_get_uv(part->vert, i);
                    g_lvl3StreamBaseUVsArr[partIdx][i * 2] = uv[0];
                    g_lvl3StreamBaseUVsArr[partIdx][i * 2 + 1] = uv[1];
                }
            }
            partIdx++;
        }
    }
    g_lvl3StreamPartCount = partIdx;
}

static inline void lvl3stream_scroll_uvs(float offset) {
    if (g_lvl3StreamPartCount == 0) return;

    // Scroll down (positive V direction)
    int16_t scrollAmount = (int16_t)(offset * 32.0f * 32.0f);

    for (int p = 0; p < g_lvl3StreamPartCount; p++) {
        if (!g_lvl3StreamBaseUVsArr[p] || !g_lvl3StreamVertsArr[p]) continue;

        for (int i = 0; i < g_lvl3StreamVertCountArr[p]; i++) {
            int16_t* uv = t3d_vertbuffer_get_uv(g_lvl3StreamVertsArr[p], i);
            uv[1] = g_lvl3StreamBaseUVsArr[p][i * 2 + 1] + scrollAmount;
        }

        int packedCount = (g_lvl3StreamVertCountArr[p] + 1) / 2;
        data_cache_hit_writeback(g_lvl3StreamVertsArr[p], packedCount * sizeof(T3DVertPacked));
    }
}

static inline void jukebox_fx_init_uvs(T3DModel* fxModel) {
    // Always refresh vertex pointers from model (may have been reloaded)
    int partIdx = 0;
    T3DModelIter iter = t3d_model_iter_create(fxModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        for (uint32_t p = 0; p < obj->numParts && partIdx < MAX_JUKEBOX_FX_PARTS; p++) {
            T3DObjectPart* part = &obj->parts[p];

            g_jukeboxFxVerts[partIdx] = part->vert;
            g_jukeboxFxVertCount[partIdx] = part->vertLoadCount;

            // Only allocate base UV buffer once per part
            if (g_jukeboxFxBaseUVs[partIdx] == NULL) {
                g_jukeboxFxBaseUVs[partIdx] = malloc(g_jukeboxFxVertCount[partIdx] * 2 * sizeof(int16_t));
                if (!g_jukeboxFxBaseUVs[partIdx]) {
                    debugf("ERROR: Failed to allocate jukebox FX UV buffer[%d]\n", partIdx);
                    g_jukeboxFxVerts[partIdx] = NULL;
                    g_jukeboxFxVertCount[partIdx] = 0;
                    partIdx++;
                    continue;  // Skip this part, try next
                }

                for (int i = 0; i < g_jukeboxFxVertCount[partIdx]; i++) {
                    int16_t* uv = t3d_vertbuffer_get_uv(g_jukeboxFxVerts[partIdx], i);
                    g_jukeboxFxBaseUVs[partIdx][i * 2] = uv[0];
                    g_jukeboxFxBaseUVs[partIdx][i * 2 + 1] = uv[1];
                }
            }
            partIdx++;
        }
    }
    g_jukeboxFxPartCount = partIdx;
}

static inline void jukebox_fx_scroll_uvs(float offset) {
    if (g_jukeboxFxPartCount == 0) return;

    int16_t scrollAmountS = (int16_t)(offset * 32.0f * 32.0f);

    for (int p = 0; p < g_jukeboxFxPartCount; p++) {
        if (!g_jukeboxFxBaseUVs[p] || !g_jukeboxFxVerts[p]) continue;

        for (int i = 0; i < g_jukeboxFxVertCount[p]; i++) {
            int16_t* uv = t3d_vertbuffer_get_uv(g_jukeboxFxVerts[p], i);
            uv[0] = g_jukeboxFxBaseUVs[p][i * 2] + scrollAmountS;
            uv[1] = g_jukeboxFxBaseUVs[p][i * 2 + 1];
        }

        int packedCount = (g_jukeboxFxVertCount[p] + 1) / 2;
        data_cache_hit_writeback(g_jukeboxFxVerts[p], packedCount * sizeof(T3DVertPacked));
    }
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

static inline void monitor_screen_init_uvs(T3DModel* screenModel) {
    // Always refresh vertex pointer from model (may have been reloaded)
    T3DModelIter iter = t3d_model_iter_create(screenModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        if (!obj->numParts) continue;

        T3DObjectPart* part = &obj->parts[0];
        g_monitorScreenVerts = part->vert;
        g_monitorScreenVertCount = part->vertLoadCount;

        // Only allocate base UV buffer once
        if (g_monitorScreenBaseUVs == NULL) {
            g_monitorScreenBaseUVs = malloc(g_monitorScreenVertCount * 2 * sizeof(int16_t));
            if (!g_monitorScreenBaseUVs) {
                debugf("ERROR: Failed to allocate monitor screen UV buffer\n");
                g_monitorScreenVerts = NULL;
                g_monitorScreenVertCount = 0;
                return;
            }

            for (int i = 0; i < g_monitorScreenVertCount; i++) {
                int16_t* uv = t3d_vertbuffer_get_uv(g_monitorScreenVerts, i);
                g_monitorScreenBaseUVs[i * 2] = uv[0];
                g_monitorScreenBaseUVs[i * 2 + 1] = uv[1];
            }
        }
        break;
    }
}

static inline void monitor_screen_scroll_uvs(float offset) {
    if (!g_monitorScreenBaseUVs || !g_monitorScreenVerts) return;

    int16_t scrollAmount = (int16_t)(offset * 32.0f * 16.0f);

    for (int i = 0; i < g_monitorScreenVertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_monitorScreenVerts, i);
        uv[1] = g_monitorScreenBaseUVs[i * 2 + 1] + scrollAmount;
    }

    int packedCount = (g_monitorScreenVertCount + 1) / 2;
    data_cache_hit_writeback(g_monitorScreenVerts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

static inline void fan2_init_uvs(T3DModel* fanModel) {
    // Always refresh vertex pointer from model (may have been reloaded)
    T3DModelIter iter = t3d_model_iter_create(fanModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        if (!obj->numParts) continue;

        T3DObjectPart* part = &obj->parts[0];
        g_fan2Verts = part->vert;
        g_fan2VertCount = part->vertLoadCount;

        // Only allocate base UV buffer once
        if (g_fan2BaseUVs == NULL) {
            g_fan2BaseUVs = malloc(g_fan2VertCount * 2 * sizeof(int16_t));
            if (!g_fan2BaseUVs) {
                debugf("ERROR: Failed to allocate fan2 UV buffer\n");
                g_fan2Verts = NULL;
                g_fan2VertCount = 0;
                return;
            }

            for (int i = 0; i < g_fan2VertCount; i++) {
                int16_t* uv = t3d_vertbuffer_get_uv(g_fan2Verts, i);
                g_fan2BaseUVs[i * 2] = uv[0];
                g_fan2BaseUVs[i * 2 + 1] = uv[1];
            }
        }
        break;
    }
}

static inline void fan2_scroll_uvs(float offset) {
    if (!g_fan2BaseUVs || !g_fan2Verts) return;

    // Scroll U coordinate for vertical rise effect (model UVs may be rotated)
    int16_t scrollAmount = (int16_t)(offset * 32.0f * 32.0f);

    for (int i = 0; i < g_fan2VertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_fan2Verts, i);
        uv[0] = g_fan2BaseUVs[i * 2] - scrollAmount;  // Scroll U instead of V
    }

    int packedCount = (g_fan2VertCount + 1) / 2;
    data_cache_hit_writeback(g_fan2Verts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

static inline void lavaslime_init_uvs(T3DModel* slimeModel) {
    // Always refresh vertex pointer from model (may have been reloaded)
    T3DModelIter iter = t3d_model_iter_create(slimeModel, T3D_CHUNK_TYPE_OBJECT);
    while (t3d_model_iter_next(&iter)) {
        T3DObject* obj = iter.object;
        if (!obj->numParts) continue;

        T3DObjectPart* part = &obj->parts[0];
        g_lavaSlimeVerts = part->vert;
        g_lavaSlimeVertCount = part->vertLoadCount;

        // Only allocate base UV buffer once
        if (g_lavaSlimeBaseUVs == NULL) {
            g_lavaSlimeBaseUVs = malloc(g_lavaSlimeVertCount * 2 * sizeof(int16_t));
            if (!g_lavaSlimeBaseUVs) {
                debugf("ERROR: Failed to allocate lava slime UV buffer\n");
                g_lavaSlimeVerts = NULL;
                g_lavaSlimeVertCount = 0;
                return;
            }

            for (int i = 0; i < g_lavaSlimeVertCount; i++) {
                int16_t* uv = t3d_vertbuffer_get_uv(g_lavaSlimeVerts, i);
                g_lavaSlimeBaseUVs[i * 2] = uv[0];
                g_lavaSlimeBaseUVs[i * 2 + 1] = uv[1];
            }
        }
        break;
    }
}

static inline void lavaslime_scroll_uvs(float offset) {
    if (!g_lavaSlimeBaseUVs || !g_lavaSlimeVerts) return;

    // Scroll both U and V for a swirling lava effect
    int16_t scrollU = (int16_t)(offset * 32.0f * 16.0f);
    int16_t scrollV = (int16_t)(offset * 32.0f * 8.0f);  // Slower V for subtle diagonal flow

    for (int i = 0; i < g_lavaSlimeVertCount; i++) {
        int16_t* uv = t3d_vertbuffer_get_uv(g_lavaSlimeVerts, i);
        uv[0] = g_lavaSlimeBaseUVs[i * 2] + scrollU;
        uv[1] = g_lavaSlimeBaseUVs[i * 2 + 1] + scrollV;
    }

    int packedCount = (g_lavaSlimeVertCount + 1) / 2;
    data_cache_hit_writeback(g_lavaSlimeVerts, packedCount * sizeof(T3DVertPacked));
    rdpq_sync_pipe();  // Sync RDP before drawing with modified UVs
}

#else // !DECO_RENDER_OWNER - provide stub functions for non-owners
// These stubs allow deco_render_single() to compile but do nothing
// (multiplayer.c doesn't need UV scrolling, game.c handles it)
static inline void conveyor_belt_init_uvs(T3DModel* m) { (void)m; }
static inline void conveyor_belt_scroll_uvs(float o) { (void)o; }
static inline void toxic_pipe_liquid_init_uvs(T3DModel* m) { (void)m; }
static inline void toxic_pipe_liquid_scroll_uvs(float o) { (void)o; }
static inline void toxic_running_init_uvs(T3DModel* m) { (void)m; }
static inline void toxic_running_scroll_uvs(float o) { (void)o; }
static inline void lavafloor_init_uvs(T3DModel* m) { (void)m; }
static inline void lavafloor_scroll_uvs(float o) { (void)o; }
static inline void lavafalls_init_uvs(T3DModel* m) { (void)m; }
static inline void lavafalls_scroll_uvs(float o) { (void)o; }
static inline void lvl3stream_init_uvs(T3DModel* m) { (void)m; }
static inline void lvl3stream_scroll_uvs(float o) { (void)o; }
static inline void jukebox_fx_init_uvs(T3DModel* m) { (void)m; }
static inline void jukebox_fx_scroll_uvs(float o) { (void)o; }
static inline void monitor_screen_init_uvs(T3DModel* m) { (void)m; }
static inline void monitor_screen_scroll_uvs(float o) { (void)o; }
static inline void fan2_init_uvs(T3DModel* m) { (void)m; }
static inline void fan2_scroll_uvs(float o) { (void)o; }
static inline void lavaslime_init_uvs(T3DModel* m) { (void)m; }
static inline void lavaslime_scroll_uvs(float o) { (void)o; }
static inline void deco_render_free_uv_data(void) { }
#endif // DECO_RENDER_OWNER

// ============================================================
// MAIN DECORATION DRAW FUNCTION
// ============================================================

// Draw a single decoration with all special handling
// Returns true if the decoration was drawn, false if skipped (invisible, etc.)
static inline bool deco_render_single(
    DecoInstance* deco,
    DecoTypeRuntime* decoType,
    MapRuntime* mapRuntime,
    T3DMat4FP* decoMatFP,
    int frameIdx,
    int matIdx
) {
    if (deco->type == DECO_NONE) return false;
    if (deco->type >= DECO_TYPE_COUNT) return false;  // Bounds check

    // Some decorations should still render when inactive (platforms wait to be activated)
    // Others (like bolts, damage zones) should be hidden when inactive
    bool alwaysRender = (deco->type == DECO_MOVE_PLAT || deco->type == DECO_SINK_PLAT ||
                         deco->type == DECO_ELEVATOR || deco->type == DECO_COG ||
                         deco->type == DECO_HOTPIPE || deco->type == DECO_MOVE_COAL);
    if (!deco->active && !alwaysRender) return false;

    // =================================================================
    // DEBUG: Render DECO_CUTSCENE_FALLOFF with barrel model (magenta tint)
    // In release builds, this decoration is invisible (no model)
    // =================================================================
#ifdef DEBUG
    if (deco->type == DECO_CUTSCENE_FALLOFF && decoType && decoType->model) {
        // Draw barrel scaled to show trigger area size
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){0.0f, deco->rotY, 0.0f},
            (float[3]){deco->posX, deco->posY, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdx]);

        // Use vertex colors with magenta tint for debug visibility
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_set_prim_color(RGBA32(255, 0, 255, 200));  // Magenta, semi-transparent
        t3d_model_draw(decoType->model);

        // Reset combiner for other decorations
        rdpq_sync_pipe();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);

        t3d_matrix_pop(1);
        return true;
    }
#endif

    if (!decoType || !decoType->model) return false;

    // Skip enemies in demo mode - don't render rats, slimes, bulldozers, turrets
    extern bool g_demoMode;
    extern bool g_replayMode;
    if ((g_demoMode || g_replayMode) &&
        (deco->type == DECO_RAT || deco->type == DECO_SLIME || deco->type == DECO_BULLDOZER || deco->type == DECO_TURRET)) {
        return false;
    }

    float renderYOffset = 0.2f;
    const DecoTypeDef* decoDef = &DECO_TYPES[deco->type];

    // =================================================================
    // SIGN: Quaternion rotation for global Z tilt
    // =================================================================
    if (deco->type == DECO_SIGN) {
        fm_quat_t quatY, quatZ, quatFinal;
        fm_vec3_t axisY = {{0, 1, 0}};
        fm_vec3_t axisZ = {{0, 0, 1}};
        fm_quat_from_axis_angle(&quatY, &axisY, deco->state.sign.baseRotY + 3.14159265f);
        fm_quat_from_axis_angle(&quatZ, &axisZ, deco->state.sign.tilt);
        fm_quat_mul(&quatFinal, &quatZ, &quatY);

        t3d_mat4fp_from_srt(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            quatFinal.v,
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
    }
    // =================================================================
    // BOLT: Spinning with pulse
    // =================================================================
    else if (deco->type == DECO_BOLT) {
        if (deco->state.bolt.wasPreCollected) return false;

        float pulse = sinf(deco->state.bolt.spinAngle * 3.0f) * 0.15f + 1.0f;
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX * pulse, deco->scaleY * pulse, deco->scaleZ * pulse},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
    }
    // =================================================================
    // SLIME: Squash/stretch with shear jiggle
    // =================================================================
    else if (deco->type == DECO_SLIME) {
        float jigX = deco->state.slime.jiggleX;
        float jigZ = deco->state.slime.jiggleZ;
        float stretchY = deco->state.slime.stretchY;
        float stretchXZ = 1.0f + (1.0f - stretchY) * 0.5f;
        float slimeYOffset = renderYOffset + 1.0f;

        float shakeOffX = 0.0f, shakeOffZ = 0.0f;
        if (deco->state.slime.shakeTimer > 0.0f) {
            float shakeIntensity = deco->state.slime.shakeTimer * 8.0f * deco->scaleX;
            shakeOffX = sinf(deco->state.slime.shakeTimer * 60.0f) * shakeIntensity;
            shakeOffZ = cosf(deco->state.slime.shakeTimer * 45.0f) * shakeIntensity * 0.7f;
        }

        T3DMat4FP* mat = &decoMatFP[matIdx];
        t3d_mat4fp_from_srt_euler(mat,
            (float[3]){deco->scaleX * stretchXZ, deco->scaleY * stretchY, deco->scaleZ * stretchXZ},
            (float[3]){0.0f, deco->rotY, 0.0f},
            (float[3]){deco->posX + shakeOffX, deco->posY + slimeYOffset, deco->posZ + shakeOffZ}
        );

        // Apply shear transform
        float shearScale = 2.5f;
        float shearX = jigX * shearScale;
        float shearZ = jigZ * shearScale;

        int32_t shearX_fp = (int32_t)(shearX * 65536.0f);
        int32_t shearZ_fp = (int32_t)(shearZ * 65536.0f);

        int32_t existingX = ((int32_t)mat->m[0].i[1] << 16) | mat->m[0].f[1];
        int32_t existingZ = ((int32_t)mat->m[2].i[1] << 16) | mat->m[2].f[1];

        existingX += shearX_fp;
        existingZ += shearZ_fp;

        mat->m[0].i[1] = (int16_t)(existingX >> 16);
        mat->m[0].f[1] = (uint16_t)(existingX & 0xFFFF);
        mat->m[2].i[1] = (int16_t)(existingZ >> 16);
        mat->m[2].f[1] = (uint16_t)(existingZ & 0xFFFF);
    }
    // =================================================================
    // SLIME_LAVA: Squash/stretch with shear jiggle + UV scrolling
    // =================================================================
    else if (deco->type == DECO_SLIME_LAVA) {
        float jigX = deco->state.slime.jiggleX;
        float jigZ = deco->state.slime.jiggleZ;
        float stretchY = deco->state.slime.stretchY;
        float stretchXZ = 1.0f + (1.0f - stretchY) * 0.5f;
        float slimeYOffset = renderYOffset + 1.0f;

        float shakeOffX = 0.0f, shakeOffZ = 0.0f;
        if (deco->state.slime.shakeTimer > 0.0f) {
            float shakeIntensity = deco->state.slime.shakeTimer * 8.0f * deco->scaleX;
            shakeOffX = sinf(deco->state.slime.shakeTimer * 60.0f) * shakeIntensity;
            shakeOffZ = cosf(deco->state.slime.shakeTimer * 45.0f) * shakeIntensity * 0.7f;
        }

        T3DMat4FP* mat = &decoMatFP[matIdx];
        t3d_mat4fp_from_srt_euler(mat,
            (float[3]){deco->scaleX * stretchXZ, deco->scaleY * stretchY, deco->scaleZ * stretchXZ},
            (float[3]){0.0f, deco->rotY, 0.0f},
            (float[3]){deco->posX + shakeOffX, deco->posY + slimeYOffset, deco->posZ + shakeOffZ}
        );

        // Apply shear transform
        float shearScale = 2.5f;
        float shearX = jigX * shearScale;
        float shearZ = jigZ * shearScale;

        int32_t shearX_fp = (int32_t)(shearX * 65536.0f);
        int32_t shearZ_fp = (int32_t)(shearZ * 65536.0f);

        int32_t existingX = ((int32_t)mat->m[0].i[1] << 16) | mat->m[0].f[1];
        int32_t existingZ = ((int32_t)mat->m[2].i[1] << 16) | mat->m[2].f[1];

        existingX += shearX_fp;
        existingZ += shearZ_fp;

        mat->m[0].i[1] = (int16_t)(existingX >> 16);
        mat->m[0].f[1] = (uint16_t)(existingX & 0xFFFF);
        mat->m[2].i[1] = (int16_t)(existingZ >> 16);
        mat->m[2].f[1] = (uint16_t)(existingZ & 0xFFFF);

        // Draw with UV scrolling
        t3d_matrix_push(mat);

        // Initialize and scroll UVs for lava texture effect
        lavaslime_init_uvs(decoType->model);
        lavaslime_scroll_uvs(lavaslime_get_offset());

        // Use standard texture+shade combiner (NOT vertex-colors-only like regular slime)
        // This allows the lava texture to be visible and scrolled
        t3d_model_draw(decoType->model);

        t3d_matrix_pop(1);
        return true;  // Already handled drawing
    }
    // =================================================================
    // ROUNDBUTTON: Base + moving top
    // =================================================================
    else if (deco->type == DECO_ROUNDBUTTON) {
        float baseY = deco->posY + renderYOffset;
        float topY = deco->posY + renderYOffset - 3.0f - deco->state.button.pressDepth;

        int matIdxBottom = matIdx;
        int matIdxTop = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS + 50;

        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxBottom],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, baseY, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdxBottom]);
        t3d_model_draw(decoType->model);
        t3d_matrix_pop(1);

        if (mapRuntime->buttonTopModel) {
            t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxTop],
                (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                (float[3]){deco->posX, topY, deco->posZ}
            );
            t3d_matrix_push(&decoMatFP[matIdxTop]);
            t3d_model_draw(mapRuntime->buttonTopModel);
            t3d_matrix_pop(1);
        }
        return true;  // Already handled drawing
    }
    // =================================================================
    // FAN: Base + rotating top
    // =================================================================
    else if (deco->type == DECO_FAN) {
        float baseY = deco->posY + renderYOffset;

        int matIdxBottom = matIdx;
        int matIdxTop = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS + 51;

        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxBottom],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, baseY, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdxBottom]);
        t3d_model_draw(decoType->model);
        t3d_matrix_pop(1);

        if (mapRuntime->fanTopModel) {
            float spinRotY = deco->rotY + deco->state.fan.spinAngle;
            t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxTop],
                (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                (float[3]){deco->rotX, spinRotY, deco->rotZ},
                (float[3]){deco->posX, baseY, deco->posZ}
            );
            t3d_matrix_push(&decoMatFP[matIdxTop]);
            t3d_model_draw(mapRuntime->fanTopModel);
            t3d_matrix_pop(1);
        }
        return true;  // Already handled drawing
    }
    // =================================================================
    // FAN2: Single model with "gust" animation
    // =================================================================
    else if (deco->type == DECO_FAN2) {
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdx]);
        if (deco->hasOwnSkeleton) {
            t3d_model_draw_skinned(decoType->model, &deco->skeleton);
        } else {
            t3d_model_draw(decoType->model);
        }
        t3d_matrix_pop(1);
        return true;  // Already handled drawing
    }
    // =================================================================
    // CONVEYOR: Frame + scrolling belt
    // =================================================================
    else if (deco->type == DECO_CONVEYERLARGE) {
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdx]);

        t3d_model_draw(decoType->model);

        if (mapRuntime->conveyorBeltModel) {
            conveyor_belt_init_uvs(mapRuntime->conveyorBeltModel);
            bool isActive = (deco->activationId == 0) || activation_get(deco->activationId);
            if (isActive) {
                conveyor_belt_scroll_uvs(conveyor_get_offset());
            }
            t3d_model_draw(mapRuntime->conveyorBeltModel);
        }

        t3d_matrix_pop(1);
        return true;  // Already handled drawing
    }
    // =================================================================
    // TOXICPIPE: Pipe + Running goo (separate matrix slots)
    // Running goo is rotated 180° and has scrolling texture
    // =================================================================
    else if (deco->type == DECO_TOXICPIPE) {
        int matIdxPipe = matIdx;
        int matIdxRunning = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS + 52;

        // Draw pipe with normal rotation
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxPipe],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdxPipe]);
        t3d_model_draw(decoType->model);
        t3d_matrix_pop(1);

        // Draw running goo with +180° rotation and texture scroll (separate slot!)
        if (mapRuntime->toxicPipeRunningModel) {
            t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxRunning],
                (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                (float[3]){deco->rotX, deco->rotY + T3D_DEG_TO_RAD(180.0f), deco->rotZ},
                (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
            );
            t3d_matrix_push(&decoMatFP[matIdxRunning]);
            toxic_running_init_uvs(mapRuntime->toxicPipeRunningModel);
            toxic_running_scroll_uvs(deco->state.toxicPipe.textureOffset);
            t3d_model_draw(mapRuntime->toxicPipeRunningModel);
            t3d_matrix_pop(1);
        }

        return true;  // Already handled drawing
    }
    // =================================================================
    // TOXICRUNNING: Scrolling toxic waste
    // =================================================================
    else if (deco->type == DECO_TOXICRUNNING) {
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdx]);

        toxic_running_init_uvs(decoType->model);
        toxic_running_scroll_uvs(deco->state.toxicRunning.textureOffset);
        t3d_model_draw(decoType->model);

        t3d_matrix_pop(1);
        return true;  // Already handled drawing
    }
    // =================================================================
    // LAVAFLOOR: Scrolling lava texture
    // =================================================================
    else if (deco->type == DECO_LAVAFLOOR) {
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdx]);

        lavafloor_init_uvs(decoType->model);
        lavafloor_scroll_uvs(lavafloor_get_offset());
        t3d_model_draw(decoType->model);

        t3d_matrix_pop(1);
        return true;  // Already handled drawing
    }
    // =================================================================
    // LAVAFALLS: Scrolling lava waterfall texture
    // =================================================================
    else if (deco->type == DECO_LAVAFALLS) {
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdx]);

        lavafalls_init_uvs(decoType->model);
        lavafalls_scroll_uvs(lavafalls_get_offset());
        t3d_model_draw(decoType->model);

        t3d_matrix_pop(1);
        return true;  // Already handled drawing
    }
    // =================================================================
    // LEVEL3_STREAM: Scrolling poison stream texture
    // =================================================================
    else if (deco->type == DECO_LEVEL3_STREAM) {
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdx]);

        lvl3stream_init_uvs(decoType->model);
        lvl3stream_scroll_uvs(lvl3stream_get_offset());
        t3d_model_draw(decoType->model);

        t3d_matrix_pop(1);
        return true;  // Already handled drawing
    }
    // =================================================================
    // JUKEBOX: Main model + FX overlay
    // =================================================================
    else if (deco->type == DECO_JUKEBOX) {
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdx]);

        if (deco->hasOwnSkeleton) {
            t3d_model_draw_skinned(decoType->model, &deco->skeleton);
        } else if (decoType->hasSkeleton) {
            t3d_model_draw_skinned(decoType->model, &decoType->skeleton);
        } else {
            t3d_model_draw(decoType->model);
        }

        if (mapRuntime->jukeboxFxModel) {
            jukebox_fx_init_uvs(mapRuntime->jukeboxFxModel);
            jukebox_fx_scroll_uvs(deco->state.jukebox.textureOffset);
            if (deco->hasOwnSkeleton) {
                t3d_model_draw_skinned(mapRuntime->jukeboxFxModel, &deco->skeleton);
            } else if (decoType->hasSkeleton) {
                t3d_model_draw_skinned(mapRuntime->jukeboxFxModel, &decoType->skeleton);
            } else {
                t3d_model_draw(mapRuntime->jukeboxFxModel);
            }
        }

        t3d_matrix_pop(1);
        return true;  // Already handled drawing
    }
    // =================================================================
    // MONITORTABLE: Table + scrolling screen
    // =================================================================
    else if (deco->type == DECO_MONITORTABLE) {
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdx]);

        t3d_model_draw(decoType->model);

        if (mapRuntime->monitorScreenModel) {
            monitor_screen_init_uvs(mapRuntime->monitorScreenModel);
            monitor_screen_scroll_uvs(deco->state.monitorTable.textureOffset);
            t3d_model_draw(mapRuntime->monitorScreenModel);
        }

        t3d_matrix_pop(1);
        return true;  // Already handled drawing
    }
    // =================================================================
    // TURRET: Rotating cannon (primary) + stationary base + projectiles
    // =================================================================
    else if (deco->type == DECO_TURRET) {
        float baseY = deco->posY + renderYOffset;
        float cannonY = baseY + TURRET_CANNON_HEIGHT * deco->scaleY;

        int matIdxCannon = matIdx;
        int matIdxBase = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS + 53;
        int matIdxProj = frameIdx * MAX_DECORATIONS + MAX_DECORATIONS + 54;

        // Draw cannon (primary model with rotation and animation)
        // Uses quaternion composition so pitch is applied in cannon's local frame (after yaw)
        T3DQuat quatYaw, quatPitch, quatCombined;
        float axisY[3] = {0.0f, 1.0f, 0.0f};
        float axisX[3] = {1.0f, 0.0f, 0.0f};
        t3d_quat_from_rotation(&quatYaw, axisY, -deco->state.turret.cannonRotY);
        t3d_quat_from_rotation(&quatPitch, axisX, deco->state.turret.cannonRotX);
        t3d_quat_mul(&quatCombined, &quatYaw, &quatPitch);  // yaw * pitch = pitch in yaw's frame

        t3d_mat4fp_from_srt(&decoMatFP[matIdxCannon],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            quatCombined.v,
            (float[3]){deco->posX, cannonY, deco->posZ}
        );
        t3d_matrix_push(&decoMatFP[matIdxCannon]);

        // Standard skinned rendering path (animation handled by decoration system)
        if (deco->hasOwnSkeleton) {
            t3d_skeleton_update(&deco->skeleton);
            t3d_model_draw_skinned(decoType->model, &deco->skeleton);
        } else {
            t3d_model_draw(decoType->model);
        }
        t3d_matrix_pop(1);

        // Draw base (stationary, positioned below cannon)
        if (mapRuntime->turretBaseModel) {
            t3d_mat4fp_from_srt_euler(&decoMatFP[matIdxBase],
                (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
                (float[3]){deco->rotX, deco->rotY, deco->rotZ},
                (float[3]){deco->posX, baseY, deco->posZ}
            );
            t3d_matrix_push(&decoMatFP[matIdxBase]);
            t3d_model_draw(mapRuntime->turretBaseModel);
            t3d_matrix_pop(1);
        }

        // Draw active projectiles (only when fired, not held in front of cannon)
        if (mapRuntime->turretRailModel && deco->state.turret.activeProjectiles > 0) {
            // Sync pipeline before drawing projectiles to reset render state
            t3d_tri_sync();
            rspq_flush();

            for (int p = 0; p < TURRET_MAX_PROJECTILES; p++) {
                if (deco->state.turret.projLife[p] <= 0.0f) continue;

                // Use cannon rotation stored at fire time (matches cannon's aim direction)
                // Apply same quaternion composition as cannon for consistent orientation
                float projRotX = deco->state.turret.projRotX[p];
                float projRotY = deco->state.turret.projRotY[p];

                T3DQuat projQuatYaw, projQuatPitch, projQuatCombined;
                float axisY[3] = {0.0f, 1.0f, 0.0f};
                float axisX[3] = {1.0f, 0.0f, 0.0f};
                t3d_quat_from_rotation(&projQuatYaw, axisY, -projRotY);
                t3d_quat_from_rotation(&projQuatPitch, axisX, projRotX);
                t3d_quat_mul(&projQuatCombined, &projQuatYaw, &projQuatPitch);

                // Use dedicated projectile slot (separate from cannon to avoid flickering)
                // Scale increased to 1.5x for better visibility (was 0.5x)
                t3d_mat4fp_from_srt(&decoMatFP[matIdxProj],
                    (float[3]){deco->scaleX * 1.5f, deco->scaleY * 1.5f, deco->scaleZ * 1.5f},
                    projQuatCombined.v,
                    (float[3]){deco->state.turret.projPosX[p], deco->state.turret.projPosY[p], deco->state.turret.projPosZ[p]}
                );
                t3d_matrix_push(&decoMatFP[matIdxProj]);
                t3d_model_draw(mapRuntime->turretRailModel);
                t3d_matrix_pop(1);
            }
        }

        return true;  // Already handled drawing
    }
    // =================================================================
    // DEFAULT: Standard decoration
    // =================================================================
    else {
        t3d_mat4fp_from_srt_euler(&decoMatFP[matIdx],
            (float[3]){deco->scaleX, deco->scaleY, deco->scaleZ},
            (float[3]){deco->rotX, deco->rotY, deco->rotZ},
            (float[3]){deco->posX, deco->posY + renderYOffset, deco->posZ}
        );
    }

    // Standard draw path (for decorations that set up matrix but don't draw)
    t3d_matrix_push(&decoMatFP[matIdx]);

    if (decoDef->vertexColorsOnly) {
        // Sync pipeline before changing combiner mode for vertex-colors-only models
        t3d_tri_sync();
        rdpq_sync_pipe();
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    }

    if (deco->hasOwnSkeleton) {
        t3d_model_draw_skinned(decoType->model, &deco->skeleton);
    } else if (decoType->hasSkeleton) {
        t3d_model_draw_skinned(decoType->model, &decoType->skeleton);
    } else {
        t3d_model_draw(decoType->model);
    }

    if (decoDef->vertexColorsOnly) {
        // Sync and reset combiner after vertex-colors-only model so next textured model works correctly
        t3d_tri_sync();
        rdpq_sync_pipe();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    }

    t3d_matrix_pop(1);
    return true;
}

// ============================================================
// CLEANUP FUNCTION (UV scrolling data)
// ============================================================

#ifdef DECO_RENDER_OWNER
// Free all UV scrolling data (call on scene deinit)
static inline void deco_render_free_uv_data(void) {
    // CRITICAL: Ensure RSP/RDP finished with vertex data before freeing.
    // The RSP may still have pending DMA reads from these vertex buffers.
    // Use rdpq_sync_full + rspq_flush instead of rspq_wait() to avoid RDP hardware bug.
    rdpq_sync_full(NULL, NULL);
    rspq_flush();

    // Conveyor belt
    if (g_conveyorBaseUVs) {
        free(g_conveyorBaseUVs);
        g_conveyorBaseUVs = NULL;
    }
    g_conveyorVertCount = 0;
    g_conveyorVerts = NULL;

    // Toxic pipe liquid
    if (g_toxicPipeLiquidBaseUVs) {
        free(g_toxicPipeLiquidBaseUVs);
        g_toxicPipeLiquidBaseUVs = NULL;
    }
    g_toxicPipeLiquidVertCount = 0;
    g_toxicPipeLiquidVerts = NULL;

    // Toxic running
    if (g_toxicRunningBaseUVs) {
        free(g_toxicRunningBaseUVs);
        g_toxicRunningBaseUVs = NULL;
    }
    g_toxicRunningVertCount = 0;
    g_toxicRunningVerts = NULL;

    // Lava floor
    if (g_lavaFloorBaseUVs) {
        free(g_lavaFloorBaseUVs);
        g_lavaFloorBaseUVs = NULL;
    }
    g_lavaFloorVertCount = 0;
    g_lavaFloorVerts = NULL;

    // Lava falls
    if (g_lavaFallsBaseUVs) {
        free(g_lavaFallsBaseUVs);
        g_lavaFallsBaseUVs = NULL;
    }
    g_lavaFallsVertCount = 0;
    g_lavaFallsVerts = NULL;

    // Jukebox FX
    for (int p = 0; p < g_jukeboxFxPartCount; p++) {
        if (g_jukeboxFxBaseUVs[p]) {
            free(g_jukeboxFxBaseUVs[p]);
            g_jukeboxFxBaseUVs[p] = NULL;
        }
        g_jukeboxFxVertCount[p] = 0;
        g_jukeboxFxVerts[p] = NULL;
    }
    g_jukeboxFxPartCount = 0;

    // Monitor screen
    if (g_monitorScreenBaseUVs) {
        free(g_monitorScreenBaseUVs);
        g_monitorScreenBaseUVs = NULL;
    }
    g_monitorScreenVertCount = 0;
    g_monitorScreenVerts = NULL;

    // Lava slime
    if (g_lavaSlimeBaseUVs) {
        free(g_lavaSlimeBaseUVs);
        g_lavaSlimeBaseUVs = NULL;
    }
    g_lavaSlimeVertCount = 0;
    g_lavaSlimeVerts = NULL;

    // Fan2
    if (g_fan2BaseUVs) {
        free(g_fan2BaseUVs);
        g_fan2BaseUVs = NULL;
    }
    g_fan2VertCount = 0;
    g_fan2Verts = NULL;

    // Level 3 stream (multiple parts)
    for (int i = 0; i < LVL3STREAM_MAX_PARTS; i++) {
        if (g_lvl3StreamBaseUVsArr[i]) {
            free(g_lvl3StreamBaseUVsArr[i]);
            g_lvl3StreamBaseUVsArr[i] = NULL;
        }
        g_lvl3StreamVertCountArr[i] = 0;
        g_lvl3StreamVertsArr[i] = NULL;
    }
    g_lvl3StreamPartCount = 0;
}
#endif // DECO_RENDER_OWNER (cleanup function)

#endif // DECO_RENDER_H
