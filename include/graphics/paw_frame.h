#ifndef HERDCAT_PAW_FRAME_H
#define HERDCAT_PAW_FRAME_H

// Shared pending-paw bits. Input child ORs bits; animation thread atomically
// consumes them. Sleep handling is caller responsibility.

#include "core/herdcat.h"
#include "platform/input_protocol.h"

#include <stdbool.h>

static inline unsigned paw_apply_mirror(unsigned paws, bool mirror) {
  if (!mirror) {
    return paws;
  }
  return ((paws & PAW_LEFT) ? PAW_RIGHT : 0U) |
         ((paws & PAW_RIGHT) ? PAW_LEFT : 0U);
}

static inline int frame_from_paw_state(bool left_live, bool right_live,
                                       int idle_frame) {
  if (left_live && right_live) {
    return HERDCAT_FRAME_BOTH_DOWN;
  }
  if (left_live) {
    return HERDCAT_FRAME_LEFT_DOWN;
  }
  if (right_live) {
    return HERDCAT_FRAME_RIGHT_DOWN;
  }
  return idle_frame;
}

#endif  // HERDCAT_PAW_FRAME_H
