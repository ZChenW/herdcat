#ifndef HERDCAT_SIGN_DRAW_INTERNAL_H
#define HERDCAT_SIGN_DRAW_INTERNAL_H
#include "graphics/sign_draw.h"
typedef struct {
  int x, y, r, b;
} pix_t;
pix_t pix_of(double x, double y, double w, double h, double scale);
pix_t intersect(pix_t a, pix_t b);
void draw_shape(uint8_t *dst, int dw, int dh, const sign_shape_t *shape,
                double scale, pix_t limit, bool store);
void draw_text(uint8_t *dst, int dw, int dh, const sign_text_t *text,
               double scale, pix_t bounds, bool store);
void draw_badge_text(uint8_t *dst, int dw, int dh, const sign_shape_t *shape,
                     double scale, pix_t bounds);
#endif
