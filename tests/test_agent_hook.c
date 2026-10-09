#define _GNU_SOURCE
#include "core/agent_hook.h"
#include "test_helpers.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static agent_hook_scanner_t scan(const char *json, size_t chunk, bool valid) {
  agent_hook_scanner_t s;
  agent_hook_scan_init(&s);
  size_t length = strlen(json);
  for (size_t i = 0; i < length; i += chunk) {
    size_t n = length - i < chunk ? length - i : chunk;
    agent_hook_scan_feed(&s, json + i, n);
  }
  TEST_ASSERT(agent_hook_scan_finish(&s) == valid);
  return s;
}

static void expect_event(const char *json, int expected) {
  for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
    agent_hook_scanner_t s = scan(json, chunk, true);
    agent_event_t event = AGENT_EVENT_COUNT;
    bool found = agent_hook_event(&s, &event);
    TEST_ASSERT(found == (expected >= 0));
    if (found) {
      TEST_ASSERT((int)event == expected);
    }
  }
}

static void test_mapping(void) {
  const char *names[] = {
      "SessionStart",       "UserPromptSubmit",  "PreToolUse",   "PostToolUse",
      "PostToolUseFailure", "PermissionRequest", "Stop",         "StopFailure",
      "Interrupt",          "SessionEnd",        "SubagentStop", "unknown"};
  const int events[] = {AGENT_EVENT_START,
                        AGENT_EVENT_WORKING,
                        AGENT_EVENT_WORKING,
                        AGENT_EVENT_WORKING,
                        AGENT_EVENT_WORKING,
                        AGENT_EVENT_WAITING,
                        AGENT_EVENT_DONE,
                        AGENT_EVENT_FAIL,
                        AGENT_EVENT_IDLE,
                        AGENT_EVENT_END,
                        -1,
                        -1};
  char json[256];
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    snprintf(json, sizeof(json), "{\"hook_event_name\":\"%s\"}", names[i]);
    expect_event(json, events[i]);
  }
  expect_event("{\"stop_hook_active\":true,\"hook_event_name\":\"Stop\"}", -1);
  expect_event("{\"hook_event_name\":\"Stop\",\"stop_hook_active\":false}",
               AGENT_EVENT_DONE);
  expect_event("{\"hook_event_name\":\"Stop\",\"stop_hook_active\":\"false\"}",
               -1);
  const char *notifications[] = {"permission_prompt", "elicitation_dialog",
                                 "agent_needs_input", "idle_prompt", "other"};
  for (size_t i = 0; i < 5; i++) {
    snprintf(json, sizeof(json),
             "{\"hook_event_name\":\"Notification\","
             "\"notification_type\":\"%s\"}",
             notifications[i]);
    expect_event(json, i < 3    ? AGENT_EVENT_WAITING
                       : i == 3 ? AGENT_EVENT_REST
                                : -1);
  }
  expect_event("{\"hook_event_name\":\"Notification\"}", -1);
  expect_event("{\"hook_event_name\":\"St\\u006fp\"}", -1);
  expect_event("{\"hook_event_name\":true}", -1);
  expect_event("{\"nested\":{\"hook_event_name\":\"Stop\"}}", -1);
  expect_event("{\"nested\":{\"hook_event_name\":\"Stop\","
               "\"stop_hook_active\":true},\"hook_event_name\":\"PreToolUse\"}",
               AGENT_EVENT_WORKING);
  expect_event("{\"hook_event_name\":\"Stop\",\"extra\":[true,false,null,"
               "{},[],[1,-2.5e+3,0,-0.0,1E-9]],\"stop_hook_active\":false}",
               AGENT_EVENT_DONE);
  expect_event("{\"text\":\"brace { quote \\\" slash \\\\ unicode \\u1234\","
               "\"hook_event_name\":\"Stop\"}",
               AGENT_EVENT_DONE);
  expect_event("{\"hook_event_name\":\"Stop\",\"text\":\"猫😀\"}",
               AGENT_EVENT_DONE);
}

