#include "graphics/signs.h"

#include "config/sign_options.h"
#include "core/agent_sessions.h"
#include "core/agent_sign_state.h"
#include "core/agent_state.h"
#include "graphics/sign_palette.h"
#include "signs_internal.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Indexed by agent_state_t. Error is darker than waiting, not only redder.

const sign_bezier_t BEZIER_MOVE = {.34, 1.4, .64, 1};
const sign_bezier_t BEZIER_WIDTH = {.34, 1.3, .64, 1};
const sign_bezier_t BEZIER_POP = {.34, 1.56, .64, 1};
const sign_bezier_t BEZIER_EASE = {.25, .1, .25, 1};
static const sign_bezier_t BEZIER_LOOP = {.42, 0, .58, 1};
const sign_bezier_t BEZIER_SLIDE = {.2, .8, .2, 1};

static double component(double t, double p1, double p2) {
  double u = 1 - t;
  return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
}
static double curve_at(double fraction, const sign_bezier_t *curve) {
  if (fraction <= 0)
    return 0;
  if (fraction >= 1)
    return 1;
  double low = 0, high = 1;
  for (int i = 0; i < 24; i++) {
    double mid = (low + high) / 2;
    if (component(mid, curve->x1, curve->x2) < fraction)
      low = mid;
    else
      high = mid;
  }
  return component((low + high) / 2, curve->y1, curve->y2);
}
double sample(const sign_scalar_t *scalar, int64_t now) {
  if (!scalar->duration || now >= scalar->start + scalar->duration)
    return scalar->target;
  if (now <= scalar->start)
    return scalar->from;
  sign_bezier_t curve = {scalar->x1, scalar->y1, scalar->x2, scalar->y2};
  double span = (double)(now - scalar->start) / scalar->duration;
  return scalar->from +
         (scalar->target - scalar->from) * curve_at(span, &curve);
}
double aim(sign_scalar_t *scalar, double target, int duration,
           const sign_bezier_t *curve, const sign_input_t *in,
           sign_frame_t *frame) {
  if (in->animations == SIGN_ANIM_OFF)
    duration = 0;
  if (scalar->target != target) {
    scalar->from = sample(scalar, in->now_ms);
    scalar->target = target;
    scalar->start = in->now_ms;
    scalar->duration = duration;
    scalar->x1 = curve->x1;
    scalar->y1 = curve->y1;
    scalar->x2 = curve->x2;
    scalar->y2 = curve->y2;
  }
  if (!duration) {
    scalar->from = scalar->target;
    scalar->duration = 0;
  }
  if (scalar->duration && in->now_ms < scalar->start + scalar->duration)
    frame->transitioning = true;
  return sample(scalar, in->now_ms);
}
double clamp_unit(double value) {
  return fmax(0, fmin(value, 1));
}
uint32_t with_alpha(uint32_t rgb, double alpha) {
  double clamped = clamp_unit(alpha);
  return (rgb & 0xffffff) | ((uint32_t)lround(clamped * 255) << 24);
}
void include_bounds(sign_frame_t *frame, double x, double y, double w,
                    double h) {
  int left = (int)floor(x) - 1, top = (int)floor(y) - 1;
  int right = (int)ceil(x + w) + 1, bottom = (int)ceil(y + h) + 1;
  if (frame->bounds_w) {
    right = right > frame->bounds_x + frame->bounds_w
                ? right
                : frame->bounds_x + frame->bounds_w;
    bottom = bottom > frame->bounds_y + frame->bounds_h
                 ? bottom
                 : frame->bounds_y + frame->bounds_h;
    left = left < frame->bounds_x ? left : frame->bounds_x;
    top = top < frame->bounds_y ? top : frame->bounds_y;
  }
  frame->bounds_x = left;
  frame->bounds_y = top;
  frame->bounds_w = right - left;
  frame->bounds_h = bottom - top;
}

