#define _POSIX_C_SOURCE 200809L
#include "platform/overlay_signs.h"

#include "config/config.h"
#include "config/sign_options.h"
#include "core/agent_sessions.h"
#include "core/agent_sign_state.h"
#include "core/agent_state.h"
#include "core/agent_title.h"
#include "core/herdcat.h"
#include "graphics/sign_names.h"
#include "graphics/sign_palette.h"
#include "graphics/signs.h"
#include "graphics/text.h"
#include "overlay_signs_internal.h"
#include "platform/agent_terminal.h"
#include "platform/drag.h"
#include "platform/focus.h"
#include "platform/focus_current.h"
#include "platform/focus_watch.h"
#include "platform/font_panel.h"
#include "platform/overlay_vertical.h"
#include "platform/surface_tiers.h"

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CLOSE_MS 150

lane_t lanes[MAX_OUTPUTS];
static bool expanded[MAX_OUTPUTS];
static bool closing[MAX_OUTPUTS];
static int64_t close_at[MAX_OUTPUTS];
bool tracking, holding, pressed, focus_armed;
size_t track_index, hold_index, focus_index;
double pointer_x, pointer_y;
static uint64_t pressed_key, focus_key;
static pid_t pressed_pid;
static bool desk_on;
static uint64_t desk_key;
static char desk_name[48];
static int64_t desk_key_ms;
double published_lift;
static void (*on_expand)(void);
static int clamp_int(int64_t value) {
  if (value < INT_MIN)
    return INT_MIN;
  if (value > INT_MAX)
    return INT_MAX;
  return (int)value;
}
int64_t overlay_signs_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
bool inside(int x, int y, int w, int h, double px, double py) {
  return w > 0 && h > 0 && px >= x && py >= y && px < x + (double)w &&
         py < y + (double)h;
}
static void track_expanded(size_t index, int cat_x, int cat_y, int cat_w,
                           int cat_h, int64_t now_ms) {
  bool was_open = expanded[index];
  bool here = tracking && track_index == index;
  bool held = holding && hold_index == index;
  const sign_frame_t *frame =
      lanes[index].has_frame ? &lanes[index].frame : NULL;
  sign_hit_t hit = {0};
  bool over_hit = here && frame && signs_hit(frame, pointer_x, pointer_y, &hit);
  bool over_cat =
      here && inside(cat_x, cat_y, cat_w, cat_h, pointer_x, pointer_y);
  bool over_pad = here && expanded[index] && frame && frame->has_pad &&
                  inside(frame->pad.x, frame->pad.y, frame->pad.w, frame->pad.h,
                         pointer_x, pointer_y);
  if (held || over_cat || over_hit || over_pad) {
    expanded[index] = true;
    closing[index] = false;
  } else if (expanded[index]) {
    if (!closing[index]) {
      closing[index] = true;
      close_at[index] = now_ms + CLOSE_MS;
    }
    if (now_ms >= close_at[index]) {
      expanded[index] = false;
      closing[index] = false;
    }
  } else {
    closing[index] = false;
  }
  if (!was_open && expanded[index] && on_expand)
    on_expand();
  // Grab coordinates stay at the press surface, so hover must not follow them.
  if (!held) {
    lanes[index].has_hover = over_hit;
    lanes[index].hover_key = over_hit ? hit.key : 0;
  }
}
static void build_frame(size_t index, const config_t *config, int cat_x,
                        int cat_y, int surface_h, int64_t now_ms, bool snap,
                        bool menu, bool browse, unsigned tap,
                        sign_frame_t *frame) {
  agent_session_view_t all[AGENT_SESSIONS_MAX];
  agent_session_view_t shown[SIGN_MAX_VISIBLE];
  int count = agent_sessions_snapshot(all, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++) {
    if (!all[i].parent)
      continue;
    for (size_t output = 0; output < MAX_OUTPUTS; output++)
      for (int slot = 0; slot < AGENT_SESSIONS_MAX; slot++)
        if (lanes[output].model.slots[slot].used &&
            lanes[output].model.slots[slot].session.key == all[i].key)
          memset(&lanes[output].model.slots[slot], 0,
                 sizeof(lanes[output].model.slots[slot]));
    if (desk_on && desk_key == all[i].key)
      desk_on = false;
  }

  int selected = count > 0 ? agent_sessions_select(all, (size_t)count, shown,
                                                   (size_t)config->sign_max)
                           : 0;
  sign_input_t input = {
      .sessions = shown,
      .count = selected > 0 ? (size_t)selected : 0,
      .style = config->sign_style,
      .orientation = lanes[index].orientation,
      .theme = sign_theme_effective(config->sign_theme),
      .theme_auto = config->sign_theme == SIGN_THEME_AUTO,
      .animations = config->sign_animations,
      .idle = config->sign_idle,
      .font_size = config->sign_font_size,
      .name = config->sign_name,
      .name_extra = config->sign_name_extra,
      .title_length = config->sign_title_length,
      .surface_width = lanes[index].surface_width,
      .english = config_sign_english(config),
      .open = expanded[index],
      .has_hover = lanes[index].has_hover,
      .has_pressed = holding && pressed && hold_index == index,
      .hover_key = lanes[index].hover_key,
      .pressed_key = pressed_key,
      .now_ms = now_ms,
      .cat_x = cat_x,
      .cat_y = cat_y,
      .cat_height = config->cat_height,
      .surface_height = lanes[index].capacity_managed
                            ? surface_tier_model_height(
                                  config, lanes[index].orientation, surface_h)
                            : surface_h,
      .typing = desk_on && !menu,
      .desk_snap = snap || (menu && desk_on),
      .desk_offset = config->sign_desk_offset,
      .menu = menu,
      .menu_below = lanes[index].card_below,
      .menu_post = config->sign_style == SIGN_STYLE_POST,
      .menu_english = config_sign_english(config),
      .menu_tap = tap,
      .menu_font_dir = menu ? font_dir : 0,
      .menu_arrow = menu && menu_segment_down == 5   ? 1
                    : menu && menu_segment_down == 6 ? 2
                                                     : 0,
      .typing_key = desk_key,
      .typing_until = desk_on ? desk_key_ms + 2500 : 0,
  };
  snprintf(input.nameplate, sizeof(input.nameplate), "%s",
           config->sign_nameplate);
  snprintf(input.desk_name, sizeof(input.desk_name), "%s", desk_name);
  for (int i = 0; i < count; i++)
    if (all[i].key == desk_key)
      sign_session_name(&input, &all[i], input.desk_name, NULL);
  if (browse) {
    // Show every session, and one name tag in the fan, so the face being
    // tried is seen on real signs.
    input.open = true;
    if (!input.has_hover && input.count > 0) {
      input.has_hover = true;
      input.hover_key = shown[0].key;
    }
  }
  input.menu_font_hot =
      menu && (font_panel_surface_is_open() ||
               (tracking && track_index == index && lanes[index].has_frame &&
                segment_at(&lanes[index].frame, pointer_x, pointer_y) == 7));
  const char *face = font_override ? font_choice : config->sign_font;
  snprintf(input.menu_font, sizeof(input.menu_font), "%s", face);
  if (menu)
    font_dir = 0;
  lane_t *lane = &lanes[index];
  sign_input_t signature = input;
  signature.sessions = NULL;
  signature.now_ms = 0;
  // A settled frame or a future phase deadline cannot change just because
  // another pointer packet woke the event loop. Hit tests still use the live
  // geometry so crossing a plate, pad or card edge invalidates this input.
  bool before_deadline =
      !lane->frame.animating || lane->frame.next_frame_ms > now_ms ||
      (lane->frame.next_frame_ms == 0 && lane->waiting_frame);
  // Compare bytes deliberately: different padding can only miss the cache.
  if (lane->cached && before_deadline &&
      lane->cached_text_key == text_layout_key() &&
      !memcmp((const unsigned char *)&signature,
              (const unsigned char *)&lane->cached_input, sizeof(signature)) &&
      !memcmp(shown, lane->cached_sessions, input.count * sizeof(*shown))) {
    *frame = lane->frame;
    return;
  }
  signs_frame(&lane->model, &input, frame);
  lane->cached_text_key = text_layout_key();
  lane->cached_input = signature;
  memcpy(lane->cached_sessions, shown, input.count * sizeof(*shown));
  lane->cached = true;
}
static bool same_ink(const sign_frame_t *a, const sign_frame_t *b) {
  if (a->shape_count != b->shape_count || a->text_count != b->text_count ||
      a->hit_count != b->hit_count || a->bounds_x != b->bounds_x ||
      a->bounds_y != b->bounds_y || a->bounds_w != b->bounds_w ||
      a->bounds_h != b->bounds_h)
    return false;
  if (a->shape_count && memcmp(a->shapes, b->shapes,
                               (size_t)a->shape_count * sizeof(sign_shape_t)))
    return false;
  if (a->text_count &&
      memcmp(a->texts, b->texts, (size_t)a->text_count * sizeof(sign_text_t)))
    return false;
  if (a->hit_count &&
      memcmp(a->hits, b->hits, (size_t)a->hit_count * sizeof(sign_hit_t)))
    return false;
  return true;
}
static box_t box_make(int x, int y, int w, int h) {
  return (box_t){.x = x, .y = y, .w = w, .h = h, .valid = w > 0 && h > 0};
}
static box_t unite(box_t a, box_t b) {
  if (!a.valid)
    return b;
  if (!b.valid)
    return a;
  int64_t left = a.x < b.x ? a.x : b.x;
  int64_t top = a.y < b.y ? a.y : b.y;
  int64_t right = (int64_t)a.x + a.w;
  int64_t other = (int64_t)b.x + b.w;
  if (other > right)
    right = other;
  int64_t bottom = (int64_t)a.y + a.h;
  other = (int64_t)b.y + b.h;
  if (other > bottom)
    bottom = other;
  box_t box = {.x = clamp_int(left),
               .y = clamp_int(top),
               .w = clamp_int(right - left),
               .h = clamp_int(bottom - top),
               .valid = true};
  if (box.w <= 0 || box.h <= 0)
    box.valid = false;
  return box;
}
static box_t covered(const sign_frame_t *frame, int cat_x, int cat_y, int cat_w,
                     int cat_h) {
  box_t ink = {0};
  if (frame->bounds_w > 0 && frame->bounds_h > 0)
    ink = box_make(frame->bounds_x, frame->bounds_y, frame->bounds_w,
                   frame->bounds_h);
  return unite(ink, box_make(cat_x, cat_y, cat_w, cat_h));
}
static int milliseconds_until(int64_t when, int64_t now) {
  int64_t delta = when - now;
  if (delta < 1)
    return 1;
  if (delta > INT_MAX)
    return INT_MAX;
  return (int)delta;
}
static void sooner(overlay_signs_step_t *out, int64_t when, int64_t now) {
  if (when <= now)
    return;
  int wait = milliseconds_until(when, now);
  if (out->timeout_ms < 0 || wait < out->timeout_ms)
    out->timeout_ms = wait;
}

