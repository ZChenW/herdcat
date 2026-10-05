#include "core/agent_state.h"

#include "core/herdcat.h"

#include <string.h>

static const char *const STATE_NAMES[AGENT_STATE_COUNT] = {"idle", "working",
                                                           "waiting", "done"};

int agent_state_parse(const char *name, agent_state_t *out) {
  if (!name || !out) {
    return -1;
  }
  for (int i = 0; i < AGENT_STATE_COUNT; i++) {
    if (strcmp(name, STATE_NAMES[i]) == 0) {
      *out = (agent_state_t)i;
      return 0;
    }
  }
  return -1;
}

const char *agent_state_name(agent_state_t state) {
  if (state < AGENT_STATE_IDLE || state >= AGENT_STATE_COUNT) {
    return STATE_NAMES[AGENT_STATE_IDLE];
  }
  return STATE_NAMES[state];
}

int agent_state_frame(agent_state_t state) {
  switch (state) {
  case AGENT_STATE_WORKING:
    return HERDCAT_FRAME_AGENT_WORKING;
  case AGENT_STATE_WAITING:
    return HERDCAT_FRAME_AGENT_WAITING;
  case AGENT_STATE_DONE:
    return HERDCAT_FRAME_AGENT_DONE;
  default:
    return -1;
  }
}
