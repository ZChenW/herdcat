#include "config/sign_options.h"
#include "core/agent_adapters.h"
#include "graphics/sign_palette.h"
#include "graphics/signs.h"
#include "signs_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
    if (shape->upright) {
      double rx, ry;
      spin_point(shape->icon_center_x, shape->icon_center_y, ox, oy, deg, &rx,
                 &ry);
      shape->icon_center_x = rx;
      shape->icon_center_y = ry;
    }
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

static double rod_length(agent_state_t state, bool visible, bool hovered) {
  bool urgent = state == AGENT_STATE_WAITING || state == AGENT_STATE_ERROR;
  if (!visible)
    return 34;
  if (urgent && hovered)
    return 116;
  if (hovered)
    return 102;
  return urgent ? 110 : 94;
}

static void emit_fan(sign_slot_t *slot, const sign_input_t *in,
                     sign_frame_t *frame, double pivot_x, double pivot_y,
                     double cat_scale, bool named, double desk_clear,
                     bool two_rows, double back_length) {
  const sign_palette_t *palette = sign_palette(in->theme);
  bool visible = slot->present && (show_session(in, slot->session.state)) &&
                 !(in->typing && in->typing_key == slot->session.key);
  bool hovered = visible && in->has_hover && in->hover_key == slot->session.key;
  bool pressed = in->has_pressed && in->pressed_key == slot->session.key;
  double len_target = rod_length(slot->session.state, visible, hovered);
  if (visible && slot->back_row)
    len_target += back_length;
  double len =
      fmax(0, aim(&slot->width, len_target, MOVE_MS, &BEZIER_MOVE, in, frame));
  double row_target = slot->back_row ? FAN_BACK_SCALE : 1;
  double row_size = row_target;
  if (slot->row_size.target != row_target ||
      in->now_ms < slot->row_size.start + slot->row_size.duration)
    row_size =
        aim(&slot->row_size, row_target, MOVE_MS, &BEZIER_EASE, in, frame);
  len += desk_clear;
  double opacity = clamp_unit(aim(&slot->opacity, visible ? 1 : 0, FAN_FADE_MS,
                                  &BEZIER_EASE, in, frame));
  if (slot->session.terminal.kind == TERMINAL_TMUX &&
      slot->session.terminal.detached)
    opacity *= .55;
  double states[AGENT_STATE_COUNT], sum = 0;
  for (int state = 0; state < AGENT_STATE_COUNT; state++) {
    double target = slot->session.state == (agent_state_t)state ? 1 : 0;
    states[state] = clamp_unit(
        aim(&slot->states[state], target, FADE_MS, &BEZIER_EASE, in, frame));
    sum += states[state];
  }
  double show = visible && named ? 1 : 0;
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
      mixed += ((palette->fills[state] >> (channel * 8)) & 255) * states[state];
    fill |= (uint32_t)lround(sum ? mixed / sum : 0) << (channel * 8);
  }
  bool popping = finished(slot->session.state) &&
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
  plate *= row_size;
  double angle = sample(&slot->bottom, in->now_ms);
  bool nudging =
      states[AGENT_STATE_WAITING] > .001 && in->animations == SIGN_ANIM_FULL;
  if (nudging)
    angle +=
        (-4 + 8 * nudge_phase(in, slot, frame)) * states[AGENT_STATE_WAITING];
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
    add_shape(frame, SIGN_RECT, pivot_x - 2.5 * cat_scale * row_size,
              pivot_y - stick_h, 5 * cat_scale * row_size, stick_h,
              2.5 * cat_scale * row_size, 1.5 * cat_scale * row_size,
              with_alpha(palette->paper, opacity),
              with_alpha(palette->ink, opacity));
  double half_w = (codex ? 15 : 17) * plate;
  double half_h = (codex ? 15 : 13.5) * plate;
  double center_sy = len - (codex ? 13 : 13.5);
  double top = pivot_y - (center_sy + half_h) * cat_scale;
  double left = pivot_x - half_w * cat_scale;
  add_shape(frame, other ? SIGN_CUT : SIGN_RECT, left, top,
            half_w * 2 * cat_scale, half_h * 2 * cat_scale,
            (codex ? 15 : 9) * plate * cat_scale, 2 * plate * cat_scale,
            with_alpha(fill, opacity), with_alpha(palette->ink, opacity));
  if (slot->session.unread && finished(slot->session.state))
    add_unread(frame, slot->session.state, left, top, half_w * 2 * cat_scale,
               plate * cat_scale, opacity, in);
  double icon_y = pivot_y - center_sy * cat_scale;
  for (int state = 0; state < AGENT_STATE_COUNT; state++)
    if (states[state] > .001)
      add_icon(frame, (agent_state_t)state, pivot_x, icon_y, cat_scale * plate,
               opacity * states[state], in);
  orbit_from(frame, stick_at, angle, pivot_x, pivot_y);
  snap_from(frame, stick_at, nudging);
  if (appear > 0.01 && frame->text_count < SIGN_MAX_TEXTS) {
    double pin_x, pin_y;
    spin_point(pivot_x, pivot_y - (len - 13) * cat_scale, pivot_x, pivot_y,
               angle, &pin_x, &pin_y);
    double rise = (1 - appear) * 6 * cat_scale;
    double box_bottom = pin_y - 24 * cat_scale + rise;
    sign_text_t *text = &frame->texts[frame->text_count++];
    *text = (sign_text_t){
        .x = pin_x,
        .pixel_snap = nudging,
        .anchor_y = box_bottom,
        .gap = 8 * cat_scale,
        .px = 13 * cat_scale * font_ratio(in),
        .meta_px = 11.5 * cat_scale * font_ratio(in),
        .color = with_alpha(palette->ink, opacity * appear),
        .meta_color =
            with_alpha(meta_color(slot->session.state, in), opacity * appear),
        .secondary_color = with_alpha(palette->secondary, opacity * appear),
        .above = true,
        .tag_scale = .96 + .04 * appear,
        .font_ratio = font_ratio(in),
        .back = with_alpha(fill, opacity * appear)};
    sign_nameplate(text, in, &slot->session, frame);
    include_bounds(frame, pin_x - 220 * cat_scale, pin_y - 70 * cat_scale,
                   440 * cat_scale, 80 * cat_scale);
  }
  if (visible && len > 1 && opacity > 0 &&
      frame->hit_count < SIGN_MAX_VISIBLE) {
    // Only the plate is a target. A box around the whole tilted sign would
    // cover its neighbours' plates and steal their hover.
    double plate_y = pivot_y - center_sy * cat_scale;
    double pad = two_rows ? 0 : 2;
    double reach_x = (half_w + pad) * cat_scale;
    double reach_y = (half_h + pad) * cat_scale;
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
        (sign_hit_t){.x = (int)floor(min_x),
                     .y = (int)floor(min_y),
                     .w = (int)ceil(max_x) - (int)floor(min_x),
                     .h = (int)ceil(max_y) - (int)floor(min_y),
                     .key = slot->session.key,
                     .pid = slot->session.pid,
                     .back_row = slot->back_row,
                     .precise = two_rows};
    if (two_rows) {
      sign_hit_t *hit = &frame->hits[frame->hit_count - 1];
      hit->half_w = half_w * cat_scale;
      hit->half_h = half_h * cat_scale;
      hit->rotation = angle;
      hit->kind = other ? SIGN_CUT : SIGN_RECT;
      hit->radius = (codex ? 15 : 9) * plate * cat_scale;
      spin_point(pivot_x, plate_y, pivot_x, pivot_y, angle, &hit->center_x,
                 &hit->center_y);
    }
  }
}

