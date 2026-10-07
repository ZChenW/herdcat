#define _GNU_SOURCE
#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "core/control.h"
#include "platform/transcript_watch.h"
#include "test_helpers.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char home[] = "/tmp/herdcat-title-hooks-XXXXXX";
static unsigned asks, titles, ids, paths;
static uint64_t last_key;

int __wrap_control_request(const char *request);

// Exercise the real hook client and the same metadata handlers used by the
// renderer. Replace only the socket transport; no desktop instance is used.
int __wrap_control_request(const char *request) {
  if (!strncmp(request, "ev ", 3)) {
    char agent[9], event[10];
    unsigned long long key;
    long pid;
    TEST_ASSERT(
        sscanf(request, "ev %8s %9s %llx %ld", agent, event, &key, &pid) == 4);
    agent_event_t kind;
    TEST_ASSERT(agent_event_parse(event, &kind) == 0);
    last_key = (uint64_t)key;
    TEST_ASSERT(agent_sessions_apply(last_key, agent, kind, (pid_t)pid, 1000, 5,
                                     NULL) == 0);
    if (kind == AGENT_EVENT_START || kind == AGENT_EVENT_DONE)
      agent_sessions_refresh_title(last_key);
    return 0;
  }
  if (!strncmp(request, "sid ", 4)) {
    ids++;
    return agent_sessions_id_command(request);
  }
  if (!strncmp(request, "ask ", 4) || !strncmp(request, "ttl ", 4)) {
    asks += request[0] == 'a';
    titles += request[0] == 't';
    return agent_sessions_title_command(request);
  }
  if (!strncmp(request, "path ", 5)) {
    paths++;
    return transcript_watch_command(request, 1000);
  }
  if (!strncmp(request, "cwd ", 4))
    return agent_sessions_cwd_command(request);
  if (!strncmp(request, "name ", 5)) {
    unsigned long long key;
    int end = 0;
    TEST_ASSERT(sscanf(request + 5, "%16llx%n", &key, &end) == 1 && end == 16);
    return agent_sessions_set_name((uint64_t)key, request + 22);
  }
  // Terminal reporting is unrelated to title sourcing.
  TEST_ASSERT(!strncmp(request, "term ", 5));
  return 0;
}

static void directory(const char *relative) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", home, relative);
  TEST_ASSERT(mkdir(path, 0700) == 0);
}
static void put(const char *relative, const char *data) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", home, relative);
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  TEST_ASSERT(fd >= 0);
  size_t length = strlen(data);
  TEST_ASSERT(write(fd, data, length) == (ssize_t)length && close(fd) == 0);
}

static void invoke(const char *agent, const char *event, const char *extra) {
  char data[2048];
  int length = snprintf(data, sizeof(data),
                        "{\"session_id\":\"test-id\",\"sessionId\":\"test-id\","
                        "\"cwd\":\"/tmp/stage21-project\",\"agent_pid\":%ld%s}",
                        (long)getppid(), extra);
  TEST_ASSERT(length > 0 && (size_t)length < sizeof(data));
  int input[2];
  TEST_ASSERT(pipe(input) == 0);
  TEST_ASSERT(write(input[1], data, (size_t)length) == length);
  close(input[1]);
  TEST_ASSERT(dup2(input[0], STDIN_FILENO) == STDIN_FILENO);
  close(input[0]);
  TEST_ASSERT(agent_hook_run(agent, event) == 0);
  alarm(0);
}

static void expect(const char *title, bool temporary) {
  agent_session_view_t view;
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1);
  TEST_ASSERT(view.key == last_key && !strcmp(view.title, title));
  TEST_ASSERT(view.title_temporary == temporary);
}

