#include "platform/focus_current.h"
#include "platform/focus_watch.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void parsed(const char *line, focus_watch_event_t *event,
                   focus_window_t *windows, size_t capacity) {
  int result = focus_watch_parse(line, strlen(line), event, windows, capacity);
  TEST_ASSERT(result == 1);
}

static void test_events(void) {
  focus_watch_event_t event;
  focus_window_t windows[4];
  parsed("{\"WindowFocusChanged\":{\"id\":7}}", &event, NULL, 0);
  TEST_ASSERT(event.kind == FOCUS_WATCH_FOCUS && event.id == 7 &&
              !event.id_null);
  parsed("{\"WindowFocusChanged\":{\"id\":null}}", &event, NULL, 0);
  TEST_ASSERT(event.kind == FOCUS_WATCH_FOCUS && event.id_null);
  parsed("{\"WindowClosed\":{\"id\":7}}", &event, NULL, 0);
  TEST_ASSERT(event.kind == FOCUS_WATCH_CLOSE && event.id == 7);
  const char *opened =
      "{\"WindowOpenedOrChanged\":{\"window\":{\"id\":7,\"title\":\"say "
      "\\\"hi\\\" "
      "{ok}\",\"app_id\":\"kitty\",\"pid\":42,\"is_focused\":true}}}";
  parsed(opened, &event, NULL, 0);
  TEST_ASSERT(event.kind == FOCUS_WATCH_UPSERT && event.id == 7 &&
              event.pid == 42);
  TEST_ASSERT(event.has_focused && event.focused == 7);
  const char *replaced =
      "{\"WindowsChanged\":{\"windows\":[{\"id\":1,\"title\":\"a {b} "
      "\\\"c\\\"\",\"pid\":9},{\"id\":2,\"pid\":null}]}}";
  parsed(replaced, &event, windows, 4);
  TEST_ASSERT(event.kind == FOCUS_WATCH_WINDOWS && event.count == 1);
  TEST_ASSERT(windows[0].id == 1 && windows[0].pid == 9);
  TEST_ASSERT(!event.has_focused);
  // A new stream starts with the window list and sends no focus event, so the
  // focused window has to be read from the list itself.
  const char *initial =
      "{\"WindowsChanged\":{\"windows\":[{\"id\":3,\"pid\":5,"
      "\"is_focused\":false},{\"id\":4,\"pid\":6,\"is_focused\":true}]}}";
  parsed(initial, &event, windows, 4);
  TEST_ASSERT(event.kind == FOCUS_WATCH_WINDOWS && event.count == 2);
  TEST_ASSERT(event.has_focused && event.focused == 4);
  const char *layout = "{\"KeyboardLayoutSwitched\":{\"idx\":0}}";
  TEST_ASSERT(focus_watch_parse(layout, strlen(layout), &event, NULL, 0) == 0);
  const char *ok = "{\"Ok\":\"Handled\"}";
  TEST_ASSERT(focus_watch_parse(ok, strlen(ok), &event, NULL, 0) == 0);
  const char *broken = "{\"WindowFocusChanged\":{\"id\":";
  TEST_ASSERT(focus_watch_parse(broken, strlen(broken), &event, NULL, 0) == -1);
  TEST_ASSERT(focus_watch_parse("{\"WindowClosed\":{\"id\":1}}", 26, NULL, NULL,
                                0) == -1);
  char *huge = malloc(65537);
  TEST_ASSERT(huge);
  TEST_ASSERT(focus_watch_parse(huge, 65537, &event, NULL, 0) == -1);
  free(huge);
  TEST_ASSERT(
      focus_watch_parse(replaced, strlen(replaced), &event, windows, 0) == -1);
}

