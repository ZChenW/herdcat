#ifndef BONGOCAT_FONT_PANEL_H
#define BONGOCAT_FONT_PANEL_H

#include "config/sign_options.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FONT_PANEL_CAP  512
#define FONT_PANEL_ROWS 10
// Two columns: three were too narrow, and families that differ only in
// their last word were cut to the same text.
#define FONT_PANEL_COLS    2
#define FONT_PANEL_VISIBLE (FONT_PANEL_ROWS * FONT_PANEL_COLS)
#define FONT_PANEL_IDLE_MS 10000
// Design pixels between the switch card and the panel. Multiply by cat scale.
#define FONT_PANEL_GAP 10

typedef struct {
  double x, y, w, h;
} font_panel_box_t;
typedef struct {
  double w, h;
} font_panel_size_t;
typedef enum {
  FONT_PANEL_ALL = 0,
  FONT_PANEL_PROP = 1,
  FONT_PANEL_MONO = 2,
} font_panel_filter_t;
// mono is the caller's spacing decision. Names are not inspected.
typedef struct {
  char name[128];
  bool mono;
} font_panel_face_t;
typedef struct {
  double from, target, x1, y1, x2, y2;
  int64_t start;
  int duration;
} font_panel_motion_t;
typedef struct {
  font_panel_face_t faces[FONT_PANEL_CAP];
  int face_count;
  char selected[128];
  bool english, open, dirty;
  double scale;
  sign_animations_t animations;
  font_panel_filter_t filter;
  int first_row, hover, hot;
  int64_t now_ms, idle_at;
  font_panel_motion_t appear, fade, thumb, mix;
  // prepared < 0 draws every cell in its own face. Otherwise only indexes
  // below it do; the rest use the main face. real_preview draws the sample
  // in the hovered face. Both default to the full sample.
  int prepared;
  bool real_preview;
} font_panel_t;
// frame asks for one frame callback while the pop, thumb, or hover moves.
// timeout_ms is the idle wake in milliseconds, or -1 when the panel is
// closed. It is never 0.
typedef struct {
  bool redraw, frame;
  int timeout_ms;
} font_panel_wake_t;
typedef struct {
  double width, height;
  font_panel_box_t count, filter, thumb, preview, bar;
  font_panel_box_t cells[FONT_PANEL_VISIBLE];
  int cell_count, first_row, rows;
} font_panel_layout_t;

void font_panel_reset(font_panel_t *panel);
// Copies up to FONT_PANEL_CAP faces. Empty names are skipped.
void font_panel_set_faces(font_panel_t *panel, const font_panel_face_t *faces,
                          int count);
// Opens on All, scrolled to the top. A second call stays open and refreshes
// the language, scale, and selected family.
bool font_panel_open(font_panel_t *panel, bool english, double scale,
                     sign_animations_t animations, const char *selected,
                     int64_t now_ms);
void font_panel_close(font_panel_t *panel);
bool font_panel_is_open(const font_panel_t *panel);
void font_panel_set_selected(font_panel_t *panel, const char *selected);
void font_panel_set_language(font_panel_t *panel, bool english);
void font_panel_activity(font_panel_t *panel, int64_t now_ms);
// Local logical pixels. The origin is the panel's top left.
void font_panel_pointer(font_panel_t *panel, double x, double y,
                        int64_t now_ms);
void font_panel_leave(font_panel_t *panel, int64_t now_ms);
void font_panel_wheel(font_panel_t *panel, int discrete, int64_t now_ms);
// True when a cell was chosen. out is empty for the default face. The panel
// stays open so several faces can be tried in a row. Hover never selects
// and never writes this result.
bool font_panel_click(font_panel_t *panel, double x, double y, int64_t now_ms,
                      char *out, size_t cap);
font_panel_wake_t font_panel_step(font_panel_t *panel, int64_t now_ms);
// Negative cells means every name uses its own face. Opacity is the open
// fade, 0 while the panel is closed.
void font_panel_set_prepared(font_panel_t *panel, int cells);
void font_panel_set_real_preview(font_panel_t *panel, bool real);
int font_panel_prepared(const font_panel_t *panel);
double font_panel_opacity(const font_panel_t *panel);

int font_panel_count(const font_panel_t *panel);
void font_panel_count_text(const font_panel_t *panel, char *out, size_t cap);
const char *font_panel_label(const font_panel_t *panel, int index);
const char *font_panel_family(const font_panel_t *panel, int index);
const char *font_panel_preview_family(const font_panel_t *panel);
// The face under the pointer, or NULL when no cell is hovered.
const char *font_panel_hover_family(const font_panel_t *panel);
int font_panel_selected_index(const font_panel_t *panel);
double font_panel_hot(const font_panel_t *panel, int *cell);

// Height depends only on how many rows are visible. Cell and preview boxes
// do not follow the face.
double font_panel_width(double scale);
double font_panel_height(int items, double scale);
void font_panel_layout(const font_panel_t *panel, font_panel_layout_t *out);
// Prefers the card's right, bottom edges aligned. Otherwise the left, then
// shifted down until the panel is inside the output. A panel larger than
// the output is pinned to the origin.
void font_panel_place(const font_panel_box_t *card,
                      const font_panel_size_t *panel,
                      const font_panel_size_t *output, double gap, double *x,
                      double *y);
void font_panel_pixels(const font_panel_t *panel, int scale_120, int *w,
                       int *h);
// dst is premultiplied BGRA and is cleared by the caller. scale_120 is the
// buffer scale.
void font_panel_draw(const font_panel_t *panel, uint8_t *dst, int dst_w,
                     int dst_h, int scale_120);

#endif
