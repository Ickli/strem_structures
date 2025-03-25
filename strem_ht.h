#ifndef STREM_HT_H_
#define STREM_HT_H_
#include "strem_vector.h"


typedef size_t(*StremHashFunction)(void const*);
typedef bool(*StremCmpFunction)(void const*, void const*);

typedef struct {
	size_t hash;
	enum {
		STREM_HT_EMPTY = 0,
		STREM_HT_TAKEN,
		STREM_HT_DEAD,
	} type;
	char* value_ptr;
	char content[];
} StremHTKey;

// TODO: replace values and dead_values with one StremSegrLine and one pointer to free chain
typedef struct {
	/* private: */
	StremVector /* StremHTKey + TKey */ keys;
	StremVector /* TValue */ values;
	StremVector /* void* */ dead_values;
	StremHashFunction func;
	StremCmpFunction cmp_func;
	size_t key_size;
	size_t value_size;
	/* public: */
	float saturation;
} StremHashTable;


StremHashTable StremHashTable_construct(
	size_t key_size, size_t value_size, StremHashFunction func, StremCmpFunction cmp_func
);
void StremHashTable_free(StremHashTable* ht);

// Inserts pair and returns pointer to value contained inside table
void* StremHashTable_insert(StremHashTable* ht, void const* const key, void const* const value);
// Removes the pair and returns ptr to associated value (NULL if no key found).
// Value pointer is valid until any further action with the table.
void* StremHashTable_remove(StremHashTable* ht, void const* key);
// Returns pointer to associated value (NULL if no key found)
void* StremHashTable_at(StremHashTable* ht, void const* key);
// Resizes and rehashes key vector
void StremHashTable_resize(StremHashTable* ht, size_t newcap);

#endif // STREM_HT_H_
