#ifndef HERDCAT_INPUT_PROTOCOL_H
#define HERDCAT_INPUT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define INPUT_MAX_DEVICES 32
#define INPUT_PATH_SIZE   64
#define PAW_LEFT          1u
#define PAW_RIGHT         2u
#define PAW_BOTH          (PAW_LEFT | PAW_RIGHT)

typedef struct {
  uint32_t paws;
  uint32_t devices;
  uint32_t denied;
  uint32_t reserved;  // Explicitly zero the existing ABI padding.
  int64_t monotonic_ns;
} input_message_t;

static inline unsigned paw_for_keycode(int keycode) {
  static const int left_keys[] = {
      1,  2,  3,  4,  5,  6,  7,  15, 16, 17, 18, 19, 20, 29,  30,
      31, 32, 33, 34, 41, 42, 44, 45, 46, 47, 48, 56, 58, 125,
  };
  for (size_t i = 0; i < sizeof(left_keys) / sizeof(left_keys[0]); i++) {
    if (keycode == left_keys[i]) {
      return PAW_LEFT;
    }
  }
  return PAW_RIGHT;
}

// Reject noncanonical spellings rather than resolving privileged symlinks.
static inline int input_event_path(const char *path) {
  static const char prefix[] = "/dev/input/event";
  size_t i = 0;
  for (; i < sizeof(prefix) - 1; i++) {
    if (path[i] != prefix[i]) {
      return 0;
    }
  }
  size_t start = i;
  for (; path[i]; i++) {
    if (path[i] < '0' || path[i] > '9' || i >= INPUT_PATH_SIZE - 1) {
      return 0;
    }
  }
  return i > start;
}

#endif