static void test_invalid_json(void) {
  const char *invalid[] = {"",
                           "[]",
                           "null",
                           "{}{}",
                           "{",
                           "{\"hook_event_name\":\"Stop\"",
                           "{\"x\":true false}",
                           "{\"x\":tru}",
                           "{\"x\":nulll}",
                           "{\"x\":01}",
                           "{\"x\":-}",
                           "{\"x\":.1}",
                           "{\"x\":1.}",
                           "{\"x\":1e}",
                           "{\"x\":1e+}",
                           "{\"x\":+1}",
                           "{\"x\":1,}",
                           "{\"x\":[1,]}",
                           "{\"x\":[1}}",
                           "{\"x\" 1}",
                           "{\"x\":\"\\q\"}",
                           "{\"x\":\"\\u123x\"}",
                           "{\"x\":\"\n\"}",
                           "{\"x\":\"\xc0\xaf\"}",
                           "{\"x\":\"\xed\xa0\x80\"}",
                           "{\"x\":\"\xf4\x90\x80\x80\"}"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
      agent_hook_scanner_t s = scan(invalid[i], chunk, false);
      agent_event_t event;
      TEST_ASSERT(!agent_hook_event(&s, &event));
    }
  }
  agent_hook_scanner_t s;
  agent_hook_scan_init(&s);
  const char nul[] = "{\"x\":\"\\\0\",\"hook_event_name\":\"Stop\"}";
  agent_hook_scan_feed(&s, nul, sizeof(nul) - 1);
  TEST_ASSERT(!agent_hook_scan_finish(&s));
  agent_hook_scan_init(&s);
  for (int i = 0; i <= AGENT_HOOK_MAX_DEPTH; i++) {
    agent_hook_scan_feed(&s, "{\"x\":", 5);
  }
  TEST_ASSERT(s.failed);
}

static void test_hash_and_large_payload(void) {
  agent_hook_scanner_t s = scan("{\"session_id\":\"test\"}", 1, true);
  TEST_ASSERT(strcmp(s.session_id, "test") == 0);
  TEST_ASSERT(agent_hook_key("claude", &s) == UINT64_C(0xe430d22bdbbe8583));
  TEST_ASSERT(agent_hook_key("codex", &s) == UINT64_C(0x0f886b5c86d51f1a));
  s = scan("{}", 1, true);
  uint64_t fallback = agent_hook_key("claude", &s);
  TEST_ASSERT(fallback == UINT64_C(0x8cd5fc330ae6ec20));
  s = scan("{\"session_id\":\"te\\u0073t\"}", 1, true);
  TEST_ASSERT(agent_hook_key("claude", &s) == fallback);
  char value[256], json[512];
  memset(value, 'x', sizeof(value) - 1);
  value[sizeof(value) - 1] = '\0';
  snprintf(json, sizeof(json), "{\"session_id\":\"%s\"}", value);
  s = scan(json, 1, true);
  TEST_ASSERT(agent_hook_key("claude", &s) == fallback);
  snprintf(json, sizeof(json), "{\"hook_event_name\":\"%s\"}", value);
  expect_event(json, -1);
  s = scan("{\"session_id\":false}", 1, true);
  TEST_ASSERT(agent_hook_key("claude", &s) == fallback);
  // A multi-megabyte ignored string must not impose a total input limit.
  agent_hook_scan_init(&s);
  const char *prefix = "{\"tool_input\":{\"content\":\"";
  agent_hook_scan_feed(&s, prefix, strlen(prefix));
  char chunk[4096];
  memset(chunk, 'x', sizeof(chunk));
  for (int i = 0; i < 1024; i++) {
    agent_hook_scan_feed(&s, chunk, sizeof(chunk));
  }
  const char *suffix = "\"},\"hook_event_name\":\"PreToolUse\","
                       "\"session_id\":\"test\"}";
  agent_hook_scan_feed(&s, suffix, strlen(suffix));
  TEST_ASSERT(agent_hook_scan_finish(&s));
  agent_event_t event;
  TEST_ASSERT(agent_hook_event(&s, &event) && event == AGENT_EVENT_WORKING);
  TEST_ASSERT(agent_hook_key("claude", &s) == UINT64_C(0xe430d22bdbbe8583));
}

