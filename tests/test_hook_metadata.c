#define _GNU_SOURCE
#include "core/agent_adapters.h"
#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "core/control.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char home[] = "/tmp/hc-meta-XXXXXX";
static char requests[16][1280];
static unsigned count;
static const char *reply;
static int result;
static uint64_t key;

int __wrap_control_request(const char *request);
int __wrap_control_request(const char *request) {
  TEST_ASSERT(count < 16);
  snprintf(requests[count++], sizeof(requests[0]), "%s", request);
  if (!strncmp(request, "ev ", 3)) {
    char agent[9], event[10];
    unsigned long long parsed;
    long pid;
    TEST_ASSERT(sscanf(request, "ev %8s %9s %llx %ld", agent, event, &parsed,
                       &pid) == 4);
    key = (uint64_t)parsed;
    agent_event_t kind;
    TEST_ASSERT(agent_event_parse(event, &kind) == 0);
    if (result)
      return result;
    TEST_ASSERT(agent_sessions_apply(key, agent, kind, 0, 1000, 5, NULL) == 0);
    printf("%s\n", reply                                ? reply
                   : agent_sessions_needs_metadata(key) ? "ok metadata"
                                                        : "ok");
    return 0;
  }
  if (!strncmp(request, "sid ", 4))
    return agent_sessions_id_command(request);
  if (!strncmp(request, "cwd ", 4))
    return agent_sessions_cwd_command(request);
  if (!strncmp(request, "ttl ", 4) || !strncmp(request, "ask ", 4))
    return agent_sessions_title_command(request);
  if (!strncmp(request, "path ", 5))
    return agent_sessions_set_transcript(key, request + 22);
  TEST_ASSERT(!strncmp(request, "term ", 5));
  return 0;
}

static void invoke(const char *agent, const char *event, bool cwd, bool title) {
  // Virtual payload paths do not inherit project names from /tmp/.git.
  char data[2048];
  int length =
      snprintf(data, sizeof(data),
               "{\"session_id\":\"late-id\",\"sessionId\":\"late-id\","
               "\"conversationId\":\"late-id\",\"agent_pid\":%ld,"
               "\"transcript_path\":\"%s/turn.jsonl\"%s%s}",
               (long)getppid(), home,
               cwd ? ",\"cwd\":\"/herdcat-test-input/late-project\","
                     "\"workspacePaths\":[\"/herdcat-test-input/late-project\"]"
                   : "",
               title ? ",\"title\":\"Late title\"" : "");
  TEST_ASSERT(length > 0 && (size_t)length < sizeof(data));
  int input[2];
  TEST_ASSERT(pipe(input) == 0);
  TEST_ASSERT(write(input[1], data, (size_t)length) == length);
  close(input[1]);
  TEST_ASSERT(dup2(input[0], STDIN_FILENO) == STDIN_FILENO);
  close(input[0]);
  count = 0;
  TEST_ASSERT(agent_hook_run(agent, event) == 0);
  alarm(0);
}

static void named(bool expected) {
  agent_session_record_t row;
  TEST_ASSERT(agent_sessions_export(&row, 1) == 1);
  TEST_ASSERT(!strcmp(row.name, "late-project") == expected);
  TEST_ASSERT(!strcmp(row.start_cwd, "/herdcat-test-input/late-project") ==
              expected);
  if (expected)
    TEST_ASSERT(!strcmp(row.session_id, "late-id"));
}