static void scenario(const char *agent) {
  agent_sessions_reset();
  if (!strcmp(agent, "grok")) {
    invoke(agent, "UserPromptSubmit", ",\"prompt\":\"SYNTHETIC_PROMPT\"");
    expect("SYNTHETIC_PROMPT", true);
    put(".grok/sessions/%2Ftmp%2Fstage21-project/test-id/summary.json",
        "{\"info\":{\"id\":\"test-id\"},\"generated_title\":\"SYNTHETIC_"
        "REAL\"}");
    invoke(agent, "Stop", "");
    expect("SYNTHETIC_REAL", false);
    TEST_ASSERT(ids == 2 && paths == 2 && asks == 1);
  } else if (!strcmp(agent, "kimi")) {
    invoke(agent, "UserPromptSubmit", ",\"prompt\":\"SYNTHETIC_PROMPT\"");
    expect("SYNTHETIC_PROMPT", true);
    put(".kimi-code/sessions/0123456789abcdef0123456789abcdef/test-id/"
        "state.json",
        "{\"title\":\"SYNTHETIC_REAL\"}");
    char index[1600];
    snprintf(
        index, sizeof(index),
        "{\"sessionId\":\"test-id\",\"sessionDir\":\"%s/.kimi-code/sessions/"
        "0123456789abcdef0123456789abcdef/test-id\"}\n",
        home);
    put(".kimi-code/session_index.jsonl", index);
    invoke(agent, "Stop", "");
    expect("SYNTHETIC_REAL", false);
    TEST_ASSERT(ids == 2 && asks == 1);
  } else if (!strcmp(agent, "pi")) {
    put(".pi/agent/sessions/--tmp-stage21-project--/timestamp_test-id.jsonl",
        "{\"type\":\"session_info\",\"name\":\"SYNTHETIC_REAL\"}\n");
    char extra[1600];
    snprintf(extra, sizeof(extra),
             ",\"transcript_path\":\"%s/.pi/agent/sessions/"
             "--tmp-stage21-project--/timestamp_test-id.jsonl\"",
             home);
    invoke(agent, "session_start", extra);
    expect("SYNTHETIC_REAL", false);
    TEST_ASSERT(ids == 1 && paths == 1);
  } else if (!strcmp(agent, "opencode")) {
    invoke(agent, "session.created", ",\"title\":\"New session - timestamp\"");
    expect("", false);
    invoke(agent, "session.execution.succeeded",
           ",\"title\":\"SYNTHETIC_REAL\"");
    expect("SYNTHETIC_REAL", false);
    TEST_ASSERT(titles == 1);
    invoke(agent, "session.execution.started",
           ",\"title\":\"New session - later\"");
    expect("SYNTHETIC_REAL", false);
  } else {
    const char *submit = !strcmp(agent, "cursor")    ? "beforeSubmitPrompt"
                         : !strcmp(agent, "copilot") ? "userPromptSubmitted"
                                                     : "UserPromptSubmit";
    invoke(agent, submit, ",\"prompt\":\" /help\"");
    expect("", false);
    invoke(agent, submit,
           ",\"prompt\":\"  SYNTHETIC_PROMPT  \\t first\\nignored\"");
    expect("SYNTHETIC_PROMPT first", true);
    invoke(agent, submit, ",\"prompt\":\"later prompt\"");
    expect("SYNTHETIC_PROMPT first", true);
    TEST_ASSERT(asks == 2);
    if (!strcmp(agent, "copilot")) {
      put(".copilot/session-state/test-id/workspace.yaml",
          "summary: SYNTHETIC_REAL\n");
      invoke(agent, "agentStop", ",\"stopReason\":\"end_turn\"");
    } else if (!strcmp(agent, "claude")) {
      put(".claude/projects/--tmp-stage21-project--/test-id.jsonl",
          "{\"type\":\"ai-title\",\"aiTitle\":\"SYNTHETIC_REAL\","
          "\"sessionId\":\"test-id\"}\n");
      char extra[1600];
      snprintf(extra, sizeof(extra),
               ",\"transcript_path\":\"%s/.claude/projects/"
               "--tmp-stage21-project--/test-id.jsonl\"",
               home);
      invoke(agent, "Stop", extra);
    } else if (!strcmp(agent, "codex")) {
      put(".codex/session_index.jsonl",
          "{\"id\":\"test-id\",\"thread_name\":\"SYNTHETIC_REAL\"}\n");
      invoke(agent, "Stop", "");
    } else {
      TEST_ASSERT(agent_sessions_set_title(last_key, "SYNTHETIC_REAL") == 0);
    }
    expect("SYNTHETIC_REAL", false);
    invoke(agent, submit, ",\"prompt\":\"final prompt\"");
    expect("SYNTHETIC_REAL", false);
  }
}

