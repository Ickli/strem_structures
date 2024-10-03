#include <strem_ht.h>
#include <stdio.h>
#include <string.h>

#define KEYSIZE(ht) (sizeof(StremHTKey) + (ht).key_size)

#define DEFAULT_HT_CAP 32
#define DEFAULT_HT_SATURATION 0.6f
#define GAP 5
// number of keys in collision chain including the tail one, which may not collide
#define COLLISION_MAX_LENGTH 5

static StremHTKey* get_key(StremHashTable* ht, size_t index) {
	return (StremHTKey*)((char*)ht->keys.content + KEYSIZE(*ht) * index);
}

// returns length of collision chain including the tail one which may not collide 
// returns 0 if any other key in chain is on its needed place
static size_t get_collision_chain(
	StremHashTable* ht, 
	StremHTKey* start_key, 
	StremHTKey* col_stack[COLLISION_MAX_LENGTH]
) {
	const size_t keys_cap = ht->keys.capacity_elems;
	size_t col_len = 0;
	StremHTKey* key = start_key;
	StremHTKey* new_key = get_key(ht, key->hash % keys_cap);

	for(;new_key->type == STREM_HT_TAKEN && col_len < COLLISION_MAX_LENGTH - 1; col_len++) {
		if(key == new_key) {
			return 0;
		}
		col_stack[col_len] = key;
		col_stack[col_len + 1] = new_key;
		key = new_key;
		new_key = get_key(ht, key->hash % keys_cap);
	}

	return col_len + 1; // + 1 is the last added new_key which may not collide 
}

// collision chain consists of pointers to keys including the tail one which may not collide
// function shifts the chain, copying each key to the location of next key
static void resolve_collision_chain(
	StremHTKey* col_stack[COLLISION_MAX_LENGTH],
	size_t key_size_with_content,
	size_t col_len
) {
	if(col_len < 2) {
		return;
	}
	for(col_len -= 2; col_len != 0; col_len--) {
		memcpy(col_stack[col_len + 1], col_stack[col_len], key_size_with_content);
	}
	col_stack[0]->type = STREM_HT_EMPTY;
}

void StremHashTable_resize(StremHashTable* ht, size_t newcap) {
	const size_t oldcap = ht->keys.capacity_elems;
	if(newcap <= oldcap) {
		return;
	}
	StremVector_reserve(&ht->keys, newcap);

	const size_t key_size = KEYSIZE(*ht);
	StremHTKey* collision_stack[COLLISION_MAX_LENGTH];
//	char* const collided_key_buf = malloc(sizeof(char)*key_size);
	char collided_key_buf[key_size]; // VLA
	bool key_buf_empty = true;

	for(size_t i = 0; i < oldcap; i++) {
		StremHTKey* key = get_key(ht, i);
		if(key->type != STREM_HT_TAKEN) {
			continue;
		}

		const size_t new_index = key->hash % newcap;
		StremHTKey* const new_key = get_key(ht, new_index);

		if(new_index == i) {
			continue;
		} else if(new_key->type != STREM_HT_TAKEN) {
			memcpy(new_key, key, key_size);
			continue;
		}

		size_t col_len = COLLISION_MAX_LENGTH + 1;
		StremHTKey* chain_start = key;

		while(col_len >= COLLISION_MAX_LENGTH) {
			col_len = get_collision_chain(ht, key, collision_stack);

			if(col_len == 0) {
				/* if 0, a key on its dedicated slot is met.
				 * In this case, we can't rehash keys in chain.
				 *
				 * If chain was long enough for 2+ iterations,
				 * buffed key is placed in the beginning of chain.
				 */ 
				if(!key_buf_empty) {
					memcpy(chain_start, collided_key_buf, key_size);
				}
			}

			if(col_len == COLLISION_MAX_LENGTH) {
				memcpy(collided_key_buf, collision_stack[col_len - 1], key_size);
				key = (StremHTKey*)collided_key_buf;
				key_buf_empty = false;
			}

			resolve_collision_chain(collision_stack, KEYSIZE(*ht), col_len);
		}
		
	}
	
//	free(collided_key_buf);
}

