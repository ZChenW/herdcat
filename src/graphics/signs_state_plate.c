#include "core/agent_sessions.h"
#include "core/agent_sign_state.h"
#include "core/agent_state.h"
#include "graphics/signs.h"
#include "signs_internal.h"

#include <string.h>

sign_plate_geometry_t state_plate_geometry(const char *agent) {
  bool codex = strcmp(agent, "codex") == 0;
  bool other = !codex && strcmp(agent, "claude") != 0;
  return (sign_plate_geometry_t){.kind = other ? SIGN_CUT : SIGN_RECT,
                                 .w = codex ? 30 : 34,
                                 .h = codex ? 30 : 27,
                                 .radius = codex ? 15 : 9};
}

void emit_state_plate(sign_frame_t *frame, const agent_session_view_t *session,
                      const sign_input_t *in, const sign_shape_t *plate,
                      double cx, double cy, double scale, double opacity,
                      const double states[AGENT_STATE_COUNT]) {
  add_shape(frame, plate->kind, plate->x, plate->y, plate->w, plate->h,
            plate->radius, plate->stroke, plate->fill, plate->outline);
  agent_state_t state = agent_sign_state(session);
  if (session->unread && finished(state)) {
    add_unread(frame, state, plate->x, plate->y, plate->w, scale, opacity, in);
  }
  for (int icon = 0; icon < AGENT_STATE_COUNT; icon++) {
    if (states[icon] > .001) {
      add_icon(frame, (agent_state_t)icon, cx, cy, scale,
               opacity * states[icon], in);
    }
  }
}
