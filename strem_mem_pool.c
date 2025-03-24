#include <stddef.h>
#include <assert.h>
#include <string.h>
#include "strem_mem_pool.h"
//#define DD

static void chain(StremMemPool_Seg* const hole, void* const next_hole) {
	*(void**)(&hole->content) = next_hole;
}

static void chain_or_prepend(StremMemPool* pool, StremMemPool_Seg* const hole, void* const next_hole) {
	if(hole == NULL) {
		pool->free_seg = next_hole;
	} else {
		chain(hole, next_hole);
	}
}

static size_t seg_size(StremMemPool_Seg const* const seg) {
	return sizeof(StremMemPool_Seg) + seg->size;
}

static StremMemPool_Seg* content_to_seg(void* content_ptr) {
	return (StremMemPool_Seg*)((char*)content_ptr - offsetof(StremMemPool_Seg, content));
}

static StremMemPool_Seg* nexthole(StremMemPool_Seg* hole) {
	return *(StremMemPool_Seg**)hole->content;
}

// Assumes asize fits into hole
// Returns address of second hole
static StremMemPool_Seg* split(StremMemPool_Seg* hole, size_t asize) {
	assert(asize + sizeof(StremMemPool_Seg) <= hole->size && "can't fit two blocks");

	const size_t second_size = hole->size - asize - sizeof(StremMemPool_Seg);
	void* content = *(void**)hole->content;

	hole->size = asize;
	hole = (StremMemPool_Seg*)((char*)hole + seg_size(hole));

	if(second_size != 0) {
		hole->size = second_size;
		chain(hole, content);
	}

	return hole;
}

typedef struct {
	StremMemPool_Seg* prev_hole;
	StremMemPool_Seg* this_hole;
} HolePair;

static HolePair first_fit(StremMemPool_Seg* const first_free, size_t asize) {
	StremMemPool_Seg* prev_hole = NULL;
	StremMemPool_Seg* hole = first_free;

	while(hole != NULL && hole->size < asize) {
		prev_hole = hole;
		hole = *(StremMemPool_Seg**)hole->content;
	}

	if(hole == NULL) {
		return (HolePair){ NULL, NULL};
	} else {
		return (HolePair){ prev_hole, hole };
	}
}

#if STREM_UNUSED
static HolePair best_fit(StremMemPool_Seg* const first_free, size_t asize) {
	StremMemPool_Seg* prev_hole;
	StremMemPool_Seg* hole = first_free;
	StremMemPool_Seg* best = NULL;
	StremMemPool_Seg* prev_best = NULL; /* previous node of best node*/
	size_t mdiff = STREM_SIZE_MAX;

	while(hole != NULL) {
		if(hole->size <= asize) {
			const size_t diff = asize - hole->size;
			if(diff < mdiff) {
				mdiff = diff;
				prev_best = prev_hole;
				best = hole;
			}
		}
		prev_hole = hole;
		hole = *(StremMemPool_Seg**)hole->content;
	}

	if(best == NULL) {
		return (HolePair){ NULL, NULL};
	} else {
		return (HolePair){ prev_best, best };
	}
}
#endif // STREM_UNUSED

StremMemPool StremMemPool_emplace_pool(void* at, size_t aligned_cap) {
	assert(sizeof(StremMemPool_Seg) % sizeof(size_t) == 0 && "sizeof StremMemPool_Seg must align to sizeof size_t");
	assert(aligned_cap % sizeof(size_t) == 0 && "StremMemPool_emplace_pool: capacity must align to sizeof size_t");
	assert(aligned_cap > sizeof(StremMemPool_Seg) && "StremMemPool_emplace_pool: capacity must be > sizeof StremMemPool_Seg");

	const size_t content_cap = aligned_cap - sizeof(StremMemPool_Seg); 
	StremMemPool pool;
	pool.segs = at;
	pool.mem_free = content_cap;
	pool.free_seg = (StremMemPool_Seg*)pool.segs;
	pool.free_seg->size = content_cap;
	chain(pool.free_seg, NULL);

	return pool;
}

