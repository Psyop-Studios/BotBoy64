// ============================================================
// MEMORY DEBUGGING HELPERS
// Canary-based corruption detection for N64
// ============================================================
// Usage:
//   1. Define MEM_DEBUG_ENABLED before including this header to enable
//   2. Use MEM_GUARD_ALLOC/FREE instead of malloc/free for tracked allocations
//   3. Call mem_debug_check_all() periodically to detect corruption
//   4. Call mem_debug_dump() to see allocation stats
// ============================================================

#ifndef MEM_DEBUG_H
#define MEM_DEBUG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Enable/disable memory debugging (comment out for release)
// #define MEM_DEBUG_ENABLED

#ifdef MEM_DEBUG_ENABLED

#include <libdragon.h>

#define MEM_CANARY_HEAD 0xDEADBEEF
#define MEM_CANARY_TAIL 0xCAFEBABE
#define MEM_FREED_FILL  0xFE  // Fill freed memory with this
#define MEM_ALLOC_FILL  0xCD  // Fill new allocations with this

#define MAX_TRACKED_ALLOCS 256

typedef struct {
    void* ptr;           // User pointer (after header)
    void* real_ptr;      // Actual malloc'd pointer
    size_t size;         // User requested size
    const char* file;    // Source file
    int line;            // Source line
    uint32_t alloc_id;   // Allocation ID
    bool active;         // Is this slot in use
} MemAlloc;

static MemAlloc g_mem_allocs[MAX_TRACKED_ALLOCS];
static uint32_t g_mem_alloc_counter = 0;
static bool g_mem_debug_initialized = false;

// Header/footer structure for guarded allocations
typedef struct {
    uint32_t canary;
    uint32_t size;
    uint32_t alloc_id;
} MemGuardHeader;

typedef struct {
    uint32_t canary;
} MemGuardFooter;

static inline void mem_debug_init(void) {
    if (g_mem_debug_initialized) return;
    memset(g_mem_allocs, 0, sizeof(g_mem_allocs));
    g_mem_alloc_counter = 0;
    g_mem_debug_initialized = true;
    debugf("[MEM_DEBUG] Initialized with %d tracking slots\n", MAX_TRACKED_ALLOCS);
}

// Find a free slot in the tracking array
static inline int mem_debug_find_slot(void) {
    for (int i = 0; i < MAX_TRACKED_ALLOCS; i++) {
        if (!g_mem_allocs[i].active) return i;
    }
    return -1;
}

// Find an allocation by user pointer
static inline int mem_debug_find_alloc(void* ptr) {
    for (int i = 0; i < MAX_TRACKED_ALLOCS; i++) {
        if (g_mem_allocs[i].active && g_mem_allocs[i].ptr == ptr) return i;
    }
    return -1;
}

// Guarded malloc - adds canaries before and after allocation
static inline void* mem_guard_alloc(size_t size, const char* file, int line) {
    if (!g_mem_debug_initialized) mem_debug_init();

    size_t total_size = sizeof(MemGuardHeader) + size + sizeof(MemGuardFooter);
    void* real_ptr = malloc(total_size);
    if (!real_ptr) {
        debugf("[MEM_DEBUG] ALLOC FAILED: %zu bytes at %s:%d\n", size, file, line);
        return NULL;
    }

    // Fill with pattern to detect uninitialized reads
    memset(real_ptr, MEM_ALLOC_FILL, total_size);

    // Setup header
    MemGuardHeader* header = (MemGuardHeader*)real_ptr;
    header->canary = MEM_CANARY_HEAD;
    header->size = size;
    header->alloc_id = ++g_mem_alloc_counter;

    // Setup footer
    void* user_ptr = (uint8_t*)real_ptr + sizeof(MemGuardHeader);
    MemGuardFooter* footer = (MemGuardFooter*)((uint8_t*)user_ptr + size);
    footer->canary = MEM_CANARY_TAIL;

    // Track allocation
    int slot = mem_debug_find_slot();
    if (slot >= 0) {
        g_mem_allocs[slot].ptr = user_ptr;
        g_mem_allocs[slot].real_ptr = real_ptr;
        g_mem_allocs[slot].size = size;
        g_mem_allocs[slot].file = file;
        g_mem_allocs[slot].line = line;
        g_mem_allocs[slot].alloc_id = header->alloc_id;
        g_mem_allocs[slot].active = true;
    } else {
        debugf("[MEM_DEBUG] WARNING: No tracking slot available!\n");
    }

    debugf("[MEM_DEBUG] ALLOC #%u: %zu bytes at %p (%s:%d)\n",
           header->alloc_id, size, user_ptr, file, line);

    return user_ptr;
}