bool over_cat(size_t index) {
  lane_t *lane = &lanes[index];
  return lane->has_box && inside(lane->box_x, lane->box_y, lane->box_w,
                                 lane->box_h, pointer_x, pointer_y);
}

bool over_sign(size_t index) {
  sign_hit_t hit;
  return lanes[index].has_frame &&
         signs_hit(&lanes[index].frame, pointer_x, pointer_y, &hit);
}

overlay_signs_step_t overlay_signs_step(size_t index, const config_t *config,
                                        int cat_x, int cat_w, int surface_h,
                                        bool invisible, int64_t now_ms) {
  overlay_signs_step_t out = {.timeout_ms = -1};
  if (index >= MAX_OUTPUTS || !config)
    return out;
  take_panel_choice();
  if (font_pending) {
    font_pending = false;
    font_save_at = now_ms + 500;
  }
  if (font_dirty && font_save_at && now_ms >= font_save_at)
    flush_font();
  lane_t *lane = &lanes[index];
  if (index == menu_index)
    follow_hover(config);
  take_toggle(index, config, now_ms);
  if (index == track_index)
    apply_choice(config, now_ms);
  config_t local = *config;
  if (style_override && local.sign_style != SIGN_STYLE_OFF)
    local.sign_style = style_choice;
  if (language_override)
    local.sign_language = language_choice;
  if (theme_override)
    local.sign_theme = theme_choice;
  if ((invisible || local.sign_style == SIGN_STYLE_OFF) && menu_open &&
      index == menu_index)
    menu_close();
  menu_timers(index, now_ms);
  remember_config(config);
  if (lane->style != local.sign_style) {
    sign_menu_t menu = lane->model.menu;
    memset(&lane->model, 0, sizeof(lane->model));
    lane->model.menu = menu;
    lane->style = local.sign_style;
  }
  int cat_h = local.cat_height > 0 ? local.cat_height : 0;
  if (desk_on &&
      (invisible || !local.sign_typing_desk ||
       local.sign_style == SIGN_STYLE_OFF || now_ms - desk_key_ms >= 2500))
    desk_on = false;
  int rest = lane_resting_y(index, &local, surface_h);
  int cat_y = rest - (lane->has_frame ? (int)lane->frame.cat_lift : 0);
  if (cat_y < 0)
    cat_y = 0;
  track_expanded(index, cat_x, cat_y, cat_w, cat_h, now_ms);
  bool browse = false;
  if (index == menu_index) {
    browse = menu_open && font_panel_surface_is_open();
    if (browse && !was_browsing)
      panel_entered = false;
    // The card is hidden while browsing, so there is nothing to return to:
    // when the panel goes, the whole menu goes with it.
    if (was_browsing && !browse && menu_open)
      menu_close();
    was_browsing = browse;
  }
  bool menu = menu_open && index == menu_index && !browse;
  if (menu && (!lane->has_frame || !lane->frame.menu_open))
    lane->card_below = overlay_card_orientation(
                           &local, lane->output_cat_y, lane->output_height,
                           lane->orientation) == SIGN_BELOW;
  unsigned tap = 0;
  if (index == menu_index && menu_tap) {
    tap = menu_tap;
    menu_tap = 0;
    menu_tap_at = now_ms + 220;
  }
  agent_session_view_t all[AGENT_SESSIONS_MAX], shown[SIGN_MAX_VISIBLE];
  int count = agent_sessions_snapshot(all, AGENT_SESSIONS_MAX);
  int selected = count > 0 ? agent_sessions_select(all, (size_t)count, shown,
                                                   (size_t)local.sign_max)
                           : 0;
  int visible = 0;
  bool persistent = false;
  for (int i = 0; i < selected; i++)
    if (agent_sign_state(&shown[i]) != AGENT_STATE_IDLE ||
        local.sign_idle == SIGN_IDLE_ALWAYS ||
        (local.sign_idle == SIGN_IDLE_HOVER && expanded[index]) || browse)
      visible++;
  for (int i = 0; i < selected; i++)
    persistent |= sign_name_persistent(local.sign_style, &shown[i]);
  bool needs_names = expanded[index] || menu || browse || persistent ||
                     (holding && hold_index == index);
  out.required_capacity = surface_tier_select(&local, visible, needs_names);
  if (((menu_open && menu_index == index) ||
       (lane->has_frame && lane->frame.menu_open)) &&
      lane->card_below && lane->orientation == SIGN_ABOVE &&
      local.overlay_opacity == 0)
    out.required_capacity |= SURFACE_TIER_CARD_BELOW;
  if (lane->capacity_managed)
    out.required_capacity =
        surface_tier_reserve(lane->capacity, out.required_capacity);
  out.shrink_blocked = expanded[index] || (holding && hold_index == index) ||
                       (menu_open && menu_index == index) ||
                       (lane->has_frame && lane->frame.menu_open);
  // Discovery is synchronous in track_expanded. Re-snapshot above, then hold
  // the current model until configure AND buffer allocation have completed.
  if (lane->capacity_managed &&
      surface_tier_needs_growth(lane->capacity, out.required_capacity)) {
    lane->last = out;
    return out;
  }
  sign_frame_t next;
  build_frame(index, &local, cat_x, rest, surface_h, now_ms, invisible, menu,
              browse, tap, &next);
  bool changed = !lane->has_frame || !same_ink(&lane->frame, &next);
  lane->frame = next;
  lane->has_frame = true;
  published_lift = next.cat_lift;
  int drawn = rest - (int)next.cat_lift;
  if (drawn < 0)
    drawn = 0;
  lane->box_x = cat_x;
  lane->box_y = drawn;
  lane->box_w = cat_w;
  lane->box_h = cat_h;
  lane->has_box = cat_w > 0 && cat_h > 0;
  if (invisible) {
    lane->was_invisible = true;
    sooner(&out, font_save_at, now_ms);
    lane->last = out;
    return out;
  }
  bool full = !lane->presented || lane->was_invisible;
  lane->was_invisible = false;
  lane->presented = true;
  box_t current = covered(&lane->frame, cat_x, drawn, cat_w, cat_h);
  box_t damage = lane->has_prev ? unite(lane->prev, current) : current;
  bool full_rate = lane->frame.animating && lane->frame.next_frame_ms == 0 &&
                   !lane->waiting_frame;
  bool due = lane->frame.animating && lane->frame.next_frame_ms > 0 &&
             lane->frame.next_frame_ms <= now_ms;
  out.redraw = changed || full || full_rate || due;
  out.frame = full_rate || due;
  out.damage_full = full;
  if (damage.valid) {
    out.damage_x = damage.x;
    out.damage_y = damage.y;
    out.damage_w = damage.w;
    out.damage_h = damage.h;
  }
  if (out.redraw) {
    lane->prev = current;
    lane->has_prev = current.valid;
  }
  if (!out.frame && lane->frame.animating && lane->frame.next_frame_ms > now_ms)
    out.timeout_ms = milliseconds_until(lane->frame.next_frame_ms, now_ms);
  if (closing[index] && close_at[index] > now_ms) {
    int wait = milliseconds_until(close_at[index], now_ms);
    if (out.timeout_ms < 0 || wait < out.timeout_ms)
      out.timeout_ms = wait;
  }
  if (menu_tap_at <= now_ms)
    menu_tap_at = 0;
  sooner(&out, font_save_at, now_ms);
  if (menu_open && index == menu_index) {
    sooner(&out, menu_leave_at, now_ms);
    sooner(&out, menu_idle_at, now_ms);
  }
  if (index == menu_index)
    sooner(&out, menu_tap_at, now_ms);
  sync_panel(index, &local, surface_h, now_ms, &out);
  lane->last = out;
  return out;
}
overlay_signs_step_t overlay_signs_last(size_t index) {
  if (index >= MAX_OUTPUTS)
    return (overlay_signs_step_t){.timeout_ms = -1};
  return lanes[index].last;
}
const sign_frame_t *overlay_signs_frame(size_t index) {
  if (index >= MAX_OUTPUTS || !lanes[index].has_frame)
    return NULL;
  published_lift = lanes[index].frame.cat_lift;
  return &lanes[index].frame;
}
int overlay_signs_regions(size_t index, const config_t *config, int cat_x,
                          int cat_w, int surface_h, overlay_signs_rect_t *out,
                          int capacity) {
  if (!config || !out || capacity <= 0 || index >= MAX_OUTPUTS)
    return 0;
  if (config->sign_style == SIGN_STYLE_OFF) {
    drag_rect_t cat =
        drag_cat_rect(cat_x, config, cat_w, config->cat_height, surface_h);
    if (cat.width <= 0 || cat.height <= 0)
      return 0;
    out[0] = (overlay_signs_rect_t){cat.x, cat.y, cat.width, cat.height};
    return 1;
  }
  int count = 0;
  int cat_h = config->cat_height > 0 ? config->cat_height : 0;
  int cat_y = overlay_signs_cat_y_at(index, config, surface_h);
  if (count < capacity && cat_w > 0 && cat_h > 0)
    out[count++] = (overlay_signs_rect_t){cat_x, cat_y, cat_w, cat_h};
  const sign_frame_t *frame = overlay_signs_frame(index);
  if (!frame)
    return count;
  if (menu_open && menu_index == index && frame->menu_open) {
    if (frame->menu_card.w > 0 && frame->menu_card.h > 0 && count < capacity)
      out[count++] =
          (overlay_signs_rect_t){frame->menu_card.x, frame->menu_card.y,
                                 frame->menu_card.w, frame->menu_card.h};
    return count;
  }
  for (int i = 0; i < frame->hit_count && count < capacity; i++) {
    if (frame->hits[i].w <= 0 || frame->hits[i].h <= 0)
      continue;
    out[count++] = (overlay_signs_rect_t){frame->hits[i].x, frame->hits[i].y,
                                          frame->hits[i].w, frame->hits[i].h};
  }
  if (expanded[index] && frame->has_pad && frame->pad.w > 0 &&
      frame->pad.h > 0 && count < capacity) {
    out[count++] = (overlay_signs_rect_t){frame->pad.x, frame->pad.y,
                                          frame->pad.w, frame->pad.h};
  }
  return count;
}
static void rebase_rect(sign_rect_t *rect, int dx, int dy) {
  rect->x += dx;
  rect->y += dy;
}
void overlay_signs_rebase(size_t index, int dx, int dy) {
  if (index >= MAX_OUTPUTS)
    return;
  lane_t *lane = &lanes[index];
  if (tracking && track_index == index && !(holding && hold_index == index)) {
    pointer_x += dx;
    pointer_y += dy;
  }
  lane->box_x += dx;
  lane->box_y += dy;
  for (int i = 0; i < lane->frame.hit_count; i++) {
    lane->frame.hits[i].x += dx;
    lane->frame.hits[i].y += dy;
    lane->frame.hits[i].center_x += dx;
    lane->frame.hits[i].center_y += dy;
  }
  rebase_rect(&lane->frame.pad, dx, dy);
  rebase_rect(&lane->frame.menu_card, dx, dy);
  rebase_rect(&lane->frame.menu_font, dx, dy);
  rebase_rect(&lane->frame.menu_font_prev, dx, dy);
  rebase_rect(&lane->frame.menu_font_next, dx, dy);
  for (int i = 0; i < 2; i++) {
    rebase_rect(&lane->frame.menu_style[i], dx, dy);
    rebase_rect(&lane->frame.menu_lang[i], dx, dy);
  }
  for (int i = 0; i < 3; i++)
    rebase_rect(&lane->frame.menu_theme[i], dx, dy);
  lane->cached = false;
}

