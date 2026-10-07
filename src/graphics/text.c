#define _POSIX_C_SOURCE 200809L
#include "graphics/text.h"

#include "utils/utf8.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include FT_TRUETYPE_TABLES_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __GLIBC__
#  include <malloc.h>
#endif

#define GLYPH_LIMIT  512
#define FACE_LIMIT   64
#define METRIC_LIMIT 32
typedef struct {
  int size, scale;
  bool bold, valid;
  text_metrics_t value;
} metric_t;
typedef struct {
  FT_Face ft;
  char *file;
  int index;
  uint64_t used;
  bool resident;
  metric_t metrics[METRIC_LIMIT];
} face_t;
typedef struct {
  uint32_t cp;
  int size, face;
  bool bold;
  int w, h, left, top, advance;
  uint8_t *pixels;
  uint64_t used;
  bool resident;
} glyph_t;
static FT_Library library;
static FcConfig *fonts;
static char *family_name;
static face_t faces[FACE_LIMIT];
static int face_count, primary[2], scale_120 = 120;
static glyph_t glyphs[GLYPH_LIMIT];
static uint64_t clock_stamp;
// Which face draws a code point in a family. Asking Fontconfig costs about a
// millisecond, and without this every glyph of every redraw asked again.
#define ROUTE_LIMIT 4096
typedef struct {
  uint64_t family;
  uint32_t cp;
  int16_t face;
  bool bold, used;
} route_t;
static route_t routes[ROUTE_LIMIT];
// Browsing families can collide with main-family routes. Keep the latter
// separately so a preview cannot force the signs to rematch a used fallback.
static route_t main_routes[ROUTE_LIMIT];
static int match_calls;
static bool preview_active, trim_pending;
static int measure_calls, metrics_calls;
#define MEASURE_LIMIT 256
typedef struct {
  uint64_t family;
  float px;
  int scale, width;
  bool bold, valid;
  char text[128];
} measure_t;
static measure_t measures[MEASURE_LIMIT];
static unsigned measure_next;
static uint64_t lifetime;
static uint64_t preview_family;
static void clear_routes(void) {
  memset(routes, 0, sizeof(routes));
  memset(main_routes, 0, sizeof(main_routes));
}
static uint64_t family_hash(const char *family) {
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char *p = (const unsigned char *)family; *p; p++)
    hash = (hash ^ *p) * 1099511628211ULL;
  return hash;
}
static size_t route_index(uint64_t family, uint32_t cp, bool bold) {
  uint64_t mix = (family ^ ((uint64_t)cp * 0x9e3779b97f4a7c15ULL)) + bold;
  return (mix ^ (mix >> 29)) % ROUTE_LIMIT;
}
static route_t *route_slot(uint64_t family, uint32_t cp, bool bold) {
  return &routes[route_index(family, cp, bold)];
}
static void clear_glyphs(void) {
  for (int i = 0; i < GLYPH_LIMIT; i++)
    free(glyphs[i].pixels);
  memset(glyphs, 0, sizeof(glyphs));
  clock_stamp = 0;
}
static int match(const char *family, uint32_t cp, bool bold);
// match() can open or compact faces, invalidating a route slot.
static int routed(const char *family, uint32_t cp, bool bold) {
  uint64_t key = family_hash(family);
  route_t *slot = route_slot(key, cp, bold);
  if (slot->used && slot->family == key && slot->cp == cp &&
      slot->bold == bold && slot->face >= 0 && slot->face < face_count) {
    faces[slot->face].used = ++clock_stamp;
    return slot->face;
  }
  // A family's own face has nearly all of its glyphs. Fontconfig ranks a
  // family that covers the code point first, so asking the face directly
  // gives the same answer without a lookup per code point. Code point 0 is
  // the family's own face.
  int face = -1;
  if (cp) {
    int own = routed(family, 0, bold);
    if (own >= 0 && own < face_count && FT_Get_Char_Index(faces[own].ft, cp))
      face = own;
  }
  if (face < 0)
    face = match(family, cp, bold);
  if (face >= 0 && face <= INT16_MAX) {
    // An eviction inside match() may have changed the slot.
    slot = route_slot(key, cp, bold);
    *slot = (route_t){.family = key,
                      .cp = cp,
                      .face = (int16_t)face,
                      .bold = bold,
                      .used = 1};
  }
  return face;
}
void text_cleanup(void) {
  lifetime++;
  memset(measures, 0, sizeof(measures));
  measure_next = 0;
  clear_glyphs();
  clear_routes();
  for (int i = 0; i < face_count; i++) {
    FT_Done_Face(faces[i].ft);
    free(faces[i].file);
  }
  face_count = 0;
  preview_active = trim_pending = false;
  primary[0] = primary[1] = -1;
  if (library)
    FT_Done_FreeType(library);
  if (fonts) {
    FcConfigDestroy(fonts);
    FcFini();
  }
  free(family_name);
  family_name = NULL;
  library = NULL;
  fonts = NULL;
}
// Compact all three caches together so surviving routes and glyphs stay valid.
static void remove_face(int victim) {
  for (int i = 0; i < GLYPH_LIMIT; i++) {
    if (!glyphs[i].used || glyphs[i].face != victim)
      continue;
    free(glyphs[i].pixels);
    glyphs[i] = (glyph_t){0};
  }
  FT_Done_Face(faces[victim].ft);
  free(faces[victim].file);
  int last = face_count - 1;
  for (int table = 0; table < 2; table++) {
    route_t *entries = table ? main_routes : routes;
    for (int i = 0; i < ROUTE_LIMIT; i++) {
      if (!entries[i].used)
        continue;
      if (entries[i].face == victim)
        entries[i] = (route_t){0};
      else if (entries[i].face == last)
        entries[i].face = (int16_t)victim;
    }
  }
  if (victim != last) {
    faces[victim] = faces[last];
    for (int i = 0; i < GLYPH_LIMIT; i++)
      if (glyphs[i].used && glyphs[i].face == last)
        glyphs[i].face = victim;
    if (primary[0] == last)
      primary[0] = victim;
    if (primary[1] == last)
      primary[1] = victim;
  }
  faces[last] = (face_t){0};
  face_count--;
}
static bool evict_face(void) {
  int victim = -1;
  uint64_t oldest = UINT64_MAX;
  for (int i = 0; i < face_count; i++) {
    if (i == primary[0] || i == primary[1] || faces[i].resident)
      continue;
    if (faces[i].used <= oldest) {
      oldest = faces[i].used;
      victim = i;
    }
  }
  if (victim < 0)
    return false;
  remove_face(victim);
  return true;
}
static void retain_current(bool reset) {
  uint64_t key = family_name ? family_hash(family_name) : 0;
  if (reset) {
    memset(main_routes, 0, sizeof(main_routes));
    for (int i = 0; i < face_count; i++)
      faces[i].resident = false;
    for (int i = 0; i < GLYPH_LIMIT; i++)
      glyphs[i].resident = false;
  }
  for (int i = 0; i < face_count; i++)
    if (i == primary[0] || i == primary[1])
      faces[i].resident = true;
  for (int i = 0; i < ROUTE_LIMIT; i++)
    if (routes[i].used && routes[i].family == key)
      faces[routes[i].face].resident = true;
}
void text_release_unused(bool trim) {
  // The menu restores the chosen family after the panel has closed. Never
  // discard its held fallback faces while the main face is still a preview.
  if (preview_active) {
    trim_pending = trim_pending || trim;
    return;
  }
  retain_current(false);
  for (int i = face_count - 1; i >= 0; i--)
    if (!faces[i].resident)
      remove_face(i);
  if (trim) {
#ifdef __GLIBC__
    malloc_trim(0);
#endif
  }
}
void text_preview_begin(void) {
  if (preview_active || !family_name)
    return;
  retain_current(false);
  preview_family = family_hash(family_name);
  preview_active = true;
}
void text_preview_end(void) {
  if (!preview_active)
    return;
  preview_active = false;
  retain_current(!family_name || family_hash(family_name) != preview_family);
  bool trim = trim_pending;
  trim_pending = false;
  text_release_unused(trim);
}
static int match(const char *family, uint32_t cp, bool bold) {
  match_calls++;
  FcPattern *pattern = FcPatternCreate();
  if (!pattern)
    return -1;
  if (!family) {
    FcPatternDestroy(pattern);
    return -1;
  }
  FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)family);
  FcPatternAddInteger(pattern, FC_WEIGHT,
                      bold ? FC_WEIGHT_BOLD : FC_WEIGHT_MEDIUM);
  if (cp) {
    FcCharSet *set = FcCharSetCreate();
    if (!set) {
      FcPatternDestroy(pattern);
      return -1;
    }
    FcCharSetAddChar(set, cp);
    FcPatternAddCharSet(pattern, FC_CHARSET, set);
    FcCharSetDestroy(set);
  }
  FcConfigSubstitute(fonts, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  FcResult result;
  FcPattern *matched = FcFontMatch(fonts, pattern, &result);
  FcPatternDestroy(pattern);
  if (!matched)
    return -1;
  FcChar8 *file;
  int index = 0, found = -1;
  if (FcPatternGetString(matched, FC_FILE, 0, &file) == FcResultMatch) {
    FcPatternGetInteger(matched, FC_INDEX, 0, &index);
    for (int i = 0; i < face_count; i++)
      if (faces[i].index == index && !strcmp(faces[i].file, (const char *)file))
        found = i;
    if (found < 0 && (face_count < FACE_LIMIT || evict_face())) {
      char *copy = strdup((const char *)file);
      FT_Face ft;
      if (copy && face_count < FACE_LIMIT &&
          !FT_New_Face(library, copy, index, &ft)) {
        found = face_count++;
        faces[found] = (face_t){
            .ft = ft, .file = copy, .index = index, .used = ++clock_stamp};
      } else
        free(copy);
    }
    if (found >= 0)
      faces[found].used = ++clock_stamp;
  }
  FcPatternDestroy(matched);
  return found;
}
int text_init(const char *family) {
  text_cleanup();
  scale_120 = 120;
  family_name = strdup(family && *family ? family : "sans-serif");
  fonts = FcInit() ? FcConfigReference(FcConfigGetCurrent()) : NULL;
  if (!family_name || !fonts || FT_Init_FreeType(&library))
    goto fail;
  primary[0] = match(family_name, 0, false);
  primary[1] = match(family_name, 0, true);
  if (primary[0] < 0 || primary[1] < 0)
    goto fail;
  retain_current(true);
  return 0;
fail:
  text_cleanup();
  return -1;
}
void text_set_scale(int scale) {
  if (scale < 1 || scale > 960 || scale == scale_120)
    return;
  // Glyphs are keyed by their physical size, so two outputs with different
  // scales share the cache instead of emptying it on every frame.
  scale_120 = scale;
}
static bool main_family(const char *family) {
  return !family || !*family || (family_name && !strcmp(family, family_name));
}
static int face_for(const char *family, uint32_t cp, bool bold) {
  if (!library)
    return -1;
  if (main_family(family)) {
    int face = primary[bold];
    if (face >= 0 && face < face_count &&
        FT_Get_Char_Index(faces[face].ft, cp)) {
      faces[face].used = ++clock_stamp;
      if (!preview_active)
        faces[face].resident = true;
      return face;
    }
    uint64_t key = family_hash(family_name);
    route_t *saved = &main_routes[route_index(key, cp, bold)];
    int fallback;
    if (!preview_active && saved->used && saved->family == key &&
        saved->cp == cp && saved->bold == bold)
      fallback = saved->face;
    else
      fallback = routed(family_name, cp, bold);
    if (fallback >= 0) {
      if (!preview_active) {
        faces[fallback].resident = true;
        *saved = (route_t){.family = key,
                           .cp = cp,
                           .face = (int16_t)fallback,
                           .bold = bold,
                           .used = true};
      }
      return fallback;
    }
    face = primary[bold];
    return face >= 0 && face < face_count ? face : -1;
  }
  return routed(family, cp, bold);
}
bool text_has_glyph(uint32_t cp, bool bold) {
  int f = face_for(NULL, cp, bold);
  return f >= 0 && FT_Get_Char_Index(faces[f].ft, cp) != 0;
}
static glyph_t *glyph(const char *family, uint32_t cp, float px, bool bold) {
  if (!library || !isfinite(px) || px <= 0 || px > 256)
    return NULL;
  int f = face_for(family, cp, bold);
  if (f < 0)
    return NULL;
  int size = (int)lround((double)px * scale_120 / 120 * 64);
  glyph_t *slot = NULL;
  bool resident = main_family(family) && !preview_active;
  for (int i = 0; i < GLYPH_LIMIT; i++) {
    glyph_t *g = &glyphs[i];
    if (g->used && g->cp == cp && g->bold == bold && g->size == size &&
        g->face == f) {
      g->used = ++clock_stamp;
      g->resident = g->resident || resident;
      faces[f].used = g->used;
      return g;
    }
    if (g->used && g->resident && faces[g->face].resident)
      continue;
    if (!slot || g->used < slot->used)
      slot = g;
  }
  // A full resident cache still obeys its fixed cap.
  if (!slot) {
    slot = &glyphs[0];
    for (int i = 1; i < GLYPH_LIMIT; i++)
      if (glyphs[i].used < slot->used)
        slot = &glyphs[i];
  }
  FT_Face face = faces[f].ft;
  if (FT_Set_Char_Size(face, 0, size, 72, 72) ||
      FT_Load_Char(face, cp,
                   FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL))
    return NULL;
  if (bold && !(face->style_flags & FT_STYLE_FLAG_BOLD))
    FT_GlyphSlot_Embolden(face->glyph);
  if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
    return NULL;
  FT_GlyphSlot src = face->glyph;
  int w = (int)src->bitmap.width, h = (int)src->bitmap.rows;
  if (w > 4096 || h > 4096 ||
      (w && h && src->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY))
    return NULL;
  size_t bytes = (size_t)w * (size_t)h;
  uint8_t *pixels = bytes ? malloc(bytes) : NULL;
  if (bytes && !pixels)
    return NULL;
  for (int y = 0; y < h; y++) {
    int row = src->bitmap.pitch < 0 ? h - 1 - y : y;
    memcpy(pixels + (size_t)y * (size_t)w,
           src->bitmap.buffer + row * abs(src->bitmap.pitch), (size_t)w);
  }
  free(slot->pixels);
  *slot = (glyph_t){.cp = cp,
                    .size = size,
                    .face = f,
                    .bold = bold,
                    .w = w,
                    .h = h,
                    .left = src->bitmap_left,
                    .top = src->bitmap_top,
                    .advance = (int)src->advance.x,
                    .pixels = pixels,
                    .used = ++clock_stamp,
                    .resident = resident};
  return slot;
}
static uint32_t next(const char **s) {
  uint32_t cp;
  size_t n = utf8_decode(*s, &cp);
  *s += n ? n : 1;
  return n ? cp : 0xfffd;
}
typedef struct {
  glyph_t *dot;
  int diameter, width;
  double step;
} ellipsis_t;
// U+2026 is an internal truncation marker. Use this geometry for both
// measuring and painting, independently of the font's centred ellipsis.
static ellipsis_t ellipsis_geometry(const char *family, float px, bool bold) {
  ellipsis_t e = {0};
  if (!library || !isfinite(px) || px <= 0 || px > 256)
    return e;
  int f = face_for(family, 0, bold);
  if (f >= 0 && FT_Get_Char_Index(faces[f].ft, '.'))
    e.dot = glyph(family, '.', px, bold);
  if (e.dot && e.dot->w > 0 && e.dot->h > 0) {
    e.diameter = e.dot->w;
    e.step = (int)lround(e.dot->advance / 64.0 * .8);
  } else {
    e.dot = NULL;
    e.diameter = (int)ceil((double)px * scale_120 / 120 * .12);
    e.step = e.diameter + 1;
  }
  // Keep three distinct dots at small sizes, including heavy faces.
  e.step = e.step > e.diameter ? e.step : e.diameter + 1;
  double scale = scale_120 / 120.0;
  // Match the public API's whole-logical-pixel budget at fractional scales.
  e.width = (int)floor(ceil((2 * e.step + e.diameter) / scale) * scale);
  e.step = (e.width - e.diameter) / 2.0;
  return e;
}
static double advance(const char *family, uint32_t cp, float px, bool bold) {
  if (cp == 0x2026)
    return ellipsis_geometry(family, px, bold).width;
  glyph_t *g = glyph(family, cp, px, bold);
  return g ? g->advance / 64.0 : 0;
}
static double width(const char *family, const char *s, float px, bool bold) {
  double w = 0, content_end = 0;
  if (s)
    while (*s) {
      uint32_t cp = next(&s);
      if (cp == 0x2026)
        w = content_end;
      w += advance(family, cp, px, bold);
      if (cp != ' ')
        content_end = w;
    }
  return w;
}
int text_measure_family(const char *family, const char *s, float px,
                        bool bold) {
  if (!s || !*s || !library || !isfinite(px) || px <= 0 || px > 256)
    return 0;
  uint64_t key = family_hash(main_family(family) ? family_name : family);
  size_t length = strlen(s);
  if (length < sizeof(measures[0].text))
    for (int i = 0; i < MEASURE_LIMIT; i++) {
      const measure_t *m = &measures[i];
      if (m->valid && m->family == key && m->px == px &&
          m->scale == scale_120 && m->bold == bold && !strcmp(m->text, s))
        return m->width;
    }
  measure_calls++;
  double w = ceil(width(family, s, px, bold) * 120 / scale_120);
  int result = w > INT32_MAX ? INT32_MAX : (int)w;
  if (length < sizeof(measures[0].text)) {
    measure_t *m = &measures[measure_next++ % MEASURE_LIMIT];
    *m = (measure_t){.family = key,
                     .px = px,
                     .scale = scale_120,
                     .width = result,
                     .bold = bold,
                     .valid = true};
    memcpy(m->text, s, length + 1);
  }
  return result;
}
int text_measure(const char *s, float px, bool bold) {
  return text_measure_family(NULL, s, px, bold);
}
int text_set_family(const char *family) {
  if (!library || !fonts)
    return -1;
  const char *name = family && *family ? family : "sans-serif";
  if (family_name && !strcmp(family_name, name))
    return 0;
  char *copy = strdup(name);
  if (!copy)
    return -1;
  int regular = match(copy, 0, false);
  int heavy = match(copy, 0, true);
  if (regular < 0 || heavy < 0) {
    free(copy);
    return -1;
  }
  free(family_name);
  family_name = copy;
  primary[0] = regular;
  primary[1] = heavy;
  if (!preview_active)
    retain_current(true);
  return 0;
}
int text_match_count(void) {
  return match_calls;
}
int text_glyph_count(void) {
  int count = 0;
  for (int i = 0; i < GLYPH_LIMIT; i++)
    if (glyphs[i].used)
      count++;
  return count;
}
int text_measure_count(void) {
  return measure_calls;
}
int text_metrics_count(void) {
  return metrics_calls;
}
uint64_t text_layout_key(void) {
  return (family_name ? family_hash(family_name) : 0) ^
         ((uint64_t)scale_120 << 32) ^ lifetime;
}
bool text_metrics_family(const char *family, float px, bool bold,
                         text_metrics_t *out) {
  if (!out || !library || !isfinite(px) || px <= 0 || px > 256)
    return false;
  int index = main_family(family) ? primary[bold] : routed(family, 0, bold);
  if (index < 0)
    return false;
  FT_Face face = faces[index].ft;
  int size = (int)lround((double)px * scale_120 / 120.0 * 64.0);
  metric_t *cached = &faces[index].metrics[0];
  for (int i = 0; i < METRIC_LIMIT; i++) {
    metric_t *m = &faces[index].metrics[i];
    if (m->valid && m->size == size && m->scale == scale_120 &&
        m->bold == bold) {
      *out = m->value;
      return true;
    }
    if (!m->valid)
      cached = m;
  }
  if (size <= 0 || FT_Set_Char_Size(face, 0, size, 72, 72))
    return false;
  double unit = 64.0 * scale_120 / 120.0;
  metrics_calls++;
  *out = (text_metrics_t){0};
  out->ascent = face->size->metrics.ascender / unit;
  out->descent = -face->size->metrics.descender / unit;
  const TT_OS2 *os2 = FT_Get_Sfnt_Table(face, ft_sfnt_os2);
  if (os2 && os2->version != 0xffff && os2->version >= 2 &&
      os2->sCapHeight > 0) {
    out->cap_height =
        FT_MulFix(os2->sCapHeight, face->size->metrics.y_scale) / unit;
    out->cap_source = TEXT_CAP_OS2;
  } else {
    FT_UInt h = FT_Get_Char_Index(face, 'H');
    if (h && !FT_Load_Glyph(face, h, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP)) {
      if (bold && !(face->style_flags & FT_STYLE_FLAG_BOLD))
        FT_GlyphSlot_Embolden(face->glyph);
      out->cap_height = face->glyph->metrics.height / unit;
      if (out->cap_height > 0)
        out->cap_source = TEXT_CAP_GLYPH;
    }
  }
  if (!(out->ascent > 0 && out->descent >= 0 && isfinite(out->ascent) &&
        isfinite(out->descent)))
    return false;
  *cached = (metric_t){.size = size,
                       .scale = scale_120,
                       .bold = bold,
                       .valid = true,
                       .value = *out};
  return true;
}
bool text_metrics(float px, bool bold, text_metrics_t *out) {
  return text_metrics_family(NULL, px, bold, out);
}
double text_baseline_family(const char *family, double line_top, double line_h,
                            float px, bool bold) {
  text_metrics_t metrics;
  if (!text_metrics_family(family, px, bold, &metrics))
    return line_top;
  if (metrics.cap_height > 0)
    return line_top + line_h / 2 + metrics.cap_height / 2;
  return line_top + (line_h - (metrics.ascent + metrics.descent)) / 2.0 +
         metrics.ascent;
}
double text_baseline(double line_top, double line_h, float px, bool bold) {
  return text_baseline_family(NULL, line_top, line_h, px, bold);
}
static void paint(uint8_t *dst, int dw, int dh, int64_t x, int y, glyph_t *g,
                  uint32_t color, text_clip_t clip) {
  for (int gy = 0; gy < g->h; gy++) {
    int64_t yy = (int64_t)y - g->top + gy;
    if (yy < 0 || yy >= dh || yy < clip.y || yy >= (int64_t)clip.y + clip.h)
      continue;
    for (int gx = 0; gx < g->w; gx++) {
      int64_t xx = (int64_t)x + g->left + gx;
      if (xx < 0 || xx >= dw || xx < clip.x || xx >= (int64_t)clip.x + clip.w)
        continue;
      unsigned a =
          ((unsigned)g->pixels[gy * g->w + gx] * (color >> 24) + 127) / 255;
      uint8_t *p = dst + ((size_t)yy * (size_t)dw + (size_t)xx) * 4;
      for (int k = 0; k < 3; k++)
        p[k] = (uint8_t)((((color >> (8 * k)) & 255) * a + p[k] * (255 - a) +
                          127) /
                         255);
      p[3] = (uint8_t)(a + (p[3] * (255 - a) + 127) / 255);
    }
  }
}
static void paint_ellipsis(uint8_t *dst, int dw, int dh, int x, int y,
                           const char *family, float px, bool bold,
                           uint32_t color, text_clip_t clip) {
  ellipsis_t e = ellipsis_geometry(family, px, bold);
  if (!e.width)
    return;
  if (e.dot) {
    // Font bearings may centre even a period. Anchor its ink bottom to the
    // baseline and remove its left bearing from the shared width budget.
    glyph_t dot = *e.dot;
    dot.left = 0;
    dot.top = dot.h - 1;
    for (int i = 0; i < 3; i++)
      paint(dst, dw, dh, (int64_t)x + lround(i * e.step), y, &dot, color, clip);
    return;
  }
  size_t bytes = (size_t)e.diameter * (size_t)e.diameter;
  uint8_t *pixels = malloc(bytes);
  if (!pixels)
    return;
  double radius = e.diameter / 2.0;
  for (int gy = 0; gy < e.diameter; gy++)
    for (int gx = 0; gx < e.diameter; gx++) {
      double dx = gx + .5 - radius, dy = gy + .5 - radius;
      double coverage = fmax(0, fmin(1, radius + .5 - hypot(dx, dy)));
      pixels[gy * e.diameter + gx] = (uint8_t)lround(coverage * 255);
    }
  glyph_t dot = {.w = e.diameter,
                 .h = e.diameter,
                 .top = e.diameter - 1,
                 .pixels = pixels};
  for (int i = 0; i < 3; i++)
    paint(dst, dw, dh, (int64_t)x + lround(i * e.step), y, &dot, color, clip);
  free(pixels);
}
static void draw_line(uint8_t *dst, int dw, int dh, int x, int y,
                      const char *family, const char *s, float px, bool bold,
                      uint32_t color, int max_w, text_clip_t clip) {
  if (!dst || dw <= 0 || dh <= 0 || !s || clip.w <= 0 || clip.h <= 0)
    return;
  double total = width(family, s, px, bold), pen = x, content_end = x;
  bool shortened = max_w > 0 && total > max_w;
  double ellipsis = shortened ? width(family, "…", px, bold) : 0;
  double available = max_w > 0 ? max_w - ellipsis : total;
  if (max_w > 0) {
    int64_t right = (int64_t)x + max_w;
    if (right < (int64_t)clip.x + clip.w) {
      if (right <= clip.x)
        return;
      clip.w = (int)(right - clip.x);
    }
  }
  if (shortened && ellipsis > max_w)
    return;
  while (*s) {
    uint32_t cp = next(&s);
    glyph_t *g = cp == 0x2026 ? NULL : glyph(family, cp, px, bold);
    double step = cp == 0x2026 ? ellipsis_geometry(family, px, bold).width
                  : g          ? g->advance / 64.0
                               : 0;
    // A prefix may end at a space, including title-length markers stored in
    // strings. Remove only the gap before the marker; " · " stays unchanged.
    if (cp == 0x2026)
      pen = content_end;
    if (cp != ' ' && pen - x + step > available + .001)
      break;
    if (pen > INT32_MAX || pen < INT32_MIN)
      break;
    if (cp == 0x2026)
      paint_ellipsis(dst, dw, dh, (int)lround(pen), y, family, px, bold, color,
                     clip);
    else if (g)
      paint(dst, dw, dh, (int)lround(pen), y, g, color, clip);
    pen += step;
    if (cp != ' ')
      content_end = pen;
  }
  if (shortened && content_end <= INT32_MAX && content_end >= INT32_MIN)
    paint_ellipsis(dst, dw, dh, (int)lround(content_end), y, family, px, bold,
                   color, clip);
}
void text_draw_clip_family(uint8_t *dst, int dw, int dh, int x, int y,
                           const char *family, const char *s, float px,
                           bool bold, uint32_t color, int max_w,
                           text_clip_t clip) {
  draw_line(dst, dw, dh, x, y, family, s, px, bold, color, max_w, clip);
}
void text_draw_clip(uint8_t *dst, int dw, int dh, int x, int y, const char *s,
                    float px, bool bold, uint32_t color, int max_w,
                    text_clip_t clip) {
  draw_line(dst, dw, dh, x, y, NULL, s, px, bold, color, max_w, clip);
}
void text_draw_family(uint8_t *dst, int dw, int dh, int x, int y,
                      const char *family, const char *s, float px, bool bold,
                      uint32_t color, int max_w) {
  draw_line(dst, dw, dh, x, y, family, s, px, bold, color, max_w,
            (text_clip_t){0, 0, dw, dh});
}
void text_draw(uint8_t *dst, int dw, int dh, int x, int y, const char *s,
               float px, bool bold, uint32_t color, int max_w) {
  text_draw_clip(dst, dw, dh, x, y, s, px, bold, color, max_w,
                 (text_clip_t){0, 0, dw, dh});
}
#define FAMILY_CAP 512
typedef struct {
  char name[128];
  bool zh, en;
  int spacing;
} family_rec_t;
static family_rec_t catalog[FAMILY_CAP];
static int catalog_count;
static bool catalog_ready;
static int cmp_family(const void *a, const void *b) {
  return strcmp(((const family_rec_t *)a)->name,
                ((const family_rec_t *)b)->name);
}
static bool lang_covers(FcLangSet *set, const char *lang) {
  if (!set)
    return true;
  return FcLangSetHasLang(set, (const FcChar8 *)lang) != FcLangDifferentLang;
}
static bool ensure_catalog(void) {
  if (catalog_ready)
    return true;
  if (!fonts)
    return false;
  catalog_ready = true;
  FcPattern *pattern = FcPatternCreate();
  FcObjectSet *objects =
      pattern ? FcObjectSetBuild(FC_FAMILY, FC_LANG, FC_SPACING, NULL) : NULL;
  if (pattern)
    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
  FcFontSet *set = objects ? FcFontList(fonts, pattern, objects) : NULL;
  if (pattern)
    FcPatternDestroy(pattern);
  if (objects)
    FcObjectSetDestroy(objects);
  if (!set)
    return true;
  for (int i = 0; i < set->nfont; i++) {
    FcChar8 *raw = NULL;
    if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &raw) !=
            FcResultMatch ||
        !raw || !*raw)
      continue;
    const char *name = (const char *)raw;
    size_t length = strlen(name);
    if (!length || length >= sizeof(catalog[0].name) || strpbrk(name, "\t\r\n"))
      continue;
    FcLangSet *langs = NULL;
    FcPatternGetLangSet(set->fonts[i], FC_LANG, 0, &langs);
    bool zh = lang_covers(langs, "zh-cn");
    bool en = lang_covers(langs, "en");
    int spacing = -1;
    if (FcPatternGetInteger(set->fonts[i], FC_SPACING, 0, &spacing) !=
        FcResultMatch)
      spacing = -1;
    int found = -1;
    for (int j = 0; j < catalog_count; j++)
      if (!strcmp(catalog[j].name, name))
        found = j;
    if (found >= 0) {
      catalog[found].zh = catalog[found].zh || zh;
      catalog[found].en = catalog[found].en || en;
      if (spacing > catalog[found].spacing)
        catalog[found].spacing = spacing;
      continue;
    }
    if (catalog_count >= FAMILY_CAP)
      continue;
    snprintf(catalog[catalog_count].name, sizeof(catalog[0].name), "%s", name);
    catalog[catalog_count].zh = zh;
    catalog[catalog_count].en = en;
    catalog[catalog_count].spacing = spacing;
    catalog_count++;
  }
  FcFontSetDestroy(set);
  qsort(catalog, (size_t)catalog_count, sizeof(catalog[0]), cmp_family);
  return true;
}
static bool listed_lang(const family_rec_t *rec, const char *lang) {
  if (!strcmp(lang, "zh-cn") || !strcmp(lang, "zh"))
    return rec->zh;
  if (!strcmp(lang, "en"))
    return rec->en;
  return false;
}
bool text_spacing_mono(int spacing) {
  return spacing >= 90;
}
static bool listed_class(const family_rec_t *rec, text_face_class_t kind) {
  bool mono = text_spacing_mono(rec->spacing);
  if (kind == TEXT_CLASS_MONO)
    return mono;
  if (kind == TEXT_CLASS_PROP)
    return !mono;
  return kind == TEXT_CLASS_ALL;
}
int text_families_class(const char *lang, text_face_class_t kind,
                        const char **out, int cap) {
  if (!lang || cap < 0 || (cap > 0 && !out) || kind > TEXT_CLASS_MONO ||
      !ensure_catalog())
    return -1;
  int count = 0;
  for (int i = 0; i < catalog_count; i++) {
    if (!listed_lang(&catalog[i], lang) || !listed_class(&catalog[i], kind))
      continue;
    if (out && count < cap)
      out[count] = catalog[i].name;
    count++;
  }
  return count;
}
int text_families(const char *lang, const char **out, int cap) {
  return text_families_class(lang, TEXT_CLASS_ALL, out, cap);
}
int text_family_spacing(const char *name) {
  if (!name || !ensure_catalog())
    return -2;
  for (int i = 0; i < catalog_count; i++)
    if (!strcmp(catalog[i].name, name))
      return catalog[i].spacing;
  return -2;
}
int text_family_step(const char *lang, const char *current, int dir,
                     const char **out) {
  const char *names[FAMILY_CAP];
  int count = text_families(lang, names, FAMILY_CAP);
  if (count < 0)
    return -1;
  if (count > FAMILY_CAP)
    count = FAMILY_CAP;
  int slots = count + 1;
  bool known = !current || !*current;
  int index = 0;
  if (!known) {
    for (int i = 0; i < count; i++)
      if (!strcmp(names[i], current)) {
        index = i + 1;
        known = true;
        break;
      }
  }
  if (!known && !dir) {
    if (out)
      *out = current;
    return -1;
  }
  if (!known)
    index = dir > 0 ? 0 : slots - 1;
  else if (dir > 0)
    index = (index + 1) % slots;
  else if (dir < 0)
    index = (index + slots - 1) % slots;
  if (out)
    *out = index == 0 ? NULL : names[index - 1];
  return index;
}