// Guarded free - checks canaries before freeing
static inline void mem_guard_free(void* ptr, const char* file, int line) {
    if (!ptr) return;

    MemGuardHeader* header = (MemGuardHeader*)((uint8_t*)ptr - sizeof(MemGuardHeader));

    // Check header canary
    if (header->canary != MEM_CANARY_HEAD) {
        debugf("[MEM_DEBUG] CORRUPTION! Head canary destroyed at %p (expected 0x%08X, got 0x%08X) - freed at %s:%d\n",
               ptr, MEM_CANARY_HEAD, header->canary, file, line);
        // Don't free - memory is corrupt
        return;
    }

    // Check footer canary
    MemGuardFooter* footer = (MemGuardFooter*)((uint8_t*)ptr + header->size);
    if (footer->canary != MEM_CANARY_TAIL) {
        debugf("[MEM_DEBUG] CORRUPTION! Tail canary destroyed at %p (expected 0x%08X, got 0x%08X) - buffer overflow? - freed at %s:%d\n",
               ptr, MEM_CANARY_TAIL, footer->canary, file, line);
        // Don't free - memory is corrupt
        return;
    }

    // Remove from tracking
    int slot = mem_debug_find_alloc(ptr);
    if (slot >= 0) {
        debugf("[MEM_DEBUG] FREE #%u: %zu bytes at %p (%s:%d)\n",
               g_mem_allocs[slot].alloc_id, g_mem_allocs[slot].size, ptr, file, line);
        g_mem_allocs[slot].active = false;
    } else {
        debugf("[MEM_DEBUG] WARNING: Freeing untracked pointer %p at %s:%d\n", ptr, file, line);
    }

    // Fill with freed pattern before freeing
    size_t total_size = sizeof(MemGuardHeader) + header->size + sizeof(MemGuardFooter);
    memset(header, MEM_FREED_FILL, total_size);

    free(header);
}

// Check all tracked allocations for corruption
static inline int mem_debug_check_all(void) {
    int errors = 0;

    for (int i = 0; i < MAX_TRACKED_ALLOCS; i++) {
        if (!g_mem_allocs[i].active) continue;

        void* ptr = g_mem_allocs[i].ptr;
        MemGuardHeader* header = (MemGuardHeader*)((uint8_t*)ptr - sizeof(MemGuardHeader));
        MemGuardFooter* footer = (MemGuardFooter*)((uint8_t*)ptr + g_mem_allocs[i].size);

        if (header->canary != MEM_CANARY_HEAD) {
            debugf("[MEM_DEBUG] CORRUPTION! Alloc #%u head canary destroyed (ptr=%p, from %s:%d)\n",
                   g_mem_allocs[i].alloc_id, ptr, g_mem_allocs[i].file, g_mem_allocs[i].line);
            errors++;
        }

        if (footer->canary != MEM_CANARY_TAIL) {
            debugf("[MEM_DEBUG] CORRUPTION! Alloc #%u tail canary destroyed (ptr=%p, size=%zu, from %s:%d) - BUFFER OVERFLOW!\n",
                   g_mem_allocs[i].alloc_id, ptr, g_mem_allocs[i].size, g_mem_allocs[i].file, g_mem_allocs[i].line);
            errors++;
        }
    }

    if (errors > 0) {
        debugf("[MEM_DEBUG] Found %d corrupted allocation(s)!\n", errors);
    }

    return errors;
}

// Dump all tracked allocations
static inline void mem_debug_dump(void) {
    int active = 0;
    size_t total_bytes = 0;

    debugf("[MEM_DEBUG] === Allocation Dump ===\n");
    for (int i = 0; i < MAX_TRACKED_ALLOCS; i++) {
        if (!g_mem_allocs[i].active) continue;
        active++;
        total_bytes += g_mem_allocs[i].size;
        debugf("  #%u: %zu bytes at %p (%s:%d)\n",
               g_mem_allocs[i].alloc_id, g_mem_allocs[i].size,
               g_mem_allocs[i].ptr, g_mem_allocs[i].file, g_mem_allocs[i].line);
    }
    debugf("[MEM_DEBUG] Total: %d allocations, %zu bytes\n", active, total_bytes);
}

// Macros for convenient usage
#define MEM_GUARD_ALLOC(size) mem_guard_alloc(size, __FILE__, __LINE__)
#define MEM_GUARD_FREE(ptr) mem_guard_free(ptr, __FILE__, __LINE__)
#define MEM_DEBUG_CHECK() mem_debug_check_all()
#define MEM_DEBUG_DUMP() mem_debug_dump()

#else  // MEM_DEBUG_ENABLED not defined

// No-op versions when debugging is disabled
#define MEM_GUARD_ALLOC(size) malloc(size)
#define MEM_GUARD_FREE(ptr) free(ptr)
#define MEM_DEBUG_CHECK() 0
#define MEM_DEBUG_DUMP() ((void)0)
#define mem_debug_init() ((void)0)
#define mem_debug_check_all() 0

#endif  // MEM_DEBUG_ENABLED

#endif  // MEM_DEBUG_H
