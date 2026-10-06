#include "graphics/nameplate_layout.h"
#include "graphics/post_text_layout.h"
#include "graphics/text.h"
#include "sign_draw_internal.h"

#include <math.h>
#include <string.h>

static void draw_tag(uint8_t *dst, int dw, int dh, const sign_text_t *text,
                     double scale, pix_t bounds, bool store) {
  double s = text->tag_scale > .05 ? text->tag_scale : 1;
  nameplate_layout_t layout;
  nameplate_layout(text, s, &layout);
  if (!layout.lines)
    return;
  double border = 2 * s, pad_x = 10 * s, pad_t = 5 * s;
  double box_h = sign_tag_height(text) * s;
  double box_w = border * 2 + pad_x * 2 + layout.width;
  if (text->max_width > 0)
    box_w = fmin(box_w, text->max_width);
  double box_top = text->anchor_y - sign_tag_height(text) / 2 - box_h / 2;
  double left = text->x - box_w / 2;
  if (text->surface_width > 0)
    left = fmax(0, fmin(left, text->surface_width - box_w));
  if (text->pixel_snap) {
    left = round(left * scale) / scale;
    box_top = round(box_top * scale) / scale;
  }
  sign_shape_t plate = {.kind = SIGN_RECT,
                        .pixel_snap = text->pixel_snap,
                        .x = left,
                        .y = box_top,
                        .w = box_w,
                        .h = box_h,
                        .radius = 10 * s,
                        .stroke = border,
                        .fill = text->back,
                        .outline = text->color};
  draw_shape(dst, dw, dh, &plate, scale, bounds, store);
  pix_t clip = intersect(bounds, pix_of(left, box_top, box_w, box_h, scale));
  if (clip.r <= clip.x || clip.b <= clip.y)
    return;
  text_clip_t box = {clip.x, clip.y, clip.r - clip.x, clip.b - clip.y};
  for (int line = 0; line < layout.lines; line++) {
    double line_top = box_top + border + pad_t;
    double line_h = text->px * 1.2 * s;
    if (line) {
      line_top += line_h;
      line_h = text->meta_px * 1.2 * s;
    }
    bool bold_line = !line;
    for (int i = 0; i < layout.count; i++)
      if (layout.runs[i].line == line && layout.runs[i].bold)
        bold_line = true;
    double baseline = text_baseline(
        line_top, line_h, (float)((bold_line ? text->px : text->meta_px) * s),
        bold_line);
    double x = left + border + pad_x;
    for (int i = 0; i < layout.count; i++) {
      const nameplate_paint_run_t *r = &layout.runs[i];
      if (r->line != line)
        continue;
      uint32_t color = r->bold    ? text->color
                       : r->state ? text->meta_color
                                  : text->secondary_color;
      text_draw_clip(dst, dw, dh, (int)lround(x * scale),
                     (int)lround(baseline * scale), r->text, r->px, r->bold,
                     color, 0, box);
      x += r->width;
    }
  }
}
void draw_text(uint8_t *dst, int dw, int dh, const sign_text_t *text,
               double scale, pix_t bounds, bool store) {
  if (text->back >> 24) {
    draw_tag(dst, dw, dh, text, scale, bounds, store);
    return;
  }
  float name_px = (float)text->px;
  const char *family = text->family[0] ? text->family : NULL;
  double baseline =
      text_baseline_family(family, text->line_top, text->line_h, name_px, true);
  int base = (int)lround(baseline * scale);
  if (text->center) {
    int measured = text->value[0]
                       ? text_measure_family(family, text->value, name_px, true)
                       : 0;
    bool shrink = text->w > 0 && measured > text->w;
    double name_x = text->x + text->slide;
    if (!shrink)
      name_x += (text->w - measured) / 2.0;
    pix_t clip = intersect(
        bounds, pix_of(text->x, text->clip_y, text->w, text->clip_h, scale));
    if (clip.r <= clip.x || clip.b <= clip.y)
      return;
    text_clip_t box = {clip.x, clip.y, clip.r - clip.x, clip.b - clip.y};
    int limit = shrink ? (int)lround(text->w * scale) : 0;
    if (text->value[0])
      text_draw_clip_family(dst, dw, dh, (int)lround(name_x * scale), base,
                            family, text->value, name_px, true, text->color,
                            limit, box);
    return;
  }
  post_text_layout_t layout;
  post_text_layout(text, &layout);
  double budget = layout.name_budget;
  // A board left of the pole is a mirror image: the name stays beside the
  // icon at the pole end, the note at the far end.
  double name_x = layout.name_x;
  double meta_x = layout.meta_x;
  pix_t clip = intersect(
      bounds, pix_of(text->x, text->clip_y, text->w, text->clip_h, scale));
  if (clip.r <= clip.x || clip.b <= clip.y)
    return;
  text_clip_t box = {clip.x, clip.y, clip.r - clip.x, clip.b - clip.y};
  if (text->value[0] && budget > 0.5)
    text_draw_clip(dst, dw, dh, (int)lround(name_x * scale), base, text->value,
                   name_px, true, text->color, (int)lround(budget * scale),
                   box);
  if (text->caret && (text->meta_color >> 24) && text->px > 0) {
    int measured = text_measure(text->value, name_px, true);
    double used = measured;
    if (used > budget)
      used = budget;
    double caret_w = text->px * (2.0 / 12.0);
    sign_shape_t bar = {.kind = SIGN_RECT,
                        .x = name_x + used + text->gap,
                        .y = text->line_top + text->line_h / 2 - text->px / 2,
                        .w = caret_w,
                        .h = text->px,
                        .radius = caret_w / 2,
                        .fill = text->meta_color};
    draw_shape(dst, dw, dh, &bar, scale, bounds, false);
  }
  if (text->meta[0])
    text_draw_clip(dst, dw, dh, (int)lround(meta_x * scale), base, text->meta,
                   (float)text->meta_px, false, text->meta_color, 0, box);
  if (layout.extra[0])
    text_draw_clip(dst, dw, dh, (int)lround(layout.extra_x * scale), base,
                   layout.extra, (float)text->meta_px, false,
                   text->secondary_color, 0, box);
}
