#include "graphics/nameplate_layout.h"

#include "config/nameplate.h"
#include "graphics/signs.h"
#include "graphics/text.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void shorten(nameplate_paint_run_t *r, double budget) {
  char original[384];
  snprintf(original, sizeof(original), "%s", r->text);
  size_t bytes = strlen(original);
  double ellipsis = text_measure("…", r->px, r->bold);
  if (budget < ellipsis) {
    r->text[0] = 0;
    r->width = 0;
    return;
  }
  do {
    if (bytes) {
      do {
        bytes--;
      } while (bytes && ((unsigned char)original[bytes] & 0xc0) == 0x80);
    }
    memcpy(r->text, original, bytes);
    memcpy(r->text + bytes, "…", 4);
    r->width = text_measure(r->text, r->px, r->bold);
  } while (bytes && r->width > budget);
}
static void fit_segment(nameplate_layout_t *out, int first, int end,
                        double budget) {
  double width = 0;
  for (int i = first; i < end; i++)
    width += out->runs[i].width;
  // Remove trailing spans together, then put one ellipsis at the cut.
  for (int i = end - 1; i >= first && width > budget; i--) {
    nameplate_paint_run_t *r = &out->runs[i];
    double before = width - r->width;
    double remaining = fmax(0, budget - before);
    if (r->gap || remaining < text_measure("…", r->px, r->bold)) {
      r->text[0] = 0;
      r->width = 0;
    } else {
      shorten(r, remaining);
    }
    width = before + r->width;
  }
  bool content = false;
  for (int i = first; i < end; i++)
    content |= out->runs[i].text[0] && !out->runs[i].gap &&
               strcmp(out->runs[i].text, " · ");
  if (!content)
    for (int i = first; i < end; i++) {
      out->runs[i].text[0] = 0;
      out->runs[i].width = 0;
    }
}
void nameplate_layout(const sign_text_t *text, double scale,
                      nameplate_layout_t *out) {
  memset(out, 0, sizeof(*out));
  const nameplate_t *model = &text->nameplate;
  nameplate_t fallback;
  if (!model->count && !text->templated) {
    memset(&fallback, 0, sizeof(fallback));
    snprintf(fallback.text, sizeof(fallback.text), "%s  %s", text->value,
             text->meta);
    size_t n = strlen(text->value), m = strlen(text->meta);
    fallback.runs[0] = (nameplate_run_t){.length = (uint16_t)n, .bold = true};
    fallback.runs[1] = (nameplate_run_t){
        .start = (uint16_t)n, .length = 2, .gap = true, .segment = 1};
    fallback.runs[2] = (nameplate_run_t){.start = (uint16_t)(n + 2),
                                         .length = (uint16_t)m,
                                         .state = true,
                                         .segment = 1};
    fallback.count = m ? 3 : 1;
    fallback.lines = 1;
    model = &fallback;
  }
  out->count = model->count;
  out->lines = model->lines;
  double widths[2] = {0};
  for (int i = 0; i < model->count; i++) {
    const nameplate_run_t *r = &model->runs[i];
    nameplate_paint_run_t *paint = &out->runs[i];
    memcpy(paint->text, model->text + r->start, r->length);
    paint->text[r->length] = 0;
    paint->bold = r->bold;
    paint->state = r->state;
    paint->gap = r->gap;
    paint->line = r->line;
    paint->px = (float)((r->bold ? text->px : text->meta_px) * scale);
    paint->width = r->gap ? text->gap * scale
                          : text_measure(paint->text, paint->px, paint->bold);
    widths[r->line] += paint->width;
  }
  double budget =
      text->max_width > 0 ? fmax(0, text->max_width - 24 * scale) : 0;
  if (text->max_width > 0) {
    // Later secondary segments give way first; bold segments are last.
    for (int line = 0; line < 2; line++) {
      for (int pass = 0; pass < 2 && widths[line] > budget; pass++) {
        for (int end = out->count; end > 0 && widths[line] > budget;) {
          int first = end - 1;
          while (first > 0 &&
                 model->runs[first - 1].line == model->runs[end - 1].line &&
                 model->runs[first - 1].segment == model->runs[end - 1].segment)
            first--;
          bool bold = false;
          double old = 0;
          for (int i = first; i < end; i++) {
            bold |= out->runs[i].bold;
            old += out->runs[i].width;
          }
          int stop = end;
          end = first;
          if (out->runs[first].line != line || bold != (pass == 1))
            continue;
          fit_segment(out, first, stop, fmax(0, old - (widths[line] - budget)));
          double fitted = 0;
          for (int i = first; i < stop; i++)
            fitted += out->runs[i].width;
          widths[line] -= old - fitted;
        }
      }
    }
  }
  out->width = fmax(widths[0], widths[1]);
}
