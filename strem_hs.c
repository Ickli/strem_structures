#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <strem_hs.h>

#define KEYSIZE(hs) (sizeof(StremHSKey) + (hs).key_size)

#define DEFAULT_HS_CAP 32
#define DEFAULT_HS_SATURATION 0.5f
#define GAP 5
// number of keys in collision chain including the tail one, which may not collide
#define COLLISION_MAX_LENGTH 5

typedef enum {
	GROW_LEFT,
	GROW_RIGHT,
	GROW_MALLOC,
} GROW_MODE;

static StremHSKey* get_key(StremHashSet* hs, size_t index) {
	return (StremHSKey*)((char*)hs->keys + KEYSIZE(*hs) * index);
}

// returns length of collision chain including the tail one which may not collide
// returns 0 if any other key in chain is on its needed place
static size_t get_collision_chain(
	StremHashSet* hs,
	StremHSKey* start_key, 
	StremHSKey* col_stack[COLLISION_MAX_LENGTH]
) {
	const size_t keys_cap = hs->cap;
	size_t col_len = 0;
	StremHSKey* key = start_key;
	StremHSKey* new_key = get_key(hs, key->hash % keys_cap);

	for(;new_key->type == STREM_HS_TAKEN && col_len < COLLISION_MAX_LENGTH - 1; col_len++) {
		if(key == new_key) {
			return 0;
		}

		col_stack[col_len] = key;
		col_stack[col_len + 1] = new_key;
		key = new_key;
		new_key = get_key(hs, key->hash % keys_cap);
	}

	return col_len + 1; // + 1 is the last added new_key which may not collide 
}

// collision chain consists of pointers to keys including the tail one which may not collide
// function shifts the chain, copying each key to the location of next key
static void resolve_collision_chain(
	StremHSKey* col_stack[COLLISION_MAX_LENGTH],
	size_t key_size_with_content,
	size_t col_len
) {
	if(col_len < 2) {
		return;
	}
	for(col_len -= 2; col_len != 0; col_len--) {
		memcpy(col_stack[col_len + 1], col_stack[col_len], key_size_with_content);
	}
	col_stack[0]->type = STREM_HS_EMPTY;
}

bool StremHashSet_resize(StremHashSet* hs, size_t newcap) {
	void* oldkeys_start = hs->keys;
	const size_t oldcap = hs->cap;
	const size_t key_size = KEYSIZE(*hs);
	
	if(newcap <= oldcap) {
		return true;
	} else if(hs->grow_mode == (int)GROW_MALLOC) {
		void* const newkeys = realloc(hs->keys, newcap*key_size);
		if(newkeys == NULL) {
			return false;
		}
		hs->keys = newkeys;
		oldkeys_start = hs->keys;
	} else if(hs->grow_mode == (int)GROW_LEFT) {
		if((char*)hs->keys <= (char*)NULL + newcap*key_size) { /* is it really needed? */
			return false;
		}
		hs->keys = (char*)hs->keys - (newcap - oldcap)*key_size;
	}
	hs->cap = newcap;

	StremHSKey* collision_stack[COLLISION_MAX_LENGTH];
//	char* const collided_key_buf = malloc(sizeof(char)*key_size);
	char collided_key_buf[key_size]; // VLA
	bool key_buf_empty = true;

	for(size_t i = 0; i < oldcap; i++) {
		/* don't use get_key here in case of GROW_LEFT */
		StremHSKey* key = (StremHSKey*)((char*)oldkeys_start + KEYSIZE(*hs)*i);
		if(key->type != STREM_HS_TAKEN) {
			continue;
		}

		const size_t new_index = key->hash % newcap;
		StremHSKey* const new_key = get_key(hs, new_index);

		if(new_index == i) {
			continue;
		} else if(new_key->type != STREM_HS_TAKEN) {
			memcpy(new_key, key, key_size);
			continue;
		}

		size_t col_len = COLLISION_MAX_LENGTH + 1;
		StremHSKey* const collision_start = key;

		while(col_len >= COLLISION_MAX_LENGTH) {
			col_len = get_collision_chain(hs, key, collision_stack);

			if(col_len == 0) {
				/* if 0, a key on its dedicated slot is met.
				 * In this case, we can't rehash keys in chain.
				 *
				 * If chain was long enough for 2+ iterations,
				 * buffed key is placed in the beginning of chain.
				 */ 
				if(!key_buf_empty) {
					memcpy(collision_start, collided_key_buf, key_size);
				}
				break;
			}
			if(col_len == COLLISION_MAX_LENGTH) {
				memcpy(collided_key_buf, collision_stack[col_len - 1], key_size);
				key = (StremHSKey*)collided_key_buf;
				key_buf_empty = false;
			}

			resolve_collision_chain(collision_stack, KEYSIZE(*hs), col_len);
		}
		
	}
	
//	free(collided_key_buf);
	return true;
}

