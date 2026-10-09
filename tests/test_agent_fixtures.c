#define _GNU_SOURCE
#include "core/agent_adapters.h"
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
static bool dump;

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

// A recorded Pi PID no longer exists. Rebind only its numeric identity to
// the live replay parent; the real hook still checks that parent and all JSON
// types. Malformed/non-numeric identities remain untouched and are rejected.
static int replay_input(const char *agent, int input) {
  if (strcmp(agent, "pi"))
    return input;
  FILE *source = fdopen(input, "r");
  TEST_ASSERT(source);
  char *text = NULL;
  size_t capacity = 0;
  FILE *bound = tmpfile();
  TEST_ASSERT(bound);
  while (getline(&text, &capacity, source) > 0) {
    char *field = strstr(text, "\"agent_pid\"");
    char *number = field ? strchr(field, ':') : NULL;
    if (number) {
      number++;
      while (*number == ' ' || *number == '\t')
        number++;
    }
    char *end = NULL;
    long pid = number ? strtol(number, &end, 10) : 0;
    if (number && pid > 1 && end != number &&
        (*end == ',' || *end == '}' || *end == ' ' || *end == '\n')) {
      TEST_ASSERT(fwrite(text, 1, (size_t)(number - text), bound) ==
                  (size_t)(number - text));
      fprintf(bound, "%jd%s", (intmax_t)getppid(), end);
    } else
      fputs(text, bound);
  }
  free(text);
  fclose(source);
  TEST_ASSERT(fflush(bound) == 0 && fseek(bound, 0, SEEK_SET) == 0);
  int result = dup(fileno(bound));
  TEST_ASSERT(result >= 0);
  fclose(bound);
  return result;
}

// Known payloads still require exactly one event request. Ignored payloads
// require exactly zero, including malformed Pi process identities.
static bool expects_request(const char *agent, const char *override,
                            int input) {
  const agent_adapter_t *adapter = agent_adapter_find(agent);
  agent_hook_scanner_t scanner;
  agent_hook_scan_adapter(&scanner, adapter);
  char buffer[4096];
  off_t offset = 0;
  ssize_t length;
  while ((length = pread(input, buffer, sizeof(buffer), offset)) > 0) {
    agent_hook_scan_feed(&scanner, buffer, (size_t)length);
    offset += length;
  }
  agent_event_t event;
  bool metadata;
  if (length < 0 || !agent_hook_scan_finish(&scanner) ||
      !agent_hook_event_override(&scanner, override, &event, &metadata))
    return false;
  return !adapter->explicit_pid || event == AGENT_EVENT_END ||
         ((scanner.valid_fields & (1U << HOOK_FIELD_PID)) &&
          scanner.pid == getppid());
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
    if (strcmp(fields[1], "-")) {
      TEST_ASSERT(snprintf(path, sizeof(path), "%s/%s", directory, fields[1]) >
                  0);
      int input = open(path, O_RDONLY), quiet = open("/dev/null", O_WRONLY);
      TEST_ASSERT(input >= 0 && quiet >= 0);
      input = replay_input(agent, input);
      int saved_out = dup(STDOUT_FILENO), saved_in = dup(STDIN_FILENO);
      TEST_ASSERT(saved_out >= 0 && saved_in >= 0);
      TEST_ASSERT(dup2(input, STDIN_FILENO) == STDIN_FILENO);
      TEST_ASSERT(dup2(quiet, STDOUT_FILENO) == STDOUT_FILENO);
      close(input);
      close(quiet);
      unsigned before = event_requests;
      bool emitted = expects_request(
          agent, strcmp(fields[2], "-") ? fields[2] : NULL, STDIN_FILENO);
      TEST_ASSERT(agent_hook_run(agent, strcmp(fields[2], "-") ? fields[2]
                                                               : NULL) == 0);
      alarm(0);
      fflush(stdout);
      TEST_ASSERT(dup2(saved_out, STDOUT_FILENO) == STDOUT_FILENO);
      TEST_ASSERT(dup2(saved_in, STDIN_FILENO) == STDIN_FILENO);
      close(saved_out);
      close(saved_in);
      // Unknown/ignored hooks are retained as evidence too.
      TEST_ASSERT(event_requests == before + (emitted ? 1U : 0U));
    }
    agent_session_view_t view;
    int rows = agent_sessions_snapshot(&view, 1);
    if (dump) {
      TEST_ASSERT(agent_sessions_count() <= 1);
      printf("%s\t%s\t%s\t%d\n", fields[0],
             rows ? agent_state_name(view.state) : "absent",
             rows && view.title[0] ? view.title : "-",
             rows && (view.parent != 0 || view.child_count != 0));
      fflush(stdout);
    } else if (!strcmp(fields[3], "absent")) {
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

int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "--adapter")) {
    const agent_adapter_t *adapter = agent_adapter_find(argv[2]);
    TEST_ASSERT(!strcmp(adapter->name, argv[2]));
    for (size_t i = 0; i < adapter->alias_count; i++)
      printf("field\t%s\n", adapter->aliases[i].key);
    for (size_t i = 0; i < adapter->rule_count; i++)
      printf("event\t%s\n", adapter->rules[i].name);
    return 0;
  }
  TEST_ASSERT(argc == 1 || (argc == 4 && !strcmp(argv[1], "--dump")));

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
  if (argc == 4) {
    dump = true;
    replay(argv[2], argv[3]);
    TEST_ASSERT(rmdir(home) == 0);
    return 0;
  }
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
