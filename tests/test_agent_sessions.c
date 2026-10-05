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
                         "start", "rest",    "end",     "interrupt"};
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
  uint64_t seen = 1;
  agent_sessions_observe_focus(true, &seen, 1);
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
  TEST_ASSERT(agent_sessions_next_deadline(1) == 0);
  TEST_ASSERT(!agent_sessions_expire(INT64_MAX, 1));
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_DONE);
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
  TEST_ASSERT((pids[0] == 42 && pids[1] == 43) ||
              (pids[0] == 43 && pids[1] == 42));
  TEST_ASSERT(agent_sessions_pids(pids, 1) == 1);
  agent_sessions_remove_pid(0);
  TEST_ASSERT(agent_sessions_count() == 3);
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
  TEST_ASSERT(strcmp(buffer,
                     "claude 12345678 working 2s pid=42 claude 1234\n"
                     "claude 00000000 idle 2s pid=- claude 0000\n") == 0);
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

static void test_names_and_selection(void) {
  agent_sessions_reset();
  agent_session_view_t all[32], selected[5];
  for (uint64_t i = 1; i <= 8; i++)
    apply(i, AGENT_EVENT_START, (pid_t)i, (int64_t)i * 100, 5);
  TEST_ASSERT(agent_sessions_set_name(99, "unknown") == 0);
  TEST_ASSERT(agent_sessions_count() == 8);
  TEST_ASSERT(agent_sessions_set_name(1, "项目 with spaces") == 0);
  TEST_ASSERT(agent_sessions_set_name(1, "bad\nname") == -1);
  TEST_ASSERT(agent_sessions_set_name(1, "\xc0\xaf") == -1);
  TEST_ASSERT(agent_sessions_set_name(
                  1, "12345678901234567890123456789012345678901") == -1);
  TEST_ASSERT(agent_sessions_snapshot(all, 32) == 8);
  TEST_ASSERT(strcmp(all[0].name, "项目 with spaces") == 0);
  TEST_ASSERT(all[0].created_ms == 100 && all[0].state_since_ms == 100);
  TEST_ASSERT(agent_sessions_select(all, 8, selected, 5) == 5);
  for (int i = 0; i < 5; i++)
    TEST_ASSERT(selected[i].key == (uint64_t)i + 4);
  apply(1, AGENT_EVENT_WORKING, 1, 900, 5);
  apply(1, AGENT_EVENT_WORKING, 1, 1000, 5);
  apply(2, AGENT_EVENT_WAITING, 2, 1100, 5);
  TEST_ASSERT(agent_sessions_snapshot(all, 32) == 8);
  TEST_ASSERT(all[0].created_ms == 100 && all[0].state_since_ms == 900);
  TEST_ASSERT(agent_sessions_select(all, 8, selected, 5) == 5);
  const uint64_t expected[] = {1, 2, 6, 7, 8};
  for (int i = 0; i < 5; i++)
    TEST_ASSERT(selected[i].key == expected[i]);
  apply(1, AGENT_EVENT_END, 1, 1200, 5);
  apply(9, AGENT_EVENT_START, 9, 1300, 5);
  TEST_ASSERT(agent_sessions_snapshot(all, 32) == 8);
  TEST_ASSERT(all[0].key == 2 && all[7].key == 9);
  TEST_ASSERT(agent_sessions_snapshot(all, 1) == 1);
  TEST_ASSERT(agent_sessions_select(all, 1, selected, 5) == 1);
}