void overlay_signs_capacity(size_t index, int capacity) {
  if (index >= MAX_OUTPUTS)
    return;
  lanes[index].capacity_managed = capacity >= 0;
  lanes[index].capacity = capacity;
  lanes[index].cached = false;
}

void overlay_signs_frame_wait(size_t index, bool waiting) {
  if (index < MAX_OUTPUTS)
    lanes[index].waiting_frame = waiting;
}
bool overlay_signs_pointer(size_t index, double x, double y) {
  if (index >= MAX_OUTPUTS)
    return false;
  tracking = true;
  track_index = index;
  pointer_x = x;
  pointer_y = y;
  menu_activity = true;
  if (lanes[index].has_frame && segment_at(&lanes[index].frame, x, y))
    return true;
  sign_hit_t hit;
  return lanes[index].has_frame && signs_hit(&lanes[index].frame, x, y, &hit);
}
void overlay_signs_leave(void) {
  tracking = false;
  holding = false;
  pressed = false;
  menu_right_down = false;
  menu_segment_down = 0;
  block_drag = false;
  font_panel_surface_left();
}
void overlay_signs_track_panel(size_t index) {
  if (index >= MAX_OUTPUTS)
    return;
  tracking = true;
  track_index = index;
  menu_activity = true;
}

bool overlay_signs_press(size_t index) {
  if (index >= MAX_OUTPUTS)
    return false;
  block_drag = false;
  menu_segment_down = 0;
  if (tracking && track_index == index && over_card(index)) {
    block_drag = true;
    menu_segment_down = segment_at(&lanes[index].frame, pointer_x, pointer_y);
    menu_activity = true;
    hold_index = index;
    pressed = false;
    return menu_segment_down != 0;
  }
  font_panel_surface_close();
  bool was_open = expanded[index];
  holding = true;
  hold_index = index;
  pressed = false;
  expanded[index] = true;
  if (!was_open && on_expand)
    on_expand();
  closing[index] = false;
  sign_hit_t hit;
  if (tracking && track_index == index && lanes[index].has_frame &&
      signs_hit(&lanes[index].frame, pointer_x, pointer_y, &hit)) {
    pressed = true;
    pressed_key = hit.key;
    pressed_pid = hit.pid;
    lanes[index].has_hover = true;
    lanes[index].hover_key = hit.key;
    return true;
  }
  lanes[index].has_hover = false;
  return false;
}
static void note_split_click(pid_t pid, uint64_t key) {
  if (pid <= 1 || !key || !focus_watch_available())
    return;
  focus_window_t windows[FOCUS_WATCH_WINDOW_MAX];
  size_t count = focus_watch_windows(windows, FOCUS_WATCH_WINDOW_MAX);
  uint64_t id = 0;
  agent_terminal_t terminal = {.kind = TERMINAL_NONE};
  char name[48] = {0};
  agent_sessions_terminal(pid, &terminal, name, sizeof(name));
  char title[AGENT_TITLE_MAX + 1];
  agent_sessions_title(pid, title, sizeof(title));
  if (!focus_terminal_window_title(pid, &terminal, name, title, windows, count,
                                   &id) ||
      !id)
    return;
  focus_current_click(id, key);
}
bool overlay_signs_release(bool dragged, size_t *index, pid_t *pid,
                           uint64_t *key) {
  int segment = menu_segment_down;
  menu_segment_down = 0;
  block_drag = false;
  if (segment) {
    int released = 0;
    if (!dragged && tracking && track_index == hold_index &&
        lanes[hold_index].has_frame)
      released = segment_at(&lanes[hold_index].frame, pointer_x, pointer_y);
    if (released == segment)
      menu_choice = segment;
    menu_activity = true;
    pressed = false;
    holding = false;
    return false;
  }
  bool click = !dragged && pressed;
  if (click) {
    if (index)
      *index = hold_index;
    if (pid)
      *pid = pressed_pid;
    if (key)
      *key = pressed_key;
    agent_sessions_note_click(pressed_key, overlay_signs_now());
    note_split_click(pressed_pid, pressed_key);
  }
  pressed = false;
  holding = false;
  return click;
}
void overlay_signs_note_key(void) {
  if (!focus_watch_available())
    return;
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  uint64_t key = focus_watch_focused_session(views, (size_t)count);
  overlay_signs_type_at(key, overlay_signs_now());
}
void overlay_signs_type_at(uint64_t key, int64_t now_ms) {
  if (!key || now_ms < 0)
    return;
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  const char *name = NULL;
  for (int i = 0; i < count; i++) {
    if (views[i].key == key && !views[i].parent)
      name = views[i].name;
  }
  if (!name)
    return;
  if (!desk_on || desk_key != key)
    snprintf(desk_name, sizeof(desk_name), "%s", name);
  desk_on = true;
  desk_key = key;
  desk_key_ms = now_ms;
}
void overlay_signs_note_working(uint64_t key) {
  if (desk_on && desk_key == key)
    desk_on = false;
}
void overlay_signs_sync_focus(uint64_t key) {
  if (desk_on && desk_key != key)
    desk_on = false;
}
void overlay_signs_fail(size_t index, uint64_t key, int64_t now_ms) {
  if (index >= MAX_OUTPUTS)
    return;
  lanes[index].cached = false;
  signs_focus_failed(&lanes[index].model, key, now_ms);
}
void overlay_signs_arm_focus(size_t index, uint64_t key) {
  if (index >= MAX_OUTPUTS)
    return;
  focus_armed = true;
  focus_index = index;
  focus_key = key;
}
void overlay_signs_note_focus(focus_result_t result, int64_t now_ms) {
  if (!focus_armed || result == FOCUS_PENDING)
    return;
  if (result == FOCUS_NOT_FOUND || result == FOCUS_UNAVAILABLE)
    overlay_signs_fail(focus_index, focus_key, now_ms);
  focus_armed = false;
}
void overlay_signs_on_expand(void (*fn)(void)) {
  on_expand = fn;
}