// clang-format off
const sign_words_t WORDS[] = {
    {.working = "工作中",
     .waiting = "等你批准",
     .idle = "空闲",
     .minute = "分钟",
     .done = "已完成",
     .done_short = "完成",
     .unread = "已完成 · 未查看",
     .unread_short = "未查看",
     .error = "出错停止",
     .error_short = "出错"},
    {.working = "Working",
     .waiting = "Needs approval",
     .idle = "Idle",
     .minute = "min",
     .done = "Done",
     .done_short = "Done",
     .unread = "Done · Unread",
     .unread_short = "Unread",
     .error = "Stopped on error",
     .error_short = "Error"},
};
// clang-format on
bool finished(agent_state_t state) {
  return state == AGENT_STATE_DONE || state == AGENT_STATE_ERROR;
}
// Meta text takes the icon colour on the two states that ask for attention.
uint32_t meta_color(agent_state_t state, const sign_input_t *in) {
  const sign_palette_t *palette = sign_palette(in->theme);
  bool urgent = state == AGENT_STATE_WAITING || state == AGENT_STATE_ERROR;
  return urgent ? palette->icons[state] & 0xffffffU : palette->secondary;
}
const char *done_label(const sign_input_t *in, bool fan,
                       const agent_session_view_t *session) {
  const sign_words_t *words = &WORDS[in->english ? 1 : 0];
  bool unread = session->unread;
  if (session->state == AGENT_STATE_ERROR)
    return fan ? words->error : words->error_short;
  if (fan)
    return unread ? words->unread : words->done;
  return unread ? words->unread_short : words->done_short;
}
bool show_session(const sign_input_t *in, agent_state_t state) {
  if (in->menu)
    return false;
  return state != AGENT_STATE_IDLE || in->idle == SIGN_IDLE_ALWAYS ||
         (in->idle == SIGN_IDLE_HOVER && in->open);
}
void subagent_state_label(char out[64], const agent_session_view_t *session,
                          const sign_input_t *in, sign_frame_t *frame) {
  int64_t elapsed = in->now_ms - session->child_started_ms;
  if (elapsed < 0)
    elapsed = 0;
  int64_t minutes = elapsed / 60000;
  if (minutes > 99999)
    minutes = 99999;
  snprintf(out, 64,
           in->english ? "Waiting on subagent %lld min"
                       : "等待子代理 %lld 分钟",
           (long long)minutes);
  if (frame)
    wake_at(frame, in->now_ms + 60000 - elapsed % 60000);
}
double font_ratio(const sign_input_t *in) {
  return in->font_size >= 10 && in->font_size <= 20 ? in->font_size / 13.0 : 1;
}
void add_shape(sign_frame_t *frame, sign_shape_kind_t kind, double x, double y,
               double w, double h, double radius, double stroke, uint32_t fill,
               uint32_t outline) {
  bool painted = (fill >> 24) || (outline >> 24);
  if (w <= 0 || h <= 0 || !painted || frame->shape_count >= SIGN_MAX_SHAPES)
    return;
  frame->shapes[frame->shape_count++] = (sign_shape_t){.kind = kind,
                                                       .x = x,
                                                       .y = y,
                                                       .w = w,
                                                       .h = h,
                                                       .radius = radius,
                                                       .stroke = stroke,
                                                       .fill = fill,
                                                       .outline = outline};
  include_bounds(frame, x, y, w, h);
}
void add_unread(sign_frame_t *frame, agent_state_t state, double x, double y,
                double w, double scale, double opacity,
                const sign_input_t *in) {
  const sign_palette_t *palette = sign_palette(in->theme);
  if (scale <= 0) {
    return;
  }
  double dot = 9 * scale;
  add_shape(frame, SIGN_RECT, x + w - 4 * scale, y - 5 * scale, dot, dot,
            dot / 2, 2 * scale, with_alpha(palette->icons[state], opacity),
            with_alpha(palette->ink, opacity));
}
void wake_at(sign_frame_t *frame, int64_t when) {
  frame->animating = true;
  if (!frame->next_frame_ms || when < frame->next_frame_ms)
    frame->next_frame_ms = when;
}
// The return trip visits the same 25 positions. Integer deadlines round up
// the 31.25 ms boundaries; unrelated wakes within a phase cannot move ink.
double nudge_phase(const sign_input_t *in, const sign_slot_t *slot,
                   sign_frame_t *frame) {
  int64_t offset = (in->now_ms - slot->session.state_since_ms) % NUDGE_MS;
  if (offset < 0)
    offset += NUDGE_MS;
  int phase = (int)(offset * NUDGE_PHASES / NUDGE_MS);
  int position = phase <= NUDGE_PHASES / 2 ? phase : NUDGE_PHASES - phase;
  int next = ((phase + 1) * NUDGE_MS + NUDGE_PHASES - 1) / NUDGE_PHASES;
  wake_at(frame, in->now_ms + next - offset);
  int half = NUDGE_PHASES / 2;
  return curve_at((double)position / half, &BEZIER_LOOP);
}
void snap_from(sign_frame_t *frame, int first, bool snap) {
  for (int i = first; i < frame->shape_count; i++)
    frame->shapes[i].pixel_snap = snap;
}
sign_rect_t cover(double x, double y, double w, double h) {
  int left = (int)floor(x), top = (int)floor(y);
  int right = (int)ceil(x + w), bottom = (int)ceil(y + h);
  return (sign_rect_t){left, top, right - left, bottom - top};
}
void add_icon(sign_frame_t *frame, agent_state_t state, double cx, double cy,
              double scale, double opacity, const sign_input_t *in) {
  const sign_palette_t *palette = sign_palette(in->theme);
  uint32_t color = with_alpha(palette->icons[state], opacity);
  int first = frame->shape_count;
  if (state == AGENT_STATE_WORKING) {
    int64_t now = 0;
    if (in->animations == SIGN_ANIM_FULL) {
      now = in->now_ms / DOT_STEP * DOT_STEP;
      wake_at(frame, (in->now_ms / DOT_STEP + 1) * DOT_STEP);
    }
    for (int i = 0; i < 3; i++) {
      double phase = fmod((double)now - i * 150 + 1100, 1100) / 1100;
      double wave = 0;
      if (in->animations == SIGN_ANIM_FULL && phase < .6) {
        double local = phase < .3 ? phase / .3 : (.6 - phase) / .3;
        wave = curve_at(local, &BEZIER_LOOP);
      }
      double dot = .55 + .45 * wave;
      add_shape(frame, SIGN_RECT, cx + (-9 + i * 7) * scale,
                cy + (-2 - 2.5 * wave) * scale, 4 * scale, 4 * scale, 2 * scale,
                0, with_alpha(color, opacity * dot), 0);
    }
  } else if (state == AGENT_STATE_WAITING) {
    add_shape(frame, SIGN_RECT, cx - 1.75 * scale, cy - 7.25 * scale,
              3.5 * scale, 9 * scale, 2 * scale, 0, color, 0);
    add_shape(frame, SIGN_RECT, cx - 1.75 * scale, cy + 3.75 * scale,
              3.5 * scale, 3.5 * scale, 1.75 * scale, 0, color, 0);
  } else if (state == AGENT_STATE_DONE) {
    add_shape(frame, SIGN_CHECK, cx - 8 * scale, cy - 6.5 * scale, 16 * scale,
              13 * scale, 0, 3 * scale, 0, color);
  } else if (state == AGENT_STATE_ERROR) {
    add_shape(frame, SIGN_CROSS, cx - 6.5 * scale, cy - 6.5 * scale, 13 * scale,
              13 * scale, 0, 3 * scale, 0, color);
  } else {
    add_shape(frame, SIGN_RECT, cx - 5 * scale, cy - 1.5 * scale, 10 * scale,
              3 * scale, 2 * scale, 0, color, 0);
  }
  upright_from(frame, first, cx, cy);
}
sign_slot_t *claim_slot(signs_t *model, const agent_session_view_t *session,
                        double rest_bottom) {
  sign_slot_t *free_slot = NULL;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    sign_slot_t *slot = &model->slots[i];
    if (slot->used && slot->session.key == session->key)
      return slot;
    if (!slot->used)
      free_slot = slot;
  }
  if (!free_slot)
    return NULL;
  *free_slot =
      (sign_slot_t){.used = true, .direction = session->order % 2 ? 1 : -1};
  free_slot->bottom.from = free_slot->bottom.target = rest_bottom;
  return free_slot;
}

