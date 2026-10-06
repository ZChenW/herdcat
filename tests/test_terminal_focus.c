#define _GNU_SOURCE
#include "core/agent_sessions.h"
#include "platform/agent_terminal.h"
#include "platform/focus.h"
#include "platform/focus_current.h"
#include "platform/focus_watch.h"
#include "test_helpers.h"

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static agent_terminal_t record;
static unsigned notes;
static bool kitty_mode;
static bool deferred_current;
static bool server_notification;
static bool lookup(pid_t pid, agent_terminal_t *out, char *name,
                   size_t capacity) {
  if (server_notification)
    return agent_sessions_terminal(pid, out, name, capacity);
  (void)pid;
  *out = record;
  if (kitty_mode)
    out->kind = TERMINAL_KITTY;
  if (name && capacity)
    snprintf(name, capacity, "repo \"a\"");
  return record.kind != TERMINAL_NONE;
}
static void note(pid_t pid, const agent_terminal_t *out) {
  (void)pid;
  TEST_ASSERT(out->detached ==
              (out->kind == TERMINAL_TMUX &&
               !strcmp(getenv("TERMINAL_TEST_MODE"), "detached")));
  record = *out;
  if (server_notification)
    agent_sessions_terminal_resolved(pid, out);
  notes++;
  if (deferred_current && out->kind == TERMINAL_WEZTERM && out->title[0] &&
      !out->window) {
    deferred_current = false;
    focus_wezterm_current(pid, 222);
  }
}
static void current(pid_t pid, uint64_t window, const char *socket,
                    const focus_wezterm_pane_t *panes, size_t count) {
  (void)pid;
  TEST_ASSERT(!strcmp(socket, record.socket));
  record.window = window;
  record.current_known = count > 0;
  for (size_t i = 1; i < count; i++)
    if (panes[i].pane != panes[0].pane)
      record.current_known = false;
  record.current_pane = count ? panes[0].pane : 0;
  notes++;
}
static bool kitty(pid_t pid, uint64_t *window, char *listen, size_t capacity) {
  (void)pid;
  if (!kitty_mode)
    return false;
  *window = 7;
  snprintf(listen, capacity, "unix:/tmp/kitty-fixture");
  return true;
}
static void drain(void) {
  while (focus_timeout() >= 0) {
    struct pollfd fd = {.fd = focus_poll_fd(), .events = POLLIN};
    poll(&fd, 1, focus_timeout());
    focus_poll();
  }
}
static void test_titles(void) {
  focus_window_t windows[] = {
      {.id = 1, .pid = 42, .title = "prefix repo suffix"},
      {.id = 2, .pid = 42, .title = "[2/3] repo"        },
      {.id = 3, .pid = 99, .title = "repo"              }
  };
  uint64_t id = 0;
  TEST_ASSERT(focus_pick_window(42, windows, 1, "missing", false, &id) &&
              id == 1);
  TEST_ASSERT(focus_pick_window(42, windows, 3, "repo", false, &id) && id == 2);
  strcpy(windows[1].title, "repo");
  TEST_ASSERT(focus_pick_window(42, windows, 3, "repo", false, &id) && id == 2);
  strcpy(windows[1].title, "repo - agent");
  TEST_ASSERT(focus_pick_window(42, windows, 3, "repo", false, &id) && id == 2);
  strcpy(windows[1].title, "agent - repo");
  TEST_ASSERT(focus_pick_window(42, windows, 3, "repo", false, &id) && id == 2);
  TEST_ASSERT(focus_pick_window(42, windows, 3, "absent", false, &id) &&
              id == 1);
  TEST_ASSERT(focus_pick_window(42, windows, 3, "", false, &id) && id == 1);
  TEST_ASSERT(focus_pick_window(42, windows, 3, NULL, false, &id) && id == 1);
  TEST_ASSERT(!focus_pick_window(100, windows, 3, "repo", false, &id));
  strcpy(windows[0].title, "unrelated");
  TEST_ASSERT(focus_pick_window(42, windows, 3, "repo", true, &id) && id == 2);
  strcpy(windows[0].title, "repo too");
  TEST_ASSERT(focus_pick_window(42, windows, 3, "repo", true, &id) && id == 1);
  const char json[] = "[{\"id\":1,\"pid\":42,\"title\":\"\\u2733 \\\"repo\\\" "
                      "\\ud83d\\ude3a\"}]";
  TEST_ASSERT(focus_parse_windows(json, strlen(json), windows, 3) == 1);
  TEST_ASSERT(!strcmp(windows[0].title, "✳ \"repo\" 😺"));
  focus_watch_event_t event;
  const char upsert[] = "{\"WindowOpenedOrChanged\":{\"window\":{\"id\":1,"
                        "\"pid\":42,\"title\":\"\\u2733 repo\"}}}";
  TEST_ASSERT(focus_watch_parse(upsert, strlen(upsert), &event, windows, 3) ==
              1);
  TEST_ASSERT(event.resting && !strcmp(event.title, "✳ repo"));
}
static void test_identifiers(void) {
  uint64_t pane = 99;
  TEST_ASSERT(agent_terminal_number("%0", true, &pane) && pane == 0);
  TEST_ASSERT(agent_terminal_number("%19", true, &pane) && pane == 19);
  TEST_ASSERT(agent_terminal_number("0", false, &pane) && pane == 0);
  TEST_ASSERT(agent_terminal_number("18446744073709551615", false, &pane));
  const char *bad[] = {"",    "%", "%+1", "%1x",
                       "% 1", "1", "%-1", "%18446744073709551616"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    TEST_ASSERT(!agent_terminal_number(bad[i], true, &pane));
  const char *digits[] = {"",    "-1",  "+1", "1x",
                          "1 2", "1\n", "%1", "18446744073709551616"};
  for (size_t i = 0; i < sizeof(digits) / sizeof(digits[0]); i++)
    TEST_ASSERT(!agent_terminal_number(digits[i], false, &pane));
  char path[] = "/tmp/herdcat-terminal-socket-XXXXXX";
  int fd = mkstemp(path);
  TEST_ASSERT(fd >= 0);
  close(fd);
  TEST_ASSERT(agent_terminal_socket_ok(path));
  TEST_ASSERT(!agent_terminal_socket_ok("relative"));
  TEST_ASSERT(!agent_terminal_socket_ok("/tmp/../tmp"));
  TEST_ASSERT(!agent_terminal_socket_ok("/tmp/./"));
  TEST_ASSERT(!agent_terminal_socket_ok("/tmp//test"));
  TEST_ASSERT(!agent_terminal_socket_ok("/tmp/nonexistent-herdcat-socket"));
  TEST_ASSERT(!agent_terminal_socket_ok("/tmp/bad\n"));
  char link[160];
  snprintf(link, sizeof(link), "%s-link", path);
  TEST_ASSERT(symlink(path, link) == 0);
  TEST_ASSERT(!agent_terminal_socket_ok(link));
  unlink(link);
  char over[160];
  memset(over, 'a', sizeof(over));
  over[0] = '/';
  over[159] = 0;
  TEST_ASSERT(!agent_terminal_socket_ok(over));
  char env[512];
  int n =
      snprintf(env, sizeof(env), "TMUX=%s,1234,0%cTMUX_PANE=%%0%c", path, 0, 0);
  agent_terminal_t t;
  TEST_ASSERT(n > 0 && agent_terminal_parse_all(env, (size_t)n, &t));
  TEST_ASSERT(t.kind == TERMINAL_TMUX && t.pane == 0 &&
              !strcmp(t.socket, path));
  char message[384];
  TEST_ASSERT(agent_terminal_message(message, sizeof(message), 1, &t));
  uint64_t key = 0;
  agent_terminal_t restored;
  TEST_ASSERT(agent_terminal_request(message, &key, &restored));
  TEST_ASSERT(key == 1 && !strcmp(restored.socket, path));
  TEST_ASSERT(
      agent_terminal_pane_message(message, sizeof(message), getpid(), 0, &t));
  TEST_ASSERT(agent_terminal_pane_request(message, &restored));
  TEST_ASSERT(restored.client_pid == getpid() && restored.pane == 0 &&
              !strcmp(restored.socket, path));
  TEST_ASSERT(!agent_terminal_pane_message(message, 5, getpid(), 0, &t));
  TEST_ASSERT(!agent_terminal_pane_request("pane 1 0 tmux 2f", &restored));
  TEST_ASSERT(!agent_terminal_pane_request("pane 2 0 tmux -1", &restored));
  TEST_ASSERT(!agent_terminal_request("term 0000000000000001 tmux 0 -1", &key,
                                      &restored));
  snprintf(env, sizeof(env), "%s,1234,0", path);
  setenv("TMUX", env, 1);
  unsetenv("TMUX_PANE");
  TEST_ASSERT(agent_terminal_tmux_message(message, sizeof(message)));
  TEST_ASSERT(agent_terminal_tmux_request(message, &restored));
  TEST_ASSERT(!strcmp(restored.socket, path));
  TEST_ASSERT(!agent_terminal_tmux_request("tmux 00", &restored));
  TEST_ASSERT(!agent_terminal_tmux_request("tmux 2f trailing", &restored));
  TEST_ASSERT(!agent_terminal_tmux_message(message, 5));
  unsetenv("TMUX");
  TEST_ASSERT(!agent_terminal_tmux_message(message, sizeof(message)));
  n = snprintf(env, sizeof(env), "WEZTERM_PANE=0%cWEZTERM_UNIX_SOCKET=%s%c", 0,
               path, 0);
  TEST_ASSERT(agent_terminal_parse_all(env, (size_t)n, &t));
  TEST_ASSERT(t.kind == TERMINAL_WEZTERM && t.pane == 0);
  TEST_ASSERT(!agent_terminal_parse_all(env, (size_t)n - 1, &t));
  const char ghostty[] = "TERM_PROGRAM=ghostty\0";
  TEST_ASSERT(agent_terminal_parse_all(ghostty, sizeof(ghostty), &t));
  TEST_ASSERT(t.kind == TERMINAL_GHOSTTY);
  unlink(path);
}
static void test_json_and_clients(void) {
  focus_wezterm_pane_t panes[4];
  const char list[] = "[{\"pane_id\":0,\"window_id\":10,\"title\":\"repo "
                      "\\\"a\\\"\",\"extra\":{\"pane_id\":9}}]";
  TEST_ASSERT(focus_parse_wezterm(list, strlen(list), false, panes, 4) == 1);
  TEST_ASSERT(panes[0].pane == 0 && panes[0].has_window &&
              panes[0].window == 10);
  TEST_ASSERT(!strcmp(panes[0].title, "repo \"a\""));
  for (size_t i = 0; i < strlen(list); i++)
    TEST_ASSERT(focus_parse_wezterm(list, i, false, panes, 4) == -1);
  const char clients[] = "[{\"focused_pane_id\":null},{\"focused_pane_id\":0}]";
  TEST_ASSERT(focus_parse_wezterm(clients, strlen(clients), true, panes, 4) ==
              1);
  TEST_ASSERT(panes[0].pane == 0);
  const char *bad[] = {"[{}]", "[{\"pane_id\":1,\"pane_id\":2}]",
                       "[{\"pane_id\":-1}]", "[]x",
                       "[{\"pane_id\":0,\"title\":1}]"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    TEST_ASSERT(focus_parse_wezterm(bad[i], strlen(bad[i]), false, panes, 4) ==
                -1);
  pid_t pid;
  char tty[64];
  TEST_ASSERT(
      focus_tmux_client("12 test session /dev/pts/1 10\n13 other /dev/pts/2 "
                        "99\n14 test session /dev/pts/3 20\n",
                        "test session", &pid, tty, sizeof(tty)));
  TEST_ASSERT(pid == 14 && !strcmp(tty, "/dev/pts/3"));
  // No client shows that session: the most recently used one is switched.
  TEST_ASSERT(focus_tmux_client("12 a /dev/pts/1 10\n13 b /dev/pts/2 99\n",
                                "test session", &pid, tty, sizeof(tty)));
  TEST_ASSERT(pid == 13 && !strcmp(tty, "/dev/pts/2"));
  // An attached client wins over a more recent one elsewhere.
  TEST_ASSERT(focus_tmux_client("12 test session /dev/pts/1 10\n13 b "
                                "/dev/pts/2 99\n",
                                "test session", &pid, tty, sizeof(tty)));
  TEST_ASSERT(pid == 12);
  TEST_ASSERT(!focus_tmux_client("", "test session", &pid, tty, sizeof(tty)));
  TEST_ASSERT(!focus_tmux_client("-1 test session /dev/pts/1 10",
                                 "test session", &pid, tty, sizeof(tty)));
}
static void test_current(void) {
  focus_window_t window = {.id = 4, .pid = getpid()};
  agent_session_view_t sessions[2] = {
      {.key = 1,
       .pid = 4321,
       .updated_ms = 10,
       .terminal = {.kind = TERMINAL_TMUX, .pane = 0}},
      {.key = 2,
       .pid = 4322,
       .updated_ms = 20,
       .terminal = {.kind = TERMINAL_TMUX, .pane = 1}}
  };
  sessions[0].terminal.client_pid = sessions[1].terminal.client_pid = getpid();
  pid_t pid;
  uint64_t pane;
  char pid_text[32];
  snprintf(pid_text, sizeof(pid_text), "%jd", (intmax_t)getpid());
  TEST_ASSERT(focus_tmux_pane_fields(pid_text, "0", &pid, &pane));
  TEST_ASSERT(!focus_pane_fields(pid_text, "0", &pid, &pane));
  focus_pane_reset();
  TEST_ASSERT(focus_tmux_pane_set(getpid(), 0));
  uint64_t keys[2], chosen;
  TEST_ASSERT(focus_current_query(4, &window, 1, sessions, 2, NULL, 0, 0, 0,
                                  keys, 2, &chosen) == 1);
  TEST_ASSERT(chosen == 1 && keys[0] == 1);
  sessions[0].terminal.outer_kitty_pid = 999;
  sessions[0].terminal.outer_kitty_pane = 7;
  focus_pane_t outer = {.pid = 999, .split = 8};
  TEST_ASSERT(focus_current_query(4, &window, 1, sessions, 2, &outer, 1, 0, 0,
                                  keys, 2, &chosen) == 0 &&
              chosen == 0);
  outer.split = 7;
  TEST_ASSERT(focus_current_query(4, &window, 1, sessions, 2, &outer, 1, 0, 0,
                                  keys, 2, &chosen) == 1 &&
              chosen == 1);
  TEST_ASSERT(focus_tmux_pane_set(getpid(), 99));
  TEST_ASSERT(focus_current_query(4, &window, 1, sessions, 2, NULL, 0, 0, 0,
                                  keys, 2, &chosen) == 0 &&
              chosen == 0);
  for (int i = 0; i < 2; i++) {
    sessions[i].pid = getpid();
    sessions[i].terminal.kind = TERMINAL_WEZTERM;
    sessions[i].terminal.window = 4;
    sessions[i].terminal.current_known = true;
    sessions[i].terminal.current_pane = 0;
  }
  TEST_ASSERT(focus_current_query(4, &window, 1, sessions, 2, NULL, 0, 0, 0,
                                  keys, 2, &chosen) == 1 &&
              chosen == 1);
  sessions[0].terminal.current_known = sessions[1].terminal.current_known =
      false;
  TEST_ASSERT(focus_current_query(4, &window, 1, sessions, 2, NULL, 0, 0, 0,
                                  keys, 2, &chosen) == 0 &&
              chosen == 0);
  focus_pane_reset();
}
static void test_tmux_clients(void) {
  focus_window_t windows[2] = {
      {.id = 4, .pid = 42},
      {.id = 8, .pid = 43}
  };
  agent_session_view_t sessions[2] = {
      {.key = 1,
       .pid = 4321,
       .terminal = {.kind = TERMINAL_TMUX, .pane = 0, .socket = "/server-one"}},
      {.key = 2,
       .pid = 4322,
       .terminal = {
           .kind = TERMINAL_TMUX, .pane = 1, .socket = "/server-one"}         }
  };
  focus_pane_reset();
  TEST_ASSERT(focus_tmux_pane_set_socket(42, 0, "/server-one"));
  TEST_ASSERT(focus_tmux_pane_set_socket(43, 1, "/server-one"));
  uint64_t keys[2], chosen;
  TEST_ASSERT(focus_current_query(4, windows, 2, sessions, 2, NULL, 0, 0, 0,
                                  keys, 2, &chosen) == 1 &&
              chosen == 1);
  TEST_ASSERT(focus_current_query(8, windows, 2, sessions, 2, NULL, 0, 0, 0,
                                  keys, 2, &chosen) == 1 &&
              chosen == 2);
  // Pane numbers from another server must not select this server's row.
  TEST_ASSERT(focus_tmux_pane_set_socket(43, 0, "/server-two"));
  TEST_ASSERT(focus_current_query(8, windows, 2, sessions, 2, NULL, 0, 0, 0,
                                  keys, 2, &chosen) == 0 &&
              chosen == 0);
  // The next query observes new PID/window ownership, without a new report.
  windows[0].pid = 44;
  windows[1].pid = 42;
  TEST_ASSERT(focus_current_query(8, windows, 2, sessions, 2, NULL, 0, 0, 0,
                                  keys, 2, &chosen) == 1 &&
              chosen == 1);
  focus_pane_reset();
}
static void test_wezterm_mapping(void) {
  focus_window_t windows[2] = {
      {.id = 111, .pid = getpid(), .title = "old"  },
      {.id = 222, .pid = getpid(), .title = "fresh"}
  };
  agent_session_view_t sessions[3] = {0};
  for (int i = 0; i < 3; i++) {
    sessions[i].pid = getpid();
    sessions[i].terminal.kind = TERMINAL_WEZTERM;
    sessions[i].terminal.pane = (uint64_t)i;
    sessions[i].terminal.native_window_known = true;
    sessions[i].terminal.native_window = i == 0 ? 10 : 20;
    sessions[i].terminal.window = i == 0 ? 111 : 222;
    snprintf(sessions[i].terminal.socket, sizeof(sessions[i].terminal.socket),
             "/fixture");
  }
  agent_terminal_t reply = sessions[0].terminal;
  strcpy(reply.title, "fresh");
  focus_current_wezterm_resolve(getpid(), &reply, windows, 2, sessions, 3);
  TEST_ASSERT(!strcmp(sessions[0].terminal.title, "fresh"));
  // The querying process can belong to a different mux window from the
  // current pane. Only the reported pane's mux group moves to niri 222.
  reply.current_known = true;
  reply.current_pane = 1;
  reply.window = 222;
  sessions[2].terminal.window = 0;
  focus_current_wezterm_resolve(getpid(), &reply, windows, 2, sessions, 3);
  TEST_ASSERT(sessions[0].terminal.window == 111);
  TEST_ASSERT(sessions[1].terminal.window == 222 &&
              sessions[2].terminal.window == 222);
  TEST_ASSERT(sessions[1].terminal.current_known &&
              sessions[2].terminal.current_pane == 1);
  // Registering a new row in the same mux window reuses that observation.
  reply = sessions[2].terminal;
  reply.window = 0;
  reply.current_known = false;
  sessions[2].terminal.window = 0;
  sessions[2].terminal.current_known = false;
  focus_current_wezterm_resolve(getpid(), &reply, windows, 2, sessions, 3);
  TEST_ASSERT(sessions[2].terminal.window == 222 &&
              sessions[2].terminal.current_known &&
              sessions[2].terminal.current_pane == 1);
  focus_wezterm_pane_t clients[2] = {{.pane = 0}, {.pane = 1}};
  uint64_t pane;
  TEST_ASSERT(focus_current_wezterm_pane(222, "/fixture", sessions, 3, clients,
                                         2, &pane) &&
              pane == 1);
  sessions[0].terminal.window = 222;
  TEST_ASSERT(!focus_current_wezterm_pane(222, "/fixture", sessions, 3, clients,
                                          2, &pane));
}
static void test_session_records(void) {
  agent_sessions_reset();
  TEST_ASSERT(agent_sessions_apply(1, "claude", AGENT_EVENT_WORKING, getpid(),
                                   1000, 5, NULL) == 0);
  agent_sessions_set_kitty(1, 123, 7, "unix:/fixture");
  agent_terminal_t t = {.kind = TERMINAL_NONE};
  TEST_ASSERT(!agent_sessions_terminal(getpid(), &t, NULL, 0));
  TEST_ASSERT(t.kind == TERMINAL_NONE);
  TEST_ASSERT(agent_sessions_apply(2, "claude", AGENT_EVENT_WORKING, getpid(),
                                   1000, 5, NULL) == 0);
  uint64_t generation = agent_sessions_generation();
  t = (agent_terminal_t){.kind = TERMINAL_TMUX,
                         .pane = 0,
                         .socket = "/fixture",
                         .title = "memory only"};
  agent_sessions_set_terminal(2, &t);
  TEST_ASSERT(agent_sessions_terminal(getpid(), &t, NULL, 0));
  TEST_ASSERT(t.kind == TERMINAL_TMUX && !strcmp(t.title, "memory only"));
  TEST_ASSERT(agent_sessions_generation() == generation);
  t.detached = true;
  agent_sessions_terminal_resolved(getpid(), &t);
  TEST_ASSERT(agent_sessions_terminal(getpid(), &t, NULL, 0) && t.detached);
  pid_t pids[AGENT_SESSIONS_MAX];
  TEST_ASSERT(agent_sessions_tmux_pids("/fixture", pids, AGENT_SESSIONS_MAX) ==
              1);
  TEST_ASSERT(pids[0] == getpid());
  TEST_ASSERT(agent_sessions_tmux_pids("/other", pids, AGENT_SESSIONS_MAX) ==
              0);
  agent_sessions_tmux_attached("/other");
  TEST_ASSERT(agent_sessions_terminal(getpid(), &t, NULL, 0) && t.detached);
  agent_sessions_tmux_attached("/fixture");
  TEST_ASSERT(agent_sessions_terminal(getpid(), &t, NULL, 0) && !t.detached);
  TEST_ASSERT(agent_sessions_generation() == generation);
  agent_sessions_adopt(2);
  agent_session_record_t rows[AGENT_SESSIONS_MAX];
  int count = agent_sessions_export(rows, AGENT_SESSIONS_MAX);
  agent_sessions_reset();
  for (int i = 0; i < count; i++)
    TEST_ASSERT(agent_sessions_restore(&rows[i], 1000, 5) == 0);
  TEST_ASSERT(!agent_sessions_terminal(getpid(), &t, NULL, 0));
  t = (agent_terminal_t){
      .kind = TERMINAL_WEZTERM, .socket = "/fixture", .detached = true};
  agent_sessions_set_terminal(2, &t);
  TEST_ASSERT(agent_sessions_terminal(getpid(), &t, NULL, 0) && !t.detached);
  t = (agent_terminal_t){.kind = TERMINAL_TMUX, .socket = "/fixture"};
  agent_sessions_set_terminal(2, &t);
  agent_sessions_set_kitty(2, 123, 7, "unix:/fixture");
  uint64_t window;
  char socket[128];
  TEST_ASSERT(agent_sessions_kitty(getpid(), &window, socket, sizeof(socket)));
  TEST_ASSERT(window == 7 && !strcmp(socket, "unix:/fixture"));
  char formatted[4096];
  TEST_ASSERT(agent_sessions_format(formatted, sizeof(formatted), 1000) > 0);
  TEST_ASSERT(!strstr(formatted, "memory only"));
  TEST_ASSERT(agent_sessions_apply(2, "claude", AGENT_EVENT_WORKING,
                                   getpid() + 1, 2000, 5, NULL) == 0);
  t = (agent_terminal_t){.kind = TERMINAL_NONE};
  TEST_ASSERT(!agent_sessions_terminal(getpid() + 1, &t, NULL, 0));
  TEST_ASSERT(t.kind == TERMINAL_NONE);
  agent_sessions_reset();
}
int main(int argc, char **argv) {
  if (argc < 2) {
    test_titles();
    test_identifiers();
    test_json_and_clients();
    test_current();
    test_wezterm_mapping();
    test_tmux_clients();
    test_session_records();
    return 0;
  }
  TEST_ASSERT(argc == 4);
  kitty_mode = !strcmp(argv[1], "kitty");
  deferred_current = !strcmp(argv[3], "deferred");
  server_notification = !strcmp(argv[3], "notification");
  record.kind = kitty_mode                    ? TERMINAL_NONE
                : !strcmp(argv[1], "tmux")    ? TERMINAL_TMUX
                : !strcmp(argv[1], "wezterm") ? TERMINAL_WEZTERM
                                              : TERMINAL_GHOSTTY;
  TEST_ASSERT(strlen(argv[2]) < sizeof(record.socket));
  strcpy(record.socket, argv[2]);
  if (record.kind == TERMINAL_WEZTERM)
    record.window = 111;
  char pid[32];
  snprintf(pid, sizeof(pid), "%jd", (intmax_t)getpid());
  setenv("TERMINAL_TEST_PID", pid, 1);
  setenv("PYTHONDONTWRITEBYTECODE", "1", 1);
  focus_set_terminal(lookup, note);
  focus_set_current(current);
  focus_set_kitty(kitty);
  focus_test_available(true);
  if (server_notification) {
    bool detached = !strcmp(getenv("TERMINAL_TEST_MODE"), "detached");
    for (int i = 0; i < 3; i++) {
      uint64_t key = (uint64_t)i + 10;
      TEST_ASSERT(agent_sessions_apply(key, "claude", AGENT_EVENT_WORKING,
                                       getpid() + i, 1000, 5, NULL) == 0);
      agent_terminal_t t = record;
      t.detached = !detached;
      if (i == 2)
        strcpy(t.socket, "/other-server");
      agent_sessions_set_terminal(key, &t);
    }
    char value[256], message[384];
    snprintf(value, sizeof(value), "%s,1234,0", record.socket);
    setenv("TMUX", value, 1);
    TEST_ASSERT(agent_terminal_tmux_message(message, sizeof(message)));
    agent_terminal_t report;
    TEST_ASSERT(agent_terminal_tmux_request(message, &report));
    pid_t pids[AGENT_SESSIONS_MAX];
    int count =
        agent_sessions_tmux_pids(report.socket, pids, AGENT_SESSIONS_MAX);
    TEST_ASSERT(count == 2);
    for (int i = 0; i < count; i++)
      focus_terminal_resolve(pids[i]);
    drain();
    TEST_ASSERT(notes == 2);
    for (int i = 0; i < 3; i++) {
      agent_terminal_t t;
      TEST_ASSERT(agent_sessions_terminal(getpid() + i, &t, NULL, 0));
      TEST_ASSERT(t.detached == (i == 2 ? !detached : detached));
    }
    printf("%d %u\n", focus_take_result(), notes);
  } else if (!strcmp(argv[3], "current") || !strcmp(argv[3], "current-again")) {
    focus_wezterm_current(getpid(), 222);
    focus_wezterm_current(getpid(), 222);
    drain();
    if (!strcmp(argv[3], "current-again")) {
      usleep(510000);
      focus_wezterm_current(getpid(), 222);
      drain();
    }
    printf("%d %ju %u\n", record.current_known, (uintmax_t)record.current_pane,
           notes);
  } else if (!strcmp(argv[3], "background")) {
    focus_terminal_resolve(getpid());
    drain();
    printf("%d %u\n", focus_take_result(), notes);
  } else {
    TEST_ASSERT(focus_session_window(getpid()) == 0);
    drain();
    printf("%d\n", focus_take_result());
  }
  focus_cleanup();
  return 0;
}
