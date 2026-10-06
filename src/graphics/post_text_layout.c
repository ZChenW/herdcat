#include "graphics/post_text_layout.h"

#include "graphics/text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static double fit_extra(char text[128], float px, double budget) {
  double width = text_measure(text, px, false);
  if (width <= budget)
    return width;
  size_t bytes = strlen(text);
  if (text_measure("…", px, false) > budget) {
    text[0] = 0;
    return 0;
  }
  do {
    do {
      bytes--;
    } while (bytes &&
             (bytes > 124 || ((unsigned char)text[bytes] & 0xc0) == 0x80));
    memcpy(text + bytes, "…", 4);
    width = text_measure(text, px, false);
  } while (bytes && width > budget);
  return width;
}

void post_text_layout(const sign_text_t *text, post_text_layout_t *out) {
  memset(out, 0, sizeof(*out));
  double meta_w = text_measure(text->meta, (float)text->meta_px, false);
  double budget = fmax(0, text->w - text->gap - meta_w);
  double name_w =
      text->value[0] ? text_measure(text->value, (float)text->px, true) : 0;
  double used = fmin(name_w, budget);
  out->name_budget = budget;
  out->name_x = text->reverse ? text->x + text->w - used : text->x;
  out->meta_x = text->reverse ? text->x : text->x + text->w - meta_w;
  double remaining = budget - used - text->gap;
  if (!text->extra[0] || remaining < 24)
    return;
  snprintf(out->extra, sizeof(out->extra), "%s", text->extra);
  out->extra_width = fit_extra(out->extra, (float)text->meta_px, remaining);
  out->extra_x = text->reverse ? out->name_x - text->gap - out->extra_width
                               : out->name_x + used + text->gap;
}
