#pragma once

#include "../../include/libCacheSim/evictionAlgo/LLSC.h"
#include "log.h"


static inline void transform_seg_to_training(cache_t *cache, bucket_t *bucket,
                                             segment_t *segment) {
  static int n = 0, n_zero = 0;
  LLSC_params_t *params = cache->cache_params;
  segment->is_training_seg = true;
  /* used to calculate the eviction penalty */
  segment->eviction_vtime = params->curr_vtime;
  /* used to calculate age at eviction for training */
  segment->eviction_rtime = params->curr_rtime;
  segment->penalty = 0;


  /* remove from the bucket */
  remove_seg_from_bucket(params, bucket, segment);

  append_seg_to_bucket(params, &params->training_bucket, segment);
}

static inline int seg_history_idx(segment_t *segment, int32_t curr_time, int32_t time_window) {
  int idx = (curr_time - segment->create_rtime) / time_window;

  if (idx >= N_FEATURE_TIME_WINDOW) return N_FEATURE_TIME_WINDOW - 1;
  else
    return idx;
}

static inline void seg_feature_shift(LLSC_params_t *params, segment_t *seg) {

  int64_t shift;


  shift = (params->curr_rtime - seg->feature.last_min_window_ts) / 60;
  if (shift <= 0) return;
  for (int i = N_FEATURE_TIME_WINDOW-1; i >= 0; i--) {
//    seg->feature.n_active_item_per_min[i + 1] = seg->feature.n_active_item_per_min[i];
    seg->feature.n_hit_per_min[i + 1] = seg->feature.n_hit_per_min[i];
  }
  seg->feature.n_hit_per_min[0] = 0;
//  seg->feature.n_active_item_per_min[0] = 0;
  seg->feature.last_min_window_ts = params->curr_rtime;
//  printf("%d: %d %d %d %d %d %d %d %d %d %d - %d %d %d %d %d %d %d %d %d %d - %d %d %d %d %d %d %d %d %d %d\n",
//         seg->seg_id,
//         seg->feature.n_hit_per_min[0], seg->feature.n_hit_per_min[1],
//         seg->feature.n_hit_per_min[2], seg->feature.n_hit_per_min[3],
//         seg->feature.n_hit_per_min[4], seg->feature.n_hit_per_min[5],
//         seg->feature.n_hit_per_min[6], seg->feature.n_hit_per_min[7],
//         seg->feature.n_hit_per_min[8], seg->feature.n_hit_per_min[9],
//
//         seg->feature.n_hit_per_ten_min[0], seg->feature.n_hit_per_ten_min[1],
//         seg->feature.n_hit_per_ten_min[2], seg->feature.n_hit_per_ten_min[3],
//         seg->feature.n_hit_per_ten_min[4], seg->feature.n_hit_per_ten_min[5],
//         seg->feature.n_hit_per_ten_min[6], seg->feature.n_hit_per_ten_min[7],
//         seg->feature.n_hit_per_ten_min[8], seg->feature.n_hit_per_ten_min[9],
//
//         seg->feature.n_hit_per_hour[0], seg->feature.n_hit_per_hour[1],
//         seg->feature.n_hit_per_hour[2], seg->feature.n_hit_per_hour[3],
//         seg->feature.n_hit_per_hour[4], seg->feature.n_hit_per_hour[5],
//         seg->feature.n_hit_per_hour[6], seg->feature.n_hit_per_hour[7],
//         seg->feature.n_hit_per_hour[8], seg->feature.n_hit_per_hour[9]
//  );
//  printf("\n");

  shift = (params->curr_rtime - seg->feature.last_ten_min_window_ts) / 600;
  if (shift <= 0) return;
  for (int i = N_FEATURE_TIME_WINDOW-1; i >= 0; i--) {
//    seg->feature.n_active_item_per_ten_min[i + 1] = seg->feature.n_active_item_per_ten_min[i];
    seg->feature.n_hit_per_ten_min[i + 1] = seg->feature.n_hit_per_ten_min[i];
  }
  seg->feature.n_hit_per_ten_min[0] = 0;
//  seg->feature.n_active_item_per_ten_min[0] = 0;
  seg->feature.last_ten_min_window_ts = params->curr_rtime;


  shift = (params->curr_rtime - seg->feature.last_hour_window_ts) / 3600;
  if (shift <= 0) return;
  for (int i = N_FEATURE_TIME_WINDOW-1; i >= 0; i--) {
//    seg->feature.n_active_item_per_hour[i + 1] = seg->feature.n_active_item_per_hour[i];
    seg->feature.n_hit_per_hour[i + 1] = seg->feature.n_hit_per_hour[i];
  }
  seg->feature.n_hit_per_hour[0] = 0;
//  seg->feature.n_active_item_per_hour[0] = 0;
  seg->feature.last_hour_window_ts = params->curr_rtime;
}

static inline void seg_hit(LLSC_params_t *params, cache_obj_t *cache_obj) {
  //  if (!params->learner.start_feature_recording) return;

  segment_t *segment = cache_obj->LSC.segment;
  segment->n_total_hit += 1;

  if (params->curr_rtime - segment->feature.last_min_window_ts >= 60) {
    seg_feature_shift(params, segment);
  }

  segment->feature.n_hit_per_min[0] += 1;
  segment->feature.n_hit_per_ten_min[0] += 1;
  segment->feature.n_hit_per_hour[0] += 1;

  if (!cache_obj->LSC.active) {
    segment->n_total_active += 1;
    cache_obj->LSC.active = 1;

//    segment->feature.n_active_item_per_min[0] += 1;
//    segment->feature.n_active_item_per_ten_min[0] += 1;
//    segment->feature.n_active_item_per_hour[0] += 1;

//    if ((params->curr_rtime - cache_obj->LSC.last_access_rtime) / 60 > 0)
//      segment->feature.n_active_item_per_min[0] += 1;
//    if ((params->curr_rtime - cache_obj->LSC.last_access_rtime) / 600 > 0)
//      segment->feature.n_active_item_per_ten_min[0] += 1;
//    if ((params->curr_rtime - cache_obj->LSC.last_access_rtime) / 3600 > 0)
//      segment->feature.n_active_item_per_hour[0] += 1;
  }

  //  segment->feature.n_hit[curr_idx] += 1;
  //  cache_obj->LSC.last_history_idx = curr_idx;
  //  cache_obj->LSC.last_access_rtime = params->curr_rtime;
}

void train(cache_t *cache);

void inference(cache_t *cache);