static void test_match(void) {
  focus_window_t windows[1] = {
      {.id = 4, .pid = getpid()}
  };
  agent_session_view_t sessions[2] = {0};
  sessions[0].key = 1;
  sessions[0].pid = getpid();
  sessions[0].updated_ms = 10;
  sessions[1].key = 2;
  sessions[1].pid = getpid();
  sessions[1].updated_ms = 20;
  TEST_ASSERT(focus_watch_match(4, windows, 1, sessions, 2) == 2);
  sessions[1].updated_ms = 5;
  TEST_ASSERT(focus_watch_match(4, windows, 1, sessions, 2) == 1);
  TEST_ASSERT(focus_watch_match(9, windows, 1, sessions, 2) == 0);
  uint64_t keys[2] = {0};
  TEST_ASSERT(focus_watch_matching(4, windows, 1, sessions, 2, keys, 2) == 2);
  TEST_ASSERT(keys[0] == 1 && keys[1] == 2);
  TEST_ASSERT(focus_watch_matching(9, windows, 1, sessions, 2, keys, 2) == 0);
  TEST_ASSERT(focus_watch_focused_keys(sessions, 2, keys, 2) == 0);
}

static agent_session_view_t split_session(uint64_t key, pid_t pid,
                                          pid_t kitty_pid, uint64_t split,
                                          int64_t updated) {
  agent_session_view_t session = {0};
  session.key = key;
  session.pid = pid;
  session.kitty_pid = kitty_pid;
  session.kitty_window = split;
  session.updated_ms = updated;
  return session;
}

static void test_pane_request(void) {
  pid_t pid = 0;
  uint64_t split = 0;
  TEST_ASSERT(focus_pane_parse("pane 2556 12", &pid, &split));
  TEST_ASSERT(pid == 2556 && split == 12);
  TEST_ASSERT(
      focus_pane_fields("4194304", "18446744073709551615", &pid, &split));
  TEST_ASSERT(pid == 4194304 && split == UINT64_MAX);
  char line[64];
  snprintf(line, sizeof(line), "pane %ld %llu", 2556L, 12ULL);
  TEST_ASSERT(focus_pane_parse(line, &pid, &split));
  TEST_ASSERT(pid == 2556 && split == 12);
  const char *rejected[] = {
      "pane 1 1",
      "pane 0 1",
      "pane 01 1",
      "pane 2 0",
      "pane 2 01",
      "pane 2 00",
      "pane -2 1",
      "pane 2 -1",
      "pane  2 1",
      "pane 2  1",
      "pane 2 1 ",
      "pane 2 1 extra",
      "pane",
      "pane 2",
      "pane 4194305 1",
      "pane 2 18446744073709551616",
      "pane 2 123456789012345678901",
      "PANE 2 1",
      "pane 2 1\n",
      "",
  };
  for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
    TEST_ASSERT(!focus_pane_parse(rejected[i], &pid, &split));
  }
  TEST_ASSERT(!focus_pane_parse(NULL, &pid, &split));
  TEST_ASSERT(!focus_pane_fields("02", "1", &pid, &split));
  TEST_ASSERT(!focus_pane_fields(NULL, "1", &pid, &split));
  TEST_ASSERT(!focus_pane_set(1, 3));
  TEST_ASSERT(!focus_pane_set(2, 0));
  focus_pane_reset();
  TEST_ASSERT(focus_pane_set(2, 10));
  TEST_ASSERT(focus_pane_get(2, &split) && split == 10);
  TEST_ASSERT(focus_pane_set(2, 11));
  TEST_ASSERT(focus_pane_get(2, &split) && split == 11);
  for (pid_t i = 3; i <= 17; i++) {
    TEST_ASSERT(focus_pane_set(i, (uint64_t)i));
  }
  focus_pane_t copied[FOCUS_PANE_MAX];
  TEST_ASSERT(focus_pane_copy(copied, FOCUS_PANE_MAX) == FOCUS_PANE_MAX);
  TEST_ASSERT(focus_pane_set(18, 18));
  TEST_ASSERT(!focus_pane_get(2, &split));
  TEST_ASSERT(focus_pane_get(3, &split) && split == 3);
  TEST_ASSERT(focus_pane_set(3, 30));
  TEST_ASSERT(focus_pane_set(19, 19));
  TEST_ASSERT(focus_pane_get(3, &split) && split == 30);
  TEST_ASSERT(!focus_pane_get(4, &split));
  TEST_ASSERT(focus_pane_get(19, &split) && split == 19);
  focus_pane_reset();
}

