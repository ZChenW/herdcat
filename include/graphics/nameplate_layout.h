#ifndef HERDCAT_NAMEPLATE_LAYOUT_H
#define HERDCAT_NAMEPLATE_LAYOUT_H
#include "graphics/signs.h"
typedef struct {
  char text[384];
  double width;
  float px;
  int line;
  bool bold, state, gap;
} nameplate_paint_run_t;
typedef struct {
  nameplate_paint_run_t runs[NAMEPLATE_RUN_MAX];
  int count, lines;
  double width;
} nameplate_layout_t;
void nameplate_layout(const sign_text_t *text, double scale,
                      nameplate_layout_t *out);
#endif
