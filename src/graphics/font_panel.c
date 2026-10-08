#include "graphics/font_panel.h"

#include "config/sign_options.h"
#include "graphics/sign_draw.h"
#include "graphics/sign_palette.h"
#include "graphics/signs.h"
#include "graphics/text.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Design pixels at cat scale 1. INSET is the 2px border plus 10px padding.
// Every box below is fixed; a face change only swaps glyphs inside it.
static const double DESIGN_W = 384;
static const double INSET = 12;
static const double HEAD = 24;
static const double FILTER_W = 168;
static const double GAP = 8;
static const double CELL_H = 26;
static const double CELL_GAP = 4;
static const double PREVIEW_H = 31;
static const double POP_MS = 260;
static const double FADE_MS = 140;
static const double THUMB_MS = 280;
static const double HOT_MS = 120;

typedef struct {
  const char *fallback, *all, *prop, *mono, *meta;
} panel_words_t;
// clang-format off
static const panel_words_t WORDS[] = {
    {"默认",    "全部", "比例", "等宽", "Claude · 等你批准"      },
    {"Default", "All",  "Text", "Mono", "Claude · Needs approval"},
};
// clang-format on
typedef struct {
  double x1, y1, x2, y2;
} font_curve_t;
static const font_curve_t CURVE_POP = {.34, 1.56, .64, 1};
static const font_curve_t CURVE_MOVE = {.34, 1.4, .64, 1};
static const font_curve_t CURVE_EASE = {.25, .1, .25, 1};
static const char PREVIEW_NAME[] = "herdcat";
static const char SANS[] = "sans-serif";

static double component(double t, double p1, double p2) {
  double u = 1 - t;
  return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
}
static double curve_at(double fraction, font_curve_t curve) {
  if (fraction <= 0)
    return 0;
  if (fraction >= 1)
    return 1;
  double low = 0, high = 1;
  for (int i = 0; i < 24; i++) {
    double mid = (low + high) / 2;
    if (component(mid, curve.x1, curve.x2) < fraction)
      low = mid;
    else
      high = mid;
  }
  return component((low + high) / 2, curve.y1, curve.y2);
}
static double motion_at(const font_panel_motion_t *motion, int64_t now) {
  if (!motion->duration || now >= motion->start + motion->duration)
    return motion->target;
  if (now <= motion->start)
    return motion->from;
  double span = (double)(now - motion->start) / motion->duration;
  font_curve_t curve = {motion->x1, motion->y1, motion->x2, motion->y2};
  return motion->from + (motion->target - motion->from) * curve_at(span, curve);
}
static bool motion_busy(const font_panel_motion_t *motion, int64_t now) {
  return motion->duration > 0 && now < motion->start + motion->duration;
}
static void motion_snap(font_panel_motion_t *motion, double value) {
  motion->from = value;
  motion->target = value;
  motion->duration = 0;
  motion->start = 0;
}
static void motion_aim(font_panel_motion_t *motion, double target, int duration,
                       int64_t now, font_curve_t curve, bool instant) {
  if (motion->target == target && !instant)
    return;
  if (instant)
    duration = 0;
  motion->from = motion_at(motion, now);
  motion->target = target;
  motion->start = now;
  motion->duration = duration;
  motion->x1 = curve.x1;
  motion->y1 = curve.y1;
  motion->x2 = curve.x2;
  motion->y2 = curve.y2;
  if (!duration)
    motion->from = target;
}
static void motion_retarget(font_panel_motion_t *motion, double from, double to,
                            int duration, int64_t now, font_curve_t curve,
                            bool instant) {
  if (instant) {
    motion_snap(motion, to);
    return;
  }
  motion->from = from;
  motion->target = to;
  motion->start = now;
  motion->duration = duration;
  motion->x1 = curve.x1;
  motion->y1 = curve.y1;
  motion->x2 = curve.x2;
  motion->y2 = curve.y2;
}
static bool instant(const font_panel_t *panel) {
  return panel->animations == SIGN_ANIM_OFF;
}
static int lang_index(const font_panel_t *panel) {
  return panel->english ? 1 : 0;
}
static double unit_scale(const font_panel_t *panel) {
  return panel && panel->scale > 0 ? panel->scale : 1;
}
static bool listed(const font_panel_face_t *face, font_panel_filter_t filter) {
  if (filter == FONT_PANEL_MONO)
    return face->mono;
  if (filter == FONT_PANEL_PROP)
    return !face->mono;
  return true;
}
static int row_count(int items) {
  if (items <= 0)
    return 0;
  return (items + FONT_PANEL_COLS - 1) / FONT_PANEL_COLS;
}
static int visible_rows(int rows) {
  return rows > FONT_PANEL_ROWS ? FONT_PANEL_ROWS : rows;
}
static const char *fallback_label(const font_panel_t *panel) {
  return WORDS[lang_index(panel)].fallback;
}
static bool is_default_name(const char *name) {
  return !name || !name[0];
}
static void copy_name(char *dst, size_t cap, const char *name) {
  if (!dst || !cap)
    return;
  if (is_default_name(name)) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, cap, "%s", name);
}
static void poke(font_panel_t *panel, int64_t now) {
  panel->now_ms = now;
  if (panel->open)
    panel->idle_at = now + FONT_PANEL_IDLE_MS;
}
static int max_first(int items) {
  int rows = row_count(items);
  return rows > FONT_PANEL_ROWS ? rows - FONT_PANEL_ROWS : 0;
}
static void clamp_scroll(font_panel_t *panel) {
  int limit = max_first(font_panel_count(panel));
  if (panel->first_row > limit)
    panel->first_row = limit;
  if (panel->first_row < 0)
    panel->first_row = 0;
  if (panel->hover >= font_panel_count(panel))
    panel->hover = -1;
}