static void child_test(const char *agent) {
  int debug[2];
  TEST_ASSERT(pipe(debug) == 0);
  pid_t pid = fork();
  TEST_ASSERT(pid >= 0);
  if (!pid) {
    close(debug[0]);
    TEST_ASSERT(dup2(debug[1], STDERR_FILENO) == STDERR_FILENO);
    close(debug[1]);
    TEST_ASSERT(setenv("HERDCAT_HOOK_DEBUG", "1", 1) == 0);
    TEST_ASSERT(freopen("/dev/null", "w", stdout));
    scenario(agent);
    transcript_watch_cleanup();
    _exit(0);
  }
  close(debug[1]);
  char captured[4096];
  ssize_t used = 0, n;
  while ((n = read(debug[0], captured + used,
                   sizeof(captured) - 1 - (size_t)used)) > 0)
    used += n;
  close(debug[0]);
  captured[used] = 0;
  int status;
  TEST_ASSERT(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0);
  TEST_ASSERT(!strstr(captured, "SYNTHETIC_REAL") &&
              !strstr(captured, "SYNTHETIC_PROMPT") &&
              !strstr(captured, "later prompt") &&
              !strstr(captured, "final prompt"));
  TEST_ASSERT(!strstr(captured, "ttl ") && !strstr(captured, "ask "));
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--transcript-prompt"))
    return agent_prompt_main(argc, argv);
  TEST_ASSERT(mkdtemp(home) && setenv("HOME", home, 1) == 0);
  TEST_ASSERT(setenv("XDG_RUNTIME_DIR", home, 1) == 0);
  unsetenv("GROK_HOME");
  unsetenv("KIMI_CODE_HOME");
  unsetenv("CODEX_HOME");
  unsetenv("COPILOT_HOME");
  const char *dirs[] = {
      ".grok",
      ".grok/sessions",
      ".grok/sessions/%2Ftmp%2Fstage21-project",
      ".grok/sessions/%2Ftmp%2Fstage21-project/test-id",
      ".kimi-code",
      ".kimi-code/sessions",
      ".kimi-code/sessions/0123456789abcdef0123456789abcdef",
      ".kimi-code/sessions/0123456789abcdef0123456789abcdef/test-id",
      ".pi",
      ".pi/agent",
      ".pi/agent/sessions",
      ".pi/agent/sessions/--tmp-stage21-project--",
      ".copilot",
      ".copilot/session-state",
      ".copilot/session-state/test-id",
      ".claude",
      ".claude/projects",
      ".claude/projects/--tmp-stage21-project--",
      ".codex"};
  for (size_t i = 0; i < sizeof(dirs) / sizeof(*dirs); i++)
    directory(dirs[i]);
  // Include the two already supported sources in the fallback matrix as well.
  const char *agents[] = {"claude", "codex", "cursor", "copilot",
                          "grok",   "kimi",  "pi",     "opencode"};
  for (size_t i = 0; i < sizeof(agents) / sizeof(*agents); i++)
    child_test(agents[i]);
  const char *files[] = {
      ".grok/sessions/%2Ftmp%2Fstage21-project/test-id/summary.json",
      ".kimi-code/session_index.jsonl",
      ".kimi-code/sessions/0123456789abcdef0123456789abcdef/test-id/state.json",
      ".pi/agent/sessions/--tmp-stage21-project--/timestamp_test-id.jsonl",
      ".copilot/session-state/test-id/workspace.yaml",
      ".claude/projects/--tmp-stage21-project--/test-id.jsonl",
      ".codex/session_index.jsonl"};
  char path[1024];
  for (size_t i = 0; i < sizeof(files) / sizeof(*files); i++) {
    snprintf(path, sizeof(path), "%s/%s", home, files[i]);
    TEST_ASSERT(unlink(path) == 0);
  }
  for (size_t i = sizeof(dirs) / sizeof(*dirs); i > 0; i--) {
    snprintf(path, sizeof(path), "%s/%s", home, dirs[i - 1]);
    TEST_ASSERT(rmdir(path) == 0);
  }
  TEST_ASSERT(rmdir(home) == 0);
  puts("synthetic hook title chains and private diagnostics passed");
  return 0;
}
