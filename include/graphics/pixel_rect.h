#ifndef HERDCAT_PIXEL_RECT_H
#define HERDCAT_PIXEL_RECT_H

#include <limits.h>
#include <stdint.h>

// Physical buffer pixels, with an exclusive right and bottom edge.
typedef struct {
  int x, y, w, h;
} pixel_rect_t;

static inline pixel_rect_t pixel_rect_intersect(pixel_rect_t a,
                                                pixel_rect_t b) {
  if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0)
    return (pixel_rect_t){0};
  int x = a.x > b.x ? a.x : b.x;
  int y = a.y > b.y ? a.y : b.y;
  int64_t r = (int64_t)a.x + a.w, bottom = (int64_t)a.y + a.h;
  if (r > (int64_t)b.x + b.w)
    r = (int64_t)b.x + b.w;
  if (bottom > (int64_t)b.y + b.h)
    bottom = (int64_t)b.y + b.h;
  if (r <= x || bottom <= y || r - x > INT_MAX || bottom - y > INT_MAX)
    return (pixel_rect_t){0};
  return (pixel_rect_t){x, y, (int)(r - x), (int)(bottom - y)};
}

static inline pixel_rect_t pixel_rect_clip(pixel_rect_t rect, int w, int h) {
  return pixel_rect_intersect(rect, (pixel_rect_t){0, 0, w, h});
}

// Both rectangles have already been clipped to the same physical buffer.
static inline pixel_rect_t pixel_rect_union(pixel_rect_t a, pixel_rect_t b) {
  if (a.w <= 0 || a.h <= 0)
    return b;
  if (b.w <= 0 || b.h <= 0)
    return a;
  int x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
  int r = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
  int bottom = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
  return (pixel_rect_t){x, y, r - x, bottom - y};
}

#endif
