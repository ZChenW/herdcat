#include "graphics/sign_draw.h"

#include "graphics/pixel_rect.h"
#include "graphics/signs.h"
#include "graphics/text.h"
#include "sign_draw_internal.h"

#include <math.h>
#include <nanosvg.h>
#include <nanosvgrast.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __GLIBC__
#  include <malloc.h>
#endif

#define CACHE_SLOTS   2048
#define CACHE_BYTES   ((size_t)16 * 1024 * 1024)
#define SHAPE_PAD     3.0
#define SVG_BYTES     1024
#define CACHE_IDLE_MS 60000

typedef struct {
  uint16_t left, right;
} opaque_run_t;

typedef struct {
  uint64_t key, used;
  int w, h;
  uint8_t *pixels;
  opaque_run_t *opaque;
  bool phase, panel_only;
} cache_slot_t;

static NSVGrasterizer *rasterizer;
static cache_slot_t cache[CACHE_SLOTS];
static uint64_t cache_clock;
static int cache_scale;
static size_t cache_bytes;
static int cache_phases;
static int64_t cache_release_at;
static bool panel_drawing;

static size_t slot_bytes(const cache_slot_t *slot) {
  return (size_t)slot->w * slot->h * 4 +
         (slot->opaque ? (size_t)slot->h * sizeof(*slot->opaque) : 0);
}

#ifdef TEST_BUILD
static uint64_t opaque_bytes_copied;
static uint64_t cache_hits, cache_misses, tag_hits, tag_misses;
static bool cache_disabled;
sign_draw_cache_stats_t sign_draw_cache_stats(void) {
  sign_draw_cache_stats_t stats = {.hits = cache_hits,
                                   .misses = cache_misses,
                                   .tag_hits = tag_hits,
                                   .tag_misses = tag_misses,
                                   .opaque_bytes_copied = opaque_bytes_copied,
                                   .slot_limit = CACHE_SLOTS,
                                   .byte_limit = CACHE_BYTES};
  for (int i = 0; i < CACHE_SLOTS; i++) {
    if (cache[i].pixels) {
      stats.entries++;
      stats.bytes += slot_bytes(&cache[i]);
      if (cache[i].phase) {
        stats.phase_entries++;
        stats.phase_bytes += slot_bytes(&cache[i]);
      }
    }
  }
  return stats;
}
void sign_draw_cache_reset_stats(void) {
  cache_hits = cache_misses = tag_hits = tag_misses = 0;
  opaque_bytes_copied = 0;
}
void sign_draw_cache_disable(bool disable) {
  cache_disabled = disable;
}
#endif

static void cache_drop(cache_slot_t *slot) {
  if (slot->phase)
    cache_phases--;
  if (slot->pixels)
    cache_bytes -= slot_bytes(slot);
  free(slot->pixels);
  free(slot->opaque);
  *slot = (cache_slot_t){0};
}
static void cache_clear(void) {
  for (int i = 0; i < CACHE_SLOTS; i++)
    cache_drop(&cache[i]);
  cache_release_at = 0;
}

void sign_draw_cache_update(bool waiting, int64_t now_ms) {
  if (waiting || !cache_phases) {
    cache_release_at = 0;
    return;
  }
  if (!cache_release_at)
    cache_release_at = now_ms + CACHE_IDLE_MS;
  if (now_ms < cache_release_at)
    return;
  for (int i = 0; i < CACHE_SLOTS; i++)
    if (cache[i].phase)
      cache_drop(&cache[i]);
#ifdef __GLIBC__
  // Small bitmaps share the heap with live font objects. Return the freed
  // pages as well, instead of retaining them in the allocator indefinitely.
  malloc_trim(0);
#endif
  cache_release_at = 0;
}
int sign_draw_cache_timeout(int64_t now_ms) {
  if (!cache_phases || !cache_release_at)
    return -1;
  int64_t left = cache_release_at - now_ms;
  return left > 0 ? (int)left : 0;
}

