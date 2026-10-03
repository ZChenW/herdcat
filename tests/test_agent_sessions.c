#include "core/agent_sessions.h"
#include "test_helpers.h"

#include <limits.h>
#include <string.h>

static void apply(uint64_t key, agent_event_t event, pid_t pid, int64_t now,
                  int timeout) {
  TEST_ASSERT(
      agent_sessions_apply(key, "claude", event, pid, now, timeout, NULL) == 0);
}

static void test_events(void) {
  const char *names[] = {"idle",  "working", "waiting", "done",
                         "start", "rest",    "end"};
  for (int i = 0; i < AGENT_EVENT_COUNT; i++) {
    agent_event_t event = AGENT_EVENT_COUNT;
    TEST_ASSERT(agent_event_parse(names[i], &event) == 0);
    TEST_ASSERT(event == (agent_event_t)i);
  }
  agent_event_t event = AGENT_EVENT_DONE;
  TEST_ASSERT(agent_event_parse("Working", &event) == -1);
  TEST_ASSERT(event == AGENT_EVENT_DONE);
  TEST_ASSERT(agent_event_parse(NULL, &event) == -1);
  TEST_ASSERT(agent_event_parse("idle", NULL) == -1);
  TEST_ASSERT(agent_sessions_apply(1, "toolongname", AGENT_EVENT_START, 0, 0, 5,
                                   NULL) == -1);
  TEST_ASSERT(agent_sessions_apply(1, "Claude", AGENT_EVENT_START, 0, 0, 5,
                                   NULL) == -1);
  TEST_ASSERT(agent_sessions_apply(1, "claude", AGENT_EVENT_COUNT, 0, 0, 5,
                                   NULL) == -1);
  TEST_ASSERT(agent_sessions_count() == 0);
}

static void test_priority_and_lifecycle(void) {
  agent_sessions_reset();
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  for (int i = 0; i < 3; i++) {
    agent_event_t no_create[] = {AGENT_EVENT_IDLE, AGENT_EVENT_REST,
                                 AGENT_EVENT_END};
    apply(1, no_create[i], 42, 0, 5);
    TEST_ASSERT(agent_sessions_count() == 0);
  }
  bool is_new = false;
  TEST_ASSERT(agent_sessions_apply(1, "claude", AGENT_EVENT_START, 42, 0, 5,
                                   &is_new) == 0);
  TEST_ASSERT(is_new && agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  apply(1, AGENT_EVENT_WORKING, 42, 1000, 5);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_WORKING);
  TEST_ASSERT(agent_sessions_apply(1, "claude", AGENT_EVENT_START, 0, 1500, 5,
                                   &is_new) == 0);
  TEST_ASSERT(!is_new && agent_sessions_resolve() == AGENT_STATE_WORKING);
  apply(1, AGENT_EVENT_WAITING, 42, 2000, 5);
  apply(2, AGENT_EVENT_WORKING, 43, 3000, 5);
  apply(2, AGENT_EVENT_WORKING, 43, 4000, 5);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_WAITING);
  apply(1, AGENT_EVENT_REST, 0, 4500, 5);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_WAITING);
  apply(1, AGENT_EVENT_DONE, 42, 5000, 5);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_DONE);
  TEST_ASSERT(agent_sessions_next_deadline(600) == 10000);
  apply(1, AGENT_EVENT_REST, 0, 6000, 5);
  apply(1, AGENT_EVENT_START, 0, 7000, 5);
  TEST_ASSERT(agent_sessions_next_deadline(600) == 10000);
  TEST_ASSERT(!agent_sessions_expire(9999, 600));
  TEST_ASSERT(agent_sessions_expire(10000, 600));
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_WORKING);
  apply(2, AGENT_EVENT_REST, 43, 11000, 5);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  TEST_ASSERT(agent_sessions_next_deadline(600) == 0);
  apply(2, AGENT_EVENT_WORKING, 43, 12000, 5);
  apply(2, AGENT_EVENT_END, 43, 12001, 5);
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  apply(1, AGENT_EVENT_END, 42, 12002, 5);
  TEST_ASSERT(agent_sessions_count() == 0);
}

