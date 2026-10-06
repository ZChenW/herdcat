#ifndef HERDCAT_GRAPHICS_SIGNS_INTERNAL_H
#define HERDCAT_GRAPHICS_SIGNS_INTERNAL_H

#include "graphics/signs.h"

#define MOVE_MS      420
#define WIDTH_MS     380
#define FADE_MS      200
#define HOVER_MS     200
#define DOT_STEP     180
#define NUDGE_MS     1500
#define NUDGE_PHASES 48
#define SHAKE_MS     350
// Peak of cubic-bezier(.34,1.4,.64,1) lifts the 5-sign cap to 162 design
// pixels above the cat. Two more pixels cover the damage outset.
// The four-row card starts 174px above the cat and keeps 6px of margin.
#define POST_CLEARANCE 180
// Wide enough for a name next to "Agent · state".
#define POST_BOARD    236
#define FAN_CLEARANCE 180
#define FAN_FADE_MS   250
#define FAN_ANGLE_MS  500
#define FAN_SCALE_MS  180
#define FAN_POP_MS    500
#define FAN_TAG_MS    220
#define DESK_MOVE_MS  320
#define DESK_FADE_MS  180
#define DESK_TOP      67
#define DESK_LIFT     8
#define DESK_SLIDE    22
#define DESK_CLEAR    22

typedef struct {
  double x1, y1, x2, y2;
} sign_bezier_t;

typedef struct {
  const char *working, *waiting, *idle, *minute;
  const char *done, *done_short, *unread, *unread_short;
  const char *error, *error_short;
} sign_words_t;

extern const sign_bezier_t BEZIER_EASE;
extern const sign_bezier_t BEZIER_MOVE;
extern const sign_bezier_t BEZIER_POP;
extern const sign_bezier_t BEZIER_SLIDE;
extern const sign_bezier_t BEZIER_WIDTH;
extern const sign_words_t WORDS[];

double sample(const sign_scalar_t *scalar, int64_t now);
double aim(sign_scalar_t *scalar, double target, int duration,
           const sign_bezier_t *curve, const sign_input_t *in,
           sign_frame_t *frame);
double clamp_unit(double value);
uint32_t with_alpha(uint32_t rgb, double alpha);
void include_bounds(sign_frame_t *frame, double x, double y, double w,
                    double h);
bool finished(agent_state_t state);
uint32_t meta_color(agent_state_t state, const sign_input_t *in);
const char *done_label(const sign_input_t *in, bool fan,
                       const agent_session_view_t *session);
bool show_session(const sign_input_t *in, agent_state_t state);
double font_ratio(const sign_input_t *in);
void add_shape(sign_frame_t *frame, sign_shape_kind_t kind, double x, double y,
               double w, double h, double radius, double stroke, uint32_t fill,
               uint32_t outline);
void add_unread(sign_frame_t *frame, agent_state_t state, double x, double y,
                double w, double scale, double opacity, const sign_input_t *in);
void wake_at(sign_frame_t *frame, int64_t when);
double nudge_phase(const sign_input_t *in, const sign_slot_t *slot,
                   sign_frame_t *frame);
void snap_from(sign_frame_t *frame, int first, bool snap);
sign_rect_t cover(double x, double y, double w, double h);
void upright_from(sign_frame_t *frame, int first, double cx, double cy);
void add_icon(sign_frame_t *frame, agent_state_t state, double cx, double cy,
              double scale, double opacity, const sign_input_t *in);
sign_slot_t *claim_slot(signs_t *model, const agent_session_view_t *session,
                        double rest_bottom);
void sign_nameplate(sign_text_t *text, const sign_input_t *in,
                    const agent_session_view_t *session, sign_frame_t *frame);
void layout_board(sign_slot_t *slot, const sign_input_t *in,
                  sign_frame_t *frame, double pole_x, double cat_bottom,
                  double scale, double desk_clear);
void layout_fan(signs_t *model, const sign_input_t *in, sign_frame_t *frame,
                size_t count, double desk_clear);
void emit_menu(signs_t *model, const sign_input_t *in, sign_frame_t *frame,
               double scale);

#endif
