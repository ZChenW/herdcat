#ifndef AGENT_STATE_H
#define AGENT_STATE_H

typedef enum {
  AGENT_STATE_IDLE = 0,
  AGENT_STATE_WORKING,
  AGENT_STATE_WAITING,
  AGENT_STATE_DONE,
  AGENT_STATE_COUNT
} agent_state_t;

// Parse an exact lowercase name; leave out unchanged on failure.
int agent_state_parse(const char *name, agent_state_t *out);
// Invalid states have the same name and frame mapping as idle.
const char *agent_state_name(agent_state_t state);
// Idle returns -1 to select the normal keyboard/sleep animation.
int agent_state_frame(agent_state_t state);

#endif  // AGENT_STATE_H
