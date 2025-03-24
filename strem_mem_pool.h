#ifndef STREM_MEM_POOL_H_
#define STREM_MEM_POOL_H_
#include "strem_common.h"

typedef struct {
	size_t size;
	char content[]; /* If free, contains pointer to next free node */
} StremMemPool_Seg;

// In-place pool, focused on low fragmentation.
// Stores service info about free segments in content memory.
// Space: sizeof(size_t) per segment;
// Complexity: alloc/realloc/free = O(n);
// Unsafe: doesn't check if to-free/realloc chunks are really allocated.
typedef struct {
	/* private: */
	size_t mem_free;
	StremMemPool_Seg* free_seg; /* Beginning of free chain */
	char* segs;
} StremMemPool;

// Reinterprets passed memory block as an empty pool's content
// Must: cap aligns to sizeof(size_t) and > sizeof(StremMemPool_Seg)
// Returns passed address as a pointer to the empty pool
StremMemPool StremMemPool_emplace_pool(void* at, size_t aligned_cap);

// Allocates memory of aligned_size + sizeof(StremMemPool_Seg)
// Returns NULL if not enough free memory
void* StremMemPool_alloc(StremMemPool* pool, size_t aligned_size);

// Extends allocated chunk to fit content's aligned_size.
// Returns NULL if not enough free memory, chunk contents stay unaffected.
void* StremMemPool_realloc(StremMemPool* pool, void* at, size_t aligned_size);

// Frees batch of ascendingly sorted pointers to allocated blocks.
// Warning: Double free yields UB
void StremMemPool_free(StremMemPool* pool, void** sorted_ptrs, size_t count);

#endif // STREM_MEM_POOL_H_
