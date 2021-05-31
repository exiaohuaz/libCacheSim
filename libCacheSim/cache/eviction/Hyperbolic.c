/* Hyperbolic caching */

#include "../include/libCacheSim/evictionAlgo/Hyperbolic.h"
#include "../dataStructure/hashtable/hashtable.h"


#ifdef __cplusplus
extern "C" {
#endif


void Hyperbolic_remove_obj(cache_t *cache, cache_obj_t *obj);


cache_t *Hyperbolic_init(common_cache_params_t ccache_params,
                      __attribute__((unused)) void *init_params) {
  cache_t *cache = cache_struct_init("Hyperbolic", ccache_params);
  cache->cache_init = Hyperbolic_init;
  cache->cache_free = Hyperbolic_free;
  cache->get = Hyperbolic_get;
  cache->check = Hyperbolic_check;
  cache->insert = Hyperbolic_insert;
  cache->evict = Hyperbolic_evict;
  cache->remove = Hyperbolic_remove;

  Hyperbolic_params_t *params = my_malloc(Hyperbolic_params_t);
  cache->eviction_params = params;

  params->pq = pqueue_init((unsigned long) 8e6);
  return cache;
}

void Hyperbolic_free(cache_t *cache) {
  Hyperbolic_params_t *params = cache->eviction_params;
  pq_node_t *node = pqueue_pop(params->pq);
  while (node) {
    my_free(sizeof(pq_node_t), node);
    node = pqueue_pop(params->pq);
  }
  pqueue_free(params->pq);

  cache_struct_free(cache);
}

cache_ck_res_e Hyperbolic_check(cache_t *cache,
                             request_t *req,
                             bool update_cache) {
  Hyperbolic_params_t *params = cache->eviction_params;
  cache_obj_t *cached_obj;
  cache_ck_res_e ret = cache_check_base(cache, req, update_cache, &cached_obj);

  if (ret == cache_ck_miss) {
    DEBUG_ASSERT(cache_get_obj(cache, req) == NULL);
  }

  if (!update_cache)
    return ret;

  if (ret == cache_ck_hit) {
    pqueue_pri_t pri;
    pri.pri1_lf = (double) cached_obj->obj_size / (double) (++cached_obj->freq);
    pqueue_change_priority(params->pq,
                           pri,
                           (pq_node_t *) (cached_obj->extra_metadata2_ptr));

    return cache_ck_hit;
  } else if (ret == cache_ck_expired) {
    Hyperbolic_remove_obj(cache, cached_obj);

    return cache_ck_expired;
  }

  return cache_ck_miss;
}

cache_ck_res_e Hyperbolic_get(cache_t *cache, request_t *req) {
  cache_ck_res_e ret = cache_get_base(cache, req);

  return ret;
}

void Hyperbolic_insert(cache_t *cache, request_t *req) {
  Hyperbolic_params_t *params = cache->eviction_params;
  cache_obj_t *cached_obj = cache_insert_base(cache, req);
  cached_obj->freq = 1;

  pq_node_t *node = my_malloc(pq_node_t);
  node->obj_id = req->obj_id_int;
  node->pri.pri1_lf = (double) req->obj_size;
  pqueue_insert(params->pq, (void *) node);
  cached_obj->extra_metadata2_ptr = node;
}

void Hyperbolic_evict(cache_t *cache,
                   __attribute__((unused)) request_t *req,
                   __attribute__((unused)) cache_obj_t *evicted_obj) {
  Hyperbolic_params_t *params = cache->eviction_params;
  pq_node_t *node = (pq_node_t *) pqueue_pop(params->pq);

  cache_obj_t *cached_obj = cache_get_obj_by_id(cache, node->obj_id);
  DEBUG_ASSERT(node == cached_obj->extra_metadata2_ptr);

#ifdef TRACK_EVICTION_AGE
  record_eviction_age(cache, (int) (req->real_time - cached_obj->last_access_rtime));
#endif

  cached_obj->extra_metadata2_ptr = NULL;
  my_free(sizeof(pq_node_t), node);

  Hyperbolic_remove_obj(cache, cached_obj);
}

void Hyperbolic_remove_obj(cache_t *cache, cache_obj_t *obj) {
  Hyperbolic_params_t *params = cache->eviction_params;

  DEBUG_ASSERT(hashtable_find_obj(cache->hashtable, obj) == obj);
  DEBUG_ASSERT(cache->occupied_size >= obj->obj_size);

  cache->occupied_size -= (obj->obj_size + cache->per_obj_overhead);
  cache->n_obj -= 1;

  if (obj->extra_metadata2_ptr != NULL) {
    /* if it is NULL, it means we have deleted the entry in pq before this */
    pqueue_remove(params->pq, obj->extra_metadata2_ptr);
    my_free(sizeof(pq_node_t), obj->extra_metadata2_ptr);
    obj->extra_metadata2_ptr = NULL;
  }

  hashtable_delete(cache->hashtable, obj);
}

void Hyperbolic_remove(cache_t *cache, obj_id_t obj_id) {
  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == NULL) {
    WARNING("obj to remove is not in the cache\n");
    return;
  }

  Hyperbolic_remove_obj(cache, obj);
}

#ifdef __cplusplus
}
#endif
