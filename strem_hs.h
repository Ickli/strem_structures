#ifndef STREM_HS_H_
#define STREM_HS_H_
#include <strem_vector.h>

typedef size_t(*StremHashFunction)(void const*);
typedef bool(*StremCmpFunction)(void const*, void const*);

typedef struct {
	size_t hash;
	enum {
		STREM_HS_EMPTY = 0,
		STREM_HS_TAKEN,
		STREM_HS_DEAD,
	} type;
	char content[];
} StremHSKey;

typedef struct {
	/* private: */
	void* /* StremHSKey + TKey */ keys;
	StremHashFunction func;
	StremCmpFunction cmp_func;
	size_t cap;
	size_t size;
	size_t key_size;
	/* public: */
	float saturation;
	/* private: */
	int grow_mode;
} StremHashSet;

// If fails to allocate, set.keys == NULL
StremHashSet StremHashSet_construct(
	size_t key_size, StremHashFunction func, StremCmpFunction cmp_func
);

// Must: at_buf_size / key_size > 0
StremHashSet StremHashSet_emplace(
	void* at_buf,
	size_t at_buf_size,
	size_t key_size,
	StremHashFunction func, 
	StremCmpFunction cmp_func, 
	bool grow_left
);

void StremHashSet_free(StremHashSet* ht);

// Inserts and returns pointer to key inside table
// Returns NULL if malloced and need to but can't reallocate
void* StremHashSet_insert(StremHashSet* ht, void const* const key);

// Removes the pair and returns ptr to just removed key (NULL if no key found).
// key pointer is valid until any action with the table.
void* StremHashSet_remove(StremHashSet* ht, void const* key);

// Returns pointer to associated value (NULL if no key found)
void* StremHashSet_at(StremHashSet* ht, void const* key);

// Resizes and rehashes key vector
// Returns false and doesn't rehash, if can't resize;
// Otherwise. returns true.
bool StremHashSet_resize(StremHashSet* ht, size_t newcap);

#endif // STREM_HS_H_