static void test_stat_and_agent(void) {
  const char *valid[] = {"a", "claude", "codex", "opencode"};
  const char *invalid[] = {NULL, "", "Claude", "a-b", "a1", "toooolong", "a b"};
  for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
    TEST_ASSERT(agent_hook_valid_agent(valid[i]));
  }
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    TEST_ASSERT(!agent_hook_valid_agent(invalid[i]));
  }
  char comm[64];
  pid_t parent;
  TEST_ASSERT(agent_hook_parse_stat("42 (odd ) ( name) S 123 0 0", comm,
                                    sizeof(comm), &parent) == 0);
  TEST_ASSERT(strcmp(comm, "odd ) ( name") == 0 && parent == 123);
  TEST_ASSERT(
      agent_hook_parse_stat("42 (sh) S 1 0", comm, sizeof(comm), &parent) == 0);
  const char *bad[] = {"",
                       "42 (sh)",
                       "42 (sh) ",
                       "42 (sh) S",
                       "42 (sh) S ",
                       "42 (sh) S -1",
                       "42 (sh) S 9999999999999999999999",
                       "42 sh S 1",
                       "42 (sh) S 1garbage"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    TEST_ASSERT(agent_hook_parse_stat(bad[i], comm, sizeof(comm), &parent) ==
                -1);
  }
  TEST_ASSERT(agent_hook_parse_stat("42 (bash) S 1", comm, 2, &parent) == -1);
  unsigned long tty = 1;
  TEST_ASSERT(agent_hook_stat_tty("42 (odd ) ( name) S 9 1 1 7 3", &tty) == 0 &&
              tty == 7);
  TEST_ASSERT(agent_hook_stat_tty("42 (codex) S 1 42 42 0 42", &tty) == 0 &&
              tty == 0);
  TEST_ASSERT(agent_hook_stat_tty("42 (codex) S 1 42 42 34817 42", &tty) == 0 &&
              tty == 34817);
  TEST_ASSERT(agent_hook_stat_tty("42 (codex) S 1 42 42 34817\n", &tty) == 0 &&
              tty == 34817);
  TEST_ASSERT(agent_hook_stat_tty("42 (sh) S 1 0 0", &tty) == -1);
  TEST_ASSERT(agent_hook_stat_tty("42 (sh) S 1", &tty) == -1);
  TEST_ASSERT(agent_hook_stat_tty(NULL, &tty) == -1);
}

static pid_t spawn_paused(bool with_tty, int *master) {
  char *name = NULL;
  *master = -1;
  if (with_tty) {
    *master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    TEST_ASSERT(*master >= 0 && grantpt(*master) == 0 &&
                unlockpt(*master) == 0);
    name = ptsname(*master);
    TEST_ASSERT(name);
  }
  int ready[2];
  TEST_ASSERT(pipe2(ready, O_CLOEXEC) == 0);
  int master_fd = *master;
  pid_t pid = fork();
  TEST_ASSERT(pid >= 0);
  if (pid == 0) {
    close(ready[0]);
    if (master_fd >= 0)
      close(master_fd);
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1)
      _exit(0);
    if (setsid() < 0)
      _exit(1);
    if (with_tty) {
      int slave = open(name, O_RDWR);
      if (slave < 0 || ioctl(slave, TIOCSCTTY, 0) < 0)
        _exit(1);
    }
    char ok = 1;
    if (write(ready[1], &ok, 1) != 1)
      _exit(1);
    for (;;)
      pause();
  }
  close(ready[1]);
  char ok = 0;
  TEST_ASSERT(read(ready[0], &ok, 1) == 1);
  close(ready[0]);
  return pid;
}

static void stop_child(pid_t pid, int master) {
  if (pid > 0)
    kill(pid, SIGKILL);
  if (master >= 0)
    close(master);
  if (pid > 0)
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
}

static void test_process_tty(void) {
  int master = -1;
  pid_t daemon = spawn_paused(false, &master);
  pid_t live = spawn_paused(true, &master);
  int live_master = master;
  TEST_ASSERT(agent_process_tty(daemon) == 0);
  TEST_ASSERT(agent_process_tty(live) == 1);
  TEST_ASSERT(agent_process_tty(0) == -1);
  stop_child(daemon, -1);
  stop_child(live, live_master);
}

