#ifndef HERDCAT_TEXT_H
#define HERDCAT_TEXT_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int x, y, w, h;
} text_clip_t;
int text_init(const char *family);
void text_cleanup(void);
void text_set_scale(int scale_120);
// Primary-face metrics in logical pixels. Fallback faces do not affect
// these, so one line keeps a single baseline. False if text is not ready.
typedef struct {
  double ascent, descent;
} text_metrics_t;
bool text_metrics(float px, bool bold, text_metrics_t *out);
bool text_metrics_family(const char *family, float px, bool bold,
                         text_metrics_t *out);
// CSS inline baseline: top + (line - (ascent + descent)) / 2 + ascent.
double text_baseline(double line_top, double line_h, float px, bool bold);
double text_baseline_family(const char *family, double line_top, double line_h,
                            float px, bool bold);
// Logical width, rounded up. Font sizes are always logical pixels.
// A null or empty family uses the main face. text_set_family keeps the
// previous face when the new one cannot be matched.
int text_measure(const char *utf8, float px, bool bold);
int text_measure_family(const char *family, const char *utf8, float px,
                        bool bold);
int text_set_family(const char *family);
// Cached glyphs. The cache stays at or below its fixed cap.
// Fontconfig lookups so far. Redrawing text already drawn must not add any.
int text_match_count(void);
int text_glyph_count(void);
// Scalable families that cover lang ("zh-cn" or "en"), deduped and sorted.
// The first call enumerates; later calls only filter that cache. The default
// face is not included. Returns the count, or -1 when text is not ready.
int text_families(const char *lang, const char **out, int cap);
// Absent spacing is negative. Dual, mono and charcell are 90 and above.
bool text_spacing_mono(int spacing);
typedef enum {
  TEXT_CLASS_ALL = 0,
  TEXT_CLASS_PROP = 1,
  TEXT_CLASS_MONO = 2,
} text_face_class_t;
// Same cache as text_families, narrowed by class. -1 when text is not ready
// or kind is not one of the three classes.
int text_families_class(const char *lang, text_face_class_t kind,
                        const char **out, int cap);
// Stored spacing for a catalog name, or -1 when that property was absent.
// -2 when the name is not in the cache.
int text_family_spacing(const char *name);
// index 0 is the default and *out is null. Other indexes name a cached
// family. A current name missing from the list is kept when dir is 0.
// dir is +1 or -1 and wraps. -1 means text is not ready.
int text_family_step(const char *lang, const char *current, int dir,
                     const char **out);
bool text_has_glyph(uint32_t codepoint, bool bold);
// Destination, positions, clipping and max_w use physical pixels.
// Color is straight ARGB; destination is premultiplied BGRA.
void text_draw(uint8_t *dst, int dst_w, int dst_h, int x, int baseline_y,
               const char *utf8, float px, bool bold, uint32_t color,
               int max_w);
void text_draw_family(uint8_t *dst, int dst_w, int dst_h, int x, int baseline_y,
                      const char *family, const char *utf8, float px, bool bold,
                      uint32_t color, int max_w);
void text_draw_clip(uint8_t *dst, int dst_w, int dst_h, int x, int baseline_y,
                    const char *utf8, float px, bool bold, uint32_t color,
                    int max_w, text_clip_t clip);
void text_draw_clip_family(uint8_t *dst, int dst_w, int dst_h, int x,
                           int baseline_y, const char *family, const char *utf8,
                           float px, bool bold, uint32_t color, int max_w,
                           text_clip_t clip);
#endif
