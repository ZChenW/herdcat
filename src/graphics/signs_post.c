#include "config/sign_options.h"
#include "core/agent_adapters.h"
#include "graphics/sign_names.h"
#include "graphics/sign_palette.h"
#include "graphics/signs.h"
#include "graphics/text.h"
#include "signs_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void board_label(const sign_slot_t *slot, const sign_input_t *in,
                        sign_text_t *text) {
  bool title_main;
  sign_session_name(in, &slot->session, text->value, &title_main);
  if (in->name_extra != SIGN_EXTRA_OFF) {
    if (title_main)
      snprintf(text->extra, sizeof(text->extra), "%s", slot->session.name);
    else
      sign_title_truncate(slot->session.title, in->title_length, text->extra);
    if (!strcmp(text->value, text->extra))
      text->extra[0] = 0;
  }
  char who[64];
  sign_agent_label(&slot->session, true, who);
  if (slot->session.state == AGENT_STATE_WORKING) {
    int64_t elapsed = in->now_ms - slot->session.state_since_ms;
    if (elapsed < 0)
      elapsed = 0;
    int64_t minutes = elapsed / 60000;
    if (minutes > 99999)
      minutes = 99999;
    snprintf(text->meta, sizeof(text->meta), "%.20s · %lld %s", who,
             (long long)minutes, WORDS[in->english ? 1 : 0].minute);
  } else {
    const char *label = WORDS[in->english ? 1 : 0].idle;
    if (slot->session.state == AGENT_STATE_WAITING)
      label = WORDS[in->english ? 1 : 0].waiting;
    else if (finished(slot->session.state))
      label = done_label(in, false, &slot->session);
    snprintf(text->meta, sizeof(text->meta), "%.20s · %.36s", who, label);
  }
  if (slot->session.terminal.kind == TERMINAL_TMUX &&
      slot->session.terminal.detached) {
    size_t used = strlen(text->meta);
    snprintf(text->meta + used, sizeof(text->meta) - used, " · %s",
             in->english ? "Detached" : "已断开");
  }
}

void layout_board(sign_slot_t *slot, const sign_input_t *in,
                  sign_frame_t *frame, double pole_x, double cat_bottom,
                  double scale, double desk_clear) {
  const sign_palette_t *palette = sign_palette(in->theme);
  bool visible = slot->present && (show_session(in, slot->session.state)) &&
                 !(in->typing && in->typing_key == slot->session.key);
  // Waiting and error say what they are without being hovered.
  bool expanded = in->open || slot->session.state == AGENT_STATE_WAITING ||
                  slot->session.state == AGENT_STATE_ERROR;
  sign_text_t label = {0};
  if (visible || slot->opacity.target > 0)
    board_label(slot, in, &label);
  double ratio = font_ratio(in);
  double name_px = 13 * scale * ratio, meta_px = 11.5 * scale * ratio;
  double content = 41 + 7 +
                   text_measure(label.value, (float)name_px, true) / scale +
                   text_measure(label.meta, (float)meta_px, false) / scale;
  if (label.extra[0])
    content += 7 + text_measure(label.extra, (float)meta_px, false) / scale;
  double board_target = fmax(POST_BOARD_MIN, fmin(POST_BOARD_MAX, content));
  // Reserve the furthest hover, waiting nudge and failure shake before
  // choosing a width. Clamp the animated width as well when the cat moves
  // towards an edge; a previous wider target must not spill during retreat.
  double available = POST_BOARD_MAX;
  if (in->surface_width > 0)
    available = fmax(
        0, (slot->direction > 0 ? in->surface_width - pole_x : pole_x) / scale -
               17);
  board_target = fmin(board_target, available);
  double width =
      fmax(0, aim(&slot->width,
                  visible ? (expanded ? board_target : fmin(34, available)) : 0,
                  WIDTH_MS, &BEZIER_WIDTH, in, frame));
  width = fmin(width, available);
  bool pressed = in->has_pressed && in->pressed_key == slot->session.key;
  double opacity_target = visible ? (pressed ? .85 : 1) : 0;
  double opacity = clamp_unit(
      aim(&slot->opacity, opacity_target, FADE_MS, &BEZIER_EASE, in, frame));
  bool hovered = visible && in->has_hover && in->hover_key == slot->session.key;
  double hover =
      aim(&slot->hover, hovered ? 5 : 0, HOVER_MS, &BEZIER_POP, in, frame);
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
      mixed += ((palette->fills[state] >> (channel * 8)) & 255) * states[state];
    fill |= (uint32_t)lround(sum ? mixed / sum : 0) << (channel * 8);
  }
  double offset = hover;
  bool nudging =
      states[AGENT_STATE_WAITING] > .001 && in->animations == SIGN_ANIM_FULL;
  if (nudging) {
    double nudge = 6 * nudge_phase(in, slot, frame);
    offset = (1 - states[AGENT_STATE_WAITING]) * hover +
             states[AGENT_STATE_WAITING] * nudge;
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
  if (in->surface_width > 0)
    x = fmax(0, fmin(x, in->surface_width - width * scale));
  double y = cat_bottom -
             (sample(&slot->bottom, in->now_ms) + desk_clear + 26) * scale;
  bool other = strcmp(slot->session.agent, "claude") &&
               strcmp(slot->session.agent, "codex");
  double radius = other ? 9 : !strcmp(slot->session.agent, "codex") ? 13 : 8;
  int board_at = frame->shape_count;
  add_shape(frame, other ? SIGN_CUT : SIGN_RECT, x, y, width * scale,
            26 * scale, radius * scale, 2 * scale, with_alpha(fill, opacity),
            with_alpha(palette->ink, opacity));
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
  if (slot->session.unread && finished(slot->session.state)) {
    add_unread(frame, slot->session.state, x, y, width * scale, scale, opacity,
               in);
  }
  snap_from(frame, board_at, nudging);
  if (width > 41 && frame->text_count < SIGN_MAX_TEXTS) {
    sign_text_t *text = &frame->texts[frame->text_count++];
    uint32_t meta = meta_color(slot->session.state, in);
    double line_h = 13 * scale * ratio;
    *text = (sign_text_t){.x = x + (direction > 0 ? 33 : 8) * scale,
                          .line_top = y + 2 * scale + (22 * scale - line_h) / 2,
                          .line_h = line_h,
                          .w = (width - 41) * scale,
                          .clip_y = y + 2 * scale,
                          .clip_h = 22 * scale,
                          .px = line_h,
                          .meta_px = 11.5 * scale * ratio,
                          .gap = 7 * scale,
                          .color = with_alpha(palette->ink, opacity),
                          .secondary_color =
                              with_alpha(palette->secondary, opacity),
                          .meta_color = with_alpha(meta, opacity),
                          .reverse = direction < 0};
    if (slot->session.state == AGENT_STATE_WORKING) {
      int64_t elapsed = in->now_ms - slot->session.state_since_ms;
      if (elapsed < 0)
        elapsed = 0;
      wake_at(frame, in->now_ms + 60000 - elapsed % 60000);
    }
    memcpy(text->value, label.value, sizeof(text->value));
    memcpy(text->extra, label.extra, sizeof(text->extra));
    memcpy(text->meta, label.meta, sizeof(text->meta));
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