static void test_cwd(void) {
  // Virtual paths avoid inheriting a repository marker in the shared /tmp.
  const char *inputs[] = {
      "{\"cwd\":\"/herdcat-test-input/项目 with spaces/\"}",
      "{\"cwd\":\"/herdcat-test-input/\\u9879\\u76ee with spaces\"}",
      "{\"cwd\":\"/herdcat-test-input/pro\\nject\\t\"}",
      "{\"cwd\":\"/herdcat-test-input/猫猫猫猫猫猫猫猫猫猫猫猫猫猫\"}",
      "{\"cwd\":\"/herdcat-test-input/\\ud83d\\ude00\"}",
      "{\"cwd\":\"relative\"}",
      "{\"cwd\":\"/\"}",
      "{\"x\":{\"cwd\":\"/herdcat-test-input/wrong\"}}",
      "{\"cwd\":false}"};
  const char *expected[] = {"项目 with spaces",
                            "项目 with spaces",
                            "project",
                            "猫猫猫猫猫猫猫猫猫猫猫猫猫",
                            "😀",
                            NULL,
                            NULL,
                            NULL,
                            NULL};
  for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
    for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
      agent_hook_scanner_t scanner = scan(inputs[i], chunk, true);
      char name[41];
      TEST_ASSERT(agent_hook_name(&scanner, name) == (expected[i] != NULL));
      if (expected[i])
        TEST_ASSERT(strcmp(name, expected[i]) == 0);
    }
  }
  char json[300];
  memset(json, 'x', sizeof(json));
  memcpy(json, "{\"cwd\":\"/", 9);
  memcpy(json + 290, "\"}", 3);
  agent_hook_scanner_t scanner = scan(json, 1, true);
  char name[41];
  TEST_ASSERT(!agent_hook_name(&scanner, name));
}

static void remove_tree(const char *path) {
  struct stat st;
  if (lstat(path, &st))
    return;
  if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
    unlink(path);
    return;
  }
  DIR *dir = opendir(path);
  if (!dir)
    return;
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    char child[512];
    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    remove_tree(child);
  }
  closedir(dir);
  rmdir(path);
}

static void test_repo_name(void) {
  char root[] = "/tmp/herdcat-name-XXXXXX";
  TEST_ASSERT(mkdtemp(root));
  char repo[160], src[180], git[180], name[41];
  snprintf(repo, sizeof(repo), "%s/repo", root);
  snprintf(src, sizeof(src), "%s/src", repo);
  snprintf(git, sizeof(git), "%s/.git", repo);
  TEST_ASSERT(mkdir(repo, 0700) == 0 && mkdir(git, 0700) == 0 &&
              mkdir(src, 0700) == 0);
  TEST_ASSERT(agent_hook_place_name(src, name) && !strcmp(name, "repo"));

  char work[160], pkg[180], marker[180];
  snprintf(work, sizeof(work), "%s/work", root);
  snprintf(pkg, sizeof(pkg), "%s/pkg", work);
  snprintf(marker, sizeof(marker), "%s/.git", work);
  TEST_ASSERT(mkdir(work, 0700) == 0 && mkdir(pkg, 0700) == 0);
  FILE *file = fopen(marker, "w");
  TEST_ASSERT(file);
  TEST_ASSERT(fputs("gitdir: /nowhere\n", file) > 0);
  TEST_ASSERT(fclose(file) == 0);
  TEST_ASSERT(agent_hook_place_name(pkg, name) && !strcmp(name, "work"));

  char parent[160], plain[180];
  snprintf(parent, sizeof(parent), "%s/plain", root);
  snprintf(plain, sizeof(plain), "%s/leaf", parent);
  TEST_ASSERT(mkdir(parent, 0700) == 0 && mkdir(plain, 0700) == 0);
  TEST_ASSERT(agent_hook_place_name(plain, name) && !strcmp(name, "leaf"));

  char deep[400], topgit[420];
  snprintf(deep, sizeof(deep), "%s/top", root);
  TEST_ASSERT(mkdir(deep, 0700) == 0);
  snprintf(topgit, sizeof(topgit), "%s/.git", deep);
  TEST_ASSERT(mkdir(topgit, 0700) == 0);
  for (int i = 0; i < 16; i++) {
    size_t used = strlen(deep);
    TEST_ASSERT(used + 4 < sizeof(deep));
    snprintf(deep + used, sizeof(deep) - used, "/%02d", i);
    TEST_ASSERT(mkdir(deep, 0700) == 0);
  }
  TEST_ASSERT(agent_hook_place_name(deep, name) && !strcmp(name, "15"));

  char json[512];
  snprintf(json, sizeof(json), "{\"cwd\":\"%s\"}", src);
  agent_hook_scanner_t scanner = scan(json, 32, true);
  TEST_ASSERT(agent_hook_name(&scanner, name) && !strcmp(name, "repo"));
  remove_tree(root);
}

