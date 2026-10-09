#define _GNU_SOURCE
#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "core/control.h"
#include "test_helpers.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int64_t now_ms;
static unsigned event_requests;

int __wrap_control_request(const char *request);

// Replace only transport. Decode/apply requests with the production functions
// used by the daemon; payloads enter through the real CLI hook implementation.
int __wrap_control_request(const char *request) {
  if (!strncmp(request, "ev ", 3)) {
    uint64_t key;
    char agent[9];
    agent_event_t event;
    pid_t pid, candidate, owner;
    bool metadata;
    TEST_ASSERT(agent_event_owner_request(request, &key, agent, &event, &pid,
                                          &candidate, &metadata, &owner));
    TEST_ASSERT(agent_sessions_apply_owned(key, agent, event, pid, candidate,
                                           metadata, owner, "/proc", now_ms,
                                           5) == 0);
    event_requests++;
    if (event == AGENT_EVENT_START || event == AGENT_EVENT_DONE)
      agent_sessions_refresh_title(key);
    printf("%s\n", agent_sessions_needs_metadata(key) ? "ok metadata" : "ok");
    return 0;
  }
  if (!strncmp(request, "sid ", 4))
    return agent_sessions_id_command(request);
  if (!strncmp(request, "cwd ", 4))
    return agent_sessions_cwd_command(request);
  if (!strncmp(request, "ask ", 4) || !strncmp(request, "ttl ", 4))
    return agent_sessions_title_command(request);
  if (!strncmp(request, "path ", 5)) {
    uint64_t key;
    TEST_ASSERT(sscanf(request + 5, "%16" SCNx64, &key) == 1);
    return agent_sessions_set_transcript(key, request + 22);
  }
  TEST_ASSERT(!strncmp(request, "term ", 5));
  return 0;
}

static void replay(const char *agent, const char *directory) {
  char path[512];
  TEST_ASSERT(snprintf(path, sizeof(path), "%s/expect.tsv", directory) > 0);
  FILE *expected = fopen(path, "r");
  TEST_ASSERT(expected);
  agent_sessions_reset();
  char *line = NULL;
  size_t capacity = 0;
  unsigned steps = 0;
  while (getline(&line, &capacity, expected) > 0) {
    if (line[0] == '#')
      continue;
    char *fields[6], *save = NULL;
    unsigned count = 0;
    for (char *field = strtok_r(line, "\t\n", &save); field;
         field = strtok_r(NULL, "\t\n", &save)) {
      TEST_ASSERT(count < 6);
      fields[count++] = field;
    }
    TEST_ASSERT(count == 6);
    now_ms = strtoll(fields[0], NULL, 10) + 1000;
    TEST_ASSERT(snprintf(path, sizeof(path), "%s/%s", directory, fields[1]) >
                0);
    fprintf(stderr, "%s: %s => %s\n", directory, fields[1], fields[3]);
    int input = open(path, O_RDONLY);
    TEST_ASSERT(input >= 0);
    int saved_out = dup(STDOUT_FILENO), saved_in = dup(STDIN_FILENO);
    TEST_ASSERT(saved_out >= 0 && saved_in >= 0);
    TEST_ASSERT(dup2(input, STDIN_FILENO) == STDIN_FILENO);
    close(input);
    unsigned before = event_requests;
    TEST_ASSERT(
        agent_hook_run(agent, strcmp(fields[2], "-") ? fields[2] : NULL) == 0);
    alarm(0);
    fflush(stdout);
    TEST_ASSERT(dup2(saved_out, STDOUT_FILENO) == STDOUT_FILENO);
    TEST_ASSERT(dup2(saved_in, STDIN_FILENO) == STDIN_FILENO);
    close(saved_out);
    close(saved_in);
    TEST_ASSERT(event_requests == before + 1);
    agent_session_view_t view;
    int rows = agent_sessions_snapshot(&view, 1);
    if (!strcmp(fields[3], "absent")) {
      TEST_ASSERT(rows == 0 && agent_sessions_count() == 0);
      TEST_ASSERT(!strcmp(fields[4], "-") && !strcmp(fields[5], "0"));
    } else {
      agent_state_t state;
      TEST_ASSERT(agent_state_parse(fields[3], &state) == 0);
      TEST_ASSERT(rows == 1 && agent_sessions_count() == 1);
      TEST_ASSERT(view.state == state && agent_sessions_resolve() == state);
      TEST_ASSERT(!strcmp(view.title, strcmp(fields[4], "-") ? fields[4] : ""));
      bool child = view.parent != 0 || view.child_count != 0;
      TEST_ASSERT(child == (strcmp(fields[5], "0") != 0));
    }
    steps++;
  }
  TEST_ASSERT(steps > 0 && !ferror(expected));
  free(line);
  fclose(expected);
}

int main(void) {
  char home[] = "/tmp/hc-fixture-XXXXXX";
  TEST_ASSERT(mkdtemp(home));
  TEST_ASSERT(setenv("HOME", home, 1) == 0);
  TEST_ASSERT(setenv("XDG_RUNTIME_DIR", home, 1) == 0);
  const char *unset[] = {
      "WAYLAND_DISPLAY", "NIRI_SOCKET",        "SWAYSOCK",
      "CLAUDE_PID",      "HERDCAT_HOOK_DEBUG", "TMUX",
      "TMUX_PANE",       "KITTY_PID",          "KITTY_WINDOW_ID",
      "KITTY_LISTEN_ON", "WEZTERM_PANE",       "WEZTERM_UNIX_SOCKET",
      "GROK_HOME",       "CODEX_HOME",         "CLAUDE_CONFIG_DIR",
      "COPILOT_HOME",    "KIMI_CODE_HOME",     "PI_CODING_AGENT_DIR"};
  for (size_t i = 0; i < sizeof(unset) / sizeof(unset[0]); i++)
    unsetenv(unset[i]);
  DIR *agents = opendir("tests/agent_fixtures");
  TEST_ASSERT(agents);
  struct dirent *agent;
  unsigned scenarios = 0;
  while ((agent = readdir(agents))) {
    if (agent->d_name[0] == '.')
      continue;
    char root[512];
    snprintf(root, sizeof(root), "tests/agent_fixtures/%s", agent->d_name);
    DIR *cases = opendir(root);
    if (!cases)
      continue;
    struct dirent *entry;
    while ((entry = readdir(cases))) {
      if (entry->d_name[0] == '.')
        continue;
      char directory[1024];
      snprintf(directory, sizeof(directory), "%s/%s", root, entry->d_name);
      // Each scenario runs in an isolated process, as hooks alter stdio and
      // alarm state. No agent or compositor process is launched.
      pid_t child = fork();
      TEST_ASSERT(child >= 0);
      if (!child) {
        replay(agent->d_name, directory);
        _exit(0);
      }
      int status;
      TEST_ASSERT(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                  WEXITSTATUS(status) == 0);
      scenarios++;
    }
    closedir(cases);
  }
  closedir(agents);
  TEST_ASSERT(scenarios > 0);
  TEST_ASSERT(rmdir(home) == 0);
  printf("PASS: %u recorded hook scenarios\n", scenarios);
  return 0;
}
