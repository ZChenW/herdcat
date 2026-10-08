#ifndef HERDCAT_TEST_TIMING_H
#define HERDCAT_TEST_TIMING_H

#include <stdlib.h>
#include <string.h>

// Internal fixture opt-in only. Production defaults and animation clocks stay
// unchanged; no configuration key or user-facing option enables this.
static inline int test_timing_ms(int duration_ms, int divisor) {
  const char *mode = getenv("HERDCAT_TEST_TIMING");
  return mode && !strcmp(mode, "fast") ? duration_ms / divisor : duration_ms;
}

#endif
