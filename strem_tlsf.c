#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define STREM_TLSF_DEFINE_LARGEST_SIZE
#include "strem_tlsf.h"

#define STREM_TLSF_LAST_BIT ((size_t)1)
#define STREM_TLSF_FREE_BIT ((size_t)0)
#define STREM_TLSF_LAST_MASK (1 << STREM_TLSF_LAST_BIT)
#define STREM_TLSF_FREE_MASK (1 << STREM_TLSF_FREE_BIT)
#define STREM_TLSF_SIZE_MASK (~(1 << STREM_TLSF_LAST_BIT | 1 << STREM_TLSF_FREE_BIT))

#define STREM_TLSF_CONTENT_MINSIZE (2*sizeof(void*))

#if 0
struct StremTLSFFree {
	StremTLSFBlock* prev;
	size_t size;
	struct StremTLSFFree* fprev;
	struct StremTLSFFree* fnext;
};
#endif

#define STREM_UNSET_BIT(bmap, off) (bmap) = (bmap) & ~(1 << off)
#define STREM_SET_BIT(bmap, off) (bmap) = (bmap) | (1 << off)
#define STREM_SET_BIT_TO(bmap, off, val) (bmap) = ((bmap) & ~(1 << off)) | (val << off)
#define STREM_GET_BIT(bmap, off) ((bmap) & (1 << off))

typedef enum {
	TLSF_FPREV = 0,
	TLSF_FNEXT = 1,
} StremTLSF_FreeOffset;

/************************************** SIZE FUNCTIONS ************************************/
static size_t rounded(const size_t asize, const size_t block_size) {
	assert(asize % STREM_TLSF_CONTENT_MINSIZE == 0 && "must align for flag storage");
	assert(block_size % STREM_TLSF_CONTENT_MINSIZE == 0 && "must align for flag storage");
	assert(asize <= block_size && "can't round to smaller block");

	if(asize + sizeof(StremTLSFBlock) + STREM_TLSF_CONTENT_MINSIZE > block_size) {
		return block_size;
	}
	return asize;
}

#ifdef STREM_PTR64
#define FLS(size) (fls64_f(size))
static int fls64_f(uint64_t size) {
	return sizeof(size)*8 - __builtin_clzll(size) - 1;
}

#elif defined(STREM_PTR32)
#define FLS(size) (fls32_f(size))
static int fls32_f(uint32_t size) {
	return sizeof(size)*8 - __builtin_clz(size) - 1;
}
#endif

#define FFS32(size) (__builtin_ffs(size) - 1)

/*********************************** BLOCK-SIZE FUNCTIONS *********************************/
static void block_set_free(StremTLSFBlock* const b, const bool flag) {
	b->size = (b->size & ~STREM_TLSF_FREE_MASK) | (flag << STREM_TLSF_FREE_BIT);
}

static void block_set_last(StremTLSFBlock* const b, const bool flag) {
	b->size = (b->size & ~STREM_TLSF_LAST_MASK) | (flag << STREM_TLSF_LAST_BIT);
}

static bool block_is_last(StremTLSFBlock const* const b) {
	return b->size & STREM_TLSF_LAST_MASK;
}

static bool block_is_free(StremTLSFBlock const* const b) {
	return b->size & STREM_TLSF_FREE_MASK;
}

static size_t block_size(StremTLSFBlock const* const b) {
	return b->size & STREM_TLSF_SIZE_MASK;
}

static size_t block_struct_size(StremTLSFBlock const* const b) {
	return block_size(b) + sizeof(StremTLSFBlock);
}

static void block_set_size(StremTLSFBlock* const b, const size_t asize) {
	assert(asize % STREM_TLSF_CONTENT_MINSIZE == 0 && "must align for flag storage");
	b->size = (asize & STREM_TLSF_SIZE_MASK) | (b->size & ~STREM_TLSF_SIZE_MASK);
}

/*********************************** BLOCK-CHAIN FUNCTIONS *********************************/
static StremTLSFBlock* block_next(StremTLSFBlock* const block) {
	assert(!block_is_last(block) && "can't get block after last one");

	return (StremTLSFBlock*)(block->content + block_size(block));
}

static StremTLSFBlock const* block_next_const(StremTLSFBlock const* const block) {
	assert(!block_is_last(block) && "can't get block after last one");

	return (StremTLSFBlock*)(block->content + block_size(block));
}


static void block_chain(StremTLSFBlock* const b) {
	assert(!block_is_last(b) && "can't chain last block");

	block_next(b)->prev = b;
}

static bool block_adjacent(StremTLSFBlock const* const f, StremTLSFBlock const* const s) {
	assert(f != NULL && s != NULL);
	assert(f < s);

	return block_next_const(f) == s;
}

