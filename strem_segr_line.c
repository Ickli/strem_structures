#include <stdlib.h>
#include <strem_segr_line.h>

// Interprets block as sequence of StremSegrLine_FreeNode structures aligned to elem_size,
// Makes every node point to the next one. The last will point to NULL;
// Must: elem_count != 0
//       elem_size >= sizeof(char*)
static void turn_memblock_in_free_chain(
	char* block,
	size_t elem_size,
	size_t elem_count
) {
	char* cur_node = block;
	char* next_node;
	char* last_node = block + elem_size*(elem_count - 1);

	for(;cur_node < last_node;) {
		next_node = cur_node + elem_size;
		((StremSegrLine_FreeNode*)cur_node)->next = (StremSegrLine_FreeNode*)next_node;
		cur_node = next_node;
	}
	((StremSegrLine_FreeNode*)last_node)->next = NULL;
}

void* StremSegrLine_alloc(size_t elem_size, size_t elem_count) {
	const size_t total_byte_size = elem_size*elem_count;
	char* line_content = malloc(total_byte_size);

	if(line_content == NULL) {
		return NULL;
	} else {
		return StremSegrLine_emplace(line_content, elem_size, elem_count);
	}
}

void* StremSegrLine_emplace(void* at, size_t elem_size, size_t elem_count) {
	turn_memblock_in_free_chain(at, elem_size, elem_count);
	return at;
}

StremSegrLine_FreeNode* StremSegrLine_grow_alloced(
	StremSegrLine* line, 
	size_t elem_size, 
	size_t old_elem_count, 
	size_t new_elem_count
) {
	char* realloced = (char*)realloc((void*)line->content, elem_size*new_elem_count);
	if(realloced != NULL) {
		char* remainder_start = realloced + elem_size * old_elem_count;

		turn_memblock_in_free_chain(
			remainder_start, 
			elem_size,
			new_elem_count - old_elem_count
		);

		line->content = realloced;
		return (StremSegrLine_FreeNode*)remainder_start;
	} else {
		return NULL;
	}
}

StremSegrLine_FreeNode* StremSegrLine_grow_emplaced(
	StremSegrLine* line, 
	size_t elem_size, 
	size_t old_elem_count, 
	size_t new_elem_count, 
	bool prepend
) {
	const size_t countdiff = new_elem_count - old_elem_count;
	char* const newreg = prepend 
		? line->content - countdiff * elem_size
		: line->content + old_elem_count * elem_size;

	turn_memblock_in_free_chain(newreg, elem_size, countdiff);
	return (StremSegrLine_FreeNode*)newreg;
}
