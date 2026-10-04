#include "graphics/signs.h"

#include "config/sign_options.h"
#include "core/agent_adapters.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define INK        0xff111827U
#define PAPER      0xfff8fafcU
#define MOVE_MS    420
#define WIDTH_MS   380
#define FADE_MS    200
#define HOVER_MS   200
#define DOT_STEP   180
#define NUDGE_STEP 33
#define SHAKE_MS   350
// Peak of cubic-bezier(.34,1.4,.64,1) lifts the 5-sign cap to 162 design
// pixels above the cat. Two more pixels cover the damage outset.
#define POST_CLEARANCE 164
#define FAN_CLEARANCE  104
#define FAN_FADE_MS    250
#define FAN_ANGLE_MS   500
#define FAN_SCALE_MS   180
#define FAN_POP_MS     500
#define FAN_TAG_MS     220
#define DESK_MOVE_MS   320
#define DESK_FADE_MS   180
#define DESK_TOP       68
#define DESK_LIFT      8
#define DESK_SLIDE     22

static const uint32_t FILLS[] = {0xfff8fafc, 0xffd9ebff, 0xffffe4a3,
                                 0xffc7f1d6};
static const uint32_t ICONS[] = {0xff8b93a1, 0xff24558f, 0xff71430b,
                                 0xff22643d};