void* StremMemPool_alloc(StremMemPool* pool, size_t aligned_size) {
	assert(aligned_size % sizeof(size_t) == 0 && "StremMemPool_alloc: size must align to sizeof size_t");
	assert((pool->free_seg == NULL) == (pool->mem_free == 0));

	if(pool->mem_free < aligned_size) {
		return NULL;
	}

	HolePair pair = first_fit(pool->free_seg, aligned_size);

	if(pair.this_hole == NULL) {
		return NULL;
	}

	StremMemPool_Seg* next_hole = nexthole(pair.this_hole);

	if(aligned_size + sizeof(StremMemPool_Seg) == pair.this_hole->size) { 
		// If next allocation will have enough memory only for service info
		// TODO: I don't like it because it allocates more than user asks for.
		aligned_size += sizeof(StremMemPool_Seg); 
	} else if(aligned_size + sizeof(StremMemPool_Seg) > pool->mem_free) {
		return NULL;
	}
	
	if(pair.this_hole->size != aligned_size) { /* chain splitted hole and next hole */
		StremMemPool_Seg* next_next_hole = next_hole;
		next_hole = split(pair.this_hole, aligned_size);
		chain(next_hole, next_next_hole);

		pool->mem_free -= seg_size(pair.this_hole); /* sizeof content and next adj StremMemPool_Seg */
	} else {
		pool->mem_free -= aligned_size;
	}

	chain_or_prepend(pool, pair.prev_hole, next_hole);

	if(pool->mem_free == 0) {
		pool->free_seg = NULL;
	}
	return pair.this_hole->content;
}

static StremMemPool_Seg* StremMemPool_free_(StremMemPool* pool, void** sorted_ptrs, size_t count);

void* StremMemPool_realloc(StremMemPool* pool, void* content_ptr, size_t aligned_size) {
	/* service info in mem_free is counted by free_ and alloc functions. Otherwise, counted by hand */
	StremMemPool_Seg* const to_free = content_to_seg(content_ptr);
	StremMemPool_Seg* res = NULL;
	// needed because in free_ and split functions we change first bytes of content
	void* const content_small_buf = *(void**)content_ptr;
	size_t size_diff = 0; // for accounting sizeof(StremMemPool_Seg), not seg->size

	if(aligned_size < to_free->size) {
		StremMemPool_Seg* remainder = split(to_free, aligned_size);
		StremMemPool_free(pool, (void**)&remainder, 1);
		return to_free->content;
	}  

	if(aligned_size > pool->mem_free + seg_size(to_free)) { // use seg_size because can merge adjacent
		return NULL;
	}

	StremMemPool_Seg* const prev = StremMemPool_free_(pool, &content_ptr, 1);
	StremMemPool_Seg* freed = prev == NULL ? to_free : nexthole(prev); // may not be equal to to_free

	if(prev != NULL && (char*)prev + seg_size(prev) > (char*)to_free) { /* merged with freed */
		const size_t prev_new_size = (uintptr_t)to_free - (uintptr_t)prev - sizeof(StremMemPool_Seg);
		const size_t new_size = prev->size - sizeof(StremMemPool_Seg) - prev_new_size;

		if(new_size >= aligned_size) {
			freed = split(prev, prev_new_size);
		} else if((res = StremMemPool_alloc(pool, aligned_size)) == NULL) {
			freed = split(prev, prev_new_size);
			chain(prev, nexthole(freed));
			*(void**)freed->content = content_small_buf;

			pool->mem_free -= new_size + sizeof(StremMemPool_Seg); // + sizeof(...) because split
			return NULL;
		} else {
			res = content_to_seg(res);
			memmove(res->content, content_ptr, aligned_size);
			*(void**)res->content = content_small_buf;
			return res->content;
		}
	}

	if(aligned_size < freed->size && aligned_size + sizeof(StremMemPool_Seg) >= freed->size) {
		aligned_size = freed->size; 
	}

	if(freed->size > aligned_size) {
		StremMemPool_Seg* const righthole = nexthole(freed);
		StremMemPool_Seg* const midhole = split(freed, aligned_size);

		chain_or_prepend(pool, prev, midhole);
		chain(midhole, righthole);

		size_diff += sizeof(StremMemPool_Seg); // because split
		res = freed;
	} else if(freed->size == aligned_size) {
		chain_or_prepend(pool, prev, nexthole(freed));
		res = freed;
	}

	if(res == NULL) {
		if((res = StremMemPool_alloc(pool, aligned_size)) == NULL) {
			chain_or_prepend(pool, prev, nexthole(freed));
			*(void**)freed->content = content_small_buf;
			pool->mem_free -= freed->size;
			return NULL;
		}
		res = content_to_seg(res);
	} else {
		pool->mem_free -= aligned_size + size_diff;
	}

	memmove(res->content, freed->content, aligned_size);
	*(void**)res->content = content_small_buf;

	return res->content;
}