int sign_clearance(sign_style_t style, int cat_height, int sign_max) {
  int design = 0;
  int extra_rows = sign_max > 5 ? (sign_max > 10 ? 5 : sign_max - 5) : 0;
  // Include the existing movement curve's overshoot and damage outset.
  if (style == SIGN_STYLE_POST)
    design = POST_CLEARANCE + 33 * extra_rows;
  else if (style == SIGN_STYLE_FAN)
    design = FAN_CLEARANCE + (extra_rows ? 93 : 0);
  if (design <= 0 || cat_height <= 0)
    return 0;
  int64_t value = ((int64_t)cat_height * design + 109) / 110;
  return value > INT_MAX ? INT_MAX : (int)value;
}
bool sign_name_persistent(sign_style_t style,
                          const agent_session_view_t *session) {
  agent_state_t state = agent_sign_state(session);
  return state == AGENT_STATE_WAITING ||
         (style == SIGN_STYLE_POST && state == AGENT_STATE_ERROR);
}
int sign_reach(sign_style_t style, int cat_height, int capacity) {
  // Four-millisecond samples: entry, urgent hover, badge/dot, two-line tag.
  // Ceil(maximum ink reach + 8 design pixels); surface clearance is separate.
  static const int post[] = {45, 77, 108, 140, 171, 203, 235, 266, 298, 329};
  int design = 0;
  if (style == SIGN_STYLE_FAN)
    design = capacity > 9 ? 190 : capacity > 5 ? 173 : 114;
  else if (style == SIGN_STYLE_POST) {
    int rows = capacity < 1 ? 5 : capacity > 10 ? 10 : capacity;
    design = post[rows - 1];
  }
  if (cat_height <= 0)
    return 0;
  int64_t value = ((int64_t)cat_height * design + 109) / 110;
  return value > INT_MAX ? INT_MAX : (int)value;
}
bool signs_hit(const sign_frame_t *frame, double x, double y, sign_hit_t *hit) {
  bool found = false;
  double best = 0;
  for (int i = 0; i < frame->hit_count; i++) {
    const sign_hit_t *candidate = &frame->hits[i];
    if (x < candidate->x || y < candidate->y ||
        x >= candidate->x + (double)candidate->w ||
        y >= candidate->y + (double)candidate->h)
      continue;
    if (candidate->precise) {
      double rad = candidate->rotation * 3.141592653589793 / 180;
      double dx = x - candidate->center_x, dy = y - candidate->center_y;
      double local_x = fabs(dx * cos(rad) + dy * sin(rad));
      double local_y = fabs(dy * cos(rad) - dx * sin(rad));
      if (local_x > candidate->half_w || local_y > candidate->half_h)
        continue;
      double corner_x = local_x - candidate->half_w + candidate->radius;
      double corner_y = local_y - candidate->half_h + candidate->radius;
      if (corner_x > 0 && corner_y > 0 &&
          (candidate->kind == SIGN_CUT
               ? corner_x + corner_y > candidate->radius
               : corner_x * corner_x + corner_y * corner_y >
                     candidate->radius * candidate->radius))
        continue;
    }
    double dx = x - (candidate->x + candidate->w / 2.0);
    double dy = y - (candidate->y + candidate->h / 2.0);
    double distance = dx * dx + dy * dy;
    bool same_row = !found || candidate->back_row == hit->back_row;
    if (!found || (hit->back_row && !candidate->back_row) ||
        (same_row && distance < best)) {
      found = true;
      best = distance;
      *hit = *candidate;
    }
  }
  return found;
}
void signs_focus_failed(signs_t *model, uint64_t key, int64_t now) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    if (model->slots[i].used && model->slots[i].session.key == key)
      model->slots[i].failure_ms = now;
}
static void emit_desk(sign_frame_t *frame, const sign_input_t *in, double scale,
                      double lift, double fade) {
  const sign_palette_t *palette = sign_palette(in->theme);
  if (fade <= 0.01 || scale <= 0)
    return;
  double span = DESK_LIFT * scale;
  double progress = span > 0 ? lift / span : 0;
  if (progress < 0)
    progress = 0;
  if (progress > 1)
    progress = 1;
  // cat_y is already lifted. Put the board back on the resting cat.
  double x = in->cat_x + 12 * scale;
  double y = in->cat_y + (double)lround(lift) +
             (DESK_TOP + (1 - progress) * DESK_SLIDE) * scale;
  // The desk is emitted after reflection to keep its default ink unchanged.
  // Reflect only the new displacement when the signs point down.
  double offset = in->desk_offset * scale;
  y += in->orientation == SIGN_BELOW ? -offset : offset;
  double w = 164 * scale;
  double h = 26 * scale;
  if (in->desk_offset && in->surface_height > 0) {
    // Tighten the displacement at a surface/output edge, including the
    // damage outset used by include_bounds. Zero keeps the old pixels.
    y = fmax(2, fmin(y, in->surface_height - h - 2));
  }
  add_shape(frame, SIGN_RECT, x, y, w, h, 8 * scale, 2 * scale,
            with_alpha(palette->paper, fade), with_alpha(palette->ink, fade));
  if (frame->text_count >= SIGN_MAX_TEXTS)
    return;
  sign_text_t *text = &frame->texts[frame->text_count++];
  double ratio = font_ratio(in);
  double line_h = 12 * scale * ratio * 1.2;
  *text = (sign_text_t){.x = x + 12 * scale,
                        .line_top = y + 21 * scale - line_h,
                        .line_h = line_h,
                        .w = 138 * scale,
                        .clip_y = y + 2 * scale,
                        .clip_h = 22 * scale,
                        .px = 12 * scale * ratio,
                        .gap = 6 * scale,
                        .color = with_alpha(palette->ink, fade),
                        .caret = true};
  int64_t phase = in->now_ms % 1000;
  if (phase < 0)
    phase += 1000;
  bool blink = in->animations != SIGN_ANIM_FULL || phase < 500;
  text->meta_color = with_alpha(palette->ink, blink ? fade : 0);
  snprintf(text->value, sizeof(text->value), "%s", in->desk_name);
}

