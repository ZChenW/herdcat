#define _GNU_SOURCE
#include "core/agent_sessions.h"
#include "graphics/sign_names.h"
#include "graphics/text.h"
#include "platform/agent_watch.h"
#include "test_helpers.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char root[] = "/tmp/herdcat-stage24-XXXXXX";
static unsigned reads;
int __real_openat(int dir, const char *path, int flags, ...);
int __wrap_openat(int dir, const char *path, int flags, ...);
int __wrap_openat(int dir, const char *path, int flags, ...) {
  if (!strcmp(path, "stat"))
    reads++;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode_t mode = (mode_t)va_arg(args, int);
    va_end(args);
    return __real_openat(dir, path, flags, mode);
  }
  return __real_openat(dir, path, flags);
}
static void process(pid_t pid, pid_t parent) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%jd", root, (intmax_t)pid);
  TEST_ASSERT(mkdir(path, 0700) == 0 || errno == EEXIST);
  snprintf(path, sizeof(path), "%s/%jd/stat", root, (intmax_t)pid);
  FILE *f = fopen(path, "w");
  TEST_ASSERT(f);
  // Parentheses in comm must not confuse PPID extraction.
  fprintf(f, "%jd (odd ) ( agent) S %jd 1 1 0 0\n", (intmax_t)pid,
          (intmax_t)parent);
  TEST_ASSERT(fclose(f) == 0);
}
static void apply(uint64_t key, const char *agent, agent_event_t event,
                  pid_t pid, int64_t now) {
  TEST_ASSERT(agent_sessions_apply(key, agent, event, pid, now, 5, NULL) == 0);
}
static agent_session_view_t view(uint64_t key) {
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++)
    if (views[i].key == key)
      return views[i];
  TEST_ASSERT(false);
  return (agent_session_view_t){0};
}
static void add(uint64_t key, const char *agent, pid_t pid, pid_t parent) {
  process(pid, parent);
  apply(key, agent, AGENT_EVENT_WORKING, pid, 1000);
  agent_sessions_process(key, pid, false, root);
}
static void requests(void) {
  const char *valid[] = {"ev codex working 123456789abcdef0 0",
                         "ev codex start 123456789abcdef0 0 200 1",
                         "ev codex done 123456789abcdef0 200 200 0"};
  char agent[9];
  uint64_t key;
  agent_event_t event;
  pid_t pid, candidate;
  bool metadata;
  for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
    TEST_ASSERT(agent_event_request(valid[i], &key, agent, &event, &pid,
                                    &candidate, &metadata));
    TEST_ASSERT(key == UINT64_C(0x123456789abcdef0) && !strcmp(agent, "codex"));
    TEST_ASSERT(metadata == (i == 1));
    TEST_ASSERT(candidate == (i ? 200 : 0));
  }
  const char *invalid[] = {"ev codex start 0000000000000000 1",
                           "ev codex start 123456789abcdef0 4194305",
                           "ev codex start 123456789abcdef0 0 1 1",
                           "ev codex start 123456789abcdef0 0 200 2",
                           "ev codex start 123456789abcdef0 0 200 10",
                           "ev codex start 123456789abcdef0 0 200 -1",
                           "ev codex start 123456789abcdef0 0 200 1 extra",
                           "ev codex start 123456789abcdef0 0 -200 1",
                           "ev codex invalid 123456789abcdef0 200"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    TEST_ASSERT(!agent_event_request(invalid[i], &key, agent, &event, &pid,
                                     &candidate, &metadata));
}
static void ancestry(void) {
  agent_sessions_reset();
  add(1, "claude", 100, 1);
  add(2, "codex", 200, 100);
  TEST_ASSERT(view(2).parent == 1);
  add(3, "kimi", 300, 200);
  TEST_ASSERT(view(3).parent == 1 && view(1).child_count == 2);
  unsigned before = reads;
  for (int i = 0; i < 100; i++) {
    apply(2, "codex", AGENT_EVENT_WORKING, 200, 1000 + i);
    agent_sessions_process(2, 200, false, root);
  }
  TEST_ASSERT(reads == before);
  // Shell-only ancestry remains independent, including metadata retries.
  add(4, "codex", 400, 50);
  TEST_ASSERT(!view(4).parent);
  before = reads;
  agent_sessions_process(4, 400, false, root);
  TEST_ASSERT(reads == before);
  agent_sessions_process(4, 400, true, root);
  TEST_ASSERT(reads > before && !view(4).parent);
  // An unknown top level appears later. Tool events cannot merge it.
  agent_sessions_reset();
  add(3, "kimi", 300, 200);
  add(2, "codex", 200, 100);
  agent_sessions_process(3, 300, true, root);
  TEST_ASSERT(view(3).parent == 2);
  add(1, "claude", 100, 1);
  agent_sessions_process(2, 200, false, root);
  TEST_ASSERT(!view(2).parent);
  agent_sessions_process(2, 200, true, root);
  TEST_ASSERT(view(2).parent == 1 && view(3).parent == 1);
  // No-PID parents and the reserved manual row cannot own a child.
  agent_sessions_reset();
  apply(1, "claude", AGENT_EVENT_START, 0, 1000);
  add(2, "codex", 200, 100);
  TEST_ASSERT(!view(2).parent);
  apply(0, "manual", AGENT_EVENT_WORKING, 100, 1000);
  agent_sessions_process(2, 200, true, root);
  TEST_ASSERT(!view(2).parent);
  // A headless child keeps its actual PID; its fallback was zero.
  agent_sessions_reset();
  add(1, "claude", 100, 1);
  apply(2, "codex", AGENT_EVENT_WORKING, 0, 1000);
  agent_sessions_process(2, 200, false, root);
  TEST_ASSERT(view(2).parent == 1 && view(2).pid == 200);
  // A same-type headless child must not alias the parent's fallback PID.
  agent_sessions_reset();
  add(1, "codex", 100, 1);
  apply(1, "codex", AGENT_EVENT_IDLE, 100, 1000);
  TEST_ASSERT(agent_sessions_apply_process(2, "codex", AGENT_EVENT_START, 100,
                                           200, true, root, 1000, 5) == 0);
  TEST_ASSERT(view(1).pid == 100 && view(1).state == AGENT_STATE_IDLE);
  TEST_ASSERT(view(2).parent == 1 && view(2).pid == 200);
  before = reads;
  for (int i = 0; i < 100; i++)
    TEST_ASSERT(agent_sessions_apply_process(2, "codex", AGENT_EVENT_WORKING,
                                             100, 200, false, root, 2000 + i,
                                             5) == 0);
  TEST_ASSERT(reads == before && view(2).pid == 200);
  // An unrelated background service still aliases its terminal fallback.
  TEST_ASSERT(agent_sessions_apply_process(3, "codex", AGENT_EVENT_WORKING, 100,
                                           400, false, root, 2000, 5) == 0);
  TEST_ASSERT(agent_sessions_count() == 2 && !view(1).parent);
  // Exactly 32 edges are allowed. A 33rd edge is never read.
  agent_sessions_reset();
  add(1, "claude", 100, 1);
  for (int i = 0; i < 33; i++)
    process(1000 + i, i == 32 ? 100 : 1001 + i);
  apply(2, "codex", AGENT_EVENT_START, 1001, 1000);
  before = reads;
  agent_sessions_process(2, 1001, false, root);
  TEST_ASSERT(view(2).parent == 1 && reads - before == 32);
  apply(2, "codex", AGENT_EVENT_END, 0, 1000);
  apply(3, "kimi", AGENT_EVENT_START, 1000, 1000);
  before = reads;
  agent_sessions_process(3, 1000, false, root);
  TEST_ASSERT(!view(3).parent && reads - before == 32);
  // A corrupt cycle must terminate and cannot adopt its own process.
  process(500, 501);
  process(501, 500);
  apply(4, "codex", AGENT_EVENT_START, 500, 1000);
  before = reads;
  agent_sessions_process(4, 500, false, root);
  TEST_ASSERT(!view(4).parent && reads - before == 2);
}
static void labels(void) {
  static const char *const expected[] = {
      "Claude + Codex",     "Claude + Codex + Kimi",
      "Claude + Codex ×2",  "Claude + Codex ×2 + Kimi",
      "Claude + Codex + 2", "Claude + Codex ×2 + 3"};
  static const char *const agents[][5] = {
      {"codex"},
      {"codex", "kimi"},
      {"codex", "codex"},
      {"codex", "codex", "kimi"},
      {"codex", "kimi", "pi"},
      {"codex", "kimi", "codex", "pi", "kimi"}
  };
  TEST_ASSERT(text_init("Noto Sans") == 0);
  for (int row = 0; row < 6; row++) {
    agent_sessions_reset();
    add(1, "claude", 100, 1);
    for (int i = 0; i < 5 && agents[row][i]; i++)
      add((uint64_t)i + 2, agents[row][i], 200 + i, 100);
    agent_session_view_t parent = view(1);
    char label[64];
    sign_agent_label(&parent, false, label);
    TEST_ASSERT(!strcmp(label, expected[row]));
    sign_agent_label(&parent, true, label);
    char count[32];
    snprintf(count, sizeof(count), "Claude +%u", parent.child_count);
    TEST_ASSERT(!strcmp(label, count));
    agent_session_view_t all[AGENT_SESSIONS_MAX], selected[5];
    int n = agent_sessions_snapshot(all, AGENT_SESSIONS_MAX);
    TEST_ASSERT(agent_sessions_select(all, (size_t)n, selected, 5) == 1);
    TEST_ASSERT(selected[0].key == 1);
    for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
      signs_t model = {0};
      sign_frame_t frame;
      sign_input_t in = {.sessions = all,
                         .count = (size_t)n,
                         .style = (sign_style_t)style,
                         .animations = SIGN_ANIM_OFF,
                         .idle = SIGN_IDLE_ALWAYS,
                         .open = true,
                         .has_hover = true,
                         .hover_key = 1,
                         .cat_x = 200,
                         .cat_y = 220,
                         .cat_height = 110,
                         .surface_width = 1000,
                         .surface_height = 500,
                         .now_ms = 181000};
      strcpy(in.nameplate, "{agent} · **{name}**");
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(frame.hit_count == 1 && frame.hits[0].key == 1);
      TEST_ASSERT(frame.text_count == 1);
      if (style == SIGN_STYLE_FAN)
        TEST_ASSERT(strstr(frame.texts[0].nameplate.text, expected[row]));
      else {
        TEST_ASSERT(strstr(frame.texts[0].meta, count));
        TEST_ASSERT(strstr(frame.texts[0].meta, "3"));
      }
    }
    apply(2, "codex", AGENT_EVENT_DONE, 200, 2000);
    TEST_ASSERT(!view(2).unread);
    TEST_ASSERT(view(1).child_count == parent.child_count - 1);
    for (int i = 0; i < 5 && agents[row][i]; i++)
      apply((uint64_t)i + 2, agents[row][i], AGENT_EVENT_DONE, 200 + i, 2000);
    parent = view(1);
    sign_agent_label(&parent, false, label);
    TEST_ASSERT(!strcmp(label, "Claude"));
    TEST_ASSERT(parent.state == AGENT_STATE_WORKING && !parent.unread);
  }
  text_cleanup();
}
static void lifecycle(void) {
  const uint64_t parent = UINT64_C(0x123456789abcdef0);
  agent_sessions_reset();
  add(parent, "claude", 100, 1);
  apply(parent, "claude", AGENT_EVENT_IDLE, 100, 1000);
  add(2, "codex", 200, 100);
  apply(2, "codex", AGENT_EVENT_WAITING, 200, 2000);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  TEST_ASSERT(!agent_sessions_answer(2, 3000, 60));
  apply(2, "codex", AGENT_EVENT_FAIL, 200, 3000);
  TEST_ASSERT(!view(2).unread && agent_sessions_resolve() == AGENT_STATE_IDLE);
  TEST_ASSERT(agent_sessions_set_title(2, "child title") == 0);
  TEST_ASSERT(agent_sessions_set_prompt(2, "child prompt") == 0);
  TEST_ASSERT(!view(2).title[0] && !view(parent).title[0]);
  char output[4096];
  TEST_ASSERT(agent_sessions_format(output, sizeof(output), 3000) > 0);
  TEST_ASSERT(strstr(output, "parent=12345678"));
  TEST_ASSERT(!agent_sessions_expire(7999, 60));
  TEST_ASSERT(agent_sessions_expire(8000, 60));
  TEST_ASSERT(agent_sessions_count() == 1);
  add(2, "codex", 200, 100);
  TEST_ASSERT(agent_sessions_expire(61000, 60));
  TEST_ASSERT(agent_sessions_count() == 1);
  add(2, "codex", 200, 100);
  apply(2, "codex", AGENT_EVENT_END, 0, 2000);
  TEST_ASSERT(agent_sessions_count() == 1 && !view(parent).child_count);
  add(2, "codex", 200, 100);
  agent_sessions_set_provisional(parent);
  apply(4, "claude", AGENT_EVENT_START, 100, 2000);
  TEST_ASSERT(view(2).parent == 4);  // key adoption keeps ownership
  agent_sessions_remove_pid(100);
  TEST_ASSERT(agent_sessions_count() == 1 && view(2).parent == 4);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  agent_sessions_remove_pid(200);
  TEST_ASSERT(agent_sessions_count() == 0);
  // Actual kernel pidfd readiness removes a child without a socket/timer.
  agent_sessions_reset();
  int pipefd[2];
  TEST_ASSERT(pipe(pipefd) == 0);
  pid_t child = fork();
  TEST_ASSERT(child >= 0);
  if (!child) {
    close(pipefd[1]);
    char byte;
    ssize_t n = read(pipefd[0], &byte, 1);
    _exit(n < 0 ? 1 : 0);
  }
  close(pipefd[0]);
  add(1, "claude", getpid(), 1);
  add(2, "codex", child, getpid());
  TEST_ASSERT(view(2).parent == 1);
  TEST_ASSERT(agent_watch_init() == 0 && agent_watch_add(child) == 0);
  agent_sessions_set_watched(2, true);
  close(pipefd[1]);
  struct pollfd fd = {.fd = agent_watch_fd(), .events = POLLIN};
  TEST_ASSERT(poll(&fd, 1, 2000) == 1);
  agent_watch_process(agent_sessions_remove_pid);
  TEST_ASSERT(agent_sessions_count() == 1 && !view(1).child_count);
  TEST_ASSERT(waitpid(child, NULL, 0) == child);
  agent_watch_cleanup();
  char path[256];
  snprintf(path, sizeof(path), "%s/%jd/stat", root, (intmax_t)child);
  TEST_ASSERT(unlink(path) == 0);
  snprintf(path, sizeof(path), "%s/%jd", root, (intmax_t)child);
  TEST_ASSERT(rmdir(path) == 0);
}
static void cleanup(void) {
  // Only the known fake PID directories this test created.
  const pid_t pids[] = {100, 200, 201, 202, 203,     204,
                        300, 400, 500, 501, getpid()};
  char path[256];
  for (size_t i = 0; i < sizeof(pids) / sizeof(pids[0]); i++) {
    snprintf(path, sizeof(path), "%s/%jd/stat", root, (intmax_t)pids[i]);
    unlink(path);
    snprintf(path, sizeof(path), "%s/%jd", root, (intmax_t)pids[i]);
    rmdir(path);
  }
  for (int i = 0; i < 33; i++) {
    snprintf(path, sizeof(path), "%s/%d/stat", root, 1000 + i);
    unlink(path);
    snprintf(path, sizeof(path), "%s/%d", root, 1000 + i);
    rmdir(path);
  }
  // The real fork PID is cleaned in lifecycle().
  rmdir(root);
}
int main(void) {
  TEST_ASSERT(mkdtemp(root));
  requests();
  ancestry();
  labels();
  lifecycle();
  cleanup();
  puts("Stage 24 ancestry, labels, alerts, lifetimes and pidfd passed.");
  return 0;
}
