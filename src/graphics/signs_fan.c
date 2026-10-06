#include "config/sign_options.h"
#include "core/agent_adapters.h"
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
                     double cat_scale, bool named) {
  bool visible = slot->present && (show_session(in, slot->session.state)) &&
                 !(in->typing && in->typing_key == slot->session.key);
  bool hovered = visible && in->has_hover && in->hover_key == slot->session.key;
  bool pressed = in->has_pressed && in->pressed_key == slot->session.key;
  bool waiting = slot->session.state == AGENT_STATE_WAITING;
  // Both are raised higher. Only waiting sways and is named without hover:
  // two nameplates side by side would cover each other.
  bool urgent = waiting || slot->session.state == AGENT_STATE_ERROR;
  double len_target = 34;
  if (visible && urgent && hovered)
    len_target = 116;
  else if (visible && hovered)
    len_target = 102;
  else if (visible && urgent)
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
      mixed += ((FILLS[state] >> (channel * 8)) & 255) * states[state];
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
  if (slot->session.unread && finished(slot->session.state))
    add_unread(frame, slot->session.state, left, top, half_w * 2 * cat_scale,
               plate * cat_scale, opacity);
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
    char fallback[9];
    const char *who = agent_adapter_display(slot->session.agent, fallback);
    *text =
        (sign_text_t){.x = pin_x,
                      .pixel_snap = nudging,
                      .anchor_y = box_bottom,
                      .gap = 8 * cat_scale,
                      .px = 13 * cat_scale * font_ratio(in),
                      .meta_px = 11.5 * cat_scale * font_ratio(in),
                      .color = with_alpha(INK, opacity * appear),
                      .meta_color = with_alpha(meta_color(slot->session.state),
                                               opacity * appear),
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
      else if (finished(slot->session.state))
        label = done_label(in, true, &slot->session);
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

void layout_fan(signs_t *model, const sign_input_t *in, sign_frame_t *frame,
                size_t count) {
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
  // Nameplates are wider than the gap between two signs, so only one is up
  // at a time: the hovered sign, otherwise whoever has waited longest. The
  // next one in line is named as soon as that one is answered.
  const sign_slot_t *named = NULL;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    const sign_slot_t *slot = &model->slots[i];
    if (!slot->used || !slot->present ||
        !show_session(in, slot->session.state) ||
        (in->typing && in->typing_key == slot->session.key))
      continue;
    if (in->has_hover && in->hover_key == slot->session.key) {
      named = slot;
      break;
    }
    if (slot->session.state != AGENT_STATE_WAITING)
      continue;
    if (!named ||
        slot->session.state_since_ms < named->session.state_since_ms ||
        (slot->session.state_since_ms == named->session.state_since_ms &&
         slot->session.order < named->session.order))
      named = slot;
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
      emit_fan(slot, in, frame, pivot_x, pivot_y, cat_scale, slot == named);
    }
  }
  frame->pad = cover(in->cat_x - 44 * cat_scale, in->cat_y - 84 * cat_scale,
                     288 * cat_scale, 194 * cat_scale);
  frame->has_pad = true;
}
