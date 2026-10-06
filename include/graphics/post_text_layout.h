#ifndef HERDCAT_POST_TEXT_LAYOUT_H
#define HERDCAT_POST_TEXT_LAYOUT_H

#include "graphics/signs.h"

typedef struct {
  double name_x, meta_x, name_budget;
  double extra_x, extra_width;
  char extra[128];
} post_text_layout_t;

// Measure in logical pixels. Preserve the main name and metadata; the extra
// yields first and is omitted when less than 24 logical pixels remain.
void post_text_layout(const sign_text_t *text, post_text_layout_t *out);

#endif
