#define _GNU_SOURCE
#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "core/agent_transcript.h"
#include "platform/agent_watch.h"
#include "platform/transcript_watch.h"
#include "test_helpers.h"

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char MARKER[] =
    "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":["
    "{\"type\":\"text\",\"text\":\"[Request interrupted by user]\"}]}}";
static int64_t now;
static void ready(uint32_t token) {
  transcript_watch_ready(token, now);
}
static bool hit(const char *agent, const char *line) {
  return agent_transcript_interrupted(agent, line, strlen(line));
}
static void parser(void) {
  TEST_ASSERT(hit("claude", MARKER));
  const char *samples[] = {
      "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":[{"
      "\"type\":\"text\",\"text\":\"[Request interrupted by user for tool "
      "use]\"}]}}",
      "{\"type\":\"assistant\",\"message\":{\"role\":\"user\",\"content\":[{"
      "\"type\":\"text\",\"text\":\"[Request interrupted by user]\"}]}}",
      "{\"type\":\"user\",\"message\":{\"role\":\"assistant\",\"content\":[{"
      "\"type\":\"text\",\"text\":\"[Request interrupted by user]\"}]}}",
      "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":[{"
      "\"type\":\"text\",\"text\":\"quote [Request interrupted by user]\"}]}}",
      "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":[{"
      "\"type\":\"text\",\"text\":\"[Request interrupted by user]\"},{}]}}",
      "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":[]}}",
      "{\"nested\":{\"type\":\"user\",\"message\":{\"content\":[{\"text\":\"["
      "Request interrupted by user]\"}]}}}",
      "{\"type\":\"user\",\"type\":\"assistant\"}",
      "",
      "{",
      "[]",
      "null",
      "{\"type\":\"user\"} trailing"};
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
    TEST_ASSERT(hit("claude", samples[i]) == (i == 0));
  TEST_ASSERT(!hit("unknown", MARKER));
  TEST_ASSERT(
      !agent_transcript_interrupted("claude", MARKER, strlen(MARKER) - 1));
  TEST_ASSERT(!agent_transcript_interrupted("claude", MARKER, 4097));
  const char *codex[] = {
      "{\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\","
      "\"error\":{\"codex_error_info\":\"usage_limit_exceeded\",\"message\":"
      "\"private\"}}}",
      "{\"type\":\"event_msg\",\"payload\":{\"type\":\"turn_aborted\","
      "\"reason\":\"interrupted\"}}",
      "{\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\"}}",
      "{\"type\":\"event_msg\",\"payload\":{\"type\":\"task_started\"}}",
      "{\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\","
      "\"error\":null}}",
      "{\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\","
      "\"error\":\"oops\"}}",
      "{\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\","
      "\"error\":[]}}",
      "{\"type\":\"response_item\",\"payload\":{\"type\":\"turn_aborted\"}}"};
  for (size_t i = 0; i < sizeof(codex) / sizeof(codex[0]); i++) {
    TEST_ASSERT(hit("codex", codex[i]) == (i < 2));
    // Only the errored completion is a failure; an abort is a cancel.
    TEST_ASSERT(agent_transcript_failed("codex", codex[i], strlen(codex[i])) ==
                (i == 0));
  }
  TEST_ASSERT(!agent_transcript_failed("claude", MARKER, strlen(MARKER)));
}
static void hook_path(void) {
  agent_hook_scanner_t s;
  char path[1025], json[7000];
  const char *samples[] = {
      "{\"transcript_path\":\"/home/test/a b\\u4e2d.jsonl\"}",
      "{\"nested\":{\"transcript_path\":\"/a.jsonl\"}}",
      "{\"transcript_path\":\"relative.jsonl\"}",
      "{\"transcript_path\":\"/a\\n.jsonl\"}",
      "{\"transcript_path\":\"/a\\u0000.jsonl\"}",
      "{\"transcript_path\":\"/a.jsonl\",\"transcript_path\":null}"};
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
    agent_hook_scan_init(&s);
    agent_hook_scan_feed(&s, samples[i], strlen(samples[i]));
    TEST_ASSERT(agent_hook_scan_finish(&s));
    TEST_ASSERT(agent_hook_transcript(&s, path) == (i == 0));
    if (!i)
      TEST_ASSERT(!strcmp(path, "/home/test/a b中.jsonl"));
  }
  // Bound decoded bytes, including worst-case escaped spelling.
  for (int n = 1024; n <= 1025; n++) {
    strcpy(json, "{\"transcript_path\":\"/");
    for (int i = 1; i < n; i++)
      strcat(json, "\\u0061");
    strcat(json, "\"}");
    agent_hook_scan_init(&s);
    agent_hook_scan_feed(&s, json, strlen(json));
    TEST_ASSERT(agent_hook_scan_finish(&s));
    TEST_ASSERT(agent_hook_transcript(&s, path) == (n == 1024));
  }
  agent_event_t event;
  agent_hook_scan_adapter(&s, agent_adapter_find("codex"));
  const char *interrupt = "{\"hook_event_name\":\"Interrupt\"}";
  agent_hook_scan_feed(&s, interrupt, strlen(interrupt));
  TEST_ASSERT(agent_hook_scan_finish(&s) && agent_hook_event(&s, &event));
  TEST_ASSERT(event == AGENT_EVENT_INTERRUPT);
}
static void append(int fd, const char *data) {
  size_t n = strlen(data);
  TEST_ASSERT(write(fd, data, n) == (ssize_t)n);
}
static bool readable(void) {
  struct pollfd p = {.fd = agent_watch_fd(), .events = POLLIN};
  return poll(&p, 1, 0) == 1;
}
static void drain(void) {
  for (int i = 0; i < 100 && readable(); i++)
    agent_watch_process(NULL);
  TEST_ASSERT(!readable());
}
static void state(agent_state_t expected) {
  TEST_ASSERT(agent_sessions_resolve() == expected);
}
static void begin(const char *agent, const char *path) {
  agent_sessions_apply(1, agent, AGENT_EVENT_END, 0, now, 5, NULL);
  transcript_watch_sync(true, now);
  TEST_ASSERT(agent_sessions_apply(1, agent, AGENT_EVENT_WORKING, 0, now, 5,
                                   NULL) == 0);
  // A titled session starts no prompt recovery job; that job shares the
  // watch descriptor and would make the readiness checks below race.
  TEST_ASSERT(agent_sessions_set_title(1, "fixture") == 0);
  transcript_watch_path(1, path, now);
  TEST_ASSERT(transcript_watch_count() == 1);
}
static agent_state_t session_state(uint64_t key) {
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++)
    if (views[i].key == key)
      return views[i].state;
  TEST_ASSERT(false);
  return AGENT_STATE_IDLE;
}
static void agy_log(const char *home) {
  char path[256], moved[270];
  snprintf(path, sizeof(path), "%s/cli-test.log", home);
  snprintf(moved, sizeof(moved), "%s.old", path);
  int fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  TEST_ASSERT(fd >= 0);
  const char *marker =
      "I1008 09:08:36.312548 2668 conversation_manager.go:1520] "
      "Cancelling in-progress response for conversation test-id";
  append(fd, marker);
  append(fd, "\n");
  agent_sessions_reset();
  TEST_ASSERT(agent_sessions_apply(1, "agy", AGENT_EVENT_WORKING, 0, now, 5,
                                   NULL) == 0);
  TEST_ASSERT(agent_sessions_set_id(1, "test-id") == 0);
  TEST_ASSERT(agent_sessions_set_title(1, "fixture") == 0);
  transcript_watch_sync(true, now);
  transcript_watch_path(1, path, now);
  TEST_ASSERT(transcript_watch_count() == 1);
  drain();
  state(AGENT_STATE_WORKING);  // Historical cancellation is not replayed.
  const char *surfacing =
      "I1008 09:07:24.405115 1183 tool_confirmation_manager.go:226] "
      "Surfacing tool confirmation: \"RunCommand\" at step 4";
  const char *responding =
      "I1008 09:07:44.501665 1183 input_loop.go:706] "
      "Responding to tool confirmation: convID=test-id, stepIdx=4, "
      "approved=true, sandboxOverride=false, persistGrants=[]\n";
  append(fd, "I1008 log.go:1] Unknown tool confirmation format\n");
  drain();
  state(AGENT_STATE_WORKING);
  append(fd, surfacing);
  drain();
  state(AGENT_STATE_WORKING);
  append(fd, "\n");
  drain();
  state(AGENT_STATE_WAITING);
  transcript_watch_sync(true, now);
  TEST_ASSERT(transcript_watch_count() == 1);
  append(fd, "I1008 server.go:2521] Tool confirmation for conversation "
             "test-id step 4 (type=*proto.Step_Generic approved=true)\n");
  append(fd, "I1008 input_loop.go:706] Responding to tool confirmation: "
             "convID=other-id, stepIdx=4, approved=true\n");
  append(fd, "I1008 input_loop.go:706] Responding to tool confirmation: "
             "convID=test-id-extra, stepIdx=4, approved=true\n");
  append(fd, "I1008 input_loop.go:706] Responding to tool confirmation: "
             "convID=test-id, stepIdx=4, approved=maybe\n");
  append(fd, "I1008 server.go:2521] Tool confirmation for conversation "
             "test-id step 4 (type=*proto.Step_Generic approved=false)\n");
  append(fd, "I1008 input_loop.go:706] Responding to tool confirmation: "
             "convID=other-id, stepIdx=4, approved=false\n");
  append(fd, "I1008 input_loop.go:706] Responding to tool confirmation: "
             "convID=test-id-extra, stepIdx=4, approved=false\n");
  append(fd, "I1008 input_loop.go:706] Responding to tool confirmation: "
             "convID=test-id, stepIdx=4, approved=falsehood\n");
  drain();
  state(AGENT_STATE_WAITING);
  append(fd, responding);
  drain();
  state(AGENT_STATE_WORKING);
  append(fd, surfacing);
  append(fd, "\n");
  drain();
  state(AGENT_STATE_WAITING);
  TEST_ASSERT(agent_sessions_answer(1, now, 1));
  state(AGENT_STATE_WORKING);  // A key press is only a tentative answer.
  append(fd, responding);
  drain();
  agent_sessions_expire(now + 1000, 0);
  state(AGENT_STATE_WORKING);  // The log confirms it, cancelling reversion.
  append(fd, surfacing);
  append(fd, "\n");
  drain();
  state(AGENT_STATE_WAITING);
  append(fd, "I1008 input_loop.go:706] Responding to tool confirmation: "
             "convID=test-id, stepIdx=4, approved=false\n");
  drain();
  state(AGENT_STATE_IDLE);
  transcript_watch_sync(true, now);
  TEST_ASSERT(transcript_watch_count() == 0);
  const char *declined =
      "I1008 input_loop.go:706] Responding to tool confirmation: "
      "convID=test-id, stepIdx=4, approved=false, sandboxOverride=false, "
      "persistGrants=[]";
  for (int tentative = 0; tentative < 2; tentative++) {
    agent_sessions_apply(1, "agy", AGENT_EVENT_WORKING, 0, now, 5, NULL);
    transcript_watch_path(1, path, now);
    if (tentative) {
      append(fd, surfacing);
      append(fd, "\n");
      drain();
      state(AGENT_STATE_WAITING);
      TEST_ASSERT(agent_sessions_answer(1, now, 1));
    }
    state(AGENT_STATE_WORKING);
    append(fd, declined);
    drain();
    state(AGENT_STATE_WORKING);  // No event before the newline.
    append(fd, "\n");
    drain();
    state(AGENT_STATE_IDLE);
    agent_sessions_expire(now + 1000, 0);
    state(AGENT_STATE_IDLE);  // A tentative answer must not revert to waiting.
    transcript_watch_sync(true, now);
    TEST_ASSERT(transcript_watch_count() == 0);
  }
  agent_sessions_apply(1, "agy", AGENT_EVENT_WORKING, 0, now, 5, NULL);
  transcript_watch_path(1, path, now);
  agent_sessions_apply(1, "agy", AGENT_EVENT_DONE, 0, now, 5, NULL);
  agent_session_view_t view;
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1 && view.unread);
  uint64_t generation = agent_sessions_generation();
  // Read already-queued lines before sync removes the completed watch.
  append(fd, declined);
  append(fd, "\n");
  append(fd, responding);
  drain();
  state(AGENT_STATE_DONE);
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1 && view.unread);
  TEST_ASSERT(agent_sessions_generation() == generation);
  transcript_watch_sync(true, now);
  TEST_ASSERT(transcript_watch_count() == 0);
  agent_sessions_apply(1, "agy", AGENT_EVENT_WORKING, 0, now, 5, NULL);
  transcript_watch_path(1, path, now);
  // Two simultaneous working conversations on one log cannot own a prompt.
  TEST_ASSERT(
      !agent_sessions_apply(2, "agy", AGENT_EVENT_WORKING, 0, now, 5, NULL));
  TEST_ASSERT(!agent_sessions_set_id(2, "other-id"));
  TEST_ASSERT(!agent_sessions_set_title(2, "fixture"));
  transcript_watch_path(2, path, now);
  append(fd, surfacing);
  append(fd, "\n");
  drain();
  TEST_ASSERT(session_state(1) == AGENT_STATE_WORKING);
  TEST_ASSERT(session_state(2) == AGENT_STATE_WORKING);
  // Alternate hard-linked names still identify one process log.
  TEST_ASSERT(link(path, moved) == 0);
  transcript_watch_path(2, moved, now);
  append(fd, surfacing);
  append(fd, "\n");
  drain();
  TEST_ASSERT(session_state(1) == AGENT_STATE_WORKING);
  TEST_ASSERT(session_state(2) == AGENT_STATE_WORKING);
  TEST_ASSERT(unlink(moved) == 0);
  agent_sessions_apply(2, "agy", AGENT_EVENT_END, 0, now, 5, NULL);
  transcript_watch_sync(true, now);
  append(fd, "I1008 log.go:1] Cancelling in-progress response for conversation "
             "other-id\n");
  append(fd, "I1008 log.go:1] Cancelling in-progress response for conversation "
             "test-id-extra\n");
  append(fd, "normal response\n");
  drain();
  state(AGENT_STATE_WORKING);
  append(fd, marker);
  // Cancellation is also recognized while a confirmation is waiting.
  append(fd, "\n");
  drain();
  state(AGENT_STATE_IDLE);
  agent_sessions_apply(1, "agy", AGENT_EVENT_WORKING, 0, now, 5, NULL);
  transcript_watch_path(1, path, now);
  append(fd, surfacing);
  append(fd, "\n");
  drain();
  state(AGENT_STATE_WAITING);
  append(fd, marker);
  drain();
  state(AGENT_STATE_WAITING);  // A partial line is not an event.
  append(fd, "\n");
  drain();
  state(AGENT_STATE_IDLE);
  TEST_ASSERT(transcript_watch_count() == 0);
  for (int rotation = 0; rotation < 3; rotation++) {
    agent_sessions_apply(1, "agy", AGENT_EVENT_WORKING, 0, now, 5, NULL);
    transcript_watch_path(1, path, now);
    TEST_ASSERT(transcript_watch_count() == 1);
    if (rotation == 2) {
      int replacement = open(moved, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
      TEST_ASSERT(replacement >= 0);
      append(replacement, marker);
      append(replacement, "\n");
      close(replacement);
      TEST_ASSERT(rename(moved, path) == 0);
    } else if (rotation) {
      TEST_ASSERT(rename(path, moved) == 0);
      int replacement = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
      TEST_ASSERT(replacement >= 0);
      append(replacement, marker);
      append(replacement, "\n");
      close(replacement);
    } else
      TEST_ASSERT(ftruncate(fd, 0) == 0);
    drain();
    state(AGENT_STATE_WORKING);
    TEST_ASSERT(transcript_watch_count() == 0);
    transcript_watch_sync(true, now);
    TEST_ASSERT(transcript_watch_count() == 0);
    append(fd, marker);
    append(fd, "\n");
    drain();
    state(AGENT_STATE_WORKING);
  }
  transcript_watch_cleanup();
  agent_sessions_reset();
  close(fd);
  unlink(path);
  unlink(moved);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--transcript-prompt"))
    return agent_prompt_main(argc, argv);
  parser();
  hook_path();
  char home[] = "/tmp/herdcat-transcript-XXXXXX";
  TEST_ASSERT(mkdtemp(home));
  TEST_ASSERT(setenv("HOME", home, 1) == 0);
  char path[256], other[300];
  snprintf(path, sizeof(path), "%s/log.jsonl", home);
  int fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  TEST_ASSERT(fd >= 0);
  int check = transcript_watch_open(path);
  TEST_ASSERT(check >= 0 && (fcntl(check, F_GETFD) & FD_CLOEXEC));
  TEST_ASSERT(fcntl(check, F_GETFL) & O_NONBLOCK);
  close(check);
  TEST_ASSERT(transcript_watch_open("relative.jsonl") < 0);
  TEST_ASSERT(transcript_watch_open("/tmp/outside.jsonl") < 0);
  snprintf(other, sizeof(other), "%s/../log.jsonl", home);
  TEST_ASSERT(transcript_watch_open(other) < 0);
  snprintf(other, sizeof(other), "%s/link.jsonl", home);
  TEST_ASSERT(symlink(path, other) == 0);
  TEST_ASSERT(transcript_watch_open(other) < 0);
  unlink(other);
  TEST_ASSERT(mkfifo(other, 0600) == 0);
  TEST_ASSERT(transcript_watch_open(other) < 0);
  unlink(other);
  TEST_ASSERT(mkdir(other, 0700) == 0);
  TEST_ASSERT(transcript_watch_open(other) < 0);
  rmdir(other);
  snprintf(other, sizeof(other), "%s/link", home);
  TEST_ASSERT(symlink(home, other) == 0);
  char nested[400];
  snprintf(nested, sizeof(nested), "%s/log.jsonl", other);
  TEST_ASSERT(transcript_watch_open(nested) < 0);
  unlink(other);
  TEST_ASSERT(agent_watch_init() == 0);
  agent_watch_on_ready(ready);
  now = 100;
  // Historical complete/partial markers are never replayed on attaching.
  append(fd, MARKER);
  append(fd, "\n");
  begin("claude", path);
  drain();
  state(AGENT_STATE_WORKING);
  append(fd, MARKER);
  append(fd, "\n");
  now = 1099;
  drain();
  state(AGENT_STATE_WORKING);
  now = 1100;
  append(fd, "{\"type\":\"user\"}\n");
  drain();
  state(AGENT_STATE_WORKING);
  append(fd, MARKER);
  drain();
  state(AGENT_STATE_WORKING);  // Must wait for the newline.
  append(fd, "\n");
  drain();
  state(AGENT_STATE_IDLE);
  TEST_ASSERT(transcript_watch_count() == 0);
  append(fd, MARKER);
  append(fd, "\n");
  TEST_ASSERT(!readable());
  begin("claude", path);
  now += 1000;
  // Over-budget and overlong line is skipped, a later complete line survives.
  char block[8193];
  memset(block, 'x', sizeof(block) - 1);
  block[sizeof(block) - 1] = 0;
  for (int i = 0; i < 40; i++)
    append(fd, block);
  append(fd, "\n");
  append(fd, MARKER);
  append(fd, "\n");
  agent_watch_process(NULL);
  state(AGENT_STATE_WORKING);  // 256 KiB budget, continuation via eventfd.
  TEST_ASSERT(readable());
  drain();
  state(AGENT_STATE_IDLE);
  begin("claude", path);
  transcript_watch_sync(false, now);
  TEST_ASSERT(transcript_watch_count() == 0);
  now += 1000;
  append(fd, MARKER);
  append(fd, "\n");
  TEST_ASSERT(!readable());
  transcript_watch_sync(true, now);
  drain();
  state(AGENT_STATE_WORKING);  // Enabling never reads old entries.
  agent_sessions_apply(1, "claude", AGENT_EVENT_WAITING, 0, now, 5, NULL);
  append(fd, MARKER);
  append(fd, "\n");
  drain();
  state(AGENT_STATE_IDLE);
  begin("claude", path);
  TEST_ASSERT(ftruncate(fd, 0) == 0);
  drain();
  state(AGENT_STATE_WORKING);
  TEST_ASSERT(transcript_watch_count() == 0);
  transcript_watch_sync(true, now);
  TEST_ASSERT(transcript_watch_count() == 0);  // Fail closed on truncation.
  begin("codex", path);
  append(fd,
         "{\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\"}}\n");
  drain();
  state(AGENT_STATE_WORKING);
  append(fd, "{\"type\":\"event_msg\",\"payload\":{\"type\":\"task_complete\","
             "\"error\":{}}}\n");
  drain();
  state(AGENT_STATE_ERROR);  // Codex error has no Claude prompt guard.
  agent_sessions_configure_done(true, now, 5);
  agent_sessions_apply(1, "codex", AGENT_EVENT_DONE, 0, now, 5, NULL);
  agent_sessions_interrupt(1, now);
  state(AGENT_STATE_DONE);
  agent_session_view_t view;
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1 && view.unread);
  agent_sessions_apply(1, "codex", AGENT_EVENT_END, 0, now, 5, NULL);
  transcript_watch_sync(true, now);
  append(fd, MARKER);
  append(fd, "\n");
  TEST_ASSERT(!readable() && !transcript_watch_count());
  agent_sessions_interrupt(1, now);
  TEST_ASSERT(!agent_sessions_count());
  TEST_ASSERT(transcript_watch_command("path a /a.jsonl", now) == 1);
  TEST_ASSERT(transcript_watch_command("path 0000000000000000 /a.jsonl", now) ==
              1);
  transcript_watch_cleanup();
  agy_log(home);
  agent_watch_cleanup();
  close(fd);
  unlink(path);
  rmdir(home);
  puts("transcript tests passed");
  return 0;
}
