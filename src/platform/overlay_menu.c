#define _POSIX_C_SOURCE 200809L
#include "config/config.h"
#include "config/sign_options.h"
#include "core/herdcat.h"
#include "graphics/signs.h"
#include "graphics/text.h"
#include "overlay_signs_internal.h"
#include "platform/font_panel.h"
#include "platform/overlay_signs.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MENU_LEAVE_MS 800
#define MENU_IDLE_MS  6000
bool menu_open, menu_right_down, menu_toggle, block_drag, menu_activity;
bool style_override, language_override, theme_override;
size_t menu_index;
int menu_segment_down, menu_choice;
bool want_toggle;
int64_t menu_leave_at, menu_idle_at, menu_tap_at;
sign_style_t style_choice;
sign_language_t language_choice;
sign_theme_t theme_choice;
unsigned menu_tap;
void (*on_style)(sign_style_t);
void (*on_language)(sign_language_t);
void (*on_theme)(sign_theme_t);
void (*on_paw)(unsigned);
void (*on_font)(const char *, bool);
bool font_override, font_dirty, font_pending, fonts_ready;
bool seen_english;
int font_dir;
int64_t font_save_at;
char font_choice[128], seen_config_font[128];
// Browsing: the font panel is open. The card steps aside, the real signs come
// up, and the main face follows the pointer until a face is chosen or the
// pointer leaves the cell.
bool previewing, was_browsing, panel_entered, has_kept_card;
char preview_face[128];
static sign_rect_t kept_card;

static bool inside_rect(const sign_rect_t *rect, double x, double y) {
  return rect && inside(rect->x, rect->y, rect->w, rect->h, x, y);
}

bool over_card(size_t index) {
  lane_t *lane = &lanes[index];
  return lane->has_frame && lane->frame.menu_open &&
         inside_rect(&lane->frame.menu_card, pointer_x, pointer_y);
}

int segment_at(const sign_frame_t *frame, double x, double y) {
  if (!frame || !frame->menu_open)
    return 0;
  for (int i = 0; i < 2; i++) {
    if (inside_rect(&frame->menu_style[i], x, y))
      return 1 + i;
    if (inside_rect(&frame->menu_lang[i], x, y))
      return 3 + i;
  }
  for (int i = 0; i < 3; i++)
    if (inside_rect(&frame->menu_theme[i], x, y))
      return 8 + i;
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

void flush_font(void) {
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

void remember_config(const config_t *config) {
  seen_english = effective_english(config);
  snprintf(seen_config_font, sizeof(seen_config_font), "%s", config->sign_font);
}

void menu_close(void) {
  flush_font();
  font_panel_surface_close();
  want_toggle = false;
  menu_open = false;
  menu_leave_at = 0;
  menu_idle_at = 0;
}

void take_toggle(size_t index, const config_t *config, int64_t now_ms) {
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

void apply_choice(const config_t *config, int64_t now_ms) {
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
  } else if ((choice >= 8 && choice <= 10) &&
             (theme_override ? theme_choice : config->sign_theme) !=
                 (choice == 8   ? SIGN_THEME_LIGHT
                  : choice == 9 ? SIGN_THEME_AUTO
                                : SIGN_THEME_DARK)) {
    theme_choice = choice == 8   ? SIGN_THEME_LIGHT
                   : choice == 9 ? SIGN_THEME_AUTO
                                 : SIGN_THEME_DARK;
    theme_override = true;
    menu_tap = 2;
    menu_idle_at = now_ms + MENU_IDLE_MS;
    if (on_theme)
      on_theme(theme_choice);
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

void menu_timers(size_t index, int64_t now_ms) {
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

void take_panel_choice(void) {
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

void follow_hover(const config_t *config) {
  const char *want =
      font_panel_surface_is_open() ? font_panel_surface_hover() : NULL;
  if (!want) {
    if (previewing) {
      previewing = false;
      if (on_font)
        on_font(font_choice, false);
      text_preview_end();
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
    text_preview_begin();
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

void sync_panel(size_t index, const config_t *config, int surface_h,
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

void overlay_signs_on_menu(void (*style)(sign_style_t),
                           void (*language)(sign_language_t),
                           void (*paw)(unsigned),
                           void (*font)(const char *, bool),
                           void (*theme)(sign_theme_t)) {
  on_style = style;
  on_language = language;
  on_paw = paw;
  on_font = font;
  on_theme = theme;
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
  theme_override = false;
  font_override = false;
  font_dirty = false;
  font_pending = false;
  font_save_at = 0;
  font_dir = 0;
  font_choice[0] = '\0';
}
