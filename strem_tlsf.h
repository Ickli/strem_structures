#ifndef STREM_TLFS_H_
#define STREM_TLFS_H_
#include <stdint.h>

/* public */
// must be pow(2, STREM_TLSF_SLI)
#define STREM_TLSF_SL_SIZE 16
// must be log2(STREM_TLSF_SL_SIZE)
#define STREM_TLSF_SLI 4

/* private */
#define STREM_TLSF_FL_SIZE 32

#if _WIN32 || _WIN64
    #if _WIN64
        #define STREM_PTR64
    #else
        #define STREM_PTR32
    #endif
#elif __GNUC__
    #if __x86_64__ || __ppc64__
        #define STREM_PTR64
    #else
        #define STREM_PTR32
    #endif
#elif UINTPTR_MAX > UINT_MAX
    #define STREM_PTR64
#else
    #define STREM_PTR32
#endif

#ifdef STREM_PTR32
/* log2(STREM_TLSF_CONTENT_MINSIZE) == 2 */
#define STREM_TLSF_CLASS_OFFSET 2
#else
/* log2(STREM_TLSF_CONTENT_MINSIZE) == 4 */
#define STREM_TLSF_CLASS_OFFSET 4
#endif

#ifdef STREM_TLSF_DEFINE_LARGEST_SIZE
#include <math.h>
#define STREM_TLSF_LARGEST_SIZE (pow(2, STREM_TLSF_FL_SIZE + STREM_TLSF_CLASS_OFFSET) - 1\
	+ sizeof(StremTLSF) + sizeof(StremTLSFBlock))
#endif // STREM_TLSF_DEFINE_LARGEST_SIZE

struct StremTLSFBlock {
	size_t size;
	struct StremTLSFBlock* prev;
	char content[];
};

struct StremTLSFBlock;
typedef struct StremTLSFBlock StremTLSFBlock;

typedef struct {
	uint32_t fbmap;
	uint32_t sbmap[STREM_TLSF_FL_SIZE];
	size_t size;
	StremTLSFBlock* blocks[STREM_TLSF_FL_SIZE][STREM_TLSF_SL_SIZE];
} StremTLSF;

StremTLSF* StremTLSF_emplace(void* at, const size_t at_size);
void* StremTLSF_alloc(StremTLSF* const t, const size_t asize);
void StremTLSF_free(StremTLSF* const t, void* const content);
void* StremTLSF_realloc(StremTLSF* const t, void* const content, const size_t asize);

#endif // STREM_TLFS_H_
