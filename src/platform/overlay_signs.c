#define _POSIX_C_SOURCE 200809L
#include "platform/overlay_signs.h"

#include "config/sign_options.h"
#include "core/agent_sessions.h"
#include "graphics/text.h"
#include "platform/drag.h"
#include "platform/focus_current.h"
#include "platform/focus_watch.h"
#include "platform/font_panel.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CLOSE_MS 150

typedef struct {
  int x, y, w, h;
  bool valid;
} box_t;
typedef struct {
  signs_t model;
  sign_style_t style;
  sign_frame_t frame;
  overlay_signs_step_t last;
  box_t prev;
  bool has_frame, has_prev, presented, was_invisible;
  bool has_hover;
  uint64_t hover_key;
  int box_x, box_y, box_w, box_h;
  bool has_box;
} lane_t;

static lane_t lanes[MAX_OUTPUTS];
static bool expanded[MAX_OUTPUTS];
static bool closing[MAX_OUTPUTS];
static int64_t close_at[MAX_OUTPUTS];
static bool tracking, holding, pressed, focus_armed;
static size_t track_index, hold_index, focus_index;
static double pointer_x, pointer_y;
static uint64_t pressed_key, focus_key;
static pid_t pressed_pid;
static bool desk_on;
static uint64_t desk_key;
static char desk_name[48];
static int64_t desk_key_ms;
static double published_lift;
static void (*on_expand)(void);
#define MENU_LEAVE_MS 800
#define MENU_IDLE_MS  6000
static bool menu_open, menu_right_down, menu_toggle, block_drag, menu_activity;
static bool style_override, language_override;
static size_t menu_index;
static int menu_segment_down, menu_choice;
static bool want_toggle;
static int64_t menu_leave_at, menu_idle_at, menu_tap_at;
static sign_style_t style_choice;
static sign_language_t language_choice;
static unsigned menu_tap;
static void (*on_style)(sign_style_t);
static void (*on_language)(sign_language_t);
static void (*on_paw)(unsigned);
static void (*on_font)(const char *, bool);
static bool font_override, font_dirty, font_pending, fonts_ready;
static bool seen_english;
static int font_dir;
static int64_t font_save_at;
static char font_choice[128], seen_config_font[128];
// Browsing: the font panel is open. The card steps aside, the real signs come
// up, and the main face follows the pointer until a face is chosen or the
// pointer leaves the cell.
static bool previewing, was_browsing, panel_entered, has_kept_card;
static char preview_face[128];
static sign_rect_t kept_card;

static int desk_hang(int cat_height) {
  if (cat_height <= 0)
    return 0;
  return (cat_height * 4 + 109) / 110;
}

