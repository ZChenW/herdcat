#include "graphics/animation.h"
#include "platform/input.h"
#include "platform/wayland.h"
#include "test_helpers.h"

#include <string.h>

atomic_uint *pending_paws;
void wayland_request_current_redraw(void) {}
void wayland_request_redraw(void) {}
int64_t input_timestamp(void) {
  return 0;
}

static size_t largest_copy;
void *__real_memcpy(void *dst, const void *src, size_t count);
void *__wrap_memcpy(void *dst, const void *src, size_t count);
void *__wrap_memcpy(void *dst, const void *src, size_t count) {
  if (count > largest_copy)
    largest_copy = count;
  return __real_memcpy(dst, src, count);
}

int main(void) {
  uint8_t source[16 * 2 * 4], dest[20 * 4 * 4];
  for (size_t i = 0; i < sizeof(source); i += 4) {
    source[i] = 7;
    source[i + 1] = 80;
    source[i + 2] = 120;
    source[i + 3] = 255;
  }
  memset(dest, 0xa5, sizeof(dest));
  largest_copy = 0;
  blit_cached_frame_clip(dest, 20, 4, source, 16, 2, 2, 1,
                         (pixel_rect_t){4, 1, 8, 2});
  TEST_ASSERT(largest_copy == 32);
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 20; x++) {
      size_t i = ((size_t)y * 20 + x) * 4;
      bool inside = y >= 1 && y < 3 && x >= 4 && x < 12;
      for (int c = 0; c < 4; c++)
        TEST_ASSERT(dest[i + c] == (inside ? source[c] : 0xa5));
    }
  return 0;
}
