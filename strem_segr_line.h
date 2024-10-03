#ifndef STREM_SEGR_LINE_H_
#define STREM_SEGR_LINE_H_
#include <stdbool.h>

struct StremSegrLine_FreeNode {
	struct StremSegrLine_FreeNode* next;
};
typedef struct StremSegrLine_FreeNode StremSegrLine_FreeNode;

typedef struct {
	char* content;
} StremSegrLine;

void* StremSegrLine_alloc(size_t elem_size, size_t elem_count);
void* StremSegrLine_emplace(void* at, size_t elem_size, size_t elem_count);
StremSegrLine_FreeNode* StremSegrLine_grow_alloced(
	StremSegrLine* line, 
	size_t elem_size, 
	size_t old_elem_count, 
	size_t new_elem_count
);
StremSegrLine_FreeNode* StremSegrLine_grow_emplaced(
	StremSegrLine* line, 
	size_t elem_size, 
	size_t old_elem_count, 
	size_t new_elem_count, 
	bool prepend
);
#endif // STREM_SEGR_LINE_H_
