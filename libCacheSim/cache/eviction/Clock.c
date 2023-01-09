//
//  Clock, the same as FIFO-Reinsertion or second chance, is a FIFO with
//  which inserts back some objects upon eviction
//
//
//  Clock.c
//  libCacheSim
//
//  Created by Juncheng on 12/4/18.
//  Copyright © 2018 Juncheng. All rights reserved.
//

#include "../../dataStructure/hashtable/hashtable.h"
#include "../../include/libCacheSim/cache.h"
#include "../../include/libCacheSim/evictionAlgo/Clock.h"

#ifdef __cplusplus
extern "C" {
#endif

// #define USE_BELADY

typedef struct {
  cache_obj_t *q_head;
  cache_obj_t *q_tail;
  // clock uses one-bit counter
  int n_bit_counter;
  // max_freq = 1 << (n_bit_counter - 1)
  int max_freq;
} Clock_params_t;

static void Clock_parse_params(cache_t *cache,
                               const char *cache_specific_params);

// ***********************************************************************
// ****                                                               ****
// ****                   end user facing functions                   ****
// ****                                                               ****
// ***********************************************************************

/**
 * @brief initialize a Clock cache
 *
 * @param ccache_params some common cache parameters
 * @param cache_specific_params Clock specific parameters as a string
 */
cache_t *Clock_init(const common_cache_params_t ccache_params,
                    const char *cache_specific_params) {
  cache_t *cache = cache_struct_init("Clock", ccache_params);
  cache->cache_init = Clock_init;
  cache->cache_free = Clock_free;
  cache->get = Clock_get;
  cache->check = Clock_check;
  cache->insert = Clock_insert;
  cache->evict = Clock_evict;
  cache->remove = Clock_remove;
  cache->to_evict = Clock_to_evict;

  cache->init_params = cache_specific_params;
  cache->obj_md_size = 0;

#ifdef USE_BELADY
  snprintf(cache->cache_name, CACHE_NAME_ARRAY_LEN, "Clock_Belady");
#endif

  cache->eviction_params = malloc(sizeof(Clock_params_t));
  Clock_params_t *params = (Clock_params_t *)cache->eviction_params;
  params->q_head = NULL;
  params->q_tail = NULL;
  params->n_bit_counter = 1;
  params->max_freq = 1;

  if (cache_specific_params != NULL) {
    Clock_parse_params(cache, cache_specific_params);
  }

  if (params->n_bit_counter != 1) {
    snprintf(cache->cache_name, CACHE_NAME_ARRAY_LEN, "Clock-%d",
             params->n_bit_counter);
  }

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
void Clock_free(cache_t *cache) { cache_struct_free(cache); }

/**
 * @brief this function is the user facing API
 * it performs the following logic
 *
 * ```
 * if obj in cache:
 *    update_metadata
 *    return true
 * else:
 *    if cache does not have enough space:
 *        evict until it has space to insert
 *    insert the object
 *    return false
 * ```
 *
 * @param cache
 * @param req
 * @return true if cache hit, false if cache miss
 */
bool Clock_get(cache_t *cache, const request_t *req) {
  return cache_get_base(cache, req);
}

// ***********************************************************************
// ****                                                               ****
// ****       developer facing APIs (used by cache developer)         ****
// ****                                                               ****
// ***********************************************************************

/**
 * @brief check whether an object is in the cache
 *
 * @param cache
 * @param req
 * @param update_cache whether to update the cache,
 *  if true, the object is promoted
 *  and if the object is expired, it is removed from the cache
 * @return true on hit, false on miss
 */
bool Clock_check(cache_t *cache, const request_t *req,
                 const bool update_cache) {
  Clock_params_t *params = (Clock_params_t *)cache->eviction_params;
  cache_obj_t *obj = NULL;
  bool cache_hit = cache_check_base(cache, req, update_cache, &obj);
  if (obj != NULL && update_cache) {
    if (obj->lfu.freq < params->max_freq) {
      obj->lfu.freq += 1;
    }
#ifdef USE_BELADY
    obj->next_access_vtime = req->next_access_vtime;
#endif
  }

  return cache_hit;
}

/**
 * @brief insert an object into the cache,
 * update the hash table and cache metadata
 * this function assumes the cache has enough space
 * and eviction is not part of this function
 *
 * @param cache
 * @param req
 * @return the inserted object
 */
cache_obj_t *Clock_insert(cache_t *cache, const request_t *req) {
  Clock_params_t *params = (Clock_params_t *)cache->eviction_params;

  cache_obj_t *obj = cache_insert_base(cache, req);
  prepend_obj_to_head(&params->q_head, &params->q_tail, obj);

  obj->lfu.freq = 0;
#ifdef USE_BELADY
  obj->next_access_vtime = req->next_access_vtime;
#endif

  return obj;
}

/**
 * @brief find the object to be evicted
 * this function does not actually evict the object or update metadata
 * not all eviction algorithms support this function
 * because the eviction logic cannot be decoupled from finding eviction
 * candidate, so use assert(false) if you cannot support this function
 *
 * @param cache the cache
 * @return the object to be evicted
 */
cache_obj_t *Clock_to_evict(cache_t *cache) {
  Clock_params_t *params = (Clock_params_t *)cache->eviction_params;

  cache_obj_t *obj_to_evict = params->q_tail;
#ifdef USE_BELADY
  while (obj_to_evict->lfu.freq >= 1 &&
         obj_to_evict->next_access_vtime != INT64_MAX) {
    obj_to_evict->lfu.freq -= 1;
    move_obj_to_head(&params->q_head, &params->q_tail, obj_to_evict);
    obj_to_evict = params->q_tail;
  }

#else
  while (obj_to_evict->lfu.freq >= 1) {
    obj_to_evict->lfu.freq -= 1;
    move_obj_to_head(&params->q_head, &params->q_tail, obj_to_evict);
    obj_to_evict = params->q_tail;
  }

#endif
  return obj_to_evict;
}

/**
 * @brief evict an object from the cache
 * it needs to call cache_evict_base before returning
 * which updates some metadata such as n_obj, occupied size, and hash table
 *
 * @param cache
 * @param req not used
 * @param evicted_obj if not NULL, return the evicted object to caller
 */
void Clock_evict(cache_t *cache, const request_t *req,
                 cache_obj_t *evicted_obj) {
  Clock_params_t *params = (Clock_params_t *)cache->eviction_params;

  cache_obj_t *obj_to_evict = Clock_to_evict(cache);
  if (evicted_obj != NULL) {
    memcpy(evicted_obj, obj_to_evict, sizeof(cache_obj_t));
  }
  remove_obj_from_list(&params->q_head, &params->q_tail, obj_to_evict);
  cache_evict_base(cache, obj_to_evict, true);
}

/**
 * @brief remove the given object from the cache
 * note that eviction should not call this function, but rather call
 * `cache_evict_base` because we track extra metadata during eviction
 *
 * and this function is different from eviction
 * because it is used to for user trigger
 * remove, and eviction is used by the cache to make space for new objects
 *
 * it needs to call cache_remove_obj_base before returning
 * which updates some metadata such as n_obj, occupied size, and hash table
 *
 * @param cache
 * @param obj
 */
void Clock_remove_obj(cache_t *cache, cache_obj_t *obj) {
  Clock_params_t *params = (Clock_params_t *)cache->eviction_params;

  DEBUG_ASSERT(obj != NULL);
  remove_obj_from_list(&params->q_head, &params->q_tail, obj);
  cache_remove_obj_base(cache, obj, true);
}

/**
 * @brief remove an object from the cache
 * this is different from cache_evict because it is used to for user trigger
 * remove, and eviction is used by the cache to make space for new objects
 *
 * it needs to call cache_remove_obj_base before returning
 * which updates some metadata such as n_obj, occupied size, and hash table
 *
 * @param cache
 * @param obj_id
 * @return true if the object is removed, false if the object is not in the
 * cache
 */
bool Clock_remove(cache_t *cache, const obj_id_t obj_id) {
  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == NULL) {
    return false;
  }

  Clock_remove_obj(cache, obj);

  return true;
}

// ***********************************************************************
// ****                                                               ****
// ****                  parameter set up functions                   ****
// ****                                                               ****
// ***********************************************************************
static const char *Clock_current_params(cache_t *cache,
                                        Clock_params_t *params) {
  static __thread char params_str[128];
  int n =
      snprintf(params_str, 128, "n-bit-counter=%d\n", params->n_bit_counter);

  return params_str;
}

static void Clock_parse_params(cache_t *cache,
                               const char *cache_specific_params) {
  Clock_params_t *params = (Clock_params_t *)cache->eviction_params;
  char *params_str = strdup(cache_specific_params);
  char *old_params_str = params_str;
  char *end;

  while (params_str != NULL && params_str[0] != '\0') {
    /* different parameters are separated by comma,
     * key and value are separated by = */
    char *key = strsep((char **)&params_str, "=");
    char *value = strsep((char **)&params_str, ",");

    // skip the white space
    while (params_str != NULL && *params_str == ' ') {
      params_str++;
    }

    if (strcasecmp(key, "n-bit-counter") == 0) {
      params->n_bit_counter = (int)strtol(value, &end, 0);
      params->max_freq = (1 << params->n_bit_counter) - 1;
      if (strlen(end) > 2) {
        ERROR("param parsing error, find string \"%s\" after number\n", end);
      }
    } else if (strcasecmp(key, "print") == 0) {
      printf("current parameters: %s\n", Clock_current_params(cache, params));
      exit(0);
    } else {
      ERROR("%s does not have parameter %s, example paramters %s\n",
            cache->cache_name, key, Clock_current_params(cache, params));
      exit(1);
    }
  }
  free(old_params_str);
}

#ifdef __cplusplus
}
#endif
