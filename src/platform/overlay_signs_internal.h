#ifndef HERDCAT_PLATFORM_OVERLAY_SIGNS_INTERNAL_H
#define HERDCAT_PLATFORM_OVERLAY_SIGNS_INTERNAL_H

#include "platform/overlay_signs.h"

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

extern lane_t lanes[MAX_OUTPUTS];
extern bool tracking, holding, pressed, focus_armed;
extern size_t track_index, hold_index, focus_index;
extern double pointer_x, pointer_y;
extern bool menu_open, menu_right_down, menu_toggle, block_drag, menu_activity;
extern bool style_override, language_override, theme_override;
extern size_t menu_index;
extern int menu_segment_down, menu_choice;
extern bool want_toggle;
extern int64_t menu_leave_at, menu_idle_at, menu_tap_at;
extern sign_style_t style_choice;
extern sign_language_t language_choice;
extern sign_theme_t theme_choice;
extern unsigned menu_tap;
extern void (*on_style)(sign_style_t);
extern void (*on_language)(sign_language_t);
extern void (*on_theme)(sign_theme_t);
extern void (*on_paw)(unsigned);
extern void (*on_font)(const char *, bool);
extern bool font_override, font_dirty, font_pending, fonts_ready;
extern bool seen_english;
extern int font_dir;
extern int64_t font_save_at;
extern char font_choice[128], seen_config_font[128];
extern bool previewing, was_browsing, panel_entered, has_kept_card;
extern char preview_face[128];

bool inside(int x, int y, int w, int h, double px, double py);
bool over_cat(size_t index);
bool over_card(size_t index);
bool over_sign(size_t index);
int segment_at(const sign_frame_t *frame, double x, double y);
void flush_font(void);
void remember_config(const config_t *config);
void menu_close(void);
void take_toggle(size_t index, const config_t *config, int64_t now_ms);
void apply_choice(const config_t *config, int64_t now_ms);
void menu_timers(size_t index, int64_t now_ms);
void take_panel_choice(void);
void follow_hover(const config_t *config);
void sync_panel(size_t index, const config_t *config, int surface_h,
                int64_t now_ms, overlay_signs_step_t *out);

#endif