static void finish_motion(sign_frame_t *frame, const sign_input_t *in,
                          double fade) {
  if (frame->transitioning) {
    frame->animating = true;
    frame->next_frame_ms = 0;
    return;
  }
  if (fade > 0.02 && in->animations == SIGN_ANIM_FULL) {
    int64_t phase = in->now_ms % 1000;
    if (phase < 0)
      phase += 1000;
    wake_at(frame, in->now_ms - phase + (phase < 500 ? 500 : 1000));
  }
  if (in->typing && in->typing_until > in->now_ms)
    wake_at(frame, in->typing_until);
}
static void emit_below_card(signs_t *model, const sign_input_t *in,
                            sign_frame_t *frame, double scale) {
  sign_frame_t card = {0};
  emit_menu(model, in, &card, scale);
  signs_reflect(&card, in->cat_y + in->cat_height / 2);
  for (int i = 0; i < card.shape_count && frame->shape_count < SIGN_MAX_SHAPES;
       i++)
    frame->shapes[frame->shape_count++] = card.shapes[i];
  for (int i = 0; i < card.text_count && frame->text_count < SIGN_MAX_TEXTS;
       i++)
    frame->texts[frame->text_count++] = card.texts[i];
  frame->menu_open = card.menu_open;
  frame->menu_card = card.menu_card;
  memcpy(frame->menu_style, card.menu_style, sizeof(card.menu_style));
  memcpy(frame->menu_lang, card.menu_lang, sizeof(card.menu_lang));
  memcpy(frame->menu_theme, card.menu_theme, sizeof(card.menu_theme));
  frame->menu_style_thumb = card.menu_style_thumb;
  frame->menu_lang_thumb = card.menu_lang_thumb;
  frame->menu_theme_thumb = card.menu_theme_thumb;
  frame->menu_font = card.menu_font;
  frame->menu_font_prev = card.menu_font_prev;
  frame->menu_font_next = card.menu_font_next;
  frame->menu_paw = card.menu_paw;
  frame->transitioning |= card.transitioning;
  if (card.bounds_w)
    include_bounds(frame, card.bounds_x, card.bounds_y, card.bounds_w,
                   card.bounds_h);
}
static void layout_post(signs_t *model, const sign_input_t *in,
                        sign_frame_t *frame, size_t count, double desk_clear,
                        double scale, const sign_palette_t *palette) {
  int shown = 0;
  for (size_t i = 0; i < count; i++)
    if (show_session(in, agent_sign_state(&in->sessions[i])))
      shown++;
  int row = shown;
  for (size_t i = 0; i < count; i++) {
    sign_slot_t *slot = claim_slot(model, &in->sessions[i], 100);
    if (!slot)
      continue;
    slot->present = true;
    slot->session = in->sessions[i];
    bool visible = show_session(in, agent_sign_state(&slot->session));
    double bottom = 100;
    if (visible)
      bottom = 100 + --row * 30;
    aim(&slot->bottom, bottom, MOVE_MS, &BEZIER_MOVE, in, frame);
  }
  double pole_x = in->cat_x + 150 * scale;
  double cat_bottom = in->cat_y + in->cat_height;
  double pole = aim(&model->pole, shown ? 106 + 30.0 * shown : 50, MOVE_MS,
                    &BEZIER_MOVE, in, frame) *
                scale;
  pole += desk_clear * scale;
  // Outer width 6, centered on the same axis. The cap sits 6px above the
  // pole top: its padding edge is 2px in, and CSS top is -8.
  add_shape(frame, SIGN_RECT, pole_x - 3 * scale, cat_bottom - pole, 6 * scale,
            pole, 3 * scale, 2 * scale, palette->paper, palette->ink);
  add_shape(frame, SIGN_RECT, pole_x - 5 * scale, cat_bottom - pole - 6 * scale,
            10 * scale, 10 * scale, 5 * scale, 2 * scale, palette->paper,
            palette->ink);
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
      sign_slot_t *slot = &model->slots[i];
      if (!slot->used)
        continue;
      bool hovered = in->has_hover && in->hover_key == slot->session.key;
      if (hovered != (pass == 1))
        continue;
      layout_board(slot, in, frame, pole_x, cat_bottom, scale, desk_clear);
    }
  }
  double extra = shown > 5 ? (shown - 5) * 30 : 0;
  frame->pad =
      cover(in->cat_x - 70 * scale, in->cat_y - (160 + extra) * scale,
            (150 + 17 + POST_BOARD_MAX + 70) * scale, (270 + extra) * scale);
  frame->has_pad = true;
}

