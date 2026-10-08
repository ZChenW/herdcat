#include "config/nameplate.h"
#include "core/agent_sessions.h"
#include "core/agent_sign_state.h"
#include "core/agent_state.h"
#include "core/agent_title.h"
#include "graphics/sign_names.h"
#include "graphics/signs.h"
#include "platform/agent_terminal.h"
#include "signs_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void sign_nameplate(sign_text_t *text, const sign_input_t *in,
                    const agent_session_view_t *session, sign_frame_t *frame) {
  text->templated = true;
  bool title_main;
  sign_session_name(in, session, text->value, &title_main);
  char title[AGENT_TITLE_MAX + 4], who[64], state[64];
  sign_title_truncate(session->title, in->title_length, title);
  sign_agent_label(session, false, who);
  if (agent_sign_waits_on_children(session)) {
    subagent_state_label(state, session, in, frame);
  } else if (session->state == AGENT_STATE_WORKING) {
    int64_t elapsed = in->now_ms - session->state_since_ms;
    if (elapsed < 0)
      elapsed = 0;
    int64_t minutes = elapsed / 60000;
    if (minutes > 99999)
      minutes = 99999;
    snprintf(state, sizeof(state), "%s %lld %s",
             WORDS[in->english ? 1 : 0].working, (long long)minutes,
             WORDS[in->english ? 1 : 0].minute);
    wake_at(frame, in->now_ms + 60000 - elapsed % 60000);
  } else {
    const char *label = WORDS[in->english ? 1 : 0].idle;
    if (session->state == AGENT_STATE_WAITING)
      label = WORDS[in->english ? 1 : 0].waiting;
    else if (finished(session->state))
      label = done_label(in, true, session);
    snprintf(state, sizeof(state), "%s", label);
  }
  if (session->terminal.kind == TERMINAL_TMUX && session->terminal.detached) {
    size_t used = strlen(state);
    snprintf(state + used, sizeof(state) - used, " · %s",
             in->english ? "Detached" : "已断开");
  }
  // Keep the legacy text fields as a readable frame snapshot.
  if (agent_sign_waits_on_children(session)) {
    size_t state_len = strlen(state);
    size_t who_len = strlen(who);
    size_t budget = sizeof(text->meta) - state_len - 5;
    if (who_len > budget) {
      who_len = budget;
      while (who_len && ((unsigned char)who[who_len] & 0xc0) == 0x80)
        who_len--;
    }
    memcpy(text->meta, who, who_len);
    memcpy(text->meta + who_len, " · ", 4);
    memcpy(text->meta + who_len + 4, state, state_len + 1);
  } else if (session->child_count)
    snprintf(text->meta, sizeof(text->meta), "%.40s · %.18s", who, state);
  else
    snprintf(text->meta, sizeof(text->meta), "%.8s · %.50s", who, state);
  const char *source = in->nameplate[0]
                           ? in->nameplate
                           : nameplate_builtin(in->name_extra, title_main);
  const char *project = session->name;
  if (!in->nameplate[0] && !strcmp(project, text->value))
    project = "";
  nameplate_fields_t fields = {text->value, project, title, who, state};
  nameplate_expand(source, &fields, &text->nameplate);
  // A second row is optional when the real output has insufficient clearance.
  double height = sign_tag_height(text);
  double available =
      in->orientation == SIGN_BELOW && in->surface_height > 0
          ? in->surface_height -
                (2 * (in->cat_y + in->cat_height / 2) - text->anchor_y)
          : text->anchor_y;
  if (text->nameplate.lines > 1 && in->surface_height > 0 &&
      height > available) {
    int keep = 0;
    for (int i = 0; i < text->nameplate.count; i++)
      if (text->nameplate.runs[i].bold) {
        keep = text->nameplate.runs[i].line;
        break;
      }
    int n = 0;
    for (int i = 0; i < text->nameplate.count; i++) {
      nameplate_run_t r = text->nameplate.runs[i];
      if (r.line == keep) {
        r.line = 0;
        text->nameplate.runs[n++] = r;
      }
    }
    text->nameplate.count = n;
    text->nameplate.lines = 1;
  }
  text->surface_width = in->surface_width;
  text->max_width = in->surface_width > 0 ? in->surface_width : 0;
  if (in->surface_width > 0 &&
      (text->x - 220 * in->cat_height / 110 > 0 ||
       text->x + 220 * in->cat_height / 110 < in->surface_width))
    include_bounds(frame, 0, text->anchor_y - sign_tag_height(text) - 2,
                   in->surface_width, sign_tag_height(text) + 4);
  if (text->nameplate.lines > 1)
    include_bounds(frame, text->x - 220 * in->cat_height / 110,
                   text->anchor_y - height - 2, 440 * in->cat_height / 110,
                   height + 4);
}