static StremHSKey* key_at(StremHashSet* hs, void const* key) {
	const size_t hash = hs->func(key);
	size_t index = hash % hs->cap;
	StremHSKey* hs_key = get_key(hs, index);

	/* Dead = continue
	 * Empty = return null
	 * Taken = check hashes
	 * 	hash != then continue
	 * 	hash == then cmp
	 * 		if cmp != then continue ?
	 * 		if cmp == then return ptr
	 */
	// using index and hs_key separately for clarity
	while(true) {
		if(hs_key->type == STREM_HS_EMPTY) {
			return NULL;
		}
		if(hs_key->type == STREM_HS_DEAD
		|| (hs_key->type == STREM_HS_TAKEN 
			&& (hash != hs_key->hash || !hs->cmp_func(hs_key->content, key))
		)) {
			index = (index + GAP) % hs->cap;
			hs_key = get_key(hs, index);
		} else {
			return hs_key;
		}
	}
}

StremHashSet StremHashSet_construct(
	size_t key_size, StremHashFunction func, StremCmpFunction cmp_func
) {
	return (StremHashSet){
		calloc((sizeof(StremHSKey) + key_size), DEFAULT_HS_CAP),
		func,
		cmp_func,
		DEFAULT_HS_CAP,
		0,
		key_size,
		DEFAULT_HS_SATURATION,
		(int)GROW_MALLOC
	};
}
StremHashSet StremHashSet_emplace(
	void* at, size_t at_buf_size, size_t key_size, StremHashFunction func, 
	StremCmpFunction cmp_func, bool grow_left
) {
	const size_t cap = at_buf_size / (sizeof(StremHSKey) + key_size);
	assert(cap != 0 && "Initial capacity mustn't be 0");

	memset(at, 0, at_buf_size);
	return (StremHashSet){
		at,
		func,
		cmp_func,
		cap,
		0,
		key_size,
		DEFAULT_HS_SATURATION,
		grow_left ? GROW_LEFT : GROW_RIGHT
	};
}
void StremHashSet_free(StremHashSet* hs) {
	if(hs->grow_mode == GROW_MALLOC) {
		free(hs->keys);
	}
}

void* StremHashSet_insert(StremHashSet* hs, void const* const key) {
	const float current_satur = (float)hs->size / hs->cap;
	if(current_satur >= hs->saturation) {
		if(StremHashSet_resize(hs, hs->cap * 2)) {
			return NULL;
		}
	}

	const size_t hash = hs->func(key);
	size_t key_index = hash % hs->cap;
	StremHSKey* hs_key = get_key(hs, key_index);

	while(hs_key->type == STREM_HS_TAKEN) {
		key_index = (key_index + GAP) % hs->cap;
		hs_key = get_key(hs, key_index);
	}
	
	memcpy(&hs_key->content, key, hs->key_size);
	hs_key->type = STREM_HS_TAKEN;
	hs_key->hash = hash;
	hs->size++;

	return hs_key->content;
}

void* StremHashSet_at(StremHashSet* hs, void const* key) {
	StremHSKey* hs_key = key_at(hs, key);

	if(hs_key == NULL) {
		return NULL;
	}
	return hs_key->content;
}

void* StremHashSet_remove(StremHashSet* hs, void const* key) {
	StremHSKey* hs_key = key_at(hs, key);

	if(hs_key == NULL || hs_key->type == STREM_HS_EMPTY) {
		return NULL;
	}

	hs_key->type = STREM_HS_DEAD;
	hs->size--;

	return hs_key->content;
}