// returns the rightmost hole got after merging or not merging
static StremMemPool_Seg* merge_if_adjacent(StremMemPool_Seg* left_hole, StremMemPool_Seg* right_hole, size_t* pool_mem_free) {
	assert(left_hole < right_hole);

	if((char*)left_hole + seg_size(left_hole) == (char*)right_hole) {
		left_hole->size += seg_size(right_hole);
		chain(left_hole, *(void**)right_hole->content);

		*pool_mem_free += sizeof(StremMemPool_Seg);
		return left_hole;
	}
	return right_hole;
}

// doesn't change contents pointed by sorted_ptrs
void StremMemPool_free(StremMemPool* pool, void** sorted_ptrs, size_t count) {
	(void)StremMemPool_free_(pool, sorted_ptrs, count);
}

// returns address of free segment which points to last newly freed segment
// returned free segment may be already merged with last newly freed segment.
static StremMemPool_Seg* StremMemPool_free_(StremMemPool* pool, void** sorted_ptrs, size_t count) {
	StremMemPool_Seg* prev_last_less = NULL; // for prev content_ptr
	StremMemPool_Seg* last_less = NULL; // for current content_ptr
	StremMemPool_Seg* first_greater = pool->free_seg; // for current content_ptr
	StremMemPool_Seg* seg;
	
	for(size_t i = 1; i < count; i++) {
		assert(sorted_ptrs[i - 1] < sorted_ptrs[i]);
	}

	for(size_t i = 0; i < count; i++) {
		seg = content_to_seg(sorted_ptrs[i]);
		pool->mem_free += seg->size;

		while(first_greater != NULL && seg > first_greater) {
			last_less = first_greater;
			first_greater = *(StremMemPool_Seg**)first_greater->content;
		}
		prev_last_less = last_less;

		if(first_greater == NULL) {
			if(last_less == NULL) {
				pool->free_seg = seg;
				chain(pool->free_seg, NULL);
				last_less = pool->free_seg;
			} else {
				chain(seg, NULL);
				chain(last_less, seg);
				last_less = merge_if_adjacent(last_less, seg, &pool->mem_free);
			}
		} else {
			if(last_less == NULL) {
				chain(seg, first_greater);
				pool->free_seg = seg;
				merge_if_adjacent(seg, first_greater, &pool->mem_free);
				last_less = seg;
			} else {
				chain(last_less, seg);
				chain(seg, first_greater);
				StremMemPool_Seg* const left_or_mid = merge_if_adjacent(last_less, seg, &pool->mem_free);
				merge_if_adjacent(left_or_mid, first_greater, &pool->mem_free);

				last_less = left_or_mid;
			}
		}
	}

	return prev_last_less;
}