static int clamp_int(int64_t value) {
  if (value < INT_MIN)
    return INT_MIN;
  if (value > INT_MAX)
    return INT_MAX;
  return (int)value;
}
int overlay_signs_height(const config_t *config) {
  if (!config || config->overlay_height <= 0)
    return 0;
  if (config->sign_style == SIGN_STYLE_OFF)
    return config->overlay_height;
  int extra = sign_clearance(config->sign_style, config->cat_height);
  int spare = config->overlay_height > config->cat_height
                  ? config->overlay_height - config->cat_height
                  : 0;
  int hang = desk_hang(config->cat_height);
  if (hang > spare)
    extra += hang - spare;
  if (extra < 0 || config->overlay_height > INT_MAX - extra)
    return INT_MAX;
  return config->overlay_height + extra;
}
static int resting_cat_y(const config_t *config, int surface_height) {
  if (!config || surface_height <= 0)
    return 0;
  if (config->sign_style == SIGN_STYLE_OFF) {
    return drag_cat_rect(0, config, 0, config->cat_height, surface_height).y;
  }
  int cat = config->cat_height > 0 ? config->cat_height : 0;
  int limit = surface_height > cat ? surface_height - cat : 0;
  int64_t y = (int64_t)surface_height - cat - desk_hang(cat);
  // A positive offset would push the pole base below the surface.
  if (config->cat_y_offset < 0)
    y += config->cat_y_offset;
  if (y < 0)
    return 0;
  if (y > limit)
    return limit;
  return (int)y;
}
int overlay_signs_cat_y(const config_t *config, int surface_height) {
  int y = resting_cat_y(config, surface_height) - (int)published_lift;
  return y > 0 ? y : 0;
}
int64_t overlay_signs_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static bool inside(int x, int y, int w, int h, double px, double py) {
  return w > 0 && h > 0 && px >= x && py >= y && px < x + (double)w &&
         py < y + (double)h;
}
static bool top_hit(const sign_frame_t *frame, double x, double y,
                    sign_hit_t *hit) {
  // Fan plates overlap, so the nearest plate centre wins, not the last drawn.
  bool found = false;
  double best = 0;
  for (int i = 0; i < frame->hit_count; i++) {
    const sign_hit_t *candidate = &frame->hits[i];
    if (!inside(candidate->x, candidate->y, candidate->w, candidate->h, x, y))
      continue;
    double dx = x - (candidate->x + candidate->w / 2.0);
    double dy = y - (candidate->y + candidate->h / 2.0);
    double distance = dx * dx + dy * dy;
    if (!found || distance < best) {
      found = true;
      best = distance;
      *hit = *candidate;
    }
  }
  return found;
}
static void track_expanded(size_t index, int cat_x, int cat_y, int cat_w,
                           int cat_h, int64_t now_ms) {
  bool was_open = expanded[index];
  bool here = tracking && track_index == index;
  bool held = holding && hold_index == index;
  const sign_frame_t *frame =
      lanes[index].has_frame ? &lanes[index].frame : NULL;
  sign_hit_t hit = {0};
  bool over_hit = here && frame && top_hit(frame, pointer_x, pointer_y, &hit);
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
static int segment_at(const sign_frame_t *frame, double x, double y);
static void build_frame(size_t index, const config_t *config, int cat_x,
                        int cat_y, int64_t now_ms, bool snap, bool menu,
                        bool browse, unsigned tap, sign_frame_t *frame) {
  agent_session_view_t all[AGENT_SESSIONS_MAX];
  agent_session_view_t shown[SIGN_MAX_VISIBLE];
  int count = agent_sessions_snapshot(all, AGENT_SESSIONS_MAX);
  int selected = count > 0 ? agent_sessions_select(all, (size_t)count, shown,
                                                   (size_t)config->sign_max)
                           : 0;
  sign_input_t input = {
      .sessions = shown,
      .count = selected > 0 ? (size_t)selected : 0,
      .style = config->sign_style,
      .animations = config->sign_animations,
      .idle = config->sign_idle,
      .font_size = config->sign_font_size,
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
      .typing = desk_on && !menu,
      .desk_snap = snap || (menu && desk_on),
      .menu = menu,
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
  snprintf(input.desk_name, sizeof(input.desk_name), "%s", desk_name);
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
  signs_frame(&lanes[index].model, &input, frame);
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
static bool inside_rect(const sign_rect_t *rect, double x, double y) {
  return rect && inside(rect->x, rect->y, rect->w, rect->h, x, y);
}
static bool over_cat(size_t index) {
  lane_t *lane = &lanes[index];
  return lane->has_box && inside(lane->box_x, lane->box_y, lane->box_w,
                                 lane->box_h, pointer_x, pointer_y);
}
static bool over_card(size_t index) {
  lane_t *lane = &lanes[index];
  return lane->has_frame && lane->frame.menu_open &&
         inside_rect(&lane->frame.menu_card, pointer_x, pointer_y);
}
static bool over_sign(size_t index) {
  sign_hit_t hit;
  return lanes[index].has_frame &&
         top_hit(&lanes[index].frame, pointer_x, pointer_y, &hit);
}
static int segment_at(const sign_frame_t *frame, double x, double y) {
  if (!frame || !frame->menu_open)
    return 0;
  for (int i = 0; i < 2; i++) {
    if (inside_rect(&frame->menu_style[i], x, y))
      return 1 + i;
    if (inside_rect(&frame->menu_lang[i], x, y))
      return 3 + i;
  }
  if (inside_rect(&frame->menu_font_prev, x, y))
    return 5;
  if (inside_rect(&frame->menu_font_next, x, y))
    return 6;
  if (inside_rect(&frame->menu_font, x, y))
    return 7;
  return 0;
}
static sign_style_t effective_style(const config_t *config) {
  if (style_override && config->sign_style != SIGN_STYLE_OFF)
    return style_choice;
  return config->sign_style;
}
static bool effective_english(const config_t *config) {
  if (language_override)
    return language_choice == SIGN_LANGUAGE_EN;
  return config_sign_english(config);
}
static void flush_font(void) {
  if (!font_dirty)
    return;
  font_dirty = false;
  font_pending = false;
  font_save_at = 0;
  if (on_font)
    on_font(font_choice, true);
}
static void ensure_fonts(void) {
  if (fonts_ready)
    return;
  if (text_families("en", NULL, 0) >= 0)
    fonts_ready = true;
}
static void step_font(const char *lang, const char *config_font, int dir,
                      int64_t now_ms) {
  if (!dir)
    return;
  const char *current = font_override ? font_choice : config_font;
  if (current && !current[0])
    current = NULL;
  const char *next = NULL;
  if (text_family_step(lang, current, dir, &next) < 0)
    return;
  font_override = true;
  snprintf(font_choice, sizeof(font_choice), "%s", next ? next : "");
  font_dir = dir > 0 ? 1 : -1;
  font_dirty = true;
  menu_activity = true;
  if (now_ms < 0) {
    font_pending = true;
    font_save_at = 0;
  } else {
    font_pending = false;
    font_save_at = now_ms + 500;
  }
  if (on_font)
    on_font(font_choice, false);
}
static void remember_config(const config_t *config) {
  seen_english = effective_english(config);
  snprintf(seen_config_font, sizeof(seen_config_font), "%s", config->sign_font);
}
static void menu_close(void) {
  flush_font();
  font_panel_surface_close();
  want_toggle = false;
  menu_open = false;
  menu_leave_at = 0;
  menu_idle_at = 0;
}
static void take_toggle(size_t index, const config_t *config, int64_t now_ms) {
  if (index != track_index || !menu_toggle)
    return;
  menu_toggle = false;
  if (effective_style(config) == SIGN_STYLE_OFF)
    return;
  if (menu_open && menu_index == index) {
    menu_close();
    return;
  }
  menu_open = true;
  menu_index = index;
  menu_idle_at = now_ms + MENU_IDLE_MS;
  menu_leave_at = 0;
  ensure_fonts();
}
static void apply_choice(const config_t *config, int64_t now_ms) {
  int choice = menu_choice;
  menu_choice = 0;
  if (!menu_open || !choice)
    return;
  sign_style_t style = effective_style(config);
  bool english = effective_english(config);
  if (choice == 1 && style != SIGN_STYLE_FAN) {
    style_choice = SIGN_STYLE_FAN;
    style_override = true;
    menu_close();
    menu_tap = 1;
    if (on_style)
      on_style(SIGN_STYLE_FAN);
    if (on_paw)
      on_paw(1);
  } else if (choice == 2 && style != SIGN_STYLE_POST) {
    style_choice = SIGN_STYLE_POST;
    style_override = true;
    menu_close();
    menu_tap = 1;
    if (on_style)
      on_style(SIGN_STYLE_POST);
    if (on_paw)
      on_paw(1);
  } else if (choice == 3 && english) {
    language_choice = SIGN_LANGUAGE_ZH;
    language_override = true;
    menu_tap = 2;
    menu_idle_at = now_ms + MENU_IDLE_MS;
    if (on_language)
      on_language(SIGN_LANGUAGE_ZH);
    if (on_paw)
      on_paw(2);
  } else if (choice == 4 && !english) {
    language_choice = SIGN_LANGUAGE_EN;
    language_override = true;
    menu_tap = 2;
    menu_idle_at = now_ms + MENU_IDLE_MS;
    if (on_language)
      on_language(SIGN_LANGUAGE_EN);
    if (on_paw)
      on_paw(2);
  } else if (choice == 5 || choice == 6) {
    step_font(english ? "en" : "zh-cn", config->sign_font, choice == 5 ? -1 : 1,
              now_ms);
    menu_idle_at = now_ms + MENU_IDLE_MS;
  } else if (choice == 7) {
    want_toggle = true;
    menu_idle_at = now_ms + MENU_IDLE_MS;
  }
}
static bool over_union(size_t index) {
  return tracking && track_index == index &&
         (over_cat(index) || over_card(index) ||
          font_panel_surface_covers(index));
}
static void menu_timers(size_t index, int64_t now_ms) {
  if (!menu_open || index != menu_index)
    return;
  bool panel = font_panel_surface_is_open();
  if (menu_activity) {
    menu_idle_at = now_ms + MENU_IDLE_MS;
    font_panel_surface_activity(now_ms);
    if (over_union(index))
      menu_leave_at = 0;
    menu_activity = false;
  }
  if (panel)
    menu_idle_at = now_ms + MENU_IDLE_MS;
  if (panel && font_panel_surface_covers(index))
    panel_entered = true;
  if (over_union(index))
    menu_leave_at = 0;
  else if (!menu_leave_at && (!panel || panel_entered))
    // The card steps aside when the panel opens, leaving the pointer over
    // nothing until it reaches the panel. That is not leaving.
    menu_leave_at = now_ms + MENU_LEAVE_MS;
  if ((menu_leave_at && now_ms >= menu_leave_at) ||
      (!panel && menu_idle_at && now_ms >= menu_idle_at))
    menu_close();
}
static void take_panel_choice(void) {
  char family[128];
  if (!font_panel_surface_take_choice(family, sizeof(family)))
    return;
  font_override = true;
  snprintf(font_choice, sizeof(font_choice), "%s", family);
  font_dirty = false;
  font_pending = false;
  font_save_at = 0;
  font_dir = 0;
  if (on_font)
    on_font(font_choice, true);
}
static void follow_hover(const config_t *config) {
  const char *want =
      font_panel_surface_is_open() ? font_panel_surface_hover() : NULL;
  if (!want) {
    if (previewing) {
      previewing = false;
      if (on_font)
        on_font(font_choice, false);
    }
    return;
  }
  if (!previewing) {
    // Hold the chosen face in font_choice so the card, the panel's selected
    // cell and the restore below do not follow the face being tried.
    if (!font_override) {
      font_override = true;
      snprintf(font_choice, sizeof(font_choice), "%s", config->sign_font);
    }
    previewing = true;
    preview_face[0] = '\1';
    preview_face[1] = '\0';
  }
  if (!strcmp(preview_face, want))
    return;
  snprintf(preview_face, sizeof(preview_face), "%s", want);
  if (on_font)
    on_font(want, false);
}
static void sync_panel(size_t index, const config_t *config, int surface_h,
                       int64_t now_ms, overlay_signs_step_t *out) {
  font_panel_anchor_t card = {0};
  if (lanes[index].has_frame) {
    const sign_rect_t *rect = &lanes[index].frame.menu_card;
    if (rect->w > 0 && rect->h > 0) {
      kept_card = *rect;
      has_kept_card = true;
    } else if (has_kept_card && font_panel_surface_is_open()) {
      // The card is aside while browsing. The panel stays where it opened.
      rect = &kept_card;
    }
    card = (font_panel_anchor_t){rect->x, rect->y, rect->w, rect->h};
  }
  bool toggle = want_toggle && index == track_index;
  if (toggle || font_panel_surface_is_open()) {
    const char *face = font_override ? font_choice : config->sign_font;
    font_panel_surface_select(face);
    font_panel_surface_language(config_sign_english(config));
  }
  font_panel_surface_sync(index, config, card, surface_h, now_ms,
                          &out->timeout_ms, toggle);
  if (index == track_index)
    want_toggle = false;
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
  int rest = resting_cat_y(&local, surface_h);
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
  unsigned tap = 0;
  if (index == menu_index && menu_tap) {
    tap = menu_tap;
    menu_tap = 0;
    menu_tap_at = now_ms + 220;
  }
  sign_frame_t next;
  build_frame(index, &local, cat_x, rest, now_ms, invisible, menu, browse, tap,
              &next);
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
  bool full_rate = lane->frame.animating && lane->frame.next_frame_ms == 0;
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
  if (lanes[index].has_frame)
    published_lift = lanes[index].frame.cat_lift;
  int cat_y = overlay_signs_cat_y(config, surface_h);
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
  return lanes[index].has_frame && top_hit(&lanes[index].frame, x, y, &hit);
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
bool overlay_signs_button(uint32_t button, uint32_t state) {
  if (font_panel_surface_button(button, state))
    return true;
  if (button == 0x110)
    return false;
  if (button != 0x111 || !tracking || track_index >= MAX_OUTPUTS) {
    menu_right_down = false;
    return true;
  }
  size_t index = track_index;
  bool hit = lanes[index].style != SIGN_STYLE_OFF && tracking &&
             (over_cat(index) || over_card(index) || over_sign(index));
  if (state == 1) {
    menu_right_down = hit;
  } else {
    if (menu_right_down)
      menu_toggle = true;
    menu_right_down = false;
  }
  if (hit)
    menu_activity = true;
  return true;
}
bool overlay_signs_blocks_drag(void) {
  return block_drag;
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
      top_hit(&lanes[index].frame, pointer_x, pointer_y, &hit)) {
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
  if (!focus_find_window(pid, windows, count, &id) || !id)
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
    if (views[i].key == key)
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
    signs_focus_failed(&lanes[focus_index].model, focus_key, now_ms);
  focus_armed = false;
}
void overlay_signs_on_expand(void (*fn)(void)) {
  on_expand = fn;
}
void overlay_signs_on_menu(void (*style)(sign_style_t),
                           void (*language)(sign_language_t),
                           void (*paw)(unsigned),
                           void (*font)(const char *, bool)) {
  on_style = style;
  on_language = language;
  on_paw = paw;
  on_font = font;
}
void overlay_signs_scroll(int32_t discrete) {
  if (font_panel_surface_armed()) {
    font_panel_surface_wheel(discrete);
    return;
  }
  if (!menu_open || !discrete || menu_index >= MAX_OUTPUTS)
    return;
  lane_t *lane = &lanes[menu_index];
  if (!lane->has_frame ||
      !inside_rect(&lane->frame.menu_font, pointer_x, pointer_y))
    return;
  ensure_fonts();
  int steps = discrete;
  if (steps > 8)
    steps = 8;
  else if (steps < -8)
    steps = -8;
  int dir = steps > 0 ? 1 : -1;
  if (steps < 0)
    steps = -steps;
  const char *lang = seen_english ? "en" : "zh-cn";
  for (int i = 0; i < steps; i++)
    step_font(lang, seen_config_font, dir, -1);
}
void overlay_signs_use_config(void) {
  style_override = false;
  language_override = false;
  font_override = false;
  font_dirty = false;
  font_pending = false;
  font_save_at = 0;
  font_dir = 0;
  font_choice[0] = '\0';
}
void overlay_signs_cleanup(void) {
  font_panel_surface_close();
  memset(lanes, 0, sizeof(lanes));
  memset(expanded, 0, sizeof(expanded));
  memset(closing, 0, sizeof(closing));
  memset(close_at, 0, sizeof(close_at));
  tracking = holding = pressed = focus_armed = desk_on = false;
  menu_open = menu_right_down = menu_toggle = block_drag = false;
  menu_activity = style_override = language_override = false;
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
