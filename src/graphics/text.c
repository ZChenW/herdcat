#define _POSIX_C_SOURCE 200809L
#include "graphics/text.h"

#include "utils/utf8.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GLYPH_LIMIT 512
#define FACE_LIMIT  64
typedef struct {
  FT_Face ft;
  char *file;
  int index;
} face_t;
typedef struct {
  uint32_t cp;
  int size, face;
  bool bold;
  int w, h, left, top, advance;
  uint8_t *pixels;
  uint64_t used;
} glyph_t;
static FT_Library library;
static FcConfig *fonts;
static char *family_name;
static face_t faces[FACE_LIMIT];
static int face_count, primary[2], scale_120 = 120;
static glyph_t glyphs[GLYPH_LIMIT];
static uint64_t clock_stamp;
static void clear_glyphs(void) {
  for (int i = 0; i < GLYPH_LIMIT; i++)
    free(glyphs[i].pixels);
  memset(glyphs, 0, sizeof(glyphs));
  clock_stamp = 0;
}
void text_cleanup(void) {
  clear_glyphs();
  for (int i = 0; i < face_count; i++) {
    FT_Done_Face(faces[i].ft);
    free(faces[i].file);
  }
  face_count = 0;
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
static int match(uint32_t cp, bool bold) {
  FcPattern *pattern = FcPatternCreate();
  if (!pattern)
    return -1;
  FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)family_name);
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
    if (found < 0 && face_count < FACE_LIMIT) {
      char *copy = strdup((const char *)file);
      FT_Face ft;
      if (copy && !FT_New_Face(library, copy, index, &ft)) {
        found = face_count++;
        faces[found] = (face_t){ft, copy, index};
      } else
        free(copy);
    }
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
  primary[0] = match(0, false);
  primary[1] = match(0, true);
  if (primary[0] < 0 || primary[1] < 0)
    goto fail;
  return 0;
fail:
  text_cleanup();
  return -1;
}
void text_set_scale(int scale) {
  if (scale < 1 || scale > 960 || scale == scale_120)
    return;
  scale_120 = scale;
  clear_glyphs();
}
static int face_for(uint32_t cp, bool bold) {
  if (!library)
    return -1;
  int face = primary[bold];
  if (FT_Get_Char_Index(faces[face].ft, cp))
    return face;
  int fallback = match(cp, bold);
  return fallback >= 0 ? fallback : face;
}
bool text_has_glyph(uint32_t cp, bool bold) {
  int f = face_for(cp, bold);
  return f >= 0 && FT_Get_Char_Index(faces[f].ft, cp) != 0;
}
static glyph_t *glyph(uint32_t cp, float px, bool bold) {
  if (!library || !isfinite(px) || px <= 0 || px > 256)
    return NULL;
  int size = (int)lround((double)px * scale_120 / 120 * 64);
  glyph_t *slot = &glyphs[0];
  for (int i = 0; i < GLYPH_LIMIT; i++) {
    glyph_t *g = &glyphs[i];
    if (g->used && g->cp == cp && g->bold == bold && g->size == size) {
      g->used = ++clock_stamp;
      return g;
    }
    if (g->used < slot->used)
      slot = g;
  }
  int f = face_for(cp, bold);
  if (f < 0)
    return NULL;
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
                    .used = ++clock_stamp};
  return slot;
}
static uint32_t next(const char **s) {
  uint32_t cp;
  size_t n = utf8_decode(*s, &cp);
  *s += n ? n : 1;
  return n ? cp : 0xfffd;
}
static double width(const char *s, float px, bool bold) {
  double w = 0;
  if (s)
    while (*s) {
      glyph_t *g = glyph(next(&s), px, bold);
      if (g)
        w += g->advance / 64.0;
    }
  return w;
}
int text_measure(const char *s, float px, bool bold) {
  double w = ceil(width(s, px, bold) * 120 / scale_120);
  return w > INT32_MAX ? INT32_MAX : (int)w;
}
static void paint(uint8_t *dst, int dw, int dh, int x, int y, glyph_t *g,
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
void text_draw_clip(uint8_t *dst, int dw, int dh, int x, int y, const char *s,
                    float px, bool bold, uint32_t color, int max_w,
                    text_clip_t clip) {
  if (!dst || dw <= 0 || dh <= 0 || !s || clip.w <= 0 || clip.h <= 0)
    return;
  double total = width(s, px, bold), pen = x;
  bool shortened = max_w > 0 && total > max_w;
  double ellipsis = shortened ? width("…", px, bold) : 0;
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
    glyph_t *g = glyph(next(&s), px, bold);
    if (!g)
      continue;
    if (pen - x + g->advance / 64.0 > available + .001)
      break;
    if (pen > INT32_MAX || pen < INT32_MIN)
      break;
    paint(dst, dw, dh, (int)lround(pen), y, g, color, clip);
    pen += g->advance / 64.0;
  }
  if (shortened && pen <= INT32_MAX && pen >= INT32_MIN) {
    glyph_t *g = glyph(0x2026, px, bold);
    if (g)
      paint(dst, dw, dh, (int)lround(pen), y, g, color, clip);
  }
}
void text_draw(uint8_t *dst, int dw, int dh, int x, int y, const char *s,
               float px, bool bold, uint32_t color, int max_w) {
  text_draw_clip(dst, dw, dh, x, y, s, px, bold, color, max_w,
                 (text_clip_t){0, 0, dw, dh});
}