static int unread_of(uint64_t key) {
  agent_session_view_t view[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(view, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++) {
    if (view[i].key == key) {
      return view[i].unread ? 1 : 0;
    }
  }
  return -1;
}

static void test_done_reload(void) {
  agent_sessions_reset();
  apply(0, AGENT_EVENT_DONE, 0, 1000, 5);
  apply(1, AGENT_EVENT_DONE, 42, 1000, 5);
  TEST_ASSERT(unread_of(0) == 1 && unread_of(1) == 1);
  agent_sessions_configure_done(false, 2000, 2);
  TEST_ASSERT(unread_of(0) == 0 && unread_of(1) == 0);
  TEST_ASSERT(agent_sessions_next_deadline(0) == 4000);
  agent_sessions_configure_done(false, 3000, 2);
  TEST_ASSERT(agent_sessions_next_deadline(0) == 4000);
  TEST_ASSERT(agent_sessions_expire(4000, 0));
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  agent_sessions_configure_done(true, 5000, 5);
  apply(0, AGENT_EVENT_DONE, 0, 5000, 5);
  TEST_ASSERT(unread_of(0) == 1);
  agent_sessions_configure_done(false, 6000, 0);
  TEST_ASSERT(unread_of(0) == 0);
  TEST_ASSERT(agent_sessions_next_deadline(0) == 0);
  agent_sessions_reset();
}

static void test_unread(void) {
  agent_sessions_reset();
  apply(1, AGENT_EVENT_DONE, 42, 1000, 5);
  TEST_ASSERT(unread_of(1) == 1);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 0);
  TEST_ASSERT(!agent_sessions_expire(INT64_MAX, 1));
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_DONE);
  char line[128];
  TEST_ASSERT(agent_sessions_format(line, sizeof(line), 3000) > 0);
  TEST_ASSERT(strstr(line, " unread\n"));
  agent_sessions_note_click(1, 2000);
  TEST_ASSERT(unread_of(1) == 0);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 7000);
  TEST_ASSERT(agent_sessions_format(line, sizeof(line), 2000) > 0);
  TEST_ASSERT(!strstr(line, " unread"));
  TEST_ASSERT(agent_sessions_expire(7000, 1));
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);

  apply(1, AGENT_EVENT_DONE, 42, 8000, 5);
  apply(1, AGENT_EVENT_WORKING, 42, 8100, 5);
  TEST_ASSERT(unread_of(1) == 0);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_WORKING);
  apply(1, AGENT_EVENT_DONE, 42, 8200, 5);
  apply(1, AGENT_EVENT_END, 42, 8300, 5);
  TEST_ASSERT(agent_sessions_count() == 0);

  uint64_t focused[] = {2, 3};
  agent_sessions_observe_focus(true, focused, 2);
  apply(2, AGENT_EVENT_DONE, 2, 9000, 4);
  apply(4, AGENT_EVENT_DONE, 4, 9000, 4);
  TEST_ASSERT(unread_of(2) == 0);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 13000);
  TEST_ASSERT(unread_of(4) == 1);
  TEST_ASSERT(agent_sessions_expire(13000, 1));
  TEST_ASSERT(unread_of(4) == 1);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_DONE);
  agent_sessions_note_focused(4, 14000, 4);
  TEST_ASSERT(unread_of(4) == 0);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 18000);

  agent_sessions_reset();
  agent_sessions_set_done_sticky(false);
  apply(1, AGENT_EVENT_DONE, 1, 1000, 5);
  TEST_ASSERT(unread_of(1) == 0);
  TEST_ASSERT(agent_sessions_next_deadline(1) == 6000);

  agent_sessions_reset();
  for (uint64_t i = 1; i <= 6; i++) {
    apply(i, AGENT_EVENT_START, (pid_t)i, (int64_t)i * 100, 5);
  }
  apply(1, AGENT_EVENT_DONE, 1, 50, 5);
  agent_session_view_t all[8], selected[5];
  TEST_ASSERT(agent_sessions_snapshot(all, 8) == 6);
  TEST_ASSERT(agent_sessions_select(all, 6, selected, 5) == 5);
  bool kept = false;
  for (int i = 0; i < 5; i++) {
    kept |= selected[i].key == 1 && selected[i].unread;
  }
  TEST_ASSERT(kept);
  for (uint64_t i = 2; i <= 6; i++) {
    apply(i, AGENT_EVENT_WORKING, (pid_t)i, 1000 + (int64_t)i, 5);
  }
  TEST_ASSERT(agent_sessions_snapshot(all, 8) == 6);
  TEST_ASSERT(agent_sessions_select(all, 6, selected, 5) == 5);
  for (int i = 0; i < 5; i++) {
    TEST_ASSERT(selected[i].key != 1);
  }
}

