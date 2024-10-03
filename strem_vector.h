#ifndef STREM_VECTOR_H_
#define STREM_VECTOR_H_
#include <stdbool.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
	size_t elem_size;
	size_t capacity_elems;
	size_t size;
	void* content;
} StremVector;

// If fail to allocate, returns vector with content == NULL
StremVector StremVector_construct(size_t elem_size, size_t capacity);

// Returns false if fail to reallocate, otherwise returns true
bool StremVector_reserve(StremVector* vector, size_t capacity);

// Returns NULL if need and fail to resize, otherwise returns address of copied block
void* StremVector_push(StremVector* vector, void const* const elems, size_t elem_count);

// Returns vector with content == NULL if fail to allocate
StremVector StremVector_copy(const StremVector* vector);

void StremVector_free(StremVector* vector);

#define StremVectorAt(vector, type, index) (((type*)((vector).content))[index])
#define StremVectorErasedAt(vector, index) ((char*)((vector).content) + (index)*(vector).elem_size)
#define StremVectorBack(vector, type) StremVectorAt(vector, type, ((vector).size - 1))
#define StremVectorPopBack(vector, type) StremVectorBack(vector, type); (vector).size--

#endif // STREM_VECTOR_H_