static StremTLSFBlock* block_from_content(void* const content) {
	return (StremTLSFBlock*)((char*)content - offsetof(StremTLSFBlock, content));
}

/********************************* FREE BLOCK-CHAIN FUNCTIONS ********************************/
static void set_free_neigh(
	StremTLSFBlock* const block, 
	StremTLSF_FreeOffset const neigh_type,
	StremTLSFBlock* const neigh
) {
	assert(block != NULL);
	assert(block_is_free(block) && "can't set free neigh if block is not free");

	((StremTLSFBlock**)block->content)[neigh_type] = neigh;
}

static StremTLSFBlock* get_free_neigh(
	StremTLSFBlock* const block, 
	StremTLSF_FreeOffset const neigh_type
) {
	assert(block != NULL);

	return ((StremTLSFBlock**)block->content)[neigh_type];
}

static void chain_free(StremTLSFBlock* const f, StremTLSFBlock* const s) {
	set_free_neigh(f, TLSF_FNEXT, s);
	set_free_neigh(s, TLSF_FPREV, f);
}

static StremTLSFBlock* split_free(StremTLSFBlock* const block, const size_t asize) {
	assert(asize % STREM_TLSF_CONTENT_MINSIZE == 0 && "must align for flag storage");
	assert(block->size > asize + sizeof(StremTLSFBlock) && "can't fit two blocks");

	const size_t nsize = block->size - asize - sizeof(StremTLSFBlock);
	const bool waslast = block_is_last(block);
	block_set_size(block, asize);
	block_set_last(block, false);

	StremTLSFBlock* const next = block_next(block);

	next->size = (nsize & STREM_TLSF_SIZE_MASK)
		| (1 << STREM_TLSF_FREE_BIT)
		| (waslast << STREM_TLSF_LAST_BIT);
	
	block_chain(block);
	
	if(!block_is_last(next)) {
		block_chain(next);
	}
	
	return next;
}

static void merge_free(StremTLSFBlock* const f, StremTLSFBlock* const s) {
	assert(block_adjacent(f, s) && "can merge only adjacent");

	block_set_size(f, block_size(f) + block_struct_size(s));

	if(!block_is_last(f)) {
		block_chain(f);
	}
}

/*********************************** TLSF FUNCTIONS *********************************/
static void mapping(const size_t asize, uint8_t* const fl, uint8_t* const sl) {
	if(asize < STREM_TLSF_CONTENT_MINSIZE) {
		*fl = *sl = 0;
		return;
	}

	{
		// taken from:
		// https://www.researchgate.net/publication/4080369_TLSF_A_new_dynamic_memory_allocator_for_real-time_systems
		*fl = FLS(asize);
		*sl = ((asize ^ (1<<*fl)) >> (*fl - STREM_TLSF_SLI));
	}
	
	*fl -= STREM_TLSF_CLASS_OFFSET;
}

static void StremTLSF_insert(
	StremTLSF* const t,
       	StremTLSFBlock* const block, 
	uint8_t const fl, 
	uint8_t const sl
) {
	assert(block_is_free(block) && "can insert only free block");
	StremTLSFBlock* chain = t->blocks[fl][sl];
	t->blocks[fl][sl] = block;
	
	if(chain != NULL) {
		chain_free(block, chain);
		set_free_neigh(block, TLSF_FPREV, NULL);
	} else {
		STREM_SET_BIT(t->fbmap, fl);
		STREM_SET_BIT(t->sbmap[fl], sl);
		set_free_neigh(block, TLSF_FPREV, NULL);
		set_free_neigh(block, TLSF_FNEXT, NULL);
	}
}

static void StremTLSF_split(
	StremTLSF* const t, 
	StremTLSFBlock* const block, 
	size_t const asize
) {
	if(block->size == asize) {
		return;
	}

	StremTLSFBlock* const next = split_free(block, asize);
	uint8_t fl, sl;
	mapping(next->size, &fl, &sl);

	StremTLSF_insert(t, next, fl, sl);
}

static void StremTLSF_remove_free(
	StremTLSF* const t, 
	StremTLSFBlock* const block,
	uint8_t const fl, 
	uint8_t const sl
) {
	assert(fl < STREM_TLSF_FL_SIZE && sl < STREM_TLSF_SL_SIZE && "wrong level codes");
	assert(
		STREM_GET_BIT(t->fbmap, fl) != 0 
		&& STREM_GET_BIT(t->sbmap[fl], sl) != 0
		&& "bitmaps count this block as taken already"
	);
	assert(block_is_free(block) && "block status is 'not free'");

	block_set_free(block, false);

	StremTLSFBlock* const fprev = get_free_neigh(block, TLSF_FPREV);
	StremTLSFBlock* const fnext = get_free_neigh(block, TLSF_FNEXT);

	if(fprev != NULL) {
		set_free_neigh(fprev, TLSF_FNEXT, fnext);
	} else {
		if(fnext == NULL) {
			STREM_UNSET_BIT(t->sbmap[fl], sl);
			STREM_SET_BIT_TO( t->fbmap, fl, !!(t->sbmap[fl]) );
		}
		t->blocks[fl][sl] = fnext;
	}

	if(fnext != NULL) {
		set_free_neigh(fnext, TLSF_FPREV, fprev);
	}
}

