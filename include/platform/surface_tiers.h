#ifndef HERDCAT_SURFACE_TIERS_H
#define HERDCAT_SURFACE_TIERS_H

#include "platform/overlay_vertical.h"

#include <stdint.h>

#define SURFACE_TIER_SHRINK_MS 10000

typedef struct {
  int width, height;
} surface_size_t;
typedef struct {
  int capacity, requested, shrink_capacity;
  int64_t shrink_at;
  bool pending;
} surface_tiers_t;

int surface_tier_capacity(const config_t *config, int count, bool expanded);
surface_size_t surface_tier_size(const config_t *config, int capacity,
                                 int output_width, uint32_t scale);
// Canvas padding covers desk exit without altering below-sign layout.
int surface_tier_model_height(const config_t *config,
                              sign_orientation_t orientation, int height);
// Returns -1 or a capacity to request. Only ready promotes the request.
int surface_tier_update(surface_tiers_t *state, int desired, bool blocked,
                        bool transitioning, int64_t now_ms);
void surface_tier_ready(surface_tiers_t *state);
// Capacity is committed after configure and buffer allocation, never inferred
// from surface dimensions. Saved displacement still uses the configured
// maximum.
overlay_vertical_t surface_tier_vertical(const config_t *config, int position_y,
                                         int output_height, int surface_height,
                                         bool has_history,
                                         sign_orientation_t previous,
                                         uint32_t scale, int capacity);

#endif