static void apply_agent(uint64_t key, const char *agent, agent_event_t event,
                        pid_t pid, int64_t now) {
  TEST_ASSERT(agent_sessions_apply(key, agent, event, pid, now, 5, NULL) == 0);
}

static void test_kitty_memory(void) {
  agent_sessions_reset();
  apply(1, AGENT_EVENT_START, 42, 1000, 5);
  uint64_t generation = agent_sessions_generation();
  agent_sessions_set_kitty(1, 2556, 7, "unix:/tmp/kitty-secret");
  TEST_ASSERT(agent_sessions_generation() == generation);
  uint64_t window = 0;
  char listen[128];
  TEST_ASSERT(agent_sessions_kitty(42, &window, listen, sizeof(listen)));
  TEST_ASSERT(window == 7 && !strcmp(listen, "unix:/tmp/kitty-secret"));
  agent_session_view_t view;
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1);
  TEST_ASSERT(view.kitty_pid == 2556 && view.kitty_window == 7);
  char text[512];
  int written = agent_sessions_format(text, sizeof(text), 1000);
  TEST_ASSERT(written > 0 && !strstr(text, "unix:") && !strstr(text, "2556"));
  agent_session_record_t record;
  TEST_ASSERT(agent_sessions_export(&record, 1) == 1);
  TEST_ASSERT(!strstr(record.name, "2556") && !strstr(record.agent, "unix"));
  char tiny[4];
  TEST_ASSERT(!agent_sessions_kitty(42, &window, tiny, sizeof(tiny)));
  agent_sessions_set_kitty(1, 8, 8, "tcp:127.0.0.1:1");
  agent_sessions_set_kitty(1, 9, 9, "unix:/tmp/bad\n");
  TEST_ASSERT(agent_sessions_kitty(42, &window, listen, sizeof(listen)));
  TEST_ASSERT(window == 7 && !strcmp(listen, "unix:/tmp/kitty-secret"));
  agent_sessions_set_kitty(1, 1, 4, "unix:/tmp/kitty-secret");
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1);
  TEST_ASSERT(view.kitty_pid == 0 && view.kitty_window == 4);
  agent_sessions_set_kitty(1, 0, 7, "unix:/tmp/kitty-secret");
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1);
  TEST_ASSERT(view.kitty_pid == 0 && view.kitty_window == 7);
  apply(1, AGENT_EVENT_WORKING, 43, 1100, 5);
  TEST_ASSERT(!agent_sessions_kitty(42, &window, listen, sizeof(listen)));
  TEST_ASSERT(!agent_sessions_kitty(43, &window, listen, sizeof(listen)));
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1);
  TEST_ASSERT(view.kitty_pid == 0 && view.kitty_window == 0);
  apply(2, AGENT_EVENT_START, 0, 1200, 5);
  agent_sessions_set_kitty(2, 5, 1, "unix:/tmp/none");
  TEST_ASSERT(!agent_sessions_kitty(0, &window, listen, sizeof(listen)));
}

static void test_one_process(void) {
  agent_sessions_reset();
  apply_agent(1, "claude", AGENT_EVENT_START, 10, 1000);
  apply_agent(1, "claude", AGENT_EVENT_WORKING, 10, 2000);
  apply_agent(2, "claude", AGENT_EVENT_START, 10, 3000);
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_pid(1) == 0);
  TEST_ASSERT(agent_sessions_pid(2) == 10);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  apply_agent(2, "claude", AGENT_EVENT_WORKING, 10, 4000);
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_WORKING);

  apply_agent(1, "claude", AGENT_EVENT_DONE, 10, 5000);
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_pid(2) == 0);
  TEST_ASSERT(agent_sessions_pid(1) == 10);

  agent_sessions_reset();
  apply_agent(3, "opencode", AGENT_EVENT_START, 0, 1000);
  apply_agent(4, "opencode", AGENT_EVENT_START, 0, 1100);
  TEST_ASSERT(agent_sessions_count() == 2);
  apply_agent(5, "codex", AGENT_EVENT_START, 10, 1200);
  apply_agent(6, "claude", AGENT_EVENT_START, 11, 1300);
  apply_agent(7, "claude", AGENT_EVENT_START, 10, 1400);
  TEST_ASSERT(agent_sessions_count() == 5);
  TEST_ASSERT(agent_sessions_pid(3) == 0);
  TEST_ASSERT(agent_sessions_pid(5) == 10);
  TEST_ASSERT(agent_sessions_pid(6) == 11);
  TEST_ASSERT(agent_sessions_pid(7) == 10);
  apply_agent(7, "claude", AGENT_EVENT_WORKING, 10, 1500);
  TEST_ASSERT(agent_sessions_count() == 5);

  apply_agent(6, "claude", AGENT_EVENT_WORKING, 10, 1600);
  TEST_ASSERT(agent_sessions_count() == 4);
  TEST_ASSERT(agent_sessions_pid(6) == 10);
  TEST_ASSERT(agent_sessions_pid(7) == 0);
}

