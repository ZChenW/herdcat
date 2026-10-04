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
} sign_shape_t;
// Name is bold, metadata is medium. Measure metadata, keep `gap`, and
// ellipsize the name into what remains. reverse puts metadata on the left.
// above is painted after the cat; post labels stay on the board.
typedef struct {
  double x, baseline_y, w, clip_y, clip_h;
  double px, meta_px, gap;
  uint32_t color, meta_color;
  bool reverse, above;
  // Fan nameplates set back and tag_scale. Post leaves both at 0.
  // tag_scale is 1 when the label has settled; it dips toward 0.96 as it
  // hides. back is the nameplate fill, including alpha. caret draws a bar
  // after the name; meta_color is that bar's color.
  double tag_scale, font_ratio;
  uint32_t back;
  bool caret;
  char value[48], meta[64];
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
  bool initialized;
  sign_scalar_t pole, desk_lift, desk_fade;
  sign_slot_t slots[AGENT_SESSIONS_MAX];
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
} sign_input_t;

void signs_frame(signs_t *model, const sign_input_t *input,
                 sign_frame_t *frame);
void signs_focus_failed(signs_t *model, uint64_t key, int64_t now_ms);
// Logical pixels the surface needs above the cat's top. Zero when off.
int sign_clearance(sign_style_t style, int cat_height);

#endif
