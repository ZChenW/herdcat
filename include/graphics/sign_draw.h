#ifndef BONGOCAT_SIGN_DRAW_H
#define BONGOCAT_SIGN_DRAW_H

#include "graphics/signs.h"

// UNDER is the pole, boards, icons, and labels that sit behind the cat.
// OVER is reserved for nameplates painted after the cat. Post leaves it empty.
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
