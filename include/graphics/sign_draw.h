#ifndef HERDCAT_SIGN_DRAW_H
#define HERDCAT_SIGN_DRAW_H

#include "graphics/signs.h"

// UNDER is the pole, boards, icons, labels, and the card holder behind the
// cat. OVER is nameplates and the switch card, painted after the cat.
typedef enum {
  SIGN_DRAW_UNDER,
  SIGN_DRAW_OVER
} sign_draw_layer_t;

// scale_120 is the overlay buffer scale. The destination is premultiplied
// BGRA. Settled shapes are reused from a small cache; a frame whose scalars
// are still moving is rasterized and not stored. Ink is clipped to bounds.
void sign_draw(uint8_t *dst, int dst_w, int dst_h, int scale_120,
               const sign_frame_t *frame, sign_draw_layer_t layer);
void sign_draw_cleanup(void);

#endif
