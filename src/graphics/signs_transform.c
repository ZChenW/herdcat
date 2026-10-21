#include "graphics/signs.h"
#include "signs_internal.h"

#include <math.h>

void upright_from(sign_frame_t *frame, int first, double cx, double cy) {
  for (int i = first; i < frame->shape_count; i++) {
    frame->shapes[i].upright = true;
    frame->shapes[i].icon_center_x = cx;
    frame->shapes[i].icon_center_y = cy;
  }
}
static void reflect_rect(sign_rect_t *rect, double axis) {
  rect->y = (int)lround(axis - (rect->y + (double)rect->h));
}
void signs_reflect(sign_frame_t *frame, double center_y) {
  double axis = 2 * center_y;
  for (int i = 0; i < frame->shape_count; i++) {
    sign_shape_t *shape = &frame->shapes[i];
    if (shape->upright) {
      double dy = axis - 2 * shape->icon_center_y;
      shape->y += dy;
      if (shape->orbit)
        shape->origin_y += dy;
      shape->icon_center_y = axis - shape->icon_center_y;
    } else {
      shape->reflected = !shape->reflected;
      shape->y = axis - (shape->y + shape->h);
      shape->rotation = -shape->rotation;
      shape->origin_y = axis - shape->origin_y;
    }
    if (shape->clipped)
      shape->clip_y = axis - (shape->clip_y + shape->clip_h);
  }
  for (int i = 0; i < frame->text_count; i++) {
    sign_text_t *text = &frame->texts[i];
    text->line_top = axis - (text->line_top + text->line_h);
    if (text->clip_h > 0)
      text->clip_y = axis - (text->clip_y + text->clip_h);
    if (text->back >> 24)
      text->anchor_y = axis - text->anchor_y + sign_tag_height(text);
  }
  for (int i = 0; i < frame->hit_count; i++) {
    sign_hit_t *hit = &frame->hits[i];
    hit->y = (int)lround(axis - (hit->y + (double)hit->h));
    if (hit->precise) {
      hit->center_y = axis - hit->center_y;
      hit->rotation = -hit->rotation;
    }
  }
  if (frame->has_pad)
    reflect_rect(&frame->pad, axis);
  if (frame->bounds_h > 0)
    frame->bounds_y =
        (int)lround(axis - (frame->bounds_y + (double)frame->bounds_h));
  if (frame->menu_open) {
    reflect_rect(&frame->menu_card, axis);
    for (int i = 0; i < 2; i++) {
      reflect_rect(&frame->menu_style[i], axis);
      reflect_rect(&frame->menu_lang[i], axis);
    }
    for (int i = 0; i < 3; i++)
      reflect_rect(&frame->menu_theme[i], axis);
    reflect_rect(&frame->menu_style_thumb, axis);
    reflect_rect(&frame->menu_lang_thumb, axis);
    reflect_rect(&frame->menu_theme_thumb, axis);
    reflect_rect(&frame->menu_font, axis);
    reflect_rect(&frame->menu_font_prev, axis);
    reflect_rect(&frame->menu_font_next, axis);
  }
}