static void StremTLSF_merge_and_insert(
	StremTLSF* const t, 
	StremTLSFBlock* const f,
	StremTLSFBlock* const s
) {
	assert(block_adjacent(f, s) && "can't merge not adjacent blocks");
	assert(block_is_free(f));
	assert(block_is_free(s));

	uint8_t fl, sl;

	mapping(block_size(f), &fl, &sl);
	StremTLSF_remove_free(t, f, fl, sl);
	
	mapping(block_size(s), &fl, &sl);
	StremTLSF_remove_free(t, s, fl, sl);

	merge_free(f, s);
	block_set_free(f, true);
	block_set_last(f, block_is_last(s));

	mapping(block_size(f), &fl, &sl);
	StremTLSF_insert(t, f, fl, sl);
}

static bool StremTLSF_find_block(
	StremTLSF* const t, 
	size_t const asize,
	uint8_t* const fl,
	uint8_t* const sl
) {
	assert(asize % STREM_TLSF_CONTENT_MINSIZE == 0 && "must align for flag storage");
	mapping(asize, fl, sl);

	const uint32_t sfree_map = t->sbmap[*fl] & ((uint32_t)(-1) << *sl);

	if(sfree_map != 0) {
		*sl = FFS32(sfree_map);
		return true;
	}

	const uint32_t ffree_map = t->fbmap & ((uint32_t)(-1) << (*fl + 1));
	*fl = FFS32(ffree_map);

	if(*fl == 0) {
		return false;
	}
	assert(*fl < STREM_TLSF_FL_SIZE);

	*sl = FFS32(t->sbmap[*fl]);
	return (fl != 0) | (sl != 0);
}

StremTLSF* StremTLSF_emplace(void* at, const size_t at_size) {
	assert(sizeof(size_t) == sizeof(StremTLSFBlock*) && "needed for alignment"); /* maybe can do better with unions */
	assert(at_size > sizeof(StremTLSF) + sizeof(StremTLSFBlock) && "not enough memory to emplace tlsf");
	assert(at_size <= STREM_TLSF_LARGEST_SIZE 
		&& "can't manage such big chunk."
		"For upper bound use STREM_TLSF_LARGEST_SIZE."
		"You can get it by defining STREM_TLSF_DEFINE_LARGEST_SIZE before header");
	
	const size_t csize = at_size - sizeof(StremTLSF) - sizeof(StremTLSFBlock);
	
	StremTLSF* const t = (StremTLSF*)at;
	t->fbmap = 0;
	memset(t->sbmap, 0, sizeof(t->sbmap));
	t->size = csize;
	memset(t->blocks, 0, sizeof(t->blocks));

	StremTLSFBlock* const first_block = (StremTLSFBlock*)((char*)at + sizeof(StremTLSF));
	first_block->size = csize
		| (1 << STREM_TLSF_FREE_BIT)
		| (1 << STREM_TLSF_LAST_BIT);
	first_block->prev = NULL;
	set_free_neigh(first_block, TLSF_FPREV, NULL);
	set_free_neigh(first_block, TLSF_FNEXT, NULL);

	uint8_t fl, sl;
	mapping(csize, &fl, &sl);
	StremTLSF_insert(t, first_block, fl, sl);

	return t;
}

void* StremTLSF_alloc(StremTLSF* const t, const size_t asize) {
	assert(asize % STREM_TLSF_CONTENT_MINSIZE == 0 && "must align for flag storage");

	uint8_t fl, sl;
	if(!StremTLSF_find_block(t, asize, &fl, &sl)) {
		return NULL;
	}

	StremTLSFBlock* const block = t->blocks[fl][sl];
	StremTLSFBlock* const next = split_free(block, rounded(asize, block_size(block)));
	StremTLSF_remove_free(t, block, fl, sl);

	mapping(block_size(next), &fl, &sl);
	StremTLSF_insert(t, next, fl, sl);

	return block->content;
}