static void test_pidless(void) {
  agent_sessions_reset();
  apply(1, AGENT_EVENT_DONE, 0, 1000, 5);
  TEST_ASSERT(unread_of(1) == 1);
  TEST_ASSERT(agent_sessions_next_deadline(600) == 0);
  TEST_ASSERT(!agent_sessions_expire(INT64_MAX, 600));
  agent_sessions_note_click(1, 2000);
  TEST_ASSERT(unread_of(1) == 0);
  TEST_ASSERT(agent_sessions_next_deadline(600) == 7000);
  TEST_ASSERT(agent_sessions_expire(7000, 600));
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  TEST_ASSERT(agent_sessions_next_deadline(600) == 601000);
  TEST_ASSERT(agent_sessions_expire(601000, 600));
  TEST_ASSERT(agent_sessions_count() == 0);

  apply(2, AGENT_EVENT_START, 0, 1000, 5);
  apply(3, AGENT_EVENT_START, 9, 1000, 5);
  TEST_ASSERT(agent_sessions_next_deadline(10) == 11000);
  TEST_ASSERT(agent_sessions_expire(11000, 10));
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_pid(3) == 9);
  TEST_ASSERT(!agent_sessions_expire(INT64_MAX, 10));
  TEST_ASSERT(agent_sessions_count() == 1);

  apply(4, AGENT_EVENT_WORKING, 0, 1000, 5);
  TEST_ASSERT(agent_sessions_expire(11000, 10));
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_pid(4) == 0);
  apply(5, AGENT_EVENT_WORKING, 8, 1000, 5);
  TEST_ASSERT(agent_sessions_expire(11000, 10));
  TEST_ASSERT(agent_sessions_count() == 2);
  TEST_ASSERT(agent_sessions_pid(5) == 8);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
}

static void test_interrupt(void) {
  agent_sessions_reset();
  apply(1, AGENT_EVENT_INTERRUPT, 42, 1000, 5);
  TEST_ASSERT(agent_sessions_count() == 0);
  apply(1, AGENT_EVENT_WORKING, 42, 1000, 5);
  apply(1, AGENT_EVENT_INTERRUPT, 42, 2000, 5);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  apply(1, AGENT_EVENT_WAITING, 42, 3000, 5);
  apply(1, AGENT_EVENT_INTERRUPT, 42, 4000, 5);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  apply(1, AGENT_EVENT_DONE, 42, 5000, 5);
  apply(1, AGENT_EVENT_INTERRUPT, 42, 6000, 5);
  agent_session_view_t view;
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1);
  TEST_ASSERT(view.state == AGENT_STATE_DONE && view.unread);
  TEST_ASSERT(view.updated_ms == 5000);
  apply(1, AGENT_EVENT_END, 42, 7000, 5);
}

int main(void) {
  test_one_process();
  test_pidless();
  test_interrupt();
  test_done_reload();
  test_events();
  test_priority_and_lifecycle();
  test_expiration();
  test_eviction_and_pids();
  test_format();
  test_names_and_selection();
  test_unread();
  test_kitty_memory();
  return 0;
}