void font_panel_reset(font_panel_t *panel) {
  if (!panel)
    return;
  memset(panel, 0, sizeof(*panel));
  panel->scale = 1;
  panel->hover = -1;
  panel->hot = -1;
  panel->prepared = -1;
  panel->real_preview = true;
}
void font_panel_set_faces(font_panel_t *panel, const font_panel_face_t *faces,
                          int count) {
  if (!panel)
    return;
  panel->face_count = 0;
  if (!faces || count <= 0) {
    clamp_scroll(panel);
    panel->dirty = true;
    return;
  }
  for (int i = 0; i < count && panel->face_count < FONT_PANEL_CAP; i++) {
    if (is_default_name(faces[i].name))
      continue;
    font_panel_face_t *dst = &panel->faces[panel->face_count++];
    copy_name(dst->name, sizeof(dst->name), faces[i].name);
    dst->mono = faces[i].mono;
  }
  clamp_scroll(panel);
  panel->dirty = true;
}
int font_panel_count(const font_panel_t *panel) {
  if (!panel)
    return 0;
  int count = panel->filter == FONT_PANEL_MONO ? 0 : 1;
  for (int i = 0; i < panel->face_count; i++)
    if (listed(&panel->faces[i], panel->filter))
      count++;
  return count;
}
static const font_panel_face_t *face_at(const font_panel_t *panel, int index) {
  if (!panel || index < 0)
    return NULL;
  int seen = panel->filter == FONT_PANEL_MONO ? 0 : 1;
  if (panel->filter != FONT_PANEL_MONO && index == 0)
    return NULL;
  for (int i = 0; i < panel->face_count; i++) {
    if (!listed(&panel->faces[i], panel->filter))
      continue;
    if (seen == index)
      return &panel->faces[i];
    seen++;
  }
  return NULL;
}
static bool index_valid(const font_panel_t *panel, int index) {
  return panel && index >= 0 && index < font_panel_count(panel);
}
const char *font_panel_label(const font_panel_t *panel, int index) {
  if (!index_valid(panel, index))
    return NULL;
  if (panel->filter != FONT_PANEL_MONO && index == 0)
    return fallback_label(panel);
  const font_panel_face_t *face = face_at(panel, index);
  return face ? face->name : NULL;
}
const char *font_panel_family(const font_panel_t *panel, int index) {
  if (!index_valid(panel, index))
    return NULL;
  const font_panel_face_t *face = face_at(panel, index);
  return face && face->name[0] ? face->name : SANS;
}
void font_panel_count_text(const font_panel_t *panel, char *out, size_t cap) {
  if (!out || !cap)
    return;
  int count = font_panel_count(panel);
  if (panel && panel->english)
    snprintf(out, cap, "%d fonts", count);
  else
    snprintf(out, cap, "%d 个字体", count);
}
int font_panel_selected_index(const font_panel_t *panel) {
  if (!panel)
    return -1;
  int count = font_panel_count(panel);
  for (int i = 0; i < count; i++) {
    const char *family = font_panel_family(panel, i);
    bool chosen = is_default_name(panel->selected);
    if (chosen && family == SANS)
      return i;
    if (!chosen && family && !strcmp(family, panel->selected))
      return i;
  }
  return -1;
}
const char *font_panel_preview_family(const font_panel_t *panel) {
  if (!panel)
    return SANS;
  if (panel->hover >= 0)
    return font_panel_family(panel, panel->hover);
  if (!is_default_name(panel->selected))
    return panel->selected;
  return SANS;
}
const char *font_panel_hover_family(const font_panel_t *panel) {
  if (!panel || !panel->open || panel->hover < 0)
    return NULL;
  return font_panel_family(panel, panel->hover);
}
// The panel's own words keep the chosen face. The main face follows the
// pointer while a face is tried, and the words must not change with it.
static const char *chrome(const font_panel_t *panel) {
  return panel && !is_default_name(panel->selected) ? panel->selected : SANS;
}
double font_panel_hot(const font_panel_t *panel, int *cell) {
  if (cell)
    *cell = panel ? panel->hot : -1;
  if (!panel)
    return 0;
  return motion_at(&panel->mix, panel->now_ms);
}
double font_panel_width(double scale) {
  if (!(scale > 0))
    scale = 1;
  return DESIGN_W * scale;
}
double font_panel_height(int items, double scale) {
  if (!(scale > 0))
    scale = 1;
  int visible = visible_rows(row_count(items));
  double grid = 0;
  if (visible)
    grid = visible * CELL_H + (visible - 1) * CELL_GAP;
  return (INSET + HEAD + GAP + grid + GAP + PREVIEW_H + INSET) * scale;
}
static void column_box(int column, double scale, double *x, double *w) {
  static const double xs[] = {12, 194};
  static const double ws[] = {178, 178};
  *x = xs[column] * scale;
  *w = ws[column] * scale;
}
static double thumb_width(double scale) {
  return (164.0 / 3.0 - 1.4) * scale;
}
static int measure_text(const char *family, const char *text, double px,
                        bool bold) {
  if (!text || !text[0])
    return 0;
  return text_measure_family(family, text, (float)px, bold);
}
static const char *preview_family(const font_panel_t *panel) {
  if (!panel || !panel->real_preview)
    return NULL;
  return font_panel_preview_family(panel);
}
static font_panel_box_t preview_box(const font_panel_t *panel, double scale,
                                    double grid_h) {
  const char *family = preview_family(panel);
  const char *meta = WORDS[panel && panel->english ? 1 : 0].meta;
  int name = measure_text(family, PREVIEW_NAME, 13 * scale, true);
  int note = measure_text(family, meta, 11.5 * scale, false);
  double width = (4 + 20) * scale + name + GAP * scale + note;
  double limit = (DESIGN_W - INSET * 2) * scale;
  if (width > limit)
    width = limit;
  if (width < 24 * scale)
    width = 24 * scale;
  return (font_panel_box_t){.x = INSET * scale,
                            .y = (INSET + HEAD + GAP + grid_h + GAP) * scale,
                            .w = width,
                            .h = PREVIEW_H * scale};
}
void font_panel_layout(const font_panel_t *panel, font_panel_layout_t *out) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  double scale = unit_scale(panel);
  int items = font_panel_count(panel);
  int rows = row_count(items);
  int visible = visible_rows(rows);
  double grid = visible ? visible * CELL_H + (visible - 1) * CELL_GAP : 0;
  out->width = font_panel_width(scale);
  out->height = font_panel_height(items, scale);
  out->rows = rows;
  out->first_row = panel ? panel->first_row : 0;
  out->count = (font_panel_box_t){.x = INSET * scale,
                                  .y = INSET * scale,
                                  .w = (204 - 10 - 12) * scale,
                                  .h = HEAD * scale};
  out->filter = (font_panel_box_t){.x = (DESIGN_W - INSET - FILTER_W) * scale,
                                   .y = INSET * scale,
                                   .w = FILTER_W * scale,
                                   .h = HEAD * scale};
  double pos = 0;
  if (panel)
    pos = motion_at(&panel->thumb, panel->now_ms);
  double tw = thumb_width(scale);
  out->thumb = (font_panel_box_t){.x = out->filter.x + 4 * scale + pos * tw,
                                  .y = out->filter.y + 4 * scale,
                                  .w = tw,
                                  .h = 16 * scale};
  int shown = items - out->first_row * FONT_PANEL_COLS;
  if (shown < 0)
    shown = 0;
  if (shown > visible * FONT_PANEL_COLS)
    shown = visible * FONT_PANEL_COLS;
  out->cell_count = shown;
  for (int i = 0; i < shown; i++) {
    double x, w;
    int row = i / FONT_PANEL_COLS;
    column_box(i % FONT_PANEL_COLS, scale, &x, &w);
    out->cells[i] = (font_panel_box_t){
        .x = x,
        .y = (INSET + HEAD + GAP + row * (CELL_H + CELL_GAP)) * scale,
        .w = w,
        .h = CELL_H * scale};
  }
  out->preview = preview_box(panel, scale, grid);
  if (rows > FONT_PANEL_ROWS && grid > 0) {
    double thumb_h = grid * FONT_PANEL_ROWS / rows;
    int limit = rows - FONT_PANEL_ROWS;
    double travel = grid - thumb_h;
    double along = limit > 0 ? (double)out->first_row / limit : 0;
    out->bar =
        (font_panel_box_t){.x = (DESIGN_W - INSET) * scale,
                           .y = (INSET + HEAD + GAP + travel * along) * scale,
                           .w = 3 * scale,
                           .h = thumb_h * scale};
  }
}
void font_panel_place(const font_panel_box_t *card,
                      const font_panel_size_t *panel,
                      const font_panel_size_t *output, double gap, double *x,
                      double *y) {
  double card_x = card ? card->x : 0;
  double card_y = card ? card->y : 0;
  double card_w = card ? card->w : 0;
  double card_h = card ? card->h : 0;
  double panel_w = panel ? panel->w : 0;
  double panel_h = panel ? panel->h : 0;
  double output_w = output ? output->w : 0;
  double output_h = output ? output->h : 0;
  double left = card_x + card_w + gap;
  if (left + panel_w > output_w)
    left = card_x - gap - panel_w;
  double top = card_y + card_h - panel_h;
  if (top < 0)
    top = 0;
  if (top + panel_h > output_h)
    top = output_h - panel_h;
  if (top < 0)
    top = 0;
  if (left < 0)
    left = 0;
  if (left + panel_w > output_w)
    left = output_w - panel_w;
  if (left < 0)
    left = 0;
  if (x)
    *x = left;
  if (y)
    *y = top;
}
void font_panel_pixels(const font_panel_t *panel, int scale_120, int *w,
                       int *h) {
  double buffer = scale_120 > 0 ? scale_120 / 120.0 : 1;
  double scale = unit_scale(panel);
  int width = (int)lround(font_panel_width(scale) * buffer);
  int height =
      (int)lround(font_panel_height(font_panel_count(panel), scale) * buffer);
  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;
  if (w)
    *w = width;
  if (h)
    *h = height;
}
bool font_panel_is_open(const font_panel_t *panel) {
  return panel && panel->open;
}
void font_panel_close(font_panel_t *panel) {
  if (!panel || !panel->open)
    return;
  panel->open = false;
  panel->hover = -1;
  panel->hot = -1;
  panel->idle_at = 0;
  panel->dirty = false;
  motion_snap(&panel->appear, 0);
  motion_snap(&panel->fade, 0);
  motion_snap(&panel->mix, 0);
  sign_draw_font_panel_cleanup();
  text_release_unused(true);
}
static void remember(font_panel_t *panel, const char *selected) {
  copy_name(panel->selected, sizeof(panel->selected), selected);
}
bool font_panel_open(font_panel_t *panel, bool english, double scale,
                     sign_animations_t animations, const char *selected,
                     int64_t now_ms) {
  if (!panel)
    return false;
  bool was = panel->open;
  panel->english = english;
  panel->scale = scale > 0 ? scale : 1;
  panel->animations = animations;
  remember(panel, selected);
  panel->now_ms = now_ms;
  if (was) {
    panel->dirty = true;
    poke(panel, now_ms);
    return true;
  }
  panel->open = true;
  panel->filter = FONT_PANEL_ALL;
  panel->first_row = 0;
  panel->hover = -1;
  panel->hot = -1;
  motion_snap(&panel->thumb, 0);
  motion_snap(&panel->mix, 0);
  motion_aim(&panel->appear, 1, (int)POP_MS, now_ms, CURVE_POP, instant(panel));
  motion_aim(&panel->fade, 1, (int)FADE_MS, now_ms, CURVE_EASE, instant(panel));
  panel->dirty = true;
  poke(panel, now_ms);
  return true;
}
void font_panel_set_selected(font_panel_t *panel, const char *selected) {
  if (!panel)
    return;
  remember(panel, selected);
  panel->dirty = true;
}
void font_panel_set_theme(font_panel_t *panel, sign_theme_t theme) {
  if (!panel || panel->theme == theme)
    return;
  panel->theme = theme;
  panel->dirty = true;
}
void font_panel_set_language(font_panel_t *panel, bool english) {
  if (!panel || panel->english == english)
    return;
  panel->english = english;
  panel->dirty = true;
}
void font_panel_activity(font_panel_t *panel, int64_t now_ms) {
  if (!panel || !panel->open)
    return;
  poke(panel, now_ms);
}
static void set_hover(font_panel_t *panel, int hover, int64_t now) {
  panel->now_ms = now;
  if (hover == panel->hover)
    return;
  bool off = instant(panel);
  if (hover >= 0) {
    panel->hover = hover;
    panel->hot = hover;
    motion_retarget(&panel->mix, 0, 1, (int)HOT_MS, now, CURVE_EASE, off);
  } else {
    panel->hover = -1;
    double from = motion_at(&panel->mix, now);
    motion_retarget(&panel->mix, from, 0, (int)HOT_MS, now, CURVE_EASE, off);
    if (off)
      panel->hot = -1;
  }
  panel->dirty = true;
}
static bool inside(font_panel_box_t box, double x, double y) {
  return x >= box.x && y >= box.y && x < box.x + box.w && y < box.y + box.h;
}
static int cell_at(const font_panel_layout_t *layout, double x, double y) {
  for (int i = 0; i < layout->cell_count; i++)
    if (inside(layout->cells[i], x, y))
      return layout->first_row * FONT_PANEL_COLS + i;
  return -1;
}
static int filter_at(const font_panel_layout_t *layout, double x, double y) {
  if (!inside(layout->filter, x, y) || layout->filter.w <= 0)
    return -1;
  int index = (int)((x - layout->filter.x) / (layout->filter.w / 3));
  if (index < 0)
    index = 0;
  if (index > 2)
    index = 2;
  return index;
}
void font_panel_pointer(font_panel_t *panel, double x, double y,
                        int64_t now_ms) {
  if (!panel || !panel->open || !isfinite(x) || !isfinite(y))
    return;
  poke(panel, now_ms);
  font_panel_layout_t layout;
  font_panel_layout(panel, &layout);
  set_hover(panel, cell_at(&layout, x, y), now_ms);
}
void font_panel_leave(font_panel_t *panel, int64_t now_ms) {
  if (!panel || !panel->open)
    return;
  set_hover(panel, -1, now_ms);
}
static void set_filter(font_panel_t *panel, font_panel_filter_t filter,
                       int64_t now) {
  poke(panel, now);
  if (filter < FONT_PANEL_ALL || filter > FONT_PANEL_MONO)
    return;
  if (panel->filter == filter)
    return;
  panel->filter = filter;
  panel->first_row = 0;
  set_hover(panel, -1, now);
  motion_aim(&panel->thumb, (double)filter, (int)THUMB_MS, now, CURVE_MOVE,
             instant(panel));
  panel->dirty = true;
}
void font_panel_wheel(font_panel_t *panel, int discrete, int64_t now_ms) {
  if (!panel || !panel->open)
    return;
  poke(panel, now_ms);
  int step = discrete;
  if (step > 8)
    step = 8;
  if (step < -8)
    step = -8;
  if (!step)
    return;
  int next = panel->first_row + step;
  int limit = max_first(font_panel_count(panel));
  if (next < 0)
    next = 0;
  if (next > limit)
    next = limit;
  if (next == panel->first_row)
    return;
  panel->first_row = next;
  set_hover(panel, -1, now_ms);
  panel->dirty = true;
}
bool font_panel_click(font_panel_t *panel, double x, double y, int64_t now_ms,
                      char *out, size_t cap) {
  if (!panel || !panel->open || !isfinite(x) || !isfinite(y))
    return false;
  poke(panel, now_ms);
  font_panel_layout_t layout;
  font_panel_layout(panel, &layout);
  int cell = cell_at(&layout, x, y);
  if (cell >= 0) {
    const font_panel_face_t *face = face_at(panel, cell);
    const char *name = face ? face->name : "";
    remember(panel, name);
    if (out && cap)
      copy_name(out, cap, name);
    panel->dirty = true;
    return true;
  }
  int filter = filter_at(&layout, x, y);
  if (filter >= 0)
    set_filter(panel, (font_panel_filter_t)filter, now_ms);
  return false;
}
void font_panel_set_prepared(font_panel_t *panel, int cells) {
  if (!panel)
    return;
  panel->prepared = cells < -1 ? -1 : cells;
}
void font_panel_set_real_preview(font_panel_t *panel, bool real) {
  if (panel)
    panel->real_preview = real;
}
int font_panel_prepared(const font_panel_t *panel) {
  return panel ? panel->prepared : -1;
}
double font_panel_opacity(const font_panel_t *panel) {
  double fade = 0;
  if (panel && panel->open)
    fade = motion_at(&panel->fade, panel->now_ms);
  if (fade < 0)
    return 0;
  return fade > 1 ? 1 : fade;
}
font_panel_wake_t font_panel_step(font_panel_t *panel, int64_t now_ms) {
  font_panel_wake_t wake = {.timeout_ms = -1};
  if (!panel || !panel->open)
    return wake;
  panel->now_ms = now_ms;
  if (panel->idle_at && now_ms >= panel->idle_at) {
    font_panel_close(panel);
    wake.redraw = true;
    return wake;
  }
  bool moving = motion_busy(&panel->appear, now_ms) ||
                motion_busy(&panel->fade, now_ms) ||
                motion_busy(&panel->thumb, now_ms) ||
                motion_busy(&panel->mix, now_ms);
  if (panel->hot >= 0 && motion_at(&panel->mix, now_ms) <= 0 &&
      panel->hover < 0) {
    panel->hot = -1;
    panel->dirty = true;
  }
  wake.redraw = panel->dirty || moving;
  wake.frame = moving;
  panel->dirty = false;
  int64_t wait = panel->idle_at - now_ms;
  if (wait < 1)
    wait = 1;
  if (wait > 86400000)
    wait = 86400000;
  wake.timeout_ms = (int)wait;
  return wake;
}