static void scenario(const agent_adapter_t *adapter) {
  const char *agent = adapter->name;
  const char *event = !strcmp(agent, "cursor")     ? "beforeShellExecution"
                      : !strcmp(agent, "copilot")  ? "preToolUse"
                      : !strcmp(agent, "pi")       ? "tool_call"
                      : !strcmp(agent, "opencode") ? "permission.asked"
                                                   : "PreToolUse";
  agent_event_t kind;
  bool metadata = true;
  TEST_ASSERT(agent_adapter_event(adapter, event, NULL, NULL, false, false,
                                  false, &kind, &metadata));
  TEST_ASSERT(!metadata);
  agent_sessions_reset();
  invoke(agent, event, true, true);
  named(true);
  unsigned late_count = count;
  char late[16][1280];
  memcpy(late, requests, sizeof(late));
  TEST_ASSERT(late_count >= 3 && !strncmp(late[1], "sid ", 4));
  agent_session_view_t view;
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1 &&
              view.state == (kind == AGENT_EVENT_WAITING
                                 ? AGENT_STATE_WAITING
                                 : AGENT_STATE_WORKING));
  if (!strcmp(agent, "opencode"))
    TEST_ASSERT(!strcmp(view.title, "Late title"));

  // A known non-metadata event must send exactly the old sequence of bytes.
  invoke(agent, event, true, false);
  TEST_ASSERT(count == 1 && !strcmp(requests[0], late[0]));

  // The requested metadata is identical to a regular META event's handoff.
  agent_sessions_reset();
  reply = "ok";
  const char *start = NULL;
  for (size_t i = 0; i < adapter->rule_count; i++)
    if (adapter->rules[i].metadata) {
      start = adapter->rules[i].name;
      break;
    }
  TEST_ASSERT(start);
  invoke(agent, start, true, true);
  named(true);
  TEST_ASSERT(count == late_count);
  for (unsigned i = 1; i < count; i++)
    TEST_ASSERT(!strcmp(requests[i], late[i]));

  // New hook / old daemon: no marker means no additional requests.
  agent_sessions_reset();
  invoke(agent, event, true, false);
  named(false);
  TEST_ASSERT(count == 1);
  reply = NULL;

  // Missing cwd retains the placeholder; the next event repairs it.
  agent_sessions_reset();
  invoke(agent, event, false, false);
  named(false);
  TEST_ASSERT(count >= 2 && !strncmp(requests[1], "sid ", 4));
  invoke(agent, event, true, false);
  named(true);
  invoke(agent, event, true, false);
  TEST_ASSERT(count == 1);
}

static void parsing(void) {
  const char *replies[] = {"ok",
                           "ok metadata",
                           "ok metadata-extra",
                           "metadata",
                           "request failed",
                           "ok\nmetadata",
                           "ok metadata\nextra"};
  for (size_t i = 0; i < sizeof(replies) / sizeof(*replies); i++) {
    agent_sessions_reset();
    reply = replies[i];
    invoke("qwen", "PermissionRequest", true, false);
    named(i == 1);
    TEST_ASSERT(count == (i == 1 ? 3U : 1U));
  }
  agent_sessions_reset();
  reply = "ok metadata";
  result = 1;
  invoke("qwen", "PermissionRequest", true, false);
  TEST_ASSERT(count == 1 && agent_sessions_count() == 0);
}

static void readiness(void) {
  agent_sessions_reset();
  TEST_ASSERT(!agent_sessions_needs_metadata(1));
  TEST_ASSERT(agent_sessions_apply(1, "codex", AGENT_EVENT_WAITING, getpid(),
                                   1000, 5, NULL) == 0);
  TEST_ASSERT(agent_sessions_needs_metadata(1));
  TEST_ASSERT(agent_sessions_set_name(1, "project") == 0);
  TEST_ASSERT(agent_sessions_needs_metadata(1));
  TEST_ASSERT(agent_sessions_set_cwd_name(1, "/tmp/project", "project") == 0);
  TEST_ASSERT(!agent_sessions_needs_metadata(1));
  // A second ID owned by the same PID uses the already named row.
  TEST_ASSERT(agent_sessions_apply(2, "codex", AGENT_EVENT_WORKING, getpid(),
                                   1000, 5, NULL) == 0);
  TEST_ASSERT(agent_sessions_count() == 1 && !agent_sessions_needs_metadata(2));
  TEST_ASSERT(agent_sessions_apply(1, "codex", AGENT_EVENT_END, getpid(), 1000,
                                   5, NULL) == 0);
  TEST_ASSERT(!agent_sessions_needs_metadata(1) &&
              !agent_sessions_needs_metadata(2));
}

int main(void) {
  TEST_ASSERT(mkdtemp(home) && setenv("HOME", home, 1) == 0);
  TEST_ASSERT(setenv("XDG_RUNTIME_DIR", home, 1) == 0);
  unsetenv("GROK_HOME");
  unsetenv("CODEX_HOME");
  unsetenv("KIMI_CODE_HOME");
  unsetenv("COPILOT_HOME");
  unsetenv("CLAUDE_PID");
  readiness();
  for (size_t i = 0; i <= agent_adapter_count(); i++) {
    pid_t pid = fork();
    TEST_ASSERT(pid >= 0);
    if (!pid) {
      if (i == agent_adapter_count())
        parsing();
      else
        scenario(agent_adapter_at(i));
      _exit(0);
    }
    int status;
    TEST_ASSERT(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
                WEXITSTATUS(status) == 0);
  }
  TEST_ASSERT(rmdir(home) == 0);
  puts("Late hook metadata: all adapters, request bytes and replies passed");
  return 0;
}