static int front_priority(const agent_session_view_t *session) {
  switch (session->state) {
  case AGENT_STATE_WAITING:
    return 0;
  case AGENT_STATE_ERROR:
    return 1;
  case AGENT_STATE_DONE:
    return session->unread ? 2 : 4;
  case AGENT_STATE_WORKING:
    return 3;
  default:
    return 4;
  }
}

void layout_fan(signs_t *model, const sign_input_t *in, sign_frame_t *frame,
                size_t count, double desk_clear) {
  int shown = 0;
  for (size_t i = 0; i < count; i++)
    if (show_session(in, in->sessions[i].state))
      shown++;
  double spread = in->open ? 22 : 15;
  double cat_scale = in->cat_height / 110.0;
  double pivot_x = in->cat_x + 108 * cat_scale;
  double pivot_y = in->cat_y + 72 * cat_scale;
  bool front[SIGN_MAX_VISIBLE] = {0};
  bool two_rows = shown > FAN_FRONT_COUNT;
  if (two_rows) {
    for (int n = 0; n < FAN_FRONT_COUNT; n++) {
      size_t best = count;
      for (size_t i = 0; i < count; i++) {
        if (front[i] || !show_session(in, in->sessions[i].state))
          continue;
        if (best == count || front_priority(&in->sessions[i]) <
                                 front_priority(&in->sessions[best]))
          best = i;
      }
      front[best] = true;
    }
  }
  double back_length = 0;
  if (two_rows) {
    double front_growth = 0;
    for (size_t i = 0; i < count; i++) {
      if (!front[i])
        continue;
      const agent_session_view_t *s = &in->sessions[i];
      bool hovered = in->has_hover && in->hover_key == s->key;
      bool visible = !(in->typing && in->typing_key == s->key);
      front_growth =
          fmax(front_growth, rod_length(s->state, visible, hovered) - 94);
    }
    back_length = FAN_BACK_LENGTH + front_growth +
                  (shown == SIGN_MAX_VISIBLE ? FAN_BACK_WIDE : 0);
  }
  // Match the front row's adjacent centre distance on the larger radius.
  double back_spread = two_rows
                           ? 2 *
                                 asin((94 - 13.5) / (94 + back_length - 13.5) *
                                      sin(spread * 3.141592653589793 / 360)) *
                                 180 / 3.141592653589793
                           : 0;
  int cursors[2] = {0};
  for (size_t i = 0; i < count; i++) {
    const agent_session_view_t *session = &in->sessions[i];
    bool visible = show_session(in, session->state);
    bool back_row = two_rows && visible && !front[i];
    int row = back_row ? 1 : 0;
    int row_count = two_rows
                        ? (back_row ? shown - FAN_FRONT_COUNT : FAN_FRONT_COUNT)
                        : shown;
    double offset = 0;
    if (visible)
      offset = cursors[row]++ - (row_count - 1) / 2.0;
    double angle = offset * (back_row ? back_spread : spread);
    bool existed = false;
    for (int s = 0; s < AGENT_SESSIONS_MAX; s++)
      if (model->slots[s].used && model->slots[s].session.key == session->key)
        existed = true;
    sign_slot_t *slot = claim_slot(model, session, 0);
    if (!slot)
      continue;
    if (!existed) {
      slot->bottom.from = slot->bottom.target = angle;
      slot->row_size.from = slot->row_size.target =
          back_row ? FAN_BACK_SCALE : 1;
    }
    // Retain a retracting row's depth until it disappears into the menu.
    if (visible || !in->menu)
      slot->back_row = back_row;
    slot->present = true;
    slot->session = *session;
    aim(&slot->bottom, angle, FAN_ANGLE_MS, &BEZIER_MOVE, in, frame);
  }
  // Nameplates are wider than the gap between two signs, so only one is up
  // at a time: the hovered sign, otherwise whoever has waited longest. The
  // next one in line is named as soon as that one is answered.
  const sign_slot_t *named = NULL;
  bool has_back = two_rows;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    const sign_slot_t *slot = &model->slots[i];
    if (!slot->used)
      continue;
    has_back |= slot->back_row;
    if (!slot->present || !show_session(in, slot->session.state) ||
        (in->typing && in->typing_key == slot->session.key))
      continue;
    if (in->has_hover && in->hover_key == slot->session.key) {
      named = slot;
      continue;
    }
    if (named && in->has_hover && named->session.key == in->hover_key)
      continue;
    if (slot->session.state != AGENT_STATE_WAITING)
      continue;
    if (!named ||
        slot->session.state_since_ms < named->session.state_since_ms ||
        (slot->session.state_since_ms == named->session.state_since_ms &&
         slot->session.order < named->session.order))
      named = slot;
  }
  // Emit complete back groups first, including hovered/pressed/shaking ink.
  // Text nameplates use SIGN_DRAW_OVER and remain above both rows and the cat.
  for (int pass = has_back ? 0 : 2; pass < 4; pass++) {
    for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
      sign_slot_t *slot = &model->slots[i];
      if (!slot->used)
        continue;
      bool visible = slot->present && (show_session(in, slot->session.state));
      bool hovered =
          visible && in->has_hover && in->hover_key == slot->session.key;
      if (slot->back_row != (pass < 2) || hovered != (pass % 2 == 1))
        continue;
      emit_fan(slot, in, frame, pivot_x, pivot_y, cat_scale, slot == named,
               desk_clear, two_rows, back_length);
    }
  }
  double extra = back_length;
  frame->pad =
      cover(in->cat_x - 44 * cat_scale, in->cat_y - (84 + extra) * cat_scale,
            288 * cat_scale, (194 + extra) * cat_scale);
  frame->has_pad = true;
}
