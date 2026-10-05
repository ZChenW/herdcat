#include "graphics/sign_draw.h"

#include "graphics/text.h"

#include <math.h>
#include <nanosvg.h>
#include <nanosvgrast.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_SLOTS 16
#define SHAPE_PAD   3.0
#define SVG_BYTES   1024

typedef struct {
  int x, y, r, b;
} pix_t;
typedef struct {
  uint64_t key, used;
  int w, h;
  uint8_t *pixels;
} cache_slot_t;

static NSVGrasterizer *rasterizer;
static cache_slot_t cache[CACHE_SLOTS];
static uint64_t cache_clock;
static int cache_scale;

static void cache_clear(void) {
  for (int i = 0; i < CACHE_SLOTS; i++) {
    free(cache[i].pixels);
    cache[i] = (cache_slot_t){0};
  }
}

static pix_t pix_of(double x, double y, double w, double h, double scale) {
  return (pix_t){(int)floor(x * scale), (int)floor(y * scale),
                 (int)ceil((x + w) * scale), (int)ceil((y + h) * scale)};
}
static pix_t intersect(pix_t a, pix_t b) {
  if (a.x < b.x)
    a.x = b.x;
  if (a.y < b.y)
    a.y = b.y;
  if (a.r > b.r)
    a.r = b.r;
  if (a.b > b.b)
    a.b = b.b;
  return a;
}
static uint64_t mix(uint64_t hash, uint64_t value) {
  hash ^= value;
  return hash * 0x100000001b3ULL;
}
static uint64_t quantize(double value) {
  return (uint64_t)llround(value * 64.0);
}
static void premultiply(uint8_t *rgba, int count) {
  for (int i = 0; i < count; i++) {
    uint8_t *px = rgba + (size_t)i * 4;
    unsigned a = px[3];
    uint8_t r = px[0], g = px[1], b = px[2];
    px[0] = (uint8_t)((b * a) / 255);
    px[1] = (uint8_t)((g * a) / 255);
    px[2] = (uint8_t)((r * a) / 255);
  }
}
static void blend(uint8_t *dst, int dw, int dh, const uint8_t *src, int sw,
                  int sh, int ox, int oy, pix_t limit) {
  for (int y = 0; y < sh; y++) {
    int dy = oy + y;
    if (dy < 0 || dy >= dh || dy < limit.y || dy >= limit.b)
      continue;
    for (int x = 0; x < sw; x++) {
      int dx = ox + x;
      if (dx < 0 || dx >= dw || dx < limit.x || dx >= limit.r)
        continue;
      const uint8_t *s = src + ((size_t)y * (size_t)sw + (size_t)x) * 4;
      unsigned sa = s[3];
      if (!sa)
        continue;
      uint8_t *d = dst + ((size_t)dy * (size_t)dw + (size_t)dx) * 4;
      if (sa == 255) {
        memcpy(d, s, 4);
        continue;
      }
      unsigned inv = 255 - sa;
      for (int k = 0; k < 4; k++)
        d[k] = (uint8_t)(s[k] + (d[k] * inv) / 255);
    }
  }
}
static NSVGrasterizer *raster(void) {
  if (!rasterizer)
    rasterizer = nsvgCreateRasterizer();
  return rasterizer;
}
static cache_slot_t *cache_find(uint64_t key, int w, int h) {
  for (int i = 0; i < CACHE_SLOTS; i++) {
    cache_slot_t *slot = &cache[i];
    if (slot->pixels && slot->key == key && slot->w == w && slot->h == h) {
      slot->used = ++cache_clock;
      return slot;
    }
  }
  return NULL;
}
static void cache_store(uint64_t key, uint8_t *pixels, int w, int h) {
  cache_slot_t *slot = &cache[0];
  for (int i = 0; i < CACHE_SLOTS; i++) {
    if (!cache[i].pixels) {
      slot = &cache[i];
      break;
    }
    if (cache[i].used < slot->used)
      slot = &cache[i];
  }
  free(slot->pixels);
  *slot = (cache_slot_t){key, ++cache_clock, w, h, pixels};
}
static int append_rect(char *svg, int used, double x, double y, double w,
                       double h, double radius, uint32_t color) {
  if (used < 0 || !(color >> 24) || w <= 0 || h <= 0)
    return used;
  double rx = radius;
  if (rx > w / 2)
    rx = w / 2;
  if (rx > h / 2)
    rx = h / 2;
  if (rx < 0)
    rx = 0;
  int wrote = snprintf(
      svg + used, SVG_BYTES - (size_t)used,
      "<rect x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" height=\"%.3f\" rx=\"%.3f\" "
      "fill=\"#%06x\" fill-opacity=\"%.4f\" stroke=\"none\"/>",
      x, y, w, h, rx, color & 0xffffffU, (color >> 24) / 255.0);
  if (wrote < 0 || used + wrote >= SVG_BYTES)
    return -1;
  return used + wrote;
}
static int append_cut(char *svg, int used, double x, double y, double w,
                      double h, double cut, uint32_t color) {
  if (used < 0 || !(color >> 24) || w <= 0 || h <= 0)
    return used;
  cut = fmax(0, fmin(cut, fmin(w, h) / 2));
  int wrote =
      snprintf(svg + used, SVG_BYTES - (size_t)used,
               "<path d=\"M%.3f %.3f H%.3f L%.3f %.3f V%.3f L%.3f %.3f H%.3f "
               "L%.3f %.3f V%.3f Z\" fill=\"#%06x\" fill-opacity=\"%.4f\"/>",
               x + cut, y, x + w - cut, x + w, y + cut, y + h - cut,
               x + w - cut, y + h, x + cut, x, y + h - cut, y + cut,
               color & 0xffffffU, (color >> 24) / 255.0);
  return wrote < 0 || used + wrote >= SVG_BYTES ? -1 : used + wrote;
}