pix_t pix_of(double x, double y, double w, double h, double scale) {
  return (pix_t){(int)floor(x * scale), (int)floor(y * scale),
                 (int)ceil((x + w) * scale), (int)ceil((y + h) * scale)};
}
pix_t intersect(pix_t a, pix_t b) {
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
                  int sh, int ox, int oy, pix_t limit,
                  const opaque_run_t *opaque) {
  pix_t clip = intersect(intersect(limit, (pix_t){0, 0, dw, dh}),
                         (pix_t){ox, oy, ox + sw, oy + sh});
  if (clip.r <= clip.x || clip.b <= clip.y)
    return;
  for (int dy = clip.y; dy < clip.b; dy++) {
    const uint8_t *s =
        src + ((size_t)(dy - oy) * (size_t)sw + (size_t)(clip.x - ox)) * 4;
    uint8_t *d = dst + ((size_t)dy * (size_t)dw + (size_t)clip.x) * 4;
    int fast_left = clip.r, fast_right = clip.r;
    if (opaque) {
      fast_left = ox + opaque[dy - oy].left;
      fast_right = ox + opaque[dy - oy].right;
      if (fast_left < clip.x)
        fast_left = clip.x;
      if (fast_right > clip.r)
        fast_right = clip.r;
    }
    for (int dx = clip.x; dx < clip.r; dx++, s += 4, d += 4) {
      if (dx == fast_left && fast_right > fast_left) {
        size_t bytes = (size_t)(fast_right - fast_left) * 4;
        memcpy(d, s, bytes);
#ifdef TEST_BUILD
        opaque_bytes_copied += bytes;
#endif
        dx = fast_right - 1;
        s += bytes - 4;
        d += bytes - 4;
        continue;
      }
      unsigned sa = s[3];
      if (!sa)
        continue;
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
static cache_slot_t *cache_oldest(void) {
  cache_slot_t *oldest = NULL;
  for (int i = 0; i < CACHE_SLOTS; i++) {
    if (cache[i].pixels && (!oldest || cache[i].used < oldest->used))
      oldest = &cache[i];
  }
  return oldest;
}
static opaque_run_t *opaque_rows(const uint8_t *pixels, int w, int h) {
  opaque_run_t *rows = calloc((size_t)h, sizeof(*rows));
  if (!rows)
    return NULL;
  for (int y = 0; y < h; y++) {
    int start = 0;
    for (int x = 0; x <= w; x++) {
      bool full = x < w && pixels[((size_t)y * w + x) * 4 + 3] == 255;
      if (full)
        continue;
      if (x - start > rows[y].right - rows[y].left)
        rows[y] = (opaque_run_t){(uint16_t)start, (uint16_t)x};
      start = x + 1;
    }
  }
  return rows;
}
// Ownership passes to the cache; cache_drop() frees this mutable allocation.
// NOLINTNEXTLINE(readability-non-const-parameter)
static void cache_store(uint64_t key, uint8_t *pixels, int w, int h,
                        bool phase) {
  opaque_run_t *opaque = opaque_rows(pixels, w, h);
  size_t bytes = (size_t)w * h * 4 + (opaque ? (size_t)h * sizeof(*opaque) : 0);
  while (cache_bytes + bytes > CACHE_BYTES)
    cache_drop(cache_oldest());
  cache_slot_t *slot = NULL;
  for (int i = 0; i < CACHE_SLOTS; i++) {
    if (!cache[i].pixels) {
      slot = &cache[i];
      break;
    }
  }
  if (!slot)
    slot = cache_oldest();
  cache_drop(slot);
  *slot = (cache_slot_t){.key = key,
                         .used = ++cache_clock,
                         .w = w,
                         .h = h,
                         .pixels = pixels,
                         .opaque = opaque,
                         .phase = phase,
                         .panel_only = panel_drawing};
  if (phase)
    cache_phases++;
  cache_bytes += bytes;
}
bool draw_cached_tag(uint8_t *dst, int dw, int dh, const sign_text_t *text,
                     double scale, pix_t bounds, double left, double top,
                     double w, double h) {
  int bw = (int)ceil(w * scale) + 6, bh = (int)ceil(h * scale) + 6;
  int x = (int)lround(left * scale) - 3;
  int y = (int)lround(top * scale) - 3;
  if (bw <= 0 || bh <= 0 || bw > 8192 || bh > 8192 ||
      (size_t)bw * bh > (size_t)512 * 512)
    return false;
#ifdef TEST_BUILD
  if (cache_disabled)
    return false;
#endif
  sign_text_t signature = *text;
  signature.x = signature.anchor_y = 0;
  signature.surface_width = 0;
  uint64_t key = mix(UINT64_C(0x7461676269746d61), text_layout_key());
  const unsigned char *bytes = (const unsigned char *)&signature;
  for (size_t i = 0; i < sizeof(signature); i++)
    key = mix(key, bytes[i]);
  cache_slot_t *slot = cache_find(key, bw, bh);
  uint8_t *pixels = slot ? slot->pixels : NULL;
  if (slot) {
#ifdef TEST_BUILD
    cache_hits++;
    tag_hits++;
#endif
  } else {
#ifdef TEST_BUILD
    tag_misses++;
#endif
    pixels = calloc((size_t)bw * bh, 4);
    if (!pixels)
      return false;
    sign_text_t local = *text;
    local.x = w / 2 + 3 / scale;
    local.anchor_y = sign_tag_height(text) + 3 / scale;
    local.surface_width = 0;
    // store=false prevents recursion. The tag background contributes the
    // ordinary shape miss; its glyphs are composed over the opaque paper.
    draw_text(pixels, bw, bh, &local, scale, (pix_t){0, 0, bw, bh}, false);
    cache_store(key, pixels, bw, bh, true);
  }
  blend(dst, dw, dh, pixels, bw, bh, x, y, bounds, slot ? slot->opaque : NULL);
  return true;
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
typedef struct {
  int bw;
  int bh;
  double x;
  double y;
  double w;
  double h;
  double radius;
  double stroke;
  double rot_x;
  double rot_y;
  int left;
  int top;
} shape_raster_t;

static uint8_t *raster_shape(const sign_shape_t *shape,
                             const shape_raster_t *drawing) {
  char svg[SVG_BYTES];
  sign_shape_t source = *shape;
  double sy = drawing->y - drawing->top, oy = drawing->rot_y - drawing->top;
  if (shape->reflected) {
    source.rotation = -source.rotation;
    sy = drawing->bh - (sy + drawing->h);
    oy = drawing->bh - oy;
  }
  if (build_svg(svg, drawing->bw, drawing->bh, &source,
                drawing->x - drawing->left, sy, drawing->w, drawing->h,
                drawing->radius, drawing->stroke,
                drawing->rot_x - drawing->left, oy))
    return NULL;
  NSVGrasterizer *r = raster();
  NSVGimage *image = nsvgParse(svg, "px", 96);
  if (!r || !image) {
    nsvgDelete(image);
    return NULL;
  }
  uint8_t *pixels = calloc((size_t)drawing->bw * (size_t)drawing->bh, 4);
  if (!pixels) {
    nsvgDelete(image);
    return NULL;
  }
  nsvgRasterize(r, image, 0, 0, 1, pixels, drawing->bw, drawing->bh,
                drawing->bw * 4);
  nsvgDelete(image);
  // NanoSVG's scanline samples are asymmetric about pixel centers. Reflect
  // the original bitmap so both sides retain precisely the same edge ink.
  if (shape->reflected) {
    for (int row = 0; row < drawing->bh / 2; row++) {
      for (int column = 0; column < drawing->bw * 4; column++) {
        size_t one = (size_t)row * drawing->bw * 4 + column;
        size_t two = (size_t)(drawing->bh - 1 - row) * drawing->bw * 4 + column;
        uint8_t byte = pixels[one];
        pixels[one] = pixels[two];
        pixels[two] = byte;
      }
    }
  }
  premultiply(pixels, drawing->bw * drawing->bh);
  return pixels;
}

void draw_shape(uint8_t *dst, int dw, int dh, const sign_shape_t *shape,
                double scale, pix_t bounds, bool store) {
  double x = shape->x * scale, y = shape->y * scale;
  if (shape->pixel_snap) {
    x = round(x);
    y = round(y);
  }
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
    if (shape->pixel_snap) {
      rot_x = round(rot_x);
      rot_y = round(rot_y);
    }
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
  // Equivalent output/surface translations can land a few ulps either
  // side of an integer. Stabilize the bitmap extent without moving ink.
  double edge = min_x - pad;
  if (fabs(edge - round(edge)) < 1e-9)
    edge = round(edge);
  int left = (int)floor(edge), top = (int)floor(min_y - pad);
  int right = (int)ceil(max_x + pad), bottom = (int)ceil(max_y + pad);
  int bw = right - left, bh = bottom - top;
  if (bw <= 0 || bh <= 0 || bw > 8192 || bh > 8192)
    return;
  bool cacheable = store && (size_t)bw * (size_t)bh <= (size_t)512 * 512;
#ifdef TEST_BUILD
  cacheable = cacheable && !cache_disabled;
#endif
  pix_t limit = bounds;
  if (shape->clipped)
    limit = intersect(limit, pix_of(shape->clip_x, shape->clip_y, shape->clip_w,
                                    shape->clip_h, scale));
  // Reject disjoint shapes before looking up or rebuilding their bitmap.
  pix_t visible = intersect(limit, (pix_t){left, top, right, bottom});
  visible = intersect(visible, (pix_t){0, 0, dw, dh});
  if (visible.r <= visible.x || visible.b <= visible.y)
    return;
  uint64_t key = shape_key(shape, x, y, w, h, radius, stroke);
  key = mix(key, (uint64_t)cache_scale);
  if (shape->reflected)
    key = mix(key, UINT64_C(0x62656c6f77));
  if (orbit) {
    key = mix(key, quantize(rot_x - x));
    key = mix(key, quantize(rot_y - y));
  }
  cache_slot_t *slot = cacheable ? cache_find(key, bw, bh) : NULL;
  if (slot && !panel_drawing)
    slot->panel_only = false;
  // A bitmap also used by a settled non-waiting shape is worth retaining.
  if (slot && slot->phase && !shape->pixel_snap) {
    slot->phase = false;
    cache_phases--;
  }
#ifdef TEST_BUILD
  if (slot)
    cache_hits++;
  else
    cache_misses++;
#endif
  uint8_t *pixels = slot ? slot->pixels : NULL;
  if (!pixels) {
    pixels = raster_shape(shape, &(shape_raster_t){.bw = bw,
                                                   .bh = bh,
                                                   .x = x,
                                                   .y = y,
                                                   .w = w,
                                                   .h = h,
                                                   .radius = radius,
                                                   .stroke = stroke,
                                                   .rot_x = rot_x,
                                                   .rot_y = rot_y,
                                                   .left = left,
                                                   .top = top});
    if (!pixels)
      return;
    if (cacheable)
      cache_store(key, pixels, bw, bh, shape->pixel_snap);
  }
  blend(dst, dw, dh, pixels, bw, bh, left, top, limit,
        slot ? slot->opaque : NULL);
  if (!slot && !cacheable)
    free(pixels);
}
void sign_draw(uint8_t *dst, int dw, int dh, int scale_120,
               const sign_frame_t *frame, sign_draw_layer_t layer) {
  sign_draw_clip(dst, dw, dh, scale_120, frame, layer,
                 (pixel_rect_t){0, 0, dw, dh});
}
void sign_draw_clip(uint8_t *dst, int dw, int dh, int scale_120,
                    const sign_frame_t *frame, sign_draw_layer_t layer,
                    pixel_rect_t clip) {
  if (!dst || !frame || dw <= 0 || dh <= 0 || scale_120 < 1 ||
      frame->bounds_w <= 0 || frame->bounds_h <= 0)
    return;
  clip = pixel_rect_clip(clip, dw, dh);
  if (clip.w <= 0 || clip.h <= 0)
    return;
  double scale = scale_120 / 120.0;
  // Outputs with different scales are drawn in turn. The key carries the
  // scale, so each keeps its own bitmaps instead of clearing the other's.
  cache_scale = scale_120;
  text_set_scale(scale_120);
  pix_t bounds = pix_of(frame->bounds_x, frame->bounds_y, frame->bounds_w,
                        frame->bounds_h, scale);
  bounds = intersect(bounds,
                     (pix_t){clip.x, clip.y, clip.x + clip.w, clip.y + clip.h});
  if (bounds.r <= bounds.x || bounds.b <= bounds.y)
    return;
  bool store = !frame->transitioning;
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    bool over = shape->above;
    if (over == (layer == SIGN_DRAW_OVER)) {
      draw_shape(dst, dw, dh, shape, scale, bounds, store);
      if (shape->kind == SIGN_BADGE)
        draw_badge_text(dst, dw, dh, shape, scale, bounds);
    }
  }
  for (int i = 0; i < frame->text_count; i++) {
    const sign_text_t *text = &frame->texts[i];
    if (text->above == (layer == SIGN_DRAW_OVER))
      draw_text(dst, dw, dh, text, scale, bounds, store);
  }
}
void sign_draw_font_panel(uint8_t *dst, int dw, int dh, int scale_120,
                          const sign_frame_t *frame) {
  panel_drawing = true;
  sign_draw(dst, dw, dh, scale_120, frame, SIGN_DRAW_OVER);
  panel_drawing = false;
}
void sign_draw_font_panel_cleanup(void) {
  for (int i = 0; i < CACHE_SLOTS; i++)
    if (cache[i].panel_only)
      cache_drop(&cache[i]);
  // Rasterizer scratch storage can grow to the panel's size. Shape bitmaps
  // and text caches remain valid when the scratch storage is recreated.
  if (rasterizer)
    nsvgDeleteRasterizer(rasterizer);
  rasterizer = NULL;
}
void sign_draw_cleanup(void) {
  cache_clear();
  cache_clock = 0;
  cache_scale = 0;
  if (rasterizer)
    nsvgDeleteRasterizer(rasterizer);
  rasterizer = NULL;
}
