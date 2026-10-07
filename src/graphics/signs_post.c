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
  if (agent_sign_waits_on_children(&slot->session)) {
    char state[64];
    subagent_state_label(state, &slot->session, in, NULL);
    snprintf(text->meta, sizeof(text->meta), "%.20s · %.38s", who, state);
  } else if (agent_sign_state(&slot->session) == AGENT_STATE_WORKING) {
    int64_t elapsed = in->now_ms - agent_sign_since(&slot->session);
    if (elapsed < 0)
      elapsed = 0;
    int64_t minutes = elapsed / 60000;
    if (minutes > 99999)
      minutes = 99999;
    snprintf(text->meta, sizeof(text->meta), "%.20s · %lld %s", who,
             (long long)minutes, WORDS[in->english ? 1 : 0].minute);
  } else {
    const char *label = WORDS[in->english ? 1 : 0].idle;
    if (agent_sign_state(&slot->session) == AGENT_STATE_WAITING)
      label = WORDS[in->english ? 1 : 0].waiting;
    else if (finished(agent_sign_state(&slot->session)))
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
  bool visible = slot->present &&
                 (show_session(in, agent_sign_state(&slot->session))) &&
                 !(in->typing && in->typing_key == slot->session.key);
  // Waiting and error say what they are without being hovered.
  bool expanded = in->open ||
                  agent_sign_state(&slot->session) == AGENT_STATE_WAITING ||
                  agent_sign_state(&slot->session) == AGENT_STATE_ERROR;
  sign_text_t label = {0};
  if (visible || slot->opacity.target > 0)
    board_label(slot, in, &label);
  double ratio = font_ratio(in);
  sign_plate_geometry_t geometry = state_plate_geometry(slot->session.agent);
  double plate_w = geometry.w;
  double name_px = 13 * scale * ratio, meta_px = 11.5 * scale * ratio;
  double fixed = plate_w + POST_NAME_GAP + 16;
  double content = fixed + 7 +
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
  // Only the neutral name pill retracts. The state plate keeps its fan size;
  // on an exceptionally narrow canvas it scales to the remaining space.
  double plate_scale = fmin(1, available / plate_w);
  plate_w *= plate_scale;
  double name_limit = fmax(0, available - plate_w - POST_NAME_GAP);
  double name_target = fmax(0, board_target - plate_w - POST_NAME_GAP);
  double name_w =
      fmax(0, aim(&slot->width, visible && expanded ? name_target : 0, WIDTH_MS,
                  &BEZIER_WIDTH, in, frame));
  name_w = fmin(name_w, name_limit);
  double appear = clamp_unit(aim(&slot->label, visible && expanded ? 1 : 0,
                                 FADE_MS, &BEZIER_EASE, in, frame));
  double width = plate_w + (name_w > .1 ? POST_NAME_GAP + name_w : 0);
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
    double target =
        agent_sign_state(&slot->session) == (agent_state_t)state ? 1 : 0;
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
  double cy = cat_bottom -
              (sample(&slot->bottom, in->now_ms) + desk_clear + 13) * scale;
  double plate_h = geometry.h * plate_scale * scale;
  double y = cy - plate_h / 2;
  int board_at = frame->shape_count;
  double plate_x = direction > 0 ? x : x + (width - plate_w) * scale;
  sign_shape_t face = {.kind = geometry.kind,
                       .x = plate_x,
                       .y = y,
                       .w = plate_w * scale,
                       .h = plate_h,
                       .radius = geometry.radius * plate_scale * scale,
                       .stroke = 2 * plate_scale * scale,
                       .fill = with_alpha(fill, opacity),
                       .outline = with_alpha(palette->ink, opacity)};
  emit_state_plate(frame, &slot->session, in, &face,
                   plate_x + plate_w * scale / 2, cy, plate_scale * scale,
                   opacity, states);
  double name_x =
      direction > 0 ? plate_x + (plate_w + POST_NAME_GAP) * scale : x;
  double name_y = cy - POST_NAME_HEIGHT * scale / 2;
  double name_opacity = opacity * appear;
  if (name_w > .1)
    add_shape(frame, SIGN_RECT, name_x, name_y, name_w * scale,
              POST_NAME_HEIGHT * scale, POST_NAME_HEIGHT * scale / 2, 2 * scale,
              with_alpha(palette->paper, name_opacity),
              with_alpha(palette->ink, name_opacity));
  snap_from(frame, board_at, nudging);
  if (name_w > 16 && name_opacity > 0 && frame->text_count < SIGN_MAX_TEXTS) {
    sign_text_t *text = &frame->texts[frame->text_count++];
    double line_h = 13 * scale * ratio;
    *text = (sign_text_t){
        .x = name_x + 8 * scale,
        .pixel_snap = nudging,
        .line_top = name_y + 2 * scale + (22 * scale - line_h) / 2,
        .line_h = line_h,
        .w = (name_w - 16) * scale,
        .clip_y = name_y + 2 * scale,
        .clip_h = 22 * scale,
        .px = line_h,
        .meta_px = 11.5 * scale * ratio,
        .gap = 7 * scale,
        .color = with_alpha(palette->ink, name_opacity),
        .secondary_color = with_alpha(palette->secondary, name_opacity),
        .meta_color = with_alpha(palette->secondary, name_opacity),
        .reverse = direction < 0};
    if (agent_sign_state(&slot->session) == AGENT_STATE_WORKING) {
      int64_t elapsed = in->now_ms - agent_sign_since(&slot->session);
      if (elapsed < 0)
        elapsed = 0;
      wake_at(frame, in->now_ms + 60000 - elapsed % 60000);
    }
    memcpy(text->value, label.value, sizeof(text->value));
    memcpy(text->extra, label.extra, sizeof(text->extra));
    memcpy(text->meta, label.meta, sizeof(text->meta));
  }
  if (visible && frame->hit_count < SIGN_MAX_VISIBLE) {
    // The board slides away from the pole when hovered, nudged or shaken.
    // Its target also covers where it rests, or a pointer on the pole-side
    // edge would lose the board it just hovered and make it slide back.
    double rest =
        pole_x + direction * 5 * scale - (direction < 0 ? width * scale : 0);
    if (in->surface_width > 0)
      rest = fmax(0, fmin(rest, in->surface_width - width * scale));
    double left = fmin(x, rest), right = fmax(x, rest) + width * scale;
    frame->hits[frame->hit_count++] = (sign_hit_t){
        .x = (int)floor(left),
        .y = (int)floor(fmin(y, name_y)),
        .w = (int)ceil(right) - (int)floor(left),
        .h = (int)ceil(fmax(y + plate_h, name_y + POST_NAME_HEIGHT * scale)) -
             (int)floor(fmin(y, name_y)),
        .key = slot->session.key,
        .pid = slot->session.pid};
  }
}