static uint32_t with_alpha(uint32_t rgb, double alpha) {
  if (alpha <= 0)
    return rgb & 0xffffffU;
  if (alpha >= 1)
    return rgb | 0xff000000U;
  return (rgb & 0xffffffU) | ((uint32_t)lround(alpha * 255) << 24);
}
static uint32_t mix_rgb(uint32_t from, uint32_t to, double t) {
  if (t <= 0)
    return from;
  if (t >= 1)
    return to;
  uint32_t mixed = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    int a = (int)((from >> shift) & 255);
    int b = (int)((to >> shift) & 255);
    mixed |= (uint32_t)lround(a + (b - a) * t) << shift;
  }
  return mixed | 0xff000000U;
}
typedef struct {
  double s, dx, oy;
} pop_t;
static pop_t pop_of(const font_panel_t *panel) {
  double amount = motion_at(&panel->appear, panel->now_ms);
  if (amount < 0)
    amount = 0;
  if (amount > 1)
    amount = 1;
  double height = font_panel_height(font_panel_count(panel), unit_scale(panel));
  return (pop_t){0.92 + 0.08 * amount, (1 - amount) * -12 * unit_scale(panel),
                 height};
}
static font_panel_box_t pop_box(pop_t pop, font_panel_box_t box) {
  box.x = box.x * pop.s + pop.dx;
  box.y = pop.oy + (box.y - pop.oy) * pop.s;
  box.w *= pop.s;
  box.h *= pop.s;
  return box;
}
static double pop_y(pop_t pop, double y) {
  return pop.oy + (y - pop.oy) * pop.s;
}
static void add_rect(sign_frame_t *frame, font_panel_box_t box, double radius,
                     double stroke, uint32_t fill, uint32_t outline) {
  if (frame->shape_count >= SIGN_MAX_SHAPES || box.w <= 0 || box.h <= 0)
    return;
  if (!((fill >> 24) || (outline >> 24)))
    return;
  frame->shapes[frame->shape_count++] = (sign_shape_t){.kind = SIGN_RECT,
                                                       .x = box.x,
                                                       .y = box.y,
                                                       .w = box.w,
                                                       .h = box.h,
                                                       .radius = radius,
                                                       .stroke = stroke,
                                                       .fill = fill,
                                                       .outline = outline,
                                                       .above = true};
}
static int physical(double logical, int scale_120) {
  return (int)lround(logical * scale_120 / 120.0);
}
static text_clip_t clip_of(font_panel_box_t box, int scale_120) {
  int x = physical(box.x, scale_120);
  int y = physical(box.y, scale_120);
  int r = physical(box.x + box.w, scale_120);
  int b = physical(box.y + box.h, scale_120);
  if (r < x)
    r = x;
  if (b < y)
    b = y;
  return (text_clip_t){x, y, r - x, b - y};
}
static void draw_line(uint8_t *dst, int dw, int dh, int scale_120, double x,
                      double baseline, const char *family, const char *text,
                      double px, bool bold, uint32_t color, double max_w,
                      font_panel_box_t clip) {
  if (!text || !text[0] || !(color >> 24))
    return;
  int limit = max_w > 0 ? physical(max_w, scale_120) : 0;
  text_draw_clip_family(dst, dw, dh, physical(x, scale_120),
                        physical(baseline, scale_120), family, text, (float)px,
                        bold, color, limit, clip_of(clip, scale_120));
}
static double centered_base(const char *family, font_panel_box_t line,
                            double px, bool bold) {
  return text_baseline_family(family, line.y, line.h, (float)px, bold);
}
static void draw_shapes(const font_panel_t *panel, sign_frame_t *frame,
                        const font_panel_layout_t *layout, pop_t pop,
                        double fade) {
  const sign_palette_t *palette = sign_palette(panel->theme);
  double scale = unit_scale(panel);
  add_rect(
      frame,
      pop_box(pop, (font_panel_box_t){0, 0, layout->width, layout->height}),
      14 * scale * pop.s, 2 * scale * pop.s, with_alpha(palette->paper, fade),
      with_alpha(palette->ink, fade));
  add_rect(frame, pop_box(pop, layout->filter), 9 * scale * pop.s,
           2 * scale * pop.s, with_alpha(palette->paper, fade),
           with_alpha(palette->ink, fade));
  add_rect(frame, pop_box(pop, layout->thumb), 5 * scale * pop.s, 0,
           with_alpha(palette->ink, fade), 0);
  int selected = font_panel_selected_index(panel);
  double mix = motion_at(&panel->mix, panel->now_ms);
  for (int i = 0; i < layout->cell_count; i++) {
    int index = layout->first_row * FONT_PANEL_COLS + i;
    font_panel_box_t box = pop_box(pop, layout->cells[i]);
    if (index == selected) {
      add_rect(frame, box, 8 * scale * pop.s, 0, with_alpha(palette->ink, fade),
               0);
    } else if (index == panel->hot && mix > 0) {
      add_rect(frame, box, 8 * scale * pop.s, 0,
               with_alpha(palette->hover, fade * mix), 0);
    }
  }
  add_rect(frame, pop_box(pop, layout->preview), 10 * scale * pop.s,
           2 * scale * pop.s, with_alpha(palette->plate, fade),
           with_alpha(palette->ink, fade));
  if (layout->bar.w > 0)
    add_rect(frame, pop_box(pop, layout->bar), 1.5 * scale * pop.s, 0,
             with_alpha(palette->ink, fade), 0);
}
static void draw_filter_labels(const font_panel_t *panel, uint8_t *dst, int dw,
                               int dh, int scale_120,
                               const font_panel_layout_t *layout, pop_t pop,
                               double fade) {
  const sign_palette_t *palette = sign_palette(panel->theme);
  double scale = unit_scale(panel);
  double pos = motion_at(&panel->thumb, panel->now_ms);
  const panel_words_t *words = &WORDS[lang_index(panel)];
  const char *labels[] = {words->all, words->prop, words->mono};
  double px = 11.5 * scale;
  // The switch's grid is the content box, inside the 2px border.
  double origin = layout->filter.x + 2 * scale;
  double column = (FILTER_W - 4) * scale / 3;
  for (int i = 0; i < 3; i++) {
    double amount = 1 - fabs(pos - i);
    if (amount < 0)
      amount = 0;
    uint32_t color =
        with_alpha(mix_rgb(palette->ink, palette->paper, amount), fade);
    int width = measure_text(chrome(panel), labels[i], px, true);
    double x = origin + column * i + (column - width) / 2;
    font_panel_box_t line = {origin + column * i, layout->filter.y + 2 * scale,
                             column, layout->filter.h - 4 * scale};
    double base = centered_base(chrome(panel), line, px, true);
    font_panel_box_t drawn = pop_box(pop, line);
    draw_line(dst, dw, dh, scale_120,
              pop_box(pop, (font_panel_box_t){x, 0, 0, 0}).x, pop_y(pop, base),
              chrome(panel), labels[i], px * pop.s, true, color, 0, drawn);
  }
}
static void draw_cells(const font_panel_t *panel, uint8_t *dst, int dw, int dh,
                       int scale_120, const font_panel_layout_t *layout,
                       pop_t pop, double fade) {
  const sign_palette_t *palette = sign_palette(panel->theme);
  double scale = unit_scale(panel);
  double px = 13 * scale;
  int selected = font_panel_selected_index(panel);
  for (int i = 0; i < layout->cell_count; i++) {
    int index = layout->first_row * FONT_PANEL_COLS + i;
    const char *label = font_panel_label(panel, index);
    const char *family = font_panel_family(panel, index);
    if (panel->prepared >= 0 && index >= panel->prepared)
      family = chrome(panel);
    font_panel_box_t cell = layout->cells[i];
    font_panel_box_t text = {cell.x + 8 * scale, cell.y, cell.w - 16 * scale,
                             cell.h};
    if (text.w <= 0)
      continue;
    double base = centered_base(family, text, px, true);
    uint32_t color =
        with_alpha(index == selected ? palette->paper : palette->ink, fade);
    font_panel_box_t drawn = pop_box(pop, text);
    draw_line(dst, dw, dh, scale_120,
              pop_box(pop, (font_panel_box_t){text.x, 0, 0, 0}).x,
              pop_y(pop, base), family, label, px * pop.s, true, color, text.w,
              drawn);
  }
}
static void draw_preview(const font_panel_t *panel, uint8_t *dst, int dw,
                         int dh, int scale_120,
                         const font_panel_layout_t *layout, pop_t pop,
                         double fade) {
  const sign_palette_t *palette = sign_palette(panel->theme);
  double scale = unit_scale(panel);
  const char *family = preview_family(panel);
  const char *meta = WORDS[lang_index(panel)].meta;
  font_panel_box_t plate = layout->preview;
  font_panel_box_t line = {plate.x + 12 * scale, plate.y + 2 * scale,
                           plate.w - 24 * scale, plate.h - 4 * scale};
  if (line.w <= 0 || line.h <= 0)
    return;
  double name_px = 13 * scale;
  double base = centered_base(family, line, name_px, true);
  int name_w = measure_text(family, PREVIEW_NAME, name_px, true);
  font_panel_box_t drawn = pop_box(pop, line);
  double x = pop_box(pop, (font_panel_box_t){line.x, 0, 0, 0}).x;
  double y = pop_y(pop, base);
  uint32_t ink = with_alpha(palette->ink, fade);
  draw_line(dst, dw, dh, scale_120, x, y, family, PREVIEW_NAME, name_px * pop.s,
            true, ink, 0, drawn);
  double meta_x = line.x + name_w + GAP * scale;
  draw_line(dst, dw, dh, scale_120,
            pop_box(pop, (font_panel_box_t){meta_x, 0, 0, 0}).x, y, family,
            meta, 11.5 * scale * pop.s, false, with_alpha(palette->meta, fade),
            0, drawn);
}
void font_panel_draw(const font_panel_t *panel, uint8_t *dst, int dst_w,
                     int dst_h, int scale_120) {
  if (!panel || !panel->open || !dst || dst_w <= 0 || dst_h <= 0)
    return;

  const sign_palette_t *palette = sign_palette(panel->theme);
  double fade = motion_at(&panel->fade, panel->now_ms);
  if (fade < 0)
    fade = 0;
  if (fade > 1)
    fade = 1;
  font_panel_layout_t layout;
  font_panel_layout(panel, &layout);
  pop_t pop = pop_of(panel);
  static sign_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.bounds_x = -20;
  frame.bounds_y = -20;
  frame.bounds_w = (int)ceil(layout.width) + 40;
  frame.bounds_h = (int)ceil(layout.height) + 40;
  frame.transitioning = motion_busy(&panel->appear, panel->now_ms) ||
                        motion_busy(&panel->fade, panel->now_ms) ||
                        motion_busy(&panel->thumb, panel->now_ms) ||
                        motion_busy(&panel->mix, panel->now_ms);
  draw_shapes(panel, &frame, &layout, pop, fade);
  if (scale_120 > 0)
    text_set_scale(scale_120);
  sign_draw_font_panel(dst, dst_w, dst_h, scale_120 > 0 ? scale_120 : 120,
                       &frame);
  if (fade <= 0)
    return;
  double scale = unit_scale(panel);
  char count[64];
  font_panel_count_text(panel, count, sizeof(count));
  double count_px = 11.5 * scale;
  double count_base =
      centered_base(chrome(panel), layout.count, count_px, false);
  draw_line(dst, dst_w, dst_h, scale_120 > 0 ? scale_120 : 120,
            pop_box(pop, (font_panel_box_t){layout.count.x, 0, 0, 0}).x,
            pop_y(pop, count_base), chrome(panel), count, count_px * pop.s,
            false, with_alpha(palette->count, fade), layout.count.w,
            pop_box(pop, layout.count));
  int buffer = scale_120 > 0 ? scale_120 : 120;
  draw_filter_labels(panel, dst, dst_w, dst_h, buffer, &layout, pop, fade);
  draw_cells(panel, dst, dst_w, dst_h, buffer, &layout, pop, fade);
  draw_preview(panel, dst, dst_w, dst_h, buffer, &layout, pop, fade);
}
