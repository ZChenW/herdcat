#ifndef HERDCAT_SIGN_DRAW_H
#define HERDCAT_SIGN_DRAW_H

#include "graphics/pixel_rect.h"
#include "graphics/signs.h"

// UNDER is the pole, boards, icons, labels, and the card holder behind the
// cat. OVER is nameplates and the switch card, painted after the cat.
typedef enum {
  SIGN_DRAW_UNDER,
  SIGN_DRAW_OVER
} sign_draw_layer_t;

// scale_120 is the overlay buffer scale. The destination is premultiplied
// BGRA. Settled shapes are reused from a small cache; a frame whose scalars
// are still moving is rasterized and not stored. The LRU holds at most 192
// bitmaps and 4 MiB of pixels. Ink is clipped to bounds.
void sign_draw(uint8_t *dst, int dst_w, int dst_h, int scale_120,
               const sign_frame_t *frame, sign_draw_layer_t layer);
void sign_draw_clip(uint8_t *dst, int dst_w, int dst_h, int scale_120,
                    const sign_frame_t *frame, sign_draw_layer_t layer,
                    pixel_rect_t clip);
void sign_draw_cleanup(void);
// Tag panel-only bitmaps for release without dropping settled sign bitmaps.
void sign_draw_font_panel(uint8_t *dst, int dw, int dh, int scale_120,
                          const sign_frame_t *frame);
void sign_draw_font_panel_cleanup(void);
// Call after session updates, with waiting true if any session is waiting.
// After 60 seconds without waiting, drop only full-motion phase bitmaps.
// The timeout is -1 when no one-shot cache deadline is pending.
void sign_draw_cache_update(bool waiting, int64_t now_ms);
int sign_draw_cache_timeout(int64_t now_ms);

#ifdef TEST_BUILD
// Counts shape bitmap reuse, including nameplate backgrounds, not glyphs.
typedef struct {
  uint64_t hits, misses;
  size_t bytes, byte_limit, phase_bytes;
  int entries, slot_limit, phase_entries;
} sign_draw_cache_stats_t;
sign_draw_cache_stats_t sign_draw_cache_stats(void);
void sign_draw_cache_reset_stats(void);
void sign_draw_cache_disable(bool disable);
#endif

#endif