typedef struct {
  double x1, y1, x2, y2;
} sign_bezier_t;
static const sign_bezier_t BEZIER_MOVE = {.34, 1.4, .64, 1};
static const sign_bezier_t BEZIER_WIDTH = {.34, 1.3, .64, 1};
static const sign_bezier_t BEZIER_POP = {.34, 1.56, .64, 1};
static const sign_bezier_t BEZIER_EASE = {.25, .1, .25, 1};
static const sign_bezier_t BEZIER_LOOP = {.42, 0, .58, 1};

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
static double sample(const sign_scalar_t *scalar, int64_t now) {
  if (!scalar->duration || now >= scalar->start + scalar->duration)
    return scalar->target;
  if (now <= scalar->start)
    return scalar->from;
  sign_bezier_t curve = {scalar->x1, scalar->y1, scalar->x2, scalar->y2};
  double span = (double)(now - scalar->start) / scalar->duration;
  return scalar->from +
         (scalar->target - scalar->from) * curve_at(span, &curve);
}
static double aim(sign_scalar_t *scalar, double target, int duration,
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
static double clamp_unit(double value) {
  return fmax(0, fmin(value, 1));
}
static uint32_t with_alpha(uint32_t rgb, double alpha) {
  double clamped = clamp_unit(alpha);
  return (rgb & 0xffffff) | ((uint32_t)lround(clamped * 255) << 24);
}
static void include_bounds(sign_frame_t *frame, double x, double y, double w,
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
typedef struct {
  const char *working, *waiting, *idle, *minute;
  const char *done, *done_short, *unread, *unread_short;
} sign_words_t;
static const sign_words_t WORDS[] = {
    {.working = "工作中",
     .waiting = "等你批准",
     .idle = "空闲",
     .minute = "分钟",
     .done = "已完成",
     .done_short = "完成",
     .unread = "已完成 · 未查看",
     .unread_short = "完成 · 未查看"},
    {.working = "Working",
     .waiting = "Needs approval",
     .idle = "Idle",
     .minute = "min",
     .done = "Done",
     .done_short = "Done",
     .unread = "Done · Unread",
     .unread_short = "Done · Unread"},
};
static const char *done_label(const sign_input_t *in, bool fan, bool unread) {
  const sign_words_t *words = &WORDS[in->english ? 1 : 0];
  if (fan)
    return unread ? words->unread : words->done;
  return unread ? words->unread_short : words->done_short;
}
static bool show_session(const sign_input_t *in, agent_state_t state) {
  return state != AGENT_STATE_IDLE || in->idle == SIGN_IDLE_ALWAYS ||
         (in->idle == SIGN_IDLE_HOVER && in->open);
}
static double font_ratio(const sign_input_t *in) {
  return in->font_size >= 10 && in->font_size <= 20 ? in->font_size / 13.0 : 1;
}
static void add_shape(sign_frame_t *frame, sign_shape_kind_t kind, double x,
                      double y, double w, double h, double radius,
                      double stroke, uint32_t fill, uint32_t outline) {
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
static void add_unread(sign_frame_t *frame, double x, double y, double w,
                       double scale, double opacity) {
  if (scale <= 0) {
    return;
  }
  double dot = 9 * scale;
  add_shape(frame, SIGN_RECT, x + w - 4 * scale, y - 5 * scale, dot, dot,
            dot / 2, 2 * scale, with_alpha(ICONS[AGENT_STATE_DONE], opacity),
            with_alpha(INK, opacity));
}
static void wake_at(sign_frame_t *frame, int64_t when) {
  frame->animating = true;
  if (!frame->next_frame_ms || when < frame->next_frame_ms)
    frame->next_frame_ms = when;
}
static sign_rect_t cover(double x, double y, double w, double h) {
  int left = (int)floor(x), top = (int)floor(y);
  int right = (int)ceil(x + w), bottom = (int)ceil(y + h);
  return (sign_rect_t){left, top, right - left, bottom - top};
}
static void add_icon(sign_frame_t *frame, agent_state_t state, double cx,
                     double cy, double scale, double opacity,
                     const sign_input_t *in) {
  uint32_t color = with_alpha(ICONS[state], opacity);
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
  } else {
    add_shape(frame, SIGN_RECT, cx - 5 * scale, cy - 1.5 * scale, 10 * scale,
              3 * scale, 2 * scale, 0, color, 0);
  }
}
static sign_slot_t *claim_slot(signs_t *model,
                               const agent_session_view_t *session,
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
static void layout_board(sign_slot_t *slot, const sign_input_t *in,
                         sign_frame_t *frame, double pole_x, double cat_bottom,
                         double scale) {
  bool visible = slot->present && (show_session(in, slot->session.state)) &&
                 !(in->typing && in->typing_key == slot->session.key);
  bool expanded = in->open || slot->session.state == AGENT_STATE_WAITING;
  double width = fmax(0, aim(&slot->width, visible ? (expanded ? 204 : 34) : 0,
                             WIDTH_MS, &BEZIER_WIDTH, in, frame));
  bool pressed = in->has_pressed && in->pressed_key == slot->session.key;
  double opacity_target = visible ? (pressed ? .85 : 1) : 0;
  double opacity = clamp_unit(
      aim(&slot->opacity, opacity_target, FADE_MS, &BEZIER_EASE, in, frame));
  bool hovered = visible && in->has_hover && in->hover_key == slot->session.key;
  double hover =
      aim(&slot->hover, hovered ? 5 : 0, HOVER_MS, &BEZIER_POP, in, frame);
  double states[AGENT_STATE_COUNT], sum = 0;
  for (int state = 0; state < AGENT_STATE_COUNT; state++) {
    double target = slot->session.state == (agent_state_t)state ? 1 : 0;
    states[state] = clamp_unit(
        aim(&slot->states[state], target, FADE_MS, &BEZIER_EASE, in, frame));
    sum += states[state];
  }
  if (!slot->present && opacity <= 0) {
    slot->used = false;
    return;
  }
  if (width <= .1 || opacity <= 0)
    return;
  uint32_t fill = 0;
  for (int channel = 0; channel < 3; channel++) {
    double mixed = 0;
    for (int state = 0; state < AGENT_STATE_COUNT; state++)
      mixed += ((FILLS[state] >> (channel * 8)) & 255) * states[state];
    fill |= (uint32_t)lround(sum ? mixed / sum : 0) << (channel * 8);
  }
  double offset = hover;
  if (states[AGENT_STATE_WAITING] > .001 && in->animations == SIGN_ANIM_FULL) {
    double phase =
        fmod((double)(in->now_ms - slot->session.state_since_ms), 1500) / 1500;
    double local = phase < .5 ? phase * 2 : (1 - phase) * 2;
    double nudge = 6 * curve_at(local, &BEZIER_LOOP);
    offset = (1 - states[AGENT_STATE_WAITING]) * hover +
             states[AGENT_STATE_WAITING] * nudge;
    wake_at(frame, in->now_ms + NUDGE_STEP);
  }
  if (slot->failure_ms && in->now_ms - slot->failure_ms < SHAKE_MS &&
      in->animations != SIGN_ANIM_OFF) {
    double t = (double)(in->now_ms - slot->failure_ms) / SHAKE_MS;
    offset += sin(t * 6 * 3.141592653589793) * 5 * (1 - t);
    frame->transitioning = true;
  }
  int direction = slot->direction;
  double x = pole_x + direction * (5 + offset) * scale -
             (direction < 0 ? width * scale : 0);
  double y = cat_bottom - (sample(&slot->bottom, in->now_ms) + 26) * scale;
  bool other = strcmp(slot->session.agent, "claude") &&
               strcmp(slot->session.agent, "codex");
  double radius = other ? 9 : !strcmp(slot->session.agent, "codex") ? 13 : 8;
  add_shape(frame, other ? SIGN_CUT : SIGN_RECT, x, y, width * scale,
            26 * scale, radius * scale, 2 * scale, with_alpha(fill, opacity),
            with_alpha(INK, opacity));
  int first_icon = frame->shape_count;
  double icon_x = direction > 0 ? x + 17 * scale : x + (width - 17) * scale;
  for (int state = 0; state < AGENT_STATE_COUNT; state++)
    if (states[state] > .001)
      add_icon(frame, (agent_state_t)state, icon_x, y + 13 * scale, scale,
               opacity * states[state], in);
  for (int i = first_icon; i < frame->shape_count; i++) {
    frame->shapes[i].clipped = true;
    frame->shapes[i].clip_x = x + 2 * scale;
    frame->shapes[i].clip_y = y + 2 * scale;
    frame->shapes[i].clip_w = fmax(0, (width - 4) * scale);
    frame->shapes[i].clip_h = 22 * scale;
  }
  if (slot->session.unread && slot->session.state == AGENT_STATE_DONE) {
    add_unread(frame, x, y, width * scale, scale, opacity);
  }
  if (width > 41 && frame->text_count < SIGN_MAX_TEXTS) {
    sign_text_t *text = &frame->texts[frame->text_count++];
    uint32_t meta =
        slot->session.state == AGENT_STATE_WAITING ? 0x71430b : 0x4a5261;
    *text = (sign_text_t){.x = x + (direction > 0 ? 33 : 8) * scale,
                          .baseline_y =
                              y + (17.5 + 4.5 * (font_ratio(in) - 1)) * scale,
                          .w = (width - 41) * scale,
                          .clip_y = y + 2 * scale,
                          .clip_h = 22 * scale,
                          .px = 13 * scale * font_ratio(in),
                          .meta_px = 11.5 * scale * font_ratio(in),
                          .gap = 7 * scale,
                          .color = with_alpha(INK, opacity),
                          .meta_color = with_alpha(meta, opacity),
                          .reverse = direction < 0};
    snprintf(text->value, sizeof(text->value), "%s", slot->session.name);
    char fallback[9];
    const char *who = agent_adapter_display(slot->session.agent, fallback);
    if (slot->session.state == AGENT_STATE_WORKING) {
      int64_t elapsed = in->now_ms - slot->session.state_since_ms;
      if (elapsed < 0)
        elapsed = 0;
      int64_t minutes = elapsed / 60000;
      if (minutes > 99999)
        minutes = 99999;
      snprintf(text->meta, sizeof(text->meta), "%s%s%lld %s", other ? who : "",
               other ? " · " : "", (long long)minutes,
               WORDS[in->english ? 1 : 0].minute);
      wake_at(frame, in->now_ms + 60000 - elapsed % 60000);
    } else {
      const char *label = WORDS[in->english ? 1 : 0].idle;
      if (slot->session.state == AGENT_STATE_WAITING)
        label = WORDS[in->english ? 1 : 0].waiting;
      else if (slot->session.state == AGENT_STATE_DONE)
        label = done_label(in, false, slot->session.unread);
      snprintf(text->meta, sizeof(text->meta), "%s%s%s", other ? who : "",
               other ? " · " : "", label);
    }
  }
  if (visible && frame->hit_count < SIGN_MAX_VISIBLE) {
    frame->hits[frame->hit_count++] =
        (sign_hit_t){(int)floor(x),
                     (int)floor(y),
                     (int)ceil(x + width * scale) - (int)floor(x),
                     (int)ceil(y + 26 * scale) - (int)floor(y),
                     slot->session.key,
                     slot->session.pid};
  }
}
static void spin_point(double x, double y, double ox, double oy, double deg,
                       double *out_x, double *out_y) {
  double rad = deg * 3.141592653589793 / 180.0;
  double cs = cos(rad), sn = sin(rad);
  double dx = x - ox, dy = y - oy;
  *out_x = ox + dx * cs - dy * sn;
  *out_y = oy + dx * sn + dy * cs;
}
static void orbit_from(sign_frame_t *frame, int first, double deg, double ox,
                       double oy) {
  if (fabs(deg) < 0.05)
    return;
  for (int i = first; i < frame->shape_count; i++) {
    sign_shape_t *shape = &frame->shapes[i];
    shape->rotation = deg;
    shape->origin_x = ox;
    shape->origin_y = oy;
    shape->orbit = true;
    double min_x = shape->x, min_y = shape->y;
    double max_x = shape->x + shape->w, max_y = shape->y + shape->h;
    for (int corner = 0; corner < 4; corner++) {
      double rx, ry;
      spin_point(shape->x + ((corner & 1) ? shape->w : 0),
                 shape->y + ((corner & 2) ? shape->h : 0), ox, oy, deg, &rx,
                 &ry);
      if (rx < min_x)
        min_x = rx;
      if (ry < min_y)
        min_y = ry;
      if (rx > max_x)
        max_x = rx;
      if (ry > max_y)
        max_y = ry;
    }
    include_bounds(frame, min_x, min_y, max_x - min_x, max_y - min_y);
  }
}
static void emit_fan(sign_slot_t *slot, const sign_input_t *in,
                     sign_frame_t *frame, double pivot_x, double pivot_y,
                     double cat_scale) {
  bool visible = slot->present && (show_session(in, slot->session.state)) &&
                 !(in->typing && in->typing_key == slot->session.key);
  bool hovered = visible && in->has_hover && in->hover_key == slot->session.key;
  bool pressed = in->has_pressed && in->pressed_key == slot->session.key;
  bool waiting = slot->session.state == AGENT_STATE_WAITING;
  double len_target = 34;
  if (visible && waiting && hovered)
    len_target = 116;
  else if (visible && hovered)
    len_target = 102;
  else if (visible && waiting)
    len_target = 110;
  else if (visible)
    len_target = 94;
  double len =
      fmax(0, aim(&slot->width, len_target, MOVE_MS, &BEZIER_MOVE, in, frame));
  double opacity = clamp_unit(aim(&slot->opacity, visible ? 1 : 0, FAN_FADE_MS,
                                  &BEZIER_EASE, in, frame));
  double states[AGENT_STATE_COUNT], sum = 0;
  for (int state = 0; state < AGENT_STATE_COUNT; state++) {
    double target = slot->session.state == (agent_state_t)state ? 1 : 0;
    states[state] = clamp_unit(
        aim(&slot->states[state], target, FADE_MS, &BEZIER_EASE, in, frame));
    sum += states[state];
  }
  double show = visible && (hovered || waiting) ? 1 : 0;
  double appear = aim(&slot->label, show, FAN_TAG_MS, &BEZIER_POP, in, frame);
  if (!slot->present && opacity <= 0) {
    slot->used = false;
    return;
  }
  if (opacity <= 0 || len <= 0.1)
    return;
  uint32_t fill = 0;
  for (int channel = 0; channel < 3; channel++) {
    double mixed = 0;
    for (int state = 0; state < AGENT_STATE_COUNT; state++)
      mixed += ((FILLS[state] >> (channel * 8)) & 255) * states[state];
    fill |= (uint32_t)lround(sum ? mixed / sum : 0) << (channel * 8);
  }
  bool popping = slot->session.state == AGENT_STATE_DONE &&
                 in->animations != SIGN_ANIM_OFF &&
                 in->now_ms < slot->session.state_since_ms + FAN_POP_MS;
  double plate;
  if (popping) {
    if (slot->hover.duration != FAN_POP_MS ||
        slot->hover.start != slot->session.state_since_ms) {
      slot->hover.from = .6;
      slot->hover.target = 1;
      slot->hover.start = slot->session.state_since_ms;
      slot->hover.duration = FAN_POP_MS;
      slot->hover.x1 = BEZIER_POP.x1;
      slot->hover.y1 = BEZIER_POP.y1;
      slot->hover.x2 = BEZIER_POP.x2;
      slot->hover.y2 = BEZIER_POP.y2;
    }
    plate = sample(&slot->hover, in->now_ms);
    if (in->now_ms < slot->hover.start + slot->hover.duration)
      frame->transitioning = true;
  } else {
    double target = pressed ? .94 : (hovered ? 1.16 : 1);
    plate = aim(&slot->hover, target, FAN_SCALE_MS, &BEZIER_POP, in, frame);
  }
  double angle = sample(&slot->bottom, in->now_ms);
  if (states[AGENT_STATE_WAITING] > .001 && in->animations == SIGN_ANIM_FULL) {
    double phase =
        fmod((double)(in->now_ms - slot->session.state_since_ms), 1500) / 1500;
    double local = phase < .5 ? phase * 2 : (1 - phase) * 2;
    angle +=
        (-4 + 8 * curve_at(local, &BEZIER_LOOP)) * states[AGENT_STATE_WAITING];
    wake_at(frame, in->now_ms + NUDGE_STEP);
  }
  if (slot->failure_ms && in->now_ms - slot->failure_ms < SHAKE_MS &&
      in->animations != SIGN_ANIM_OFF) {
    double t = (double)(in->now_ms - slot->failure_ms) / SHAKE_MS;
    double deg = atan(5 / fmax(len, 1)) * (180 / 3.141592653589793);
    angle += sin(t * 6 * 3.141592653589793) * deg * (1 - t);
    frame->transitioning = true;
  }
  bool codex = !strcmp(slot->session.agent, "codex");
  bool other = !codex && strcmp(slot->session.agent, "claude");
  int stick_at = frame->shape_count;
  double stick_h = (len - 14) * cat_scale;
  if (stick_h > 0.4)
    add_shape(frame, SIGN_RECT, pivot_x - 2.5 * cat_scale, pivot_y - stick_h,
              5 * cat_scale, stick_h, 2.5 * cat_scale, 1.5 * cat_scale,
              with_alpha(PAPER, opacity), with_alpha(INK, opacity));
  double half_w = (codex ? 15 : 17) * plate;
  double half_h = (codex ? 15 : 13.5) * plate;
  double center_sy = len - (codex ? 13 : 13.5);
  double top = pivot_y - (center_sy + half_h) * cat_scale;
  double left = pivot_x - half_w * cat_scale;
  add_shape(frame, other ? SIGN_CUT : SIGN_RECT, left, top,
            half_w * 2 * cat_scale, half_h * 2 * cat_scale,
            (codex ? 15 : 9) * plate * cat_scale, 2 * plate * cat_scale,
            with_alpha(fill, opacity), with_alpha(INK, opacity));
  if (slot->session.unread && slot->session.state == AGENT_STATE_DONE)
    add_unread(frame, left, top, half_w * 2 * cat_scale, plate * cat_scale,
               opacity);
  double icon_y = pivot_y - center_sy * cat_scale;
  for (int state = 0; state < AGENT_STATE_COUNT; state++)
    if (states[state] > .001)
      add_icon(frame, (agent_state_t)state, pivot_x, icon_y, cat_scale * plate,
               opacity * states[state], in);
  orbit_from(frame, stick_at, angle, pivot_x, pivot_y);
  if (appear > 0.01 && frame->text_count < SIGN_MAX_TEXTS) {
    double pin_x, pin_y;
    spin_point(pivot_x, pivot_y - (len - 13) * cat_scale, pivot_x, pivot_y,
               angle, &pin_x, &pin_y);
    double rise = (1 - appear) * 6 * cat_scale;
    double box_bottom = pin_y - 24 * cat_scale + rise;
    sign_text_t *text = &frame->texts[frame->text_count++];
    char fallback[9];
    const char *who = agent_adapter_display(slot->session.agent, fallback);
    *text = (sign_text_t){.x = pin_x,
                          .baseline_y = box_bottom - 13.2 * cat_scale,
                          .gap = 8 * cat_scale,
                          .px = 13 * cat_scale * font_ratio(in),
                          .meta_px = 11.5 * cat_scale * font_ratio(in),
                          .color = with_alpha(INK, opacity * appear),
                          .meta_color = with_alpha(
                              waiting ? 0x71430b : 0x4a5261, opacity * appear),
                          .above = true,
                          .tag_scale = .96 + .04 * appear,
                          .font_ratio = font_ratio(in),
                          .back = with_alpha(fill, opacity * appear)};
    snprintf(text->value, sizeof(text->value), "%s", slot->session.name);
    if (slot->session.state == AGENT_STATE_WORKING) {
      int64_t elapsed = in->now_ms - slot->session.state_since_ms;
      if (elapsed < 0)
        elapsed = 0;
      int64_t minutes = elapsed / 60000;
      if (minutes > 99999)
        minutes = 99999;
      snprintf(text->meta, sizeof(text->meta), "%s · %s %lld %s", who,
               WORDS[in->english ? 1 : 0].working, (long long)minutes,
               WORDS[in->english ? 1 : 0].minute);
      wake_at(frame, in->now_ms + 60000 - elapsed % 60000);
    } else {
      const char *label = WORDS[in->english ? 1 : 0].idle;
      if (waiting)
        label = WORDS[in->english ? 1 : 0].waiting;
      else if (slot->session.state == AGENT_STATE_DONE)
        label = done_label(in, true, slot->session.unread);
      snprintf(text->meta, sizeof(text->meta), "%s · %s", who, label);
    }
    include_bounds(frame, pin_x - 220 * cat_scale, pin_y - 70 * cat_scale,
                   440 * cat_scale, 80 * cat_scale);
  }
  if (visible && len > 1 && opacity > 0 &&
      frame->hit_count < SIGN_MAX_VISIBLE) {
    // Only the plate is a target. A box around the whole tilted sign would
    // cover its neighbours' plates and steal their hover.
    double plate_y = pivot_y - center_sy * cat_scale;
    double reach_x = (half_w + 2) * cat_scale;
    double reach_y = (half_h + 2) * cat_scale;
    double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    double corners[4][2] = {
        {pivot_x - reach_x, plate_y - reach_y},
        {pivot_x + reach_x, plate_y - reach_y},
        {pivot_x - reach_x, plate_y + reach_y},
        {pivot_x + reach_x, plate_y + reach_y}
    };
    spin_point(pivot_x, plate_y, pivot_x, pivot_y, angle, &min_x, &min_y);
    max_x = min_x;
    max_y = min_y;
    for (int corner = 0; corner < 4; corner++) {
      double rx, ry;
      spin_point(corners[corner][0], corners[corner][1], pivot_x, pivot_y,
                 angle, &rx, &ry);
      if (rx < min_x)
        min_x = rx;
      if (ry < min_y)
        min_y = ry;
      if (rx > max_x)
        max_x = rx;
      if (ry > max_y)
        max_y = ry;
    }
    frame->hits[frame->hit_count++] =
        (sign_hit_t){(int)floor(min_x),
                     (int)floor(min_y),
                     (int)ceil(max_x) - (int)floor(min_x),
                     (int)ceil(max_y) - (int)floor(min_y),
                     slot->session.key,
                     slot->session.pid};
  }
}
static void layout_fan(signs_t *model, const sign_input_t *in,
                       sign_frame_t *frame, size_t count) {
  int shown = 0;
  for (size_t i = 0; i < count; i++)
    if (show_session(in, in->sessions[i].state))
      shown++;
  double spread = in->open ? 22 : 15;
  double cat_scale = in->cat_height / 110.0;
  double pivot_x = in->cat_x + 108 * cat_scale;
  double pivot_y = in->cat_y + 72 * cat_scale;
  int cursor = 0;
  for (size_t i = 0; i < count; i++) {
    const agent_session_view_t *session = &in->sessions[i];
    bool visible = show_session(in, session->state);
    double offset = 0;
    if (visible)
      offset = cursor++ - (shown - 1) / 2.0;
    bool existed = false;
    for (int s = 0; s < AGENT_SESSIONS_MAX; s++)
      if (model->slots[s].used && model->slots[s].session.key == session->key)
        existed = true;
    sign_slot_t *slot = claim_slot(model, session, 0);
    if (!slot)
      continue;
    if (!existed)
      slot->bottom.from = slot->bottom.target = offset * spread;
    slot->present = true;
    slot->session = *session;
    aim(&slot->bottom, offset * spread, FAN_ANGLE_MS, &BEZIER_MOVE, in, frame);
  }
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
      sign_slot_t *slot = &model->slots[i];
      if (!slot->used)
        continue;
      bool visible = slot->present && (show_session(in, slot->session.state));
      bool hovered =
          visible && in->has_hover && in->hover_key == slot->session.key;
      if (hovered != (pass == 1))
        continue;
      emit_fan(slot, in, frame, pivot_x, pivot_y, cat_scale);
    }
  }
  frame->pad = cover(in->cat_x - 44 * cat_scale, in->cat_y - 84 * cat_scale,
                     288 * cat_scale, 194 * cat_scale);
  frame->has_pad = true;
}
int sign_clearance(sign_style_t style, int cat_height) {
  int design = 0;
  if (style == SIGN_STYLE_POST)
    design = POST_CLEARANCE;
  else if (style == SIGN_STYLE_FAN)
    design = FAN_CLEARANCE;
  if (design <= 0 || cat_height <= 0)
    return 0;
  return (cat_height * design + 109) / 110;
}
void signs_focus_failed(signs_t *model, uint64_t key, int64_t now) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    if (model->slots[i].used && model->slots[i].session.key == key)
      model->slots[i].failure_ms = now;
}
static void emit_desk(sign_frame_t *frame, const sign_input_t *in, double scale,
                      double lift, double fade) {
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
  double y = in->cat_y + lround(lift) +
             (DESK_TOP + (1 - progress) * DESK_SLIDE) * scale;
  double w = 164 * scale;
  double h = 26 * scale;
  add_shape(frame, SIGN_RECT, x, y, w, h, 8 * scale, 2 * scale,
            with_alpha(PAPER, fade), with_alpha(INK, fade));
  if (frame->text_count >= SIGN_MAX_TEXTS)
    return;
  sign_text_t *text = &frame->texts[frame->text_count++];
  *text = (sign_text_t){.x = x + 12 * scale,
                        .baseline_y =
                            y + (18.5 + 4.5 * (font_ratio(in) - 1)) * scale,
                        .w = 138 * scale,
                        .clip_y = y + 2 * scale,
                        .clip_h = 22 * scale,
                        .px = 12 * scale * font_ratio(in),
                        .gap = 6 * scale,
                        .color = with_alpha(INK, fade),
                        .caret = true};
  int64_t phase = in->now_ms % 1000;
  if (phase < 0)
    phase += 1000;
  bool blink = in->animations != SIGN_ANIM_FULL || phase < 500;
  text->meta_color = with_alpha(INK, blink ? fade : 0);
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
void signs_frame(signs_t *model, const sign_input_t *in, sign_frame_t *frame) {
  memset(frame, 0, sizeof(*frame));
  if (!in || in->style == SIGN_STYLE_OFF || in->cat_height <= 0) {
    memset(model, 0, sizeof(*model));
    return;
  }
  if (!model->initialized) {
    model->initialized = true;
    model->pole.from = model->pole.target = 50;
  }
  double scale = in->cat_height / 110.0;
  int move_ms = in->desk_snap ? 0 : DESK_MOVE_MS;
  int fade_ms = in->desk_snap ? 0 : DESK_FADE_MS;
  double lift = aim(&model->desk_lift, in->typing ? DESK_LIFT * scale : 0,
                    move_ms, &BEZIER_MOVE, in, frame);
  double fade = clamp_unit(aim(&model->desk_fade, in->typing ? 1 : 0, fade_ms,
                               &BEZIER_EASE, in, frame));
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
    layout_fan(model, in, frame, count);
  } else {
    int shown = 0;
    for (size_t i = 0; i < count; i++)
      if (show_session(in, in->sessions[i].state))
        shown++;
    int row = shown;
    for (size_t i = 0; i < count; i++) {
      sign_slot_t *slot = claim_slot(model, &in->sessions[i], 100);
      if (!slot)
        continue;
      slot->present = true;
      slot->session = in->sessions[i];
      bool visible = show_session(in, slot->session.state);
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
    // Outer width 6, centered on the same axis. The cap sits 6px above the
    // pole top: its padding edge is 2px in, and CSS top is -8.
    add_shape(frame, SIGN_RECT, pole_x - 3 * scale, cat_bottom - pole,
              6 * scale, pole, 3 * scale, 2 * scale, PAPER, INK);
    add_shape(frame, SIGN_RECT, pole_x - 5 * scale,
              cat_bottom - pole - 6 * scale, 10 * scale, 10 * scale, 5 * scale,
              2 * scale, PAPER, INK);
    for (int pass = 0; pass < 2; pass++) {
      for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
        sign_slot_t *slot = &model->slots[i];
        if (!slot->used)
          continue;
        bool hovered = in->has_hover && in->hover_key == slot->session.key;
        if (hovered != (pass == 1))
          continue;
        layout_board(slot, in, frame, pole_x, cat_bottom, scale);
      }
    }
    frame->pad = cover(in->cat_x - 70 * scale, in->cat_y - 160 * scale,
                       440 * scale, 270 * scale);
    frame->has_pad = true;
  }
  // The desk stays on the unshifted cat. The pivot above already moved.
  emit_desk(frame, &placed, scale, lift, fade);
  finish_motion(frame, &placed, fade);
}
