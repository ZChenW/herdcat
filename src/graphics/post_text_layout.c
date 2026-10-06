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
  // Width fitting can visit many UTF-8 prefixes. Cache the whole layout so
  // a settled waiting board never repeats that work, even after glyph churn.
  static struct {
    uint64_t context;
    sign_text_t key;
    post_text_layout_t value;
    bool valid;
  } cache[32];
  static unsigned next;
  sign_text_t key = {.w = text->w,
                     .px = text->px,
                     .meta_px = text->meta_px,
                     .gap = text->gap,
                     .reverse = text->reverse};
  memcpy(key.value, text->value, sizeof(key.value));
  memcpy(key.meta, text->meta, sizeof(key.meta));
  memcpy(key.extra, text->extra, sizeof(key.extra));
  uint64_t context = text_layout_key();
  for (int i = 0; i < 32; i++)
    if (cache[i].valid && cache[i].context == context &&
        cache[i].key.w == key.w && cache[i].key.px == key.px &&
        cache[i].key.meta_px == key.meta_px && cache[i].key.gap == key.gap &&
        cache[i].key.reverse == key.reverse &&
        !strcmp(cache[i].key.value, key.value) &&
        !strcmp(cache[i].key.meta, key.meta) &&
        !strcmp(cache[i].key.extra, key.extra)) {
      *out = cache[i].value;
      out->name_x += text->x;
      out->meta_x += text->x;
      out->extra_x += text->x;
      return;
    }
  memset(out, 0, sizeof(*out));
  double meta_w =
      fmin(text->w, text_measure(text->meta, (float)text->meta_px, false));
  double budget = fmax(0, text->w - text->gap - meta_w);
  double name_w =
      text->value[0] ? text_measure(text->value, (float)text->px, true) : 0;
  double used = fmin(name_w, budget);
  out->name_budget = budget;
  out->name_x = text->reverse ? text->w - used : 0;
  out->meta_x = text->reverse ? 0 : text->w - meta_w;
  double remaining = budget - used - text->gap;
  if (text->extra[0] && remaining >= 24) {
    snprintf(out->extra, sizeof(out->extra), "%s", text->extra);
    out->extra_width = fit_extra(out->extra, (float)text->meta_px, remaining);
    out->extra_x = text->reverse ? out->name_x - text->gap - out->extra_width
                                 : out->name_x + used + text->gap;
  }
  unsigned at = next++ % 32;
  cache[at].context = context;
  cache[at].key = key;
  cache[at].value = *out;
  cache[at].valid = true;
  out->name_x += text->x;
  out->meta_x += text->x;
  out->extra_x += text->x;
}