void StremTLSF_free(StremTLSF* const t, void* const content) {
	StremTLSFBlock* const block = block_from_content(content);

	assert((uintptr_t)content % STREM_TLSF_CONTENT_MINSIZE == 0 
		&& "ptr must align to 2*sizeof(void*)"
	);
	assert((uintptr_t)block % STREM_TLSF_CONTENT_MINSIZE == 0 
		&& "ptr must align to 2*sizeof(void*)"
	);
	assert(!block_is_free(block) && "can't free already freed");

	block_set_free(block, true);

	uint8_t fl, sl;
	mapping(block_size(block), &fl, &sl);

	StremTLSF_insert(t, block, fl, sl);
	if(!block_is_last(block) && block_is_free(block_next(block))) {
		StremTLSF_merge_and_insert(t, block, block_next(block));
	}

	if(block->prev != NULL && block_is_free(block->prev)) {
		StremTLSF_merge_and_insert(t, block->prev, block);
	}
}

void* StremTLSF_realloc(StremTLSF* const t, void* const content, const size_t asize) {
	StremTLSFBlock* const block = block_from_content(content);
	const size_t prevsize = block_size(block);
	
	assert(asize != 0 && "realloc isn't for 0 size");
	assert(asize % STREM_TLSF_CONTENT_MINSIZE == 0 && "must align for flag storage");
	assert((uintptr_t)content % STREM_TLSF_CONTENT_MINSIZE == 0 
		&& "ptr must align to 2*sizeof(void*)"
	);
	assert((uintptr_t)block % STREM_TLSF_CONTENT_MINSIZE == 0 
		&& "ptr must align to 2*sizeof(void*)"
	);
	assert(!block_is_free(block) && "can't realloc already freed");


	if(prevsize == asize) {
		return content;
	}

	if(prevsize > asize) {
		if(prevsize - asize > sizeof(StremTLSFBlock)) {
			StremTLSF_split(t, block, asize);
		}
		return content;
	}

	StremTLSFBlock* const nextblock = block_next(block);

	if(
		block_is_last(block) 
		|| !block_is_free(nextblock) 
		|| asize > block_size(block) + block_struct_size(nextblock)
	) {
		char content_small_copy[STREM_TLSF_CONTENT_MINSIZE];
		memcpy(content_small_copy, content, STREM_TLSF_CONTENT_MINSIZE);
		StremTLSF_free(t, content);

		void* const ncontent = StremTLSF_alloc(t, asize);
		memcpy(ncontent, content_small_copy, sizeof(content_small_copy));
		memmove(
			(char*)ncontent + sizeof(content_small_copy), 
			(char*)content + sizeof(content_small_copy),
			prevsize - sizeof(content_small_copy)
		);
		return ncontent;
	}

	uint8_t fl, sl;
	mapping(block_size(nextblock), &fl, &sl);

	StremTLSF_remove_free(t, nextblock, fl, sl);
	merge_free(block, nextblock);

	if(block_size(block) - asize > sizeof(StremTLSFBlock)) {
		StremTLSF_split(t, block, asize);
	}

	return content;
}

#ifdef STREM_UNUSED
#include <stdio.h>
static void block_debug(StremTLSFBlock* const b) {
	char buf[256] = {0};
	const char fc = block_is_free(b) ? 'f' : '-';
	const char lc = block_is_last(b) ? 'l' : '-';
	StremTLSFBlock** const ns = (StremTLSFBlock**)(b->content);
	if(block_is_free(b)) {
		sprintf(buf, "%x %x", (size_t)ns[TLSF_FPREV], (size_t)ns[TLSF_FNEXT]);
	}
	
	printf("(%p %c%c %d %s)", b, fc, lc, block_size(b), buf);
}

void StremTLSF_print_chain(StremTLSF* const t) {
	StremTLSFBlock* cur = (StremTLSFBlock*)((char*)t + sizeof(StremTLSF));

	for(;!block_is_last(cur); cur = block_next(cur)) {
		block_debug(cur);
		putc(' ', stdout);
	}
	block_debug(cur);
	putc('\n', stdout);
}

void StremTLSF_print_free(StremTLSF* const t) {
	StremTLSFBlock* cur = NULL;
	size_t flsize = 0, slsize = 0;
	uint8_t fl = 0, sl = 0;

	for(;fl != STREM_TLSF_FL_SIZE; fl++) {
		if(STREM_GET_BIT(t->fbmap, fl) == 0) {
			continue;
		}
		flsize = pow(2, fl);
		for(;sl != STREM_TLSF_SL_SIZE; sl++) {
			if(STREM_GET_BIT(t->sbmap[fl], sl) == 0) {
				continue;
			}
			slsize = flsize / STREM_TLSF_SL_SIZE * sl;

			printf("[%lld %lld] ", flsize, slsize);
			cur = t->blocks[fl][sl];
			for(; cur != NULL; cur = get_free_neigh(cur, TLSF_FNEXT)) {
				block_debug(cur);
				putc(' ', stdout);
			}
			putc('\n', stdout);
		}	
	}
}
#endif // STREM_UNUSED