static void test_current_session(void) {
  focus_window_t windows[2] = {
      {.id = 4, .pid = 42},
      {.id = 8, .pid = 80},
  };
  agent_session_view_t sessions[3];
  sessions[0] = split_session(1, 42, 2556, 10, 10);
  sessions[1] = split_session(2, 42, 2556, 11, 20);
  sessions[2] = split_session(3, 80, 2556, 10, 30);
  focus_pane_t panes[2];
  panes[0] = (focus_pane_t){.pid = 2556, .split = 10};
  TEST_ASSERT(
      focus_current_choose(4, windows, 2, sessions, 3, panes, 1, 4, 2) == 1);
  panes[0].split = 99;
  TEST_ASSERT(
      focus_current_choose(4, windows, 2, sessions, 3, panes, 1, 4, 1) == 0);
  uint64_t keys[3] = {0};
  TEST_ASSERT(
      focus_current_seen(4, windows, 2, sessions, 3, panes, 1, keys, 3) == 0);
  panes[0].split = 11;
  TEST_ASSERT(
      focus_current_choose(4, windows, 2, sessions, 3, panes, 1, 4, 1) == 2);
  TEST_ASSERT(
      focus_current_seen(4, windows, 2, sessions, 3, panes, 1, keys, 3) == 1);
  TEST_ASSERT(keys[0] == 2);
  sessions[0].updated_ms = 20;
  sessions[1].kitty_window = 11;
  sessions[0].kitty_window = 11;
  TEST_ASSERT(
      focus_current_choose(4, windows, 2, sessions, 2, panes, 1, 0, 0) == 2);
  sessions[0] = split_session(1, 42, 2556, 10, 10);
  sessions[1] = split_session(2, 42, 2556, 11, 20);
  TEST_ASSERT(focus_current_choose(4, windows, 2, sessions, 3, NULL, 0, 4, 1) ==
              1);
  TEST_ASSERT(focus_current_choose(4, windows, 2, sessions, 3, NULL, 0, 0, 0) ==
              focus_watch_match(4, windows, 2, sessions, 3));
  sessions[1].updated_ms = 10;
  TEST_ASSERT(focus_current_choose(4, windows, 2, sessions, 2, NULL, 0, 0, 0) ==
              2);
  sessions[1].updated_ms = 20;
  TEST_ASSERT(
      focus_current_seen(4, windows, 2, sessions, 3, NULL, 0, keys, 3) == 2);
  TEST_ASSERT(keys[0] == 1 && keys[1] == 2);
  focus_current_reset();
  focus_current_click(4, 1);
  focus_current_observe(4);
  uint64_t clicked_window = 0;
  uint64_t clicked_key = 0;
  focus_current_clicked(&clicked_window, &clicked_key);
  TEST_ASSERT(clicked_window == 4 && clicked_key == 1);
  TEST_ASSERT(focus_current_choose(4, windows, 2, sessions, 3, NULL, 0,
                                   clicked_window, clicked_key) == 1);
  focus_current_observe(0);
  focus_current_clicked(&clicked_window, &clicked_key);
  TEST_ASSERT(clicked_window == 0 && clicked_key == 0);
  TEST_ASSERT(focus_current_choose(4, windows, 2, sessions, 3, NULL, 0,
                                   clicked_window, clicked_key) == 2);
  focus_current_click(4, 1);
  focus_current_observe(9);
  focus_current_clicked(&clicked_window, &clicked_key);
  TEST_ASSERT(clicked_window == 0 && clicked_key == 0);
  focus_current_click(0, 1);
  focus_current_click(4, 0);
  focus_current_clicked(&clicked_window, &clicked_key);
  TEST_ASSERT(clicked_window == 0 && clicked_key == 0);
  sessions[0].kitty_pid = 0;
  sessions[1].kitty_pid = 0;
  panes[0].split = 10;
  TEST_ASSERT(
      focus_current_choose(4, windows, 2, sessions, 3, panes, 1, 0, 0) == 2);
  TEST_ASSERT(
      focus_current_seen(4, windows, 2, sessions, 3, panes, 1, keys, 3) == 2);
  sessions[0] = split_session(1, 42, 100, 1, 10);
  sessions[1] = split_session(2, 42, 200, 1, 40);
  panes[0] = (focus_pane_t){.pid = 100, .split = 1};
  panes[1] = (focus_pane_t){.pid = 200, .split = 5};
  TEST_ASSERT(
      focus_current_choose(4, windows, 2, sessions, 2, panes, 2, 4, 2) == 1);
  TEST_ASSERT(
      focus_current_seen(4, windows, 2, sessions, 2, panes, 2, keys, 3) == 1);
  TEST_ASSERT(keys[0] == 1);
  panes[0].split = 7;
  TEST_ASSERT(
      focus_current_choose(4, windows, 2, sessions, 2, panes, 1, 4, 1) == 0);
}

