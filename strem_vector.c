#include "strem_vector.h"
#include <string.h>

StremVector StremVector_construct(size_t elem_size, size_t capacity) {
	return (StremVector){ 
		elem_size,
		capacity,
		0,
		calloc(elem_size, capacity)
	};
}

bool StremVector_reserve(StremVector* vector, size_t capacity) {
	if(vector->capacity_elems < capacity) {
		void* newcontent = realloc(vector->content, capacity * vector->elem_size);
		if(newcontent == NULL) {
			return false;
		}
		vector->content = newcontent;
		vector->capacity_elems = capacity;
	}
	return true;
}

void* StremVector_push(StremVector* vector, void const* const elems, size_t elem_count) {
	bool success = true;
	if(vector->size + elem_count > vector->capacity_elems) {
		success = StremVector_reserve(vector, vector->capacity_elems * 2);
	}
	
	if(!success) {
		return NULL;
	}

	char* const vector_tip = (char*)vector->content + vector->size * vector->elem_size;
	memcpy(vector_tip, elems, vector->elem_size * elem_count);
	vector->size += elem_count;

	return vector_tip;
}

StremVector StremVector_copy(const StremVector* vector) {
	StremVector newvec = *vector;

	const size_t bytesize = vector->capacity_elems * vector->elem_size;
	newvec.content = malloc(bytesize);
	memcpy(newvec.content, vector->content, bytesize);

	return newvec;
}

void StremVector_free(StremVector* vector) {
	free(vector->content);
	vector->content = NULL;
	vector->size = 0;
}