static void fake_process(const char *root, int pid, const char *comm, int tty,
                         const char *cwd) {
  char path[256], line[128];
  snprintf(path, sizeof(path), "%s/%d", root, pid);
  TEST_ASSERT(mkdir(path, 0700) == 0);
  snprintf(path, sizeof(path), "%s/%d/stat", root, pid);
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file);
  snprintf(line, sizeof(line), "%d (%s) S 1 %d %d %d 0 0\n", pid, comm, pid,
           pid, tty);
  TEST_ASSERT(fputs(line, file) > 0 && fclose(file) == 0);
  snprintf(path, sizeof(path), "%s/%d/cwd", root, pid);
  TEST_ASSERT(symlink(cwd, path) == 0);
}

static void test_front_process(void) {
  char root[] = "/tmp/herdcat-front-XXXXXX";
  TEST_ASSERT(mkdtemp(root));
  fake_process(root, 50, "codex", 0, "/work/a");      // The server: no tty.
  fake_process(root, 60, "codex", 34817, "/work/a");  // Its terminal program.
  fake_process(root, 70, "codex", 34818, "/work/b");
  fake_process(root, 80, "zsh", 34819, "/work/a");
  TEST_ASSERT(agent_hook_front_process(root, "codex", "/work/a") == 60);
  TEST_ASSERT(agent_hook_front_process(root, "codex", "/work/b") == 70);
  TEST_ASSERT(agent_hook_front_process(root, "codex", "/work/c") == 0);
  TEST_ASSERT(agent_hook_front_process(root, "claude", "/work/a") == 0);
  TEST_ASSERT(agent_hook_front_process(root, "codex", "work/a") == 0);
  TEST_ASSERT(agent_hook_front_process(root, "codex", NULL) == 0);
  TEST_ASSERT(agent_hook_front_process("/nonexistent", "codex", "/work/a") ==
              0);
  // Two terminals in one directory: no guess.
  fake_process(root, 90, "codex", 34820, "/work/a");
  TEST_ASSERT(agent_hook_front_process(root, "codex", "/work/a") == 0);
  remove_tree(root);
}

