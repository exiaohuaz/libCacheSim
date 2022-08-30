//
// TODO
// This hash table stores pointers to cache_obj_t in the table, it uses
// one-level of indirection.
// draw a table
// |----------------|
// |     void*      | ----> cache_obj_t* ----> cache_obj_t* ----> NULL
// |----------------|
// |     void*      | ----> cache_obj_t*
// |----------------|
// |     void*      | ----> NULL
// |----------------|
// |     void*      | ----> cache_obj_t* ----> cache_obj_t* ----> nULL
// |----------------|
// |     void*      | ----> NULL
// |----------------|
// |     void*      | ----> NULL
// |----------------|
//
//

#ifdef __cplusplus
extern "C" {
#endif

#include "chainedHashTableV2.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "../../include/libCacheSim/logging.h"
#include "../../include/libCacheSim/macro.h"
#include "../../utils/include/mymath.h"
#include "../hash/hash.h"

#define OBJ_EMPTY(cache_obj) ((cache_obj)->obj_size == 0)
#define NEXT_OBJ(cur_obj) (((cache_obj_t *)(cur_obj))->hash_next)

void _chained_hashtable_expand_v2(hashtable_t *hashtable);

/************************ helper func ************************/
/**
 * get the last object in the hash bucket
 */
static inline cache_obj_t *_last_obj_in_bucket(hashtable_t *hashtable,
                                               uint64_t hv) {
  cache_obj_t *cur_obj_in_bucket = hashtable->ptr_table[hv];
  while (cur_obj_in_bucket->hash_next) {
    cur_obj_in_bucket = cur_obj_in_bucket->hash_next;
  }
  return cur_obj_in_bucket;
}

/* add an object to the hashtable */
static inline void add_to_bucket(hashtable_t *hashtable,
                                 cache_obj_t *cache_obj) {
  uint64_t hv = get_hash_value_int_64(&cache_obj->obj_id) &
                hashmask(hashtable->hashpower);
  if (hashtable->ptr_table[hv] == NULL) {
    hashtable->ptr_table[hv] = cache_obj;
    return;
  }
  cache_obj_t *head_ptr = hashtable->ptr_table[hv];

  cache_obj->hash_next = head_ptr;
  hashtable->ptr_table[hv] = cache_obj;
}

/* free object, called by other functions when iterating through the hashtable
 */
static inline void foreach_free_obj(cache_obj_t *cache_obj, void *user_data) {
  my_free(sizeof(cache_obj_t), cache_obj);
}

/************************ hashtable func ************************/
hashtable_t *create_chained_hashtable_v2(const uint16_t hashpower) {
  hashtable_t *hashtable = my_malloc(hashtable_t);
  memset(hashtable, 0, sizeof(hashtable_t));

  size_t size = sizeof(cache_obj_t *) * hashsize(hashtable->hashpower);
  hashtable->ptr_table = my_malloc_n(cache_obj_t *, hashsize(hashpower));
  memset(hashtable->ptr_table, 0, size);
#ifdef USE_HUGEPAGE
  madvise(hashtable->table, size, MADV_HUGEPAGE);
#endif
  hashtable->external_obj = false;
  hashtable->hashpower = hashpower;
  hashtable->n_obj = 0;
  return hashtable;
}

cache_obj_t *chained_hashtable_find_obj_id_v2(hashtable_t *hashtable,
                                              obj_id_t obj_id) {
  cache_obj_t *cache_obj = NULL;
  uint64_t hv = get_hash_value_int_64(&obj_id);
  hv = hv & hashmask(hashtable->hashpower);
  cache_obj = hashtable->ptr_table[hv];

  while (cache_obj) {
    if (cache_obj->obj_id == obj_id) {
      return cache_obj;
    }
    cache_obj = cache_obj->hash_next;
  }
  return cache_obj;
}

cache_obj_t *chained_hashtable_find_v2(hashtable_t *hashtable, request_t *req) {
  return chained_hashtable_find_obj_id_v2(hashtable, req->obj_id);
}

cache_obj_t *chained_hashtable_find_obj_v2(hashtable_t *hashtable,
                                           cache_obj_t *obj_to_find) {
  return chained_hashtable_find_obj_id_v2(hashtable, obj_to_find->obj_id);
}

/* the user needs to make sure the added object is not in the hash table */
cache_obj_t *chained_hashtable_insert_v2(hashtable_t *hashtable,
                                         request_t *req) {
  if (hashtable->n_obj > (uint64_t)(hashsize(hashtable->hashpower) *
                                    CHAINED_HASHTABLE_EXPAND_THRESHOLD)) {
    _chained_hashtable_expand_v2(hashtable);
  }

  cache_obj_t *new_cache_obj = create_cache_obj_from_request(req);
  add_to_bucket(hashtable, new_cache_obj);
  hashtable->n_obj += 1;
  return new_cache_obj;
}

/* the user needs to make sure the added object is not in the hash table */
cache_obj_t *chained_hashtable_insert_obj_v2(hashtable_t *hashtable,
                                             cache_obj_t *cache_obj) {
  DEBUG_ASSERT(hashtable->external_obj);
  if (hashtable->n_obj > (uint64_t)(hashsize(hashtable->hashpower) *
                                    CHAINED_HASHTABLE_EXPAND_THRESHOLD))
    _chained_hashtable_expand_v2(hashtable);

  add_to_bucket(hashtable, cache_obj);
  hashtable->n_obj += 1;
  return cache_obj;
}

/* you need to free the extra_metadata before deleting from hash table */
void chained_hashtable_delete_v2(hashtable_t *hashtable,
                                 cache_obj_t *cache_obj) {
  hashtable->n_obj -= 1;
  uint64_t hv = get_hash_value_int_64(&cache_obj->obj_id) &
                hashmask(hashtable->hashpower);
  if (hashtable->ptr_table[hv] == cache_obj) {
    hashtable->ptr_table[hv] = cache_obj->hash_next;
    if (!hashtable->external_obj) free_cache_obj(cache_obj);
    return;
  }

  static int max_chain_len = 1;
  int chain_len = 1;
  cache_obj_t *cur_obj = hashtable->ptr_table[hv];
  while (cur_obj != NULL && cur_obj->hash_next != cache_obj) {
    cur_obj = cur_obj->hash_next;
    chain_len += 1;
  }

  if (chain_len > 16 && chain_len > max_chain_len) {
    max_chain_len = chain_len;
    //    WARN("hashtable remove %lu max chain len %d, hashtable load %ld/%ld
    //    %lf\n",
    //           (unsigned long) cache_obj->obj_id, max_chain_len,
    //           (long) hashtable->n_obj,
    //           (long) hashsize(hashtable->hashpower),
    //           (double) hashtable->n_obj / hashsize(hashtable->hashpower)
    //           );
  }

  DEBUG_ASSERT(cur_obj != NULL);
  cur_obj->hash_next = cache_obj->hash_next;
  if (!hashtable->external_obj) free_cache_obj(cache_obj);
}

bool chained_hashtable_try_delete_v2(hashtable_t *hashtable,
                                     cache_obj_t *cache_obj) {
  static int max_chain_len = 1;

  uint64_t hv = get_hash_value_int_64(&cache_obj->obj_id) &
                hashmask(hashtable->hashpower);
  if (hashtable->ptr_table[hv] == cache_obj) {
    hashtable->ptr_table[hv] = cache_obj->hash_next;
    hashtable->n_obj -= 1;
    if (!hashtable->external_obj) free_cache_obj(cache_obj);
    return true;
  }

  int chain_len = 1;
  cache_obj_t *cur_obj = hashtable->ptr_table[hv];
  while (cur_obj != NULL && cur_obj->hash_next != cache_obj) {
    cur_obj = cur_obj->hash_next;
    chain_len += 1;
  }

  if (chain_len > 16 && chain_len > max_chain_len) {
    max_chain_len = chain_len;
    //    WARN("hashtable remove %ld, hv %lu, max chain len %d, hashtable load
    //    %ld/%ld %lf\n",
    //           (long) cache_obj->obj_id,
    //           (unsigned long) hv, max_chain_len,
    //           (long) hashtable->n_obj,
    //           (long) hashsize(hashtable->hashpower),
    //           (double) hashtable->n_obj / hashsize(hashtable->hashpower)
    //    );

    //    cache_obj_t *tmp_obj = hashtable->ptr_table[hv];
    //    while (tmp_obj) {
    //      printf("%ld (%d), ", (long) tmp_obj->obj_id, tmp_obj->LSC.in_cache);
    //      tmp_obj = tmp_obj->hash_next;
    //    }
    //    printf("\n");
  }

  if (cur_obj != NULL) {
    cur_obj->hash_next = cache_obj->hash_next;
    hashtable->n_obj -= 1;
    if (!hashtable->external_obj) free_cache_obj(cache_obj);
    return true;
  }
  return false;
}

void chained_hashtable_delete_obj_id_v2(hashtable_t *hashtable,
                                        obj_id_t obj_id) {
  hashtable->n_obj -= 1;
  uint64_t hv = get_hash_value_int_64(obj_id) & hashmask(hashtable->hashpower);
  cache_obj_t *cache_obj = hashtable->ptr_table[hv];
  if (cache_obj != NULL && cache_obj->obj_id == obj_id) {
    hashtable->ptr_table[hv] = cache_obj->hash_next;
    if (!hashtable->external_obj) free_cache_obj(cache_obj);
    return;
  }

  cache_obj = cache_obj->hash_next;
  while (cache_obj != NULL && cache_obj->obj_id != obj_id) {
    cache_obj = cache_obj->hash_next;
  }

  if (cache_obj != NULL) {
    cache_obj->hash_next = cache_obj->hash_next;
    if (!hashtable->external_obj) free_cache_obj(cache_obj);
  }
}

cache_obj_t *chained_hashtable_rand_obj_v2(hashtable_t *hashtable) {
  uint64_t pos = next_rand() & hashmask(hashtable->hashpower);
  while (hashtable->ptr_table[pos] == NULL)
    pos = next_rand() & hashmask(hashtable->hashpower);
  return hashtable->ptr_table[pos];
}

void chained_hashtable_foreach_v2(hashtable_t *hashtable,
                                  hashtable_iter iter_func, void *user_data) {
  cache_obj_t *cur_obj, *next_obj;
  for (uint64_t i = 0; i < hashsize(hashtable->hashpower); i++) {
    cur_obj = hashtable->ptr_table[i];
    while (cur_obj != NULL) {
      next_obj = cur_obj->hash_next;
      iter_func(cur_obj, user_data);
      cur_obj = next_obj;
    }
  }
}

void free_chained_hashtable_v2(hashtable_t *hashtable) {
  if (!hashtable->external_obj)
    chained_hashtable_foreach_v2(hashtable, foreach_free_obj, NULL);
  my_free(sizeof(cache_obj_t *) * hashsize(hashtable->hashpower),
          hashtable->ptr_table);
  my_free(sizeof(hashtable_t), hashtable);
}

/* grows the hashtable to the next power of 2. */
void _chained_hashtable_expand_v2(hashtable_t *hashtable) {
  cache_obj_t **old_table = hashtable->ptr_table;
  hashtable->ptr_table =
      my_malloc_n(cache_obj_t *, hashsize(++hashtable->hashpower));
#ifdef USE_HUGEPAGE
  madvise(hashtable->table,
          sizeof(cache_obj_t *) * hashsize(hashtable->hashpower),
          MADV_HUGEPAGE);
#endif
  memset(hashtable->ptr_table, 0,
         hashsize(hashtable->hashpower) * sizeof(cache_obj_t *));
  ASSERT_NOT_NULL(hashtable->ptr_table,
                  "unable to grow hashtable to size %llu\n",
                  hashsizeULL(hashtable->hashpower));

  VERBOSE("hashtable resized from %llu to %llu\n",
          hashsizeULL((uint16_t)(hashtable->hashpower - 1)),
          hashsizeULL(hashtable->hashpower));

  cache_obj_t *cur_obj, *next_obj;
  for (uint64_t i = 0; i < hashsize((uint16_t)(hashtable->hashpower - 1));
       i++) {
    cur_obj = old_table[i];
    while (cur_obj != NULL) {
      next_obj = cur_obj->hash_next;
      cur_obj->hash_next = NULL;
      add_to_bucket(hashtable, cur_obj);
      cur_obj = next_obj;
    }
  }
  my_free(sizeof(cache_obj_t) * hashsize(hashtable->hashpower), old_table);
}

void check_hashtable_integrity_v2(hashtable_t *hashtable) {
  cache_obj_t *cur_obj, *next_obj;
  for (uint64_t i = 0; i < hashsize(hashtable->hashpower); i++) {
    cur_obj = hashtable->ptr_table[i];
    while (cur_obj != NULL) {
      next_obj = cur_obj->hash_next;
      assert(i == (get_hash_value_int_64(&cur_obj->obj_id) &
                   hashmask(hashtable->hashpower)));
      cur_obj = next_obj;
    }
  }
}

#ifdef __cplusplus
}
#endif
