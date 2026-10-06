#ifndef HERDCAT_SHM_BUFFER_H
#define HERDCAT_SHM_BUFFER_H
#include "core/herdcat.h"
#include "graphics/pixel_rect.h"
typedef struct shm_buffer {
  struct wl_buffer *object;
  uint8_t *pixels;
  size_t size;
  int width, height;
  // Union of every submitted frame's damage since this buffer was drawn.
  pixel_rect_t dirty;
  bool busy;
  bool retired;
  struct shm_buffer *next;
} shm_buffer_t;
shm_buffer_t *shm_buffer_create(struct wl_shm *shm, int width, int height);
void shm_buffer_damage(shm_buffer_t *buffer, pixel_rect_t damage);
// Clear only accumulated damage, consume it, and return the repaint clip.
pixel_rect_t shm_buffer_begin_draw(shm_buffer_t *buffer);
void shm_buffer_fill(shm_buffer_t *buffer, pixel_rect_t rect, uint32_t color);
void shm_buffer_retire(shm_buffer_t *buffer);
void shm_buffers_cleanup(void);
#endif