static uint64_t shape_key(const sign_shape_t *shape, double x, double y,
                          double w, double h, double radius, double stroke) {
  uint64_t key = 14695981039346656037ULL;
  key = mix(key, (uint64_t)shape->kind);
  key = mix(key, quantize(x - floor(x)));
  key = mix(key, quantize(y - floor(y)));
  key = mix(key, quantize(w));
  key = mix(key, quantize(h));
  key = mix(key, quantize(radius));
  key = mix(key, quantize(stroke));
  key = mix(key, quantize(shape->rotation));
  key = mix(key, shape->fill);
  key = mix(key, shape->outline);
  return key;
}
static double spin_extra(double w, double h, double degrees) {
  if (fabs(degrees) < 0.05)
    return 0;
  double rad = degrees * 3.141592653589793 / 180.0;
  double cs = cos(rad), sn = sin(rad);
  double extra = 0;
  double hx = w / 2, hy = h / 2;
  for (int i = 0; i < 4; i++) {
    double dx = (i & 1) ? hx : -hx;
    double dy = (i & 2) ? hy : -hy;
    double rx = fabs(cs * dx - sn * dy) - hx;
    double ry = fabs(sn * dx + cs * dy) - hy;
    if (rx > extra)
      extra = rx;
    if (ry > extra)
      extra = ry;
  }
  return extra > 0 ? extra : 0;
}
static void clockwise(double x, double y, double ox, double oy, double deg,
                      double *out_x, double *out_y) {
  double rad = deg * 3.141592653589793 / 180.0;
  double cs = cos(rad), sn = sin(rad);
  double dx = x - ox, dy = y - oy;
  *out_x = ox + dx * cs - dy * sn;
  *out_y = oy + dx * sn + dy * cs;
}
static int build_svg(char *svg, int bw, int bh, const sign_shape_t *shape,
                     double lx, double ly, double w, double h, double radius,
                     double stroke, double cx, double cy) {
  int used = snprintf(svg, SVG_BYTES,
                      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" "
                      "height=\"%d\">",
                      bw, bh);
  if (used < 0 || used >= SVG_BYTES)
    return -1;
  bool rotate = fabs(shape->rotation) >= 0.05;
  if (rotate) {
    int wrote = snprintf(svg + used, SVG_BYTES - (size_t)used,
                         "<g transform=\"rotate(%.3f %.3f %.3f)\">",
                         shape->rotation, cx, cy);
    if (wrote < 0 || used + wrote >= SVG_BYTES)
      return -1;
    used += wrote;
  }
  if (shape->kind == SIGN_CHECK) {
    double sx = w / 16.0, sy = h / 13.0;
    int wrote = snprintf(
        svg + used, SVG_BYTES - (size_t)used,
        "<path d=\"M%.3f %.3f L%.3f %.3f L%.3f %.3f\" fill=\"none\" "
        "stroke=\"#%06x\" stroke-opacity=\"%.4f\" stroke-width=\"%.3f\" "
        "stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",
        lx + 2 * sx, ly + 7 * sy, lx + 6 * sx, ly + 11 * sy, lx + 14 * sx,
        ly + 2 * sy, shape->outline & 0xffffffU, (shape->outline >> 24) / 255.0,
        stroke);
    if (wrote < 0 || used + wrote >= SVG_BYTES)
      return -1;
    used += wrote;
  } else if (shape->kind == SIGN_CROSS) {
    // Two strokes of equal length, so the mark stays square in any box.
    double arm = fmin(w, h) / 2 - stroke / 2;
    double mx = lx + w / 2, my = ly + h / 2;
    int wrote = snprintf(
        svg + used, SVG_BYTES - (size_t)used,
        "<path d=\"M%.3f %.3f L%.3f %.3f M%.3f %.3f L%.3f %.3f\" fill=\"none\" "
        "stroke=\"#%06x\" stroke-opacity=\"%.4f\" stroke-width=\"%.3f\" "
        "stroke-linecap=\"round\"/>",
        mx - arm, my - arm, mx + arm, my + arm, mx + arm, my - arm, mx - arm,
        my + arm, shape->outline & 0xffffffU, (shape->outline >> 24) / 255.0,
        stroke);
    if (wrote < 0 || used + wrote >= SVG_BYTES)
      return -1;
    used += wrote;
  } else if (shape->kind == SIGN_CUT) {
    double cut = fmin(radius, fmin(w, h) / 2);
    if (stroke > .05 && (shape->outline >> 24)) {
      used = append_cut(svg, used, lx, ly, w, h, cut, shape->outline);
      // Parallel inset of a 45-degree edge, not a second arbitrary polygon.
      double inner = cut - stroke * (2 - sqrt(2));
      used = append_cut(svg, used, lx + stroke, ly + stroke, w - 2 * stroke,
                        h - 2 * stroke, inner, shape->fill);
    } else
      used = append_cut(svg, used, lx, ly, w, h, cut, shape->fill);
  } else if (stroke > 0.05 && (shape->outline >> 24)) {
    used = append_rect(svg, used, lx, ly, w, h, radius, shape->outline);
    double inner = radius - stroke;
    used = append_rect(svg, used, lx + stroke, ly + stroke, w - stroke * 2,
                       h - stroke * 2, inner, shape->fill);
  } else
    used = append_rect(svg, used, lx, ly, w, h, radius, shape->fill);
  if (used < 0)
    return -1;
  int tail = snprintf(svg + used, SVG_BYTES - (size_t)used, "%s</svg>",
                      rotate ? "</g>" : "");
  if (tail < 0 || used + tail >= SVG_BYTES)
    return -1;
  return 0;
}
static void draw_shape(uint8_t *dst, int dw, int dh, const sign_shape_t *shape,
                       double scale, pix_t bounds, bool store) {
  double x = shape->x * scale, y = shape->y * scale;
  double w = shape->w * scale, h = shape->h * scale;
  double radius = shape->radius * scale, stroke = shape->stroke * scale;
  if (w <= 0.05 || h <= 0.05)
    return;
  bool orbit = shape->orbit && fabs(shape->rotation) >= 0.05;
  double rot_x = x + w / 2, rot_y = y + h / 2;
  double min_x = x, min_y = y, max_x = x + w, max_y = y + h;
  if (orbit) {
    rot_x = shape->origin_x * scale;
    rot_y = shape->origin_y * scale;
    min_x = min_y = 1e9;
    max_x = max_y = -1e9;
    for (int i = 0; i < 4; i++) {
      double rx, ry;
      clockwise(x + ((i & 1) ? w : 0), y + ((i & 2) ? h : 0), rot_x, rot_y,
                shape->rotation, &rx, &ry);
      if (rx < min_x)
        min_x = rx;
      if (ry < min_y)
        min_y = ry;
      if (rx > max_x)
        max_x = rx;
      if (ry > max_y)
        max_y = ry;
    }
  }
  double pad = SHAPE_PAD + (orbit ? 0 : spin_extra(w, h, shape->rotation));
  if (shape->kind == SIGN_CHECK || shape->kind == SIGN_CROSS)
    pad += stroke;
  int left = (int)floor(min_x - pad), top = (int)floor(min_y - pad);
  int right = (int)ceil(max_x + pad), bottom = (int)ceil(max_y + pad);
  int bw = right - left, bh = bottom - top;
  if (bw <= 0 || bh <= 0 || bw > 8192 || bh > 8192)
    return;
  bool cacheable = store && (size_t)bw * (size_t)bh <= (size_t)512 * 512;
  pix_t limit = bounds;
  if (shape->clipped)
    limit = intersect(limit, pix_of(shape->clip_x, shape->clip_y, shape->clip_w,
                                    shape->clip_h, scale));
  if (limit.r <= limit.x || limit.b <= limit.y)
    return;
  uint64_t key = shape_key(shape, x, y, w, h, radius, stroke);
  if (orbit) {
    key = mix(key, quantize(rot_x - x));
    key = mix(key, quantize(rot_y - y));
  }
  cache_slot_t *slot = cacheable ? cache_find(key, bw, bh) : NULL;
  uint8_t *pixels = slot ? slot->pixels : NULL;
  if (!pixels) {
    char svg[SVG_BYTES];
    if (build_svg(svg, bw, bh, shape, x - left, y - top, w, h, radius, stroke,
                  rot_x - left, rot_y - top))
      return;
    NSVGrasterizer *r = raster();
    NSVGimage *image = nsvgParse(svg, "px", 96);
    if (!r || !image) {
      nsvgDelete(image);
      return;
    }
    pixels = calloc((size_t)bw * (size_t)bh, 4);
    if (!pixels) {
      nsvgDelete(image);
      return;
    }
    nsvgRasterize(r, image, 0, 0, 1, pixels, bw, bh, bw * 4);
    nsvgDelete(image);
    premultiply(pixels, bw * bh);
    if (cacheable)
      cache_store(key, pixels, bw, bh);
  }
  blend(dst, dw, dh, pixels, bw, bh, left, top, limit);
  if (!slot && !cacheable)
    free(pixels);
}
static void draw_tag(uint8_t *dst, int dw, int dh, const sign_text_t *text,
                     double scale, pix_t bounds) {
  double s = text->tag_scale > 0.05 ? text->tag_scale : 1;
  float name_px = (float)(text->px * s);
  int name_w = text_measure(text->value, name_px, true);
  int meta_w = text_measure(text->meta, (float)(text->meta_px * s), false);
  double gap = text->gap * s;
  double content = name_w + (meta_w > 0 ? gap + meta_w : 0);
  double border = 2 * s, pad_x = 10 * s, pad_t = 5 * s, pad_b = 6 * s;
  // Unitless line-height 1.2. The plate is anchored on the resting bottom
  // and scales about that plate's center, matching the pop transition.
  double line_h = text->px * 1.2 * s;
  double box_h = border * 2 + pad_t + line_h + pad_b;
  double box_w = border * 2 + pad_x * 2 + content;
  double box_h1 = 4 + 5 + text->px * 1.2 + 6;
  double center_y = text->anchor_y - box_h1 / 2;
  double box_top = center_y - box_h / 2;
  double line_top = box_top + border + pad_t;
  double baseline = text_baseline(line_top, line_h, name_px, true);
  double left = text->x - box_w / 2;
  sign_shape_t plate = {.kind = SIGN_RECT,
                        .x = left,
                        .y = box_top,
                        .w = box_w,
                        .h = box_h,
                        .radius = 10 * s,
                        .stroke = border,
                        .fill = text->back,
                        .outline = text->color};
  draw_shape(dst, dw, dh, &plate, scale, bounds, false);
  double name_x = left + border + pad_x;
  double meta_x = name_x + name_w + gap;
  pix_t clip = intersect(bounds, pix_of(left, box_top, box_w, box_h, scale));
  if (clip.r <= clip.x || clip.b <= clip.y)
    return;
  text_clip_t box = {clip.x, clip.y, clip.r - clip.x, clip.b - clip.y};
  int base = (int)lround(baseline * scale);
  if (text->value[0])
    text_draw_clip(dst, dw, dh, (int)lround(name_x * scale), base, text->value,
                   (float)(text->px * s), true, text->color, 0, box);
  if (text->meta[0])
    text_draw_clip(dst, dw, dh, (int)lround(meta_x * scale), base, text->meta,
                   (float)(text->meta_px * s), false, text->meta_color, 0, box);
}
static void draw_text(uint8_t *dst, int dw, int dh, const sign_text_t *text,
                      double scale, pix_t bounds) {
  if (text->back >> 24) {
    draw_tag(dst, dw, dh, text, scale, bounds);
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
  int meta_w = text_measure(text->meta, (float)text->meta_px, false);
  double budget = text->w - text->gap - meta_w;
  if (budget < 0)
    budget = 0;
  double name_x = text->reverse ? text->x + meta_w + text->gap : text->x;
  double meta_x = text->reverse ? text->x : text->x + text->w - meta_w;
  pix_t clip = intersect(
      bounds, pix_of(text->x, text->clip_y, text->w, text->clip_h, scale));
  if (clip.r <= clip.x || clip.b <= clip.y)
    return;
  text_clip_t box = {clip.x, clip.y, clip.r - clip.x, clip.b - clip.y};
  if (text->value[0] && budget > 0.5)
    text_draw_clip(dst, dw, dh, (int)lround(name_x * scale), base, text->value,
                   name_px, true, text->color, (int)lround(budget * scale),
                   box);
  text_metrics_t metrics;
  if (text->caret && (text->meta_color >> 24) && text->px > 0 &&
      text_metrics(name_px, true, &metrics)) {
    int measured = text_measure(text->value, name_px, true);
    double used = measured;
    if (used > budget)
      used = budget;
    double caret_w = text->px * (2.0 / 12.0);
    sign_shape_t bar = {.kind = SIGN_RECT,
                        .x = name_x + used + text->gap,
                        .y = baseline - metrics.ascent,
                        .w = caret_w,
                        .h = text->px,
                        .radius = caret_w / 2,
                        .fill = text->meta_color};
    draw_shape(dst, dw, dh, &bar, scale, bounds, false);
  }
  if (text->meta[0])
    text_draw_clip(dst, dw, dh, (int)lround(meta_x * scale), base, text->meta,
                   (float)text->meta_px, false, text->meta_color, 0, box);
}
void sign_draw(uint8_t *dst, int dw, int dh, int scale_120,
               const sign_frame_t *frame, sign_draw_layer_t layer) {
  if (!dst || !frame || dw <= 0 || dh <= 0 || scale_120 < 1 ||
      frame->bounds_w <= 0 || frame->bounds_h <= 0)
    return;
  double scale = scale_120 / 120.0;
  if (cache_scale != scale_120) {
    cache_clear();
    cache_scale = scale_120;
  }
  text_set_scale(scale_120);
  pix_t bounds = pix_of(frame->bounds_x, frame->bounds_y, frame->bounds_w,
                        frame->bounds_h, scale);
  bool store = !frame->transitioning;
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    bool over = shape->above;
    if (over == (layer == SIGN_DRAW_OVER))
      draw_shape(dst, dw, dh, shape, scale, bounds, store);
  }
  for (int i = 0; i < frame->text_count; i++) {
    const sign_text_t *text = &frame->texts[i];
    if (text->above == (layer == SIGN_DRAW_OVER))
      draw_text(dst, dw, dh, text, scale, bounds);
  }
}
void sign_draw_cleanup(void) {
  cache_clear();
  cache_clock = 0;
  cache_scale = 0;
  if (rasterizer)
    nsvgDeleteRasterizer(rasterizer);
  rasterizer = NULL;
}