static void test_window_titles(void) {
  // These are ordinary titles, independent of interruption detection.
  const char *titles[] = {"✳ Claude Code", "\\u2733 Claude Code",
                          "◐ Claude Code"};
  const char *decoded[] = {"✳ Claude Code", "✳ Claude Code", "◐ Claude Code"};
  focus_watch_event_t event;
  char line[256];
  for (size_t i = 0; i < sizeof(titles) / sizeof(titles[0]); i++) {
    snprintf(line, sizeof(line),
             "{\"WindowOpenedOrChanged\":{\"window\":{\"id\":7,"
             "\"title\":\"%s\",\"pid\":42,\"is_focused\":true}}}",
             titles[i]);
    parsed(line, &event, NULL, 0);
    TEST_ASSERT(event.kind == FOCUS_WATCH_UPSERT && event.pid == 42);
    TEST_ASSERT(event.has_focused && event.focused == 7);
    TEST_ASSERT(!strcmp(event.title, decoded[i]));
  }
  parsed("{\"WindowOpenedOrChanged\":{\"window\":{\"id\":7,"
         "\"title\":null,\"pid\":42}}}",
         &event, NULL, 0);
  TEST_ASSERT(!event.title[0]);
  focus_window_t windows[2];
  parsed("{\"WindowsChanged\":{\"windows\":[{\"id\":7,\"pid\":42,"
         "\"title\":\"\\u2733 Claude Code\",\"is_focused\":true},"
         "{\"id\":8,\"pid\":43,\"title\":\"vim ✳\"}]}}",
         &event, windows, 2);
  TEST_ASSERT(event.count == 2 && event.has_focused && event.focused == 7);
  TEST_ASSERT(!strcmp(windows[0].title, "✳ Claude Code"));
  TEST_ASSERT(!strcmp(windows[1].title, "vim ✳"));
}

static void test_stream_down(void) {
  agent_session_view_t sessions[1] = {0};
  sessions[0].key = 1;
  sessions[0].pid = 42;
  sessions[0].updated_ms = 10;
  focus_current_reset();
  focus_current_click(4, 1);
  uint64_t keys[1] = {0};
  TEST_ASSERT(!focus_watch_available());
  TEST_ASSERT(focus_watch_focused_session(sessions, 1) == 0);
  TEST_ASSERT(focus_watch_focused_keys(sessions, 1, keys, 1) == 0);
  uint64_t window = 0;
  uint64_t key = 0;
  focus_current_clicked(&window, &key);
  TEST_ASSERT(window == 4 && key == 1);
  focus_current_reset();
}