static void test_prompt(void) {
  const char *inputs[] = {" hello   world ", "\\n  hello\\t  world  ",
                          "hello\\nignored", "\\u4f60\\u597d \\ud83d\\ude3a",
                          " /help",          "  \\n\\t ",
                          "bad\\u0000text",  "bad\\u007ftext",
                          "\\ud800",         "\\udc00",
                          "你好世界"};
  const char *expected[] = {"hello world", "hello world", "hello",   "你好 😺",
                            NULL,          NULL,          NULL,      NULL,
                            NULL,          NULL,          "你好世界"};
  for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); i++) {
    char json[512];
    snprintf(json, sizeof(json),
             "{\"hook_event_name\":\"UserPromptSubmit\",\"prompt\":\"%s\"}",
             inputs[i]);
    for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
      agent_hook_scanner_t s = scan(json, chunk, true);
      char out[AGENT_TITLE_MAX + 1];
      TEST_ASSERT(agent_hook_prompt(&s, NULL, out) == (expected[i] != NULL));
      TEST_ASSERT(!strcmp(out, expected[i] ? expected[i] : ""));
    }
  }
  agent_hook_scanner_t s = scan(
      "{\"hook_event_name\":\"PreToolUse\",\"prompt\":\"ignored\"}", 1, true);
  char out[AGENT_TITLE_MAX + 1];
  TEST_ASSERT(!agent_hook_prompt(&s, NULL, out));
  s = scan("{\"hook_event_name\":\"UserPromptSubmit\",\"prompt\":7}", 1, true);
  TEST_ASSERT(!agent_hook_prompt(&s, NULL, out));
  s = scan("{\"hook_event_name\":\"UserPromptSubmit\",\"nested\":{\"prompt\":"
           "\"no\"}}",
           1, true);
  TEST_ASSERT(!agent_hook_prompt(&s, NULL, out));
  // A long leading pad and body do not change the bounded prefix storage.
  agent_hook_scan_init(&s);
  const char *prefix =
      "{\"hook_event_name\":\"UserPromptSubmit\",\"prompt\":\"";
  agent_hook_scan_feed(&s, prefix, strlen(prefix));
  char block[4096];
  memset(block, ' ', sizeof(block));
  agent_hook_scan_feed(&s, block, sizeof(block));
  agent_hook_scan_feed(&s, block, sizeof(block));
  for (int i = 0; i < 40; i++)
    agent_hook_scan_feed(&s, "猫", 3);
  memset(block, 'x', sizeof(block));
  agent_hook_scan_feed(&s, block, sizeof(block));
  agent_hook_scan_feed(&s, "\"}", 2);
  TEST_ASSERT(agent_hook_scan_finish(&s) && agent_hook_prompt(&s, NULL, out));
  TEST_ASSERT(strlen(out) == 96);
  for (int i = 0; i < 32; i++)
    TEST_ASSERT(!memcmp(out + i * 3, "猫", 3));
}
static void test_hook_title(void) {
  const char *inputs[] = {"\"Bridge title\"",
                          "\"\\u4f60\\u597d\"",
                          "\"New session - timestamp\"",
                          "\"\"",
                          "7",
                          "\"bad\\nline\"",
                          "\"\\ud800\"",
                          "\"bad\\u0000text\""};
  const char *expected[] = {"Bridge title", "你好", NULL, NULL,
                            NULL,           NULL,   NULL, NULL};
  for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); i++) {
    char json[256], out[97];
    snprintf(json, sizeof(json), "{\"title\":%s}", inputs[i]);
    agent_hook_scanner_t s;
    agent_hook_scan_adapter(&s, agent_adapter_find("opencode"));
    for (size_t j = 0; j < strlen(json); j++)
      agent_hook_scan_feed(&s, json + j, 1);
    TEST_ASSERT(agent_hook_scan_finish(&s));
    TEST_ASSERT(agent_hook_title(&s, out) == (expected[i] != NULL));
    TEST_ASSERT(!strcmp(out, expected[i] ? expected[i] : ""));
  }
}
static void test_agy_log_path(void) {
  char home[] = "/tmp/herdcat-agy-fd-XXXXXX";
  TEST_ASSERT(mkdtemp(home));
  const char *previous = getenv("HOME");
  char *saved = previous ? strdup(previous) : NULL;
  TEST_ASSERT(setenv("HOME", home, 1) == 0);
  char proc[256], process[256], fds[256], link[300], log[512], out[1025];
  snprintf(proc, sizeof(proc), "%s/proc", home);
  snprintf(process, sizeof(process), "%s/proc/42", home);
  snprintf(fds, sizeof(fds), "%s/proc/42/fd", home);
  TEST_ASSERT(mkdir(proc, 0700) == 0 && mkdir(process, 0700) == 0 &&
              mkdir(fds, 0700) == 0);
  snprintf(link, sizeof(link), "%s/7", fds);
  snprintf(log, sizeof(log), "%s/.gemini/antigravity-cli/log/cli-test.log",
           home);
  TEST_ASSERT(symlink(log, link) == 0);
  TEST_ASSERT(agent_hook_agy_log(proc, 42, out) && !strcmp(out, log));
  unlink(link);
  const char *bad[] = {"/tmp/cli-outside.log", "/tmp/cli.log", "/dev/pts/1"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    TEST_ASSERT(symlink(bad[i], link) == 0);
    TEST_ASSERT(!agent_hook_agy_log(proc, 42, out));
    unlink(link);
  }
  // The 'latest' symlink, deleted files and ambiguous descriptors fail closed.
  snprintf(log, sizeof(log), "%s/.gemini/antigravity-cli/cli.log", home);
  TEST_ASSERT(symlink(log, link) == 0);
  TEST_ASSERT(!agent_hook_agy_log(proc, 42, out));
  unlink(link);
  snprintf(log, sizeof(log),
           "%s/.gemini/antigravity-cli/log/cli-test.log (deleted)", home);
  TEST_ASSERT(symlink(log, link) == 0);
  TEST_ASSERT(!agent_hook_agy_log(proc, 42, out));
  unlink(link);
  snprintf(log, sizeof(log), "%s/.gemini/antigravity-cli/log/cli-test.log",
           home);
  TEST_ASSERT(symlink(log, link) == 0);
  char second[300];
  snprintf(second, sizeof(second), "%s/8", fds);
  strcat(log, ".log");
  TEST_ASSERT(symlink(log, second) == 0);
  TEST_ASSERT(!agent_hook_agy_log(proc, 42, out));
  unlink(link);
  unlink(second);
  rmdir(fds);
  rmdir(process);
  rmdir(proc);
  rmdir(home);
  if (saved) {
    TEST_ASSERT(setenv("HOME", saved, 1) == 0);
    free(saved);
  } else
    unsetenv("HOME");
}

int main(void) {
  test_agy_log_path();
  test_prompt();
  test_hook_title();
  test_front_process();
  test_cwd();
  test_repo_name();
  test_mapping();
  test_invalid_json();
  test_hash_and_large_payload();
  test_stat_and_agent();
  test_process_tty();
  return 0;
}