void overlay_signs_output_gone(size_t index) {
  if (index >= MAX_OUTPUTS)
    return;
  if (menu_open && menu_index == index)
    menu_close();
  if (tracking && track_index == index)
    tracking = false;
  if (holding && hold_index == index)
    holding = pressed = false;
  if (focus_armed && focus_index == index)
    focus_armed = false;
  lanes[index] = (lane_t){0};
  expanded[index] = closing[index] = false;
  close_at[index] = 0;
}

void overlay_signs_cleanup(void) {
  font_panel_surface_close();
  memset(lanes, 0, sizeof(lanes));
  memset(expanded, 0, sizeof(expanded));
  memset(closing, 0, sizeof(closing));
  memset(close_at, 0, sizeof(close_at));
  tracking = holding = pressed = focus_armed = desk_on = false;
  menu_open = menu_right_down = menu_toggle = block_drag = false;
  menu_activity = style_override = language_override = theme_override = false;
  font_override = font_dirty = font_pending = fonts_ready = false;
  previewing = was_browsing = panel_entered = has_kept_card = false;
  preview_face[0] = '\0';
  seen_english = false;
  menu_segment_down = menu_choice = font_dir = 0;
  want_toggle = false;
  menu_index = 0;
  menu_leave_at = menu_idle_at = menu_tap_at = font_save_at = 0;
  menu_tap = 0;
  font_choice[0] = seen_config_font[0] = '\0';
  desk_key = 0;
  desk_key_ms = 0;
  desk_name[0] = '\0';
  published_lift = 0;
  on_expand = NULL;
  on_style = NULL;
  on_language = NULL;
  on_paw = NULL;
  on_font = NULL;
  for (size_t i = 0; i < MAX_OUTPUTS; i++)
    lanes[i].last.timeout_ms = -1;
}