static void test_query_ancestry(void) {
  // A terminal owns an ancestor of the agent, as in the runtime fixture.
  focus_window_t window = {.id = 4, .pid = getppid()};
  agent_session_view_t session = split_session(1, getpid(), getppid(), 10, 20);
  uint64_t keys[1], chosen = 0;
  focus_reset_stat_reads();
  TEST_ASSERT(focus_current_query(4, &window, 1, &session, 1, NULL, 0, 0, 0,
                                  keys, 1, &chosen) == 1);
  TEST_ASSERT(keys[0] == 1 && chosen == 1);
  printf("joint focus query: %u ancestry reads\n", focus_stat_reads());
  TEST_ASSERT(focus_stat_reads() == 1);
  // A child cannot take focus acknowledgement or the typing desk, even
  // though its PID belongs to the same terminal and its update is newer.
  agent_session_view_t pair[2] = {session, session};
  pair[1].key = 2;
  pair[1].parent = 1;
  pair[1].updated_ms = 99999;
  uint64_t seen[2];
  TEST_ASSERT(focus_current_query(4, &window, 1, pair, 2, NULL, 0, 0, 2, seen,
                                  2, &chosen) == 1);
  TEST_ASSERT(seen[0] == 1 && chosen == 1);
  pair[0].parent = 3;
  TEST_ASSERT(focus_current_query(4, &window, 1, pair, 2, NULL, 0, 0, 0, seen,
                                  2, &chosen) == 0);
  TEST_ASSERT(chosen == 0);
  // The next query must observe the new window list, including no match.
  window.pid = getpid();
  window.id = 9;
  TEST_ASSERT(focus_current_query(4, &window, 1, &session, 1, NULL, 0, 0, 0,
                                  keys, 1, &chosen) == 0);
  TEST_ASSERT(chosen == 0);
}

static void test_query_choices(void) {
  focus_window_t windows[2] = {
      {.id = 4, .pid = getpid() },
      {.id = 8, .pid = getppid()}
  };
  agent_session_view_t sessions[3] = {split_session(1, getpid(), 100, 10, 20),
                                      split_session(2, getpid(), 200, 11, 20),
                                      split_session(3, getppid(), 100, 10, 30)};
  for (int focus = 0; focus < 10; focus++) {
    for (int split = 0; split < 4; split++) {
      focus_pane_t panes[2] = {
          {100, split == 1 ? 10 : 99},
          {200, 11                  }
      };
      size_t pane_count = split == 0 ? 0 : split == 3 ? 2 : 1;
      for (uint64_t click = 0; click < 5; click++) {
        for (size_t cap = 0; cap <= 3; cap++) {
          uint64_t before[3] = {0}, after[3] = {0}, chosen = 99;
          uint64_t expected =
              focus_current_choose((uint64_t)focus, windows, 2, sessions, 3,
                                   panes, pane_count, 4, click);
          int seen = focus_current_seen((uint64_t)focus, windows, 2, sessions,
                                        3, panes, pane_count, before, cap);
          TEST_ASSERT(focus_current_query((uint64_t)focus, windows, 2, sessions,
                                          3, panes, pane_count, 4, click, after,
                                          cap, &chosen) == seen);
          TEST_ASSERT(chosen == expected);
          TEST_ASSERT(!memcmp(before, after, sizeof(before)));
        }
      }
    }
  }
  uint64_t chosen = 99;
  TEST_ASSERT(focus_watch_query(sessions, 3, NULL, 0, &chosen) == 0);
  TEST_ASSERT(chosen == 0);
  TEST_ASSERT(focus_current_query(4, windows, 2, sessions, 3, NULL, 0, 0, 0,
                                  NULL, 0, NULL) == 0);
}

int main(void) {
  test_events();
  test_match();
  test_pane_request();
  test_current_session();
  test_window_titles();
  test_stream_down();
  test_query_ancestry();
  test_query_choices();
  TEST_ASSERT(!focus_watch_available());
  TEST_ASSERT(focus_watch_focused_id() == 0);
  TEST_ASSERT(focus_watch_focused_session(NULL, 0) == 0);
  return 0;
}
