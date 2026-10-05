#ifndef BONGOCAT_SIGNS_H
#define BONGOCAT_SIGNS_H

#include "config/sign_options.h"
#include "core/agent_sessions.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define SIGN_MAX_SHAPES  320
#define SIGN_MAX_TEXTS   AGENT_SESSIONS_MAX
#define SIGN_MAX_VISIBLE 5

typedef enum {
  SIGN_RECT,
  SIGN_CUT,  // radius is the 45-degree corner cut; stroke is inset.
  SIGN_CHECK
} sign_shape_kind_t;

// Surface logical pixels, y growing downward. A RECT stroke is an inset
// border; 0 means fill only. CHECK is the 16x13 done path with a centered
// stroke and no fill. rotation is clockwise degrees. orbit uses
// (origin_x, origin_y); otherwise the shape spins about its own center.
// Post leaves rotation at 0: its tilt is the one allowed omission.
typedef struct {
  sign_shape_kind_t kind;
  double x, y, w, h, radius, stroke, rotation;
  double origin_x, origin_y;
  uint32_t fill, outline;
  double clip_x, clip_y, clip_w, clip_h;
  bool clipped, orbit;
  // Painted after the cat. The switch-card holder leaves this false.
  bool above;
} sign_shape_t;
// Name is bold, metadata is medium. Measure metadata, keep `gap`, and
// ellipsize the name into what remains. reverse puts metadata on the left.
// above is painted after the cat; post labels stay on the board.
// line_top and line_h are the CSS line box. Drawing derives the baseline
// from the primary face. Fan nameplates leave the line box at 0 and set
// anchor_y to the plate bottom at tag_scale 1.
typedef struct {
  double x, line_top, line_h, w, clip_y, clip_h;
  double px, meta_px, gap;
  uint32_t color, meta_color;
  bool reverse, above;
  // Fan nameplates set back and tag_scale. Post leaves both at 0.
  // tag_scale is 1 when the label has settled; it dips toward 0.96 as it
  // hides. back is the nameplate fill, including alpha. caret draws a bar
  // after the name; meta_color is that bar's color.
  double tag_scale, font_ratio;
  uint32_t back;
  bool caret, center;
  double anchor_y, slide;
  char value[128], meta[64];
  // Empty draws with the main face. A name here draws this one line in
  // that family and leaves the main face unchanged.
  char family[128];
} sign_text_t;
typedef struct {
  int x, y, w, h;
  uint64_t key;
  pid_t pid;
} sign_hit_t;
typedef struct {
  int x, y, w, h;
} sign_rect_t;
// animating: a transition, loop, or label deadline is still pending.
// next_frame_ms == 0 redraws every frame. A positive value is an absolute
// wake time and must not also arm a per-frame callback. transitioning means
// a scalar has not arrived; next_frame_ms is then 0. pad is the open-state
// hover catcher and is not part of the damage bounds.
typedef struct {
  sign_shape_t shapes[SIGN_MAX_SHAPES];
  int shape_count;
  sign_text_t texts[SIGN_MAX_TEXTS];
  int text_count;
  sign_hit_t hits[SIGN_MAX_VISIBLE];
  int hit_count;
  sign_rect_t pad;
  bool has_pad;
  int bounds_x, bounds_y, bounds_w, bounds_h;
  bool animating, transitioning;
  int64_t next_frame_ms;
  // Logical pixels the cat and sign pivot move up while the desk is out.
  double cat_lift;
  // Switch card. Rects match the drawn card, each switch half, and the
  // thumbs. menu_paw is 1 for the left paw and 2 for the right, on the
  // frame that starts a tap.
  bool menu_open;
  sign_rect_t menu_card;
  sign_rect_t menu_style[2];
  sign_rect_t menu_lang[2];
  sign_rect_t menu_style_thumb, menu_lang_thumb;
  // Font row, and the left and right arrow buttons inside it.
  sign_rect_t menu_font, menu_font_prev, menu_font_next;
  unsigned menu_paw;
} sign_frame_t;
typedef struct {
  double from, target;
  int64_t start;
  int duration;
  double x1, y1, x2, y2;
} sign_scalar_t;
typedef struct {
  bool used, present;
  agent_session_view_t session;
  int direction;
  // width: post board width, or fan length. bottom: post row, or fan angle.
  // hover: post shift, or fan plate scale. label: fan nameplate 0..1.
  sign_scalar_t width, bottom, opacity, hover, label, states[AGENT_STATE_COUNT];
  int64_t failure_ms;
} sign_slot_t;
typedef struct {
  sign_scalar_t open, fade, holder, holder_fade;
  sign_scalar_t style, language, style_color, language_color;
  // font_in moves from 0 to 1 as the name settles. arrow is 0.8 while an
  // arrow is held and returns to 1. arrow_id is 1 for the left arrow.
  sign_scalar_t font_in, arrow;
  int arrow_id, font_dir;
  char font_label[128];
} sign_menu_t;
typedef struct {
  bool initialized;
  sign_scalar_t pole, desk_lift, desk_fade;
  sign_slot_t slots[AGENT_SESSIONS_MAX];
  sign_menu_t menu;
} signs_t;
// sessions is top-to-bottom display order, already limited to the visible set.
typedef struct {
  const agent_session_view_t *sessions;
  size_t count;
  sign_style_t style;
  sign_animations_t animations;
  sign_idle_t idle;
  bool english;
  int font_size;  // Zero retains the 13px model default.
  bool open, has_hover, has_pressed;
  uint64_t hover_key, pressed_key;
  int64_t now_ms;
  double cat_x, cat_y, cat_height;
  // typing retracts that sign but keeps its slot. desk_snap skips the slide.
  // typing_until is the absolute time the desk should start leaving.
  bool typing, desk_snap;
  uint64_t typing_key;
  int64_t typing_until;
  char desk_name[48];
  // menu raises the switch card and retracts signs. menu_post and
  // menu_english select the right-hand segment. menu_tap is 1 or 2.
  // menu_font empty is the default face. menu_font_dir is +1 or -1 on the
  // frame the name changes. menu_arrow is 1 or 2 while that arrow is down.
  // menu_font_hot fills the name while the pointer is on it or the panel
  // is open.
  bool menu, menu_post, menu_english;
  unsigned menu_tap;
  int menu_font_dir, menu_arrow;
  bool menu_font_hot;
  char menu_font[128];
} sign_input_t;

void signs_frame(signs_t *model, const sign_input_t *input,
                 sign_frame_t *frame);
void signs_focus_failed(signs_t *model, uint64_t key, int64_t now_ms);
// Logical pixels the surface needs above the cat's top. Zero when off.
int sign_clearance(sign_style_t style, int cat_height);

#endif
