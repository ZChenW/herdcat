#ifndef BONGOCAT_TEXT_H
#define BONGOCAT_TEXT_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int x, y, w, h;
} text_clip_t;
int text_init(const char *family);
void text_cleanup(void);
void text_set_scale(int scale_120);
// Logical width, rounded up. Font sizes are always logical pixels.
int text_measure(const char *utf8, float px, bool bold);
bool text_has_glyph(uint32_t codepoint, bool bold);
// Destination, positions, clipping and max_w use physical pixels.
// Color is straight ARGB; destination is premultiplied BGRA.
void text_draw(uint8_t *dst, int dst_w, int dst_h, int x, int baseline_y,
               const char *utf8, float px, bool bold, uint32_t color,
               int max_w);
void text_draw_clip(uint8_t *dst, int dst_w, int dst_h, int x, int baseline_y,
                    const char *utf8, float px, bool bold, uint32_t color,
                    int max_w, text_clip_t clip);
#endif
