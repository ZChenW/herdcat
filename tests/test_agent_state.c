#include "core/agent_state.h"
#include "core/herdcat.h"
#include "test_helpers.h"

#include <string.h>

int main(void) {
  const char *names[] = {"idle", "working", "waiting", "done"};
  const int frames[] = {-1, HERDCAT_FRAME_AGENT_WORKING,
                        HERDCAT_FRAME_AGENT_WAITING, HERDCAT_FRAME_AGENT_DONE};
  for (int i = 0; i < AGENT_STATE_COUNT; i++) {
    agent_state_t state = AGENT_STATE_COUNT;
    TEST_ASSERT(agent_state_parse(names[i], &state) == 0);
    TEST_ASSERT(state == (agent_state_t)i);
    TEST_ASSERT(strcmp(agent_state_name(state), names[i]) == 0);
    TEST_ASSERT(agent_state_frame(state) == frames[i]);
  }
  agent_state_t state = AGENT_STATE_WAITING;
  const char *invalid[] = {"unknown", "", "Working", "working ", NULL};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    TEST_ASSERT(agent_state_parse(invalid[i], &state) == -1);
    TEST_ASSERT(state == AGENT_STATE_WAITING);
  }
  TEST_ASSERT(agent_state_parse("idle", NULL) == -1);
  TEST_ASSERT(strcmp(agent_state_name(AGENT_STATE_COUNT), "idle") == 0);
  TEST_ASSERT(strcmp(agent_state_name((agent_state_t)-1), "idle") == 0);
  TEST_ASSERT(agent_state_frame(AGENT_STATE_COUNT) == -1);
  TEST_ASSERT(agent_state_frame((agent_state_t)-1) == -1);
  return 0;
}