StremHashTable StremHashTable_construct(
	size_t key_size, 
	size_t value_size, 
	StremHashFunction func,
	StremCmpFunction cmp_func
) {
	StremHashTable ht;

	ht.key_size = key_size;
	ht.value_size = value_size;
	ht.saturation = DEFAULT_HT_SATURATION;
	ht.func = func;
	ht.cmp_func = cmp_func;
	ht.keys = StremVector_construct(KEYSIZE(ht), DEFAULT_HT_CAP);
	ht.values = StremVector_construct(ht.value_size, DEFAULT_HT_CAP);
	ht.dead_values = StremVector_construct(sizeof(void*), DEFAULT_HT_CAP);
	return ht;
}

void StremHashTable_free(StremHashTable* ht) {
	StremVector_free(&ht->keys);
	StremVector_free(&ht->values);
	StremVector_free(&ht->dead_values);
}

static StremHTKey* key_at(StremHashTable* ht, void const* key) {
	const size_t hash = ht->func(key);
	const size_t keys_cap = ht->keys.capacity_elems;
	size_t index = hash % ht->keys.capacity_elems;
	StremHTKey* ht_key = get_key(ht, index);


	/* Dead = continue
	 * Empty = return null
	 * Taken = check hashes
	 * 	hash != then continue
	 * 	hash == then cmp
	 * 		if cmp != then continue ?
	 * 		if cmp == then return ptr
	 */
	// using index and ht_key separately for clarity
	while(true) {
		if(ht_key->type == STREM_HT_EMPTY) {
			return NULL;
		}
		if(ht_key->type == STREM_HT_DEAD
		|| (ht_key->type == STREM_HT_TAKEN 
//			&& !keys_equal(ht->cmp_func, key, &ht_key->content)
			&& (hash != ht_key->hash || !ht->cmp_func(ht_key->content, key))
		)) {
			index = (index + GAP) % keys_cap;
			ht_key = get_key(ht, index);
		} else {
			return ht_key;
		}
	}
}

void* StremHashTable_at(StremHashTable* ht, void const* key) {
	StremHTKey* ht_key = key_at(ht, key);

	if(ht_key == NULL) {
		return NULL;
	}
	return ht_key->value_ptr;
}

void* StremHashTable_insert(StremHashTable* ht, void const* const key, void const* const value) {
	const float current_satur = (float)ht->keys.size / ht->keys.capacity_elems;
	if(current_satur >= ht->saturation) {
		StremHashTable_resize(ht, ht->keys.capacity_elems * 2);
	}

	const size_t hash = ht->func(key);
	size_t key_index = hash % ht->keys.capacity_elems;
	StremHTKey* ht_key = get_key(ht, key_index);

	while(ht_key->type == STREM_HT_TAKEN) {
		key_index = (key_index + GAP) % ht->keys.capacity_elems;
		ht_key = get_key(ht, key_index);
	}

	void* value_ptr;
	if(ht->dead_values.size != 0) {
		value_ptr = StremVectorPopBack(ht->dead_values, void*);
		memcpy(value_ptr, value, ht->value_size);
	} else {
		StremVector_push(&ht->values, value, 1);
		value_ptr = StremVectorErasedAt(ht->values, ht->values.size - 1);
	}

	ht_key->hash = hash;
	ht_key->type = STREM_HT_TAKEN;
	ht_key->value_ptr = value_ptr;
	memcpy(&ht_key->content, key, ht->key_size);
	ht->keys.size++;

	return ht_key->value_ptr;
}

void* StremHashTable_remove(StremHashTable* ht, void const* key) {
	StremHTKey* ht_key = key_at(ht, key);

	if(ht_key == NULL || ht_key->type != STREM_HT_TAKEN) {
		return NULL;
	}

	void* value_ptr = ht_key->value_ptr;
	StremVector_push(&ht->dead_values, &value_ptr, 1);
	ht_key->type = STREM_HT_DEAD;
	ht_key->value_ptr = NULL;

	ht->keys.size--;

	return value_ptr;
}