static void test_expiration(void) {
  agent_sessions_reset();
  apply(1, AGENT_EVENT_DONE, 42, 1000, 0);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 0);
  TEST_ASSERT(!agent_sessions_expire(INT64_MAX, 1));
  apply(1, AGENT_EVENT_DONE, 42, 1000, 5);
  apply(1, AGENT_EVENT_WAITING, 42, 1100, 5);
  TEST_ASSERT(agent_sessions_next_deadline(0) == 0);
  TEST_ASSERT(!agent_sessions_expire(6000, 0));
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_WAITING);
  agent_sessions_set_watched(1, true);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 0);
  TEST_ASSERT(!agent_sessions_expire(6000, 1));
  agent_sessions_set_watched(1, false);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 2100);
  TEST_ASSERT(agent_sessions_expire(6000, 1));
  apply(1, AGENT_EVENT_WORKING, 42, 10000, 5);
  agent_sessions_set_watched(1, true);
  TEST_ASSERT(agent_sessions_next_deadline(2) == 12000);
  apply(1, AGENT_EVENT_WORKING, 42, 11000, 5);
  TEST_ASSERT(agent_sessions_next_deadline(2) == 13000);
  TEST_ASSERT(!agent_sessions_expire(12000, 2));
  TEST_ASSERT(agent_sessions_expire(13000, 2));
  TEST_ASSERT(!agent_sessions_expire(13000, 2));
  apply(1, AGENT_EVENT_WAITING, 42, 14000, 5);
  agent_sessions_set_watched(1, true);
  apply(1, AGENT_EVENT_START, 0, 15000, 5);
  TEST_ASSERT(agent_sessions_pid(1) == 42);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 0);
  apply(1, AGENT_EVENT_START, 43, 16000, 5);
  TEST_ASSERT(agent_sessions_pid(1) == 43);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 17000);
  apply(1, AGENT_EVENT_DONE, 0, INT64_MAX - 1, INT_MAX);
  TEST_ASSERT(agent_sessions_next_deadline(1) == INT64_MAX);
  TEST_ASSERT(agent_sessions_expire(INT64_MAX, 1));
  apply(0, AGENT_EVENT_DONE, 0, 10, 5);
  TEST_ASSERT(agent_sessions_count() == 2);
  apply(0, AGENT_EVENT_END, 0, 11, 5);
  TEST_ASSERT(agent_sessions_count() == 1);
}

static void test_eviction_and_pids(void) {
  agent_sessions_reset();
  for (uint64_t i = 1; i <= AGENT_SESSIONS_MAX; i++) {
    apply(i, AGENT_EVENT_WORKING, (pid_t)i, (int64_t)i, 5);
  }
  apply(20, AGENT_EVENT_IDLE, 0, 50, 5);
  apply(25, AGENT_EVENT_IDLE, 0, 60, 5);
  apply(33, AGENT_EVENT_WORKING, 33, 70, 5);
  TEST_ASSERT(agent_sessions_count() == AGENT_SESSIONS_MAX);
  TEST_ASSERT(agent_sessions_pid(20) == 0);
  TEST_ASSERT(agent_sessions_pid(1) == 1);
  apply(34, AGENT_EVENT_WORKING, 34, 80, 5);
  TEST_ASSERT(agent_sessions_pid(25) == 0);
  apply(35, AGENT_EVENT_WORKING, 35, 90, 5);
  TEST_ASSERT(agent_sessions_pid(1) == 0);
  TEST_ASSERT(agent_sessions_pid(2) == 2);
  agent_sessions_reset();
  apply(1, AGENT_EVENT_WORKING, 42, 0, 5);
  apply(2, AGENT_EVENT_WAITING, 42, 0, 5);
  apply(3, AGENT_EVENT_WORKING, 43, 0, 5);
  apply(4, AGENT_EVENT_START, 0, 0, 5);
  pid_t pids[AGENT_SESSIONS_MAX];
  TEST_ASSERT(agent_sessions_pids(pids, AGENT_SESSIONS_MAX) == 2);
  TEST_ASSERT(pids[0] == 42 && pids[1] == 43);
  TEST_ASSERT(agent_sessions_pids(pids, 1) == 1);
  agent_sessions_remove_pid(0);
  TEST_ASSERT(agent_sessions_count() == 4);
  agent_sessions_remove_pid(42);
  TEST_ASSERT(agent_sessions_count() == 2);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_WORKING);
}

static void test_format(void) {
  agent_sessions_reset();
  char buffer[128];
  apply(UINT64_C(0x123456789abcdef0), AGENT_EVENT_WORKING, 42, 1000, 5);
  apply(0, AGENT_EVENT_START, 0, 1000, 5);
  int n = agent_sessions_format(buffer, sizeof(buffer), 3000);
  TEST_ASSERT(n == (int)strlen(buffer));
  TEST_ASSERT(strcmp(buffer, "claude 12345678 working 2s pid=42\n"
                             "claude 00000000 idle 2s pid=-\n") == 0);
  memset(buffer, 'X', sizeof(buffer));
  TEST_ASSERT(agent_sessions_format(buffer, 8, 3000) == 7);
  TEST_ASSERT(buffer[7] == '\0' && buffer[8] == 'X');
  TEST_ASSERT(agent_sessions_format(buffer, 1, 3000) == 0);
  TEST_ASSERT(buffer[0] == '\0');
  TEST_ASSERT(agent_sessions_format(NULL, 0, 0) == 0);
  TEST_ASSERT(agent_sessions_format(NULL, 1, 0) == -1);
  agent_sessions_reset();
  TEST_ASSERT(agent_sessions_format(buffer, sizeof(buffer), 0) == 0);
}

int main(void) {
  test_events();
  test_priority_and_lifecycle();
  test_expiration();
  test_eviction_and_pids();
  test_format();
  return 0;
}
