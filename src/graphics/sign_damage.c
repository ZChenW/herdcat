#include "graphics/sign_damage.h"

#include "graphics/nameplate_layout.h"
#include "graphics/pixel_rect.h"
#include "graphics/signs.h"

#include <math.h>
#include <string.h>

static pixel_rect_t frame_box(const sign_frame_t *frame) {
  return (pixel_rect_t){frame->bounds_x, frame->bounds_y, frame->bounds_w,
                        frame->bounds_h};
}
static pixel_rect_t shape_box(const sign_shape_t *shape,
                              const sign_frame_t *frame) {
  double ox = shape->orbit ? shape->origin_x : shape->x + shape->w / 2;
  double oy = shape->orbit ? shape->origin_y : shape->y + shape->h / 2;
  double rad = shape->rotation * 3.141592653589793 / 180;
  double cs = cos(rad), sn = sin(rad);
  double left = INFINITY, top = INFINITY;
  double right = -INFINITY, bottom = -INFINITY;
  for (int i = 0; i < 4; i++) {
    double dx = shape->x + ((i & 1) ? shape->w : 0) - ox;
    double dy = shape->y + ((i & 2) ? shape->h : 0) - oy;
    double x = ox + dx * cs - dy * sn;
    double y = oy + dx * sn + dy * cs;
    left = fmin(left, x);
    top = fmin(top, y);
    right = fmax(right, x);
    bottom = fmax(bottom, y);
  }
  // Include raster padding, physical-pixel snapping and the check/cross
  // stroke. This is deliberately wider than ink, including at half scale.
  double pad = 8 + shape->stroke;
  int x = (int)floor(left - pad), y = (int)floor(top - pad);
  pixel_rect_t box = {x, y, (int)ceil(right + pad) - x,
                      (int)ceil(bottom + pad) - y};
  return pixel_rect_intersect(box, frame_box(frame));
}
static pixel_rect_t text_box(const sign_text_t *text,
                             const sign_frame_t *frame) {
  double x = text->x, y = text->clip_y, w = text->w, h = text->clip_h;
  double pad = 0;
  if (text->back >> 24) {
    double s = text->tag_scale > .05 ? text->tag_scale : 1;
    nameplate_layout_t layout;
    nameplate_layout(text, s, &layout);
    if (!layout.lines)
      return (pixel_rect_t){0};
    w = 24 * s + layout.width;
    if (text->max_width > 0)
      w = fmin(w, text->max_width);
    h = sign_tag_height(text) * s;
    x = text->x - w / 2;
    if (text->surface_width > 0)
      x = fmax(0, fmin(x, text->surface_width - w));
    y = text->anchor_y - sign_tag_height(text) / 2 - h / 2;
    pad = 8;
  }
  int left = (int)floor(x - pad), top = (int)floor(y - pad);
  pixel_rect_t box = {left, top, (int)ceil(x + w + pad) - left,
                      (int)ceil(y + h + pad) - top};
  return pixel_rect_intersect(box, frame_box(frame));
}
pixel_rect_t sign_damage(const sign_frame_t *before,
                         const sign_frame_t *after) {
  pixel_rect_t full = pixel_rect_union(frame_box(before), frame_box(after));
  if (before->transitioning || after->transitioning ||
      before->shape_count != after->shape_count ||
      before->text_count != after->text_count)
    return full;
  pixel_rect_t damage = {0};
  for (int i = 0; i < after->shape_count; i++) {
    // Padding differences only enlarge damage; they cannot hide changed ink.
    if (!memcmp((const unsigned char *)&before->shapes[i],
                (const unsigned char *)&after->shapes[i], sizeof(sign_shape_t)))
      continue;
    damage = pixel_rect_union(damage, shape_box(&before->shapes[i], before));
    damage = pixel_rect_union(damage, shape_box(&after->shapes[i], after));
  }
  for (int i = 0; i < after->text_count; i++) {
    if (!memcmp((const unsigned char *)&before->texts[i],
                (const unsigned char *)&after->texts[i], sizeof(sign_text_t)))
      continue;
    damage = pixel_rect_union(damage, text_box(&before->texts[i], before));
    damage = pixel_rect_union(damage, text_box(&after->texts[i], after));
  }
  return damage;
}