static void build_frame(signs_t *model, const sign_input_t *in,
                        sign_frame_t *frame, double desk_clear) {
  memset(frame, 0, sizeof(*frame));
  if (!in || in->style == SIGN_STYLE_OFF || in->cat_height <= 0) {
    memset(model, 0, sizeof(*model));
    return;
  }
  if (!model->initialized) {
    model->initialized = true;
    model->pole.from = model->pole.target = 50;
  }

  agent_session_view_t roots[SIGN_MAX_VISIBLE];
  sign_input_t filtered = *in;
  size_t n = 0;
  bool children = false;
  for (size_t i = 0; in->sessions && i < in->count && i < AGENT_SESSIONS_MAX;
       i++) {
    const agent_session_view_t *s = &in->sessions[i];
    if (s->parent) {
      children = true;
      for (int j = 0; j < AGENT_SESSIONS_MAX; j++)
        if (model->slots[j].used && model->slots[j].session.key == s->key)
          memset(&model->slots[j], 0, sizeof(model->slots[j]));
      if (filtered.typing && filtered.typing_key == s->key)
        filtered.typing = false;
    } else if (n < SIGN_MAX_VISIBLE) {
      roots[n++] = *s;
    }
  }
  if (children) {
    filtered.sessions = roots;
    filtered.count = n;
    in = &filtered;
  }
  const sign_palette_t *palette = sign_palette(in->theme);
  double scale = in->cat_height / 110.0;
  int move_ms = in->desk_snap ? 0 : DESK_MOVE_MS;
  int fade_ms = in->desk_snap ? 0 : DESK_FADE_MS;
  double lift = aim(&model->desk_lift, in->typing ? DESK_LIFT * scale : 0,
                    move_ms, &BEZIER_MOVE, in, frame);
  double fade = clamp_unit(aim(&model->desk_fade, in->typing ? 1 : 0, fade_ms,
                               &BEZIER_EASE, in, frame));
  // At the output edge the available lift can be smaller than DESK_LIFT.
  // The desk keeps its place under the paws: it is positioned from how far
  // the lift animation has run, not from how far the cat could actually rise.
  double travel = lift;
  if (in->orientation == SIGN_BELOW)
    lift = fmin(lift, in->cat_y);
  int shift = (int)lround(lift);
  frame->cat_lift = shift;
  sign_input_t placed = *in;
  placed.cat_y = in->cat_y - shift;
  in = &placed;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    model->slots[i].present = false;
  size_t count = in->count;
  if (!in->sessions)
    count = 0;
  if (count > SIGN_MAX_VISIBLE)
    count = SIGN_MAX_VISIBLE;
  if (in->style == SIGN_STYLE_FAN) {
    layout_fan(model, in, frame, count, desk_clear);
  } else {
    layout_post(model, in, frame, count, desk_clear, scale, palette);
  }
  // The desk stays on the unshifted cat. The pivot above already moved.
  if (in->orientation == SIGN_BELOW) {
    emit_menu(model, &placed, frame, scale);
    signs_reflect(frame, in->cat_y + in->cat_height / 2);
    emit_desk(frame, &placed, scale, travel, fade);
  } else {
    emit_desk(frame, &placed, scale, travel, fade);
    if (in->menu_below)
      emit_below_card(model, &placed, frame, scale);
    else
      emit_menu(model, &placed, frame, scale);
  }
  finish_motion(frame, &placed, fade);
}

void signs_frame(signs_t *model, const sign_input_t *in, sign_frame_t *frame) {
  build_frame(model, in, frame, 0);
  if (!in || in->orientation != SIGN_BELOW || in->style == SIGN_STYLE_OFF ||
      in->cat_height <= 0 || in->menu)
    return;
  double scale = in->cat_height / 110.0;
  double clear = (DESK_CLEAR + (in->desk_offset > 0 ? in->desk_offset : 0)) *
                 scale * clamp_unit(sample(&model->desk_fade, in->now_ms));
  // Measure the unextended frame first, including animated nameplates and
  // damage outsets. Extending a fan rod moves its ink by at most this amount;
  // post rows move by exactly it. Menu and desk geometry remain unchanged.
  if (in->surface_height > 0)
    clear = fmin(clear, fmax(0, in->surface_height -
                                    (frame->bounds_y + frame->bounds_h)));
  if (clear > 0)
    build_frame(model, in, frame, clear / scale);
}
