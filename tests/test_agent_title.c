#define _GNU_SOURCE
#include "core/agent_sessions.h"
#include "core/agent_title.h"
#include "platform/transcript_watch.h"
#include "test_helpers.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char home[] = "/tmp/herdcat-titles-XXXXXX";
static char path[1024];
static void put(const char *file, const char *data) {
  int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  TEST_ASSERT(fd >= 0);
  size_t n = strlen(data);
  TEST_ASSERT(write(fd, data, n) == (ssize_t)n);
  TEST_ASSERT(close(fd) == 0);
}
static void parses(const char *agent, const char *line, const char *expected) {
  char out[97] = "unchanged";
  bool ok = agent_title_line(agent, "test-id", line, strlen(line), out);
  TEST_ASSERT(ok == (expected != NULL));
  TEST_ASSERT(!strcmp(out, expected ? expected : ""));
}
static void parser(void) {
  parses(
      "claude",
      "{\"type\":\"ai-title\",\"aiTitle\":\"hello\",\"sessionId\":\"test-id\"}",
      "hello");
  parses("claude",
         "{\"type\":\"ai-title\",\"aiTitle\":\"\\u4f60\\u597d\\ud83d\\ude3a\","
         "\"sessionId\":\"test-id\"}",
         "你好😺");
  parses("claude",
         "{\"type\":\"ai-title\",\"aiTitle\":\"bad\\u0000title\",\"sessionId\":"
         "\"test-id\"}",
         NULL);
  parses("claude",
         "{\"type\":\"ai-title\",\"aiTitle\":\"\\ud800\",\"sessionId\":\"test-"
         "id\"}",
         NULL);
  parses("claude",
         "{\"type\":\"ai-title\",\"aiTitle\":\"bad\\nline\",\"sessionId\":"
         "\"test-id\"}",
         NULL);
  parses("claude",
         "{\"type\":\"ai-title\",\"aiTitle\":\"bad\\u0085line\",\"sessionId\":"
         "\"test-id\"}",
         NULL);
  parses("claude",
         "{\"type\":\"ai-title\",\"aiTitle\":\"x\",\"sessionId\":\"other\"}",
         NULL);
  parses("claude",
         "{\"type\":\"ai-title\",\"aiTitle\":\"x\",\"aiTitle\":\"y\"}", NULL);
  parses("claude", "{\"type\":\"ai-title\",\"aiTitle\":4}", NULL);
  parses("codex",
         "{\"id\":\"test-id\",\"thread_name\":\"索引标题\",\"updated_at\":"
         "\"time\"}",
         "索引标题");
  parses("codex", "{\"id\":\"test-id\",\"thread_name\":\"bad\",}", NULL);
  parses("codex", "{\"id\":\"test-id\",\"thread_name\":null}", NULL);
  parses("codex", "{\"id\":\"other\",\"thread_name\":\"title\"}", NULL);
  parses("codex",
         "{\"id\":\"test-id\",\"thread_name\":\"a\",\"id\":\"test-id\"}", NULL);
  parses("pi", "{\"type\":\"session_info\",\"name\":\"Pi title\"}", "Pi title");
  parses("grok",
         "{\"info\":{\"id\":\"test-id\"},\"generated_title\":\"Grok title\"}",
         "Grok title");
  parses("kimi", "{\"title\":\"Kimi title\"}", "Kimi title");
  char long_line[256], title[98];
  memset(title, 'a', 97);
  title[97] = 0;
  snprintf(long_line, sizeof(long_line),
           "{\"id\":\"test-id\",\"thread_name\":\"%s\"}", title);
  parses("codex", long_line, NULL);
  title[96] = 0;
  snprintf(long_line, sizeof(long_line),
           "{\"id\":\"test-id\",\"thread_name\":\"%s\"}", title);
  parses("codex", long_line, title);
  TEST_ASSERT(agent_session_id_valid("a_B-1"));
  TEST_ASSERT(!agent_session_id_valid("../bad") &&
              !agent_session_id_valid("two ids"));
}
static void tail_and_paths(void) {
  char out[97];
  snprintf(path, sizeof(path), "%s/transcript.jsonl", home);
  const char *a = "{\"type\":\"ai-title\",\"aiTitle\":\"first\",\"sessionId\":"
                  "\"test-id\"}\n";
  const char *b = "{\"type\":\"ai-title\",\"aiTitle\":\"last\",\"sessionId\":"
                  "\"test-id\"}\n";
  int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  TEST_ASSERT(fd >= 0 && write(fd, a, strlen(a)) == (ssize_t)strlen(a));
  // A sparse prefix places the old title outside the entire bounded tail.
  TEST_ASSERT(lseek(fd, AGENT_TITLE_TAIL_MAX + 128, SEEK_SET) > 0);
  TEST_ASSERT(write(fd, "\n", 1) == 1);
  TEST_ASSERT(write(fd, b, strlen(b)) == (ssize_t)strlen(b));
  // An overlong line must be skipped, even with title-like JSON inside it.
  char huge[5000];
  memset(huge, 'x', sizeof(huge));
  huge[4999] = '\n';
  TEST_ASSERT(write(fd, huge, sizeof(huge)) == sizeof(huge));
  TEST_ASSERT(write(fd, "{\"type\":\"ai-title\",", 19) == 19);
  close(fd);
  TEST_ASSERT(agent_title_read("claude", "test-id", path, out) &&
              !strcmp(out, "last"));
  put(path, a);
  fd = open(path, O_RDWR);
  TEST_ASSERT(fd >= 0);
  TEST_ASSERT(ftruncate(fd, AGENT_TITLE_TAIL_MAX * 2) == 0);
  close(fd);
  TEST_ASSERT(!agent_title_read("claude", "test-id", path, out) && !out[0]);
  char link[1024];
  snprintf(link, sizeof(link), "%s/link.jsonl", home);
  TEST_ASSERT(symlink(path, link) == 0);
  TEST_ASSERT(agent_title_open(link) < 0);
  unlink(link);
  char dir[1024];
  snprintf(dir, sizeof(dir), "%s/parent", home);
  TEST_ASSERT(mkdir(dir, 0700) == 0);
  snprintf(link, sizeof(link), "%s/link", home);
  TEST_ASSERT(symlink(dir, link) == 0);
  char nested[1100];
  snprintf(nested, sizeof(nested), "%s/data.jsonl", link);
  TEST_ASSERT(agent_title_open(nested) < 0);
  snprintf(nested, sizeof(nested), "%s/../transcript.jsonl", dir);
  TEST_ASSERT(agent_title_open(nested) < 0);
  TEST_ASSERT(agent_title_open("/etc/test.jsonl") < 0);
  TEST_ASSERT(agent_title_open(dir) < 0);
  char fifo[1024];
  snprintf(fifo, sizeof(fifo), "%s/pipe.jsonl", home);
  TEST_ASSERT(mkfifo(fifo, 0600) == 0 && agent_title_open(fifo) < 0);
  unlink(fifo);
  unlink(link);
  rmdir(dir);
  char codex[1024];
  snprintf(codex, sizeof(codex), "%s/.codex", home);
  TEST_ASSERT(mkdir(codex, 0700) == 0);
  unsetenv("CODEX_HOME");
  char index[1100];
  snprintf(index, sizeof(index), "%s/session_index.jsonl", codex);
  put(index,
      "{\"id\":\"test-id\",\"thread_name\":\"first\"}\n{\"id\":\"other\","
      "\"thread_name\":\"unrelated\"}\n{\"id\":\"test-id\",\"thread_name\":"
      "\"last\"}\n{\"id\":\"test-id\",\"thread_name\":\"partial\"}");
  TEST_ASSERT(agent_title_read("codex", "test-id", NULL, out) &&
              !strcmp(out, "last"));
  TEST_ASSERT(setenv("CODEX_HOME", "/etc", 1) == 0);
  TEST_ASSERT(!agent_title_read("codex", "test-id", NULL, out));
  TEST_ASSERT(setenv("CODEX_HOME", codex, 1) == 0);
  unlink(index);
  rmdir(codex);
  unlink(path);
}
static void other_sources(void) {
  char root[1024], bucket[1100], session[1200], file[1300], index[1100],
      data[1600], out[97];
  snprintf(root, sizeof(root), "%s/.kimi-code", home);
  TEST_ASSERT(mkdir(root, 0700) == 0);
  snprintf(bucket, sizeof(bucket), "%s/sessions", root);
  TEST_ASSERT(mkdir(bucket, 0700) == 0);
  snprintf(session, sizeof(session), "%s/test-id", bucket);
  TEST_ASSERT(mkdir(session, 0700) == 0);
  snprintf(file, sizeof(file), "%s/state.json", session);
  put(file, "{\"title\":\"Kimi title\"}");
  snprintf(index, sizeof(index), "%s/session_index.jsonl", root);
  snprintf(data, sizeof(data),
           "{\"sessionId\":\"test-id\",\"sessionDir\":\"%s\",\"workDir\":"
           "\"unused\"}\n",
           session);
  put(index, data);
  unsetenv("KIMI_CODE_HOME");
  TEST_ASSERT(agent_title_read("kimi", "test-id", NULL, out) &&
              !strcmp(out, "Kimi title"));
  put(file, "{\"title\":false}");
  TEST_ASSERT(!agent_title_read("kimi", "test-id", NULL, out));
  put(file, "{\"title\":\"Kimi title\"}");
  snprintf(data, sizeof(data),
           "{\"sessionId\":\"test-id\",\"sessionDir\":\"%s\"}\n", home);
  put(index, data);
  TEST_ASSERT(!agent_title_read("kimi", "test-id", NULL, out));
  unlink(index);
  unlink(file);
  rmdir(session);
  rmdir(bucket);
  rmdir(root);
  snprintf(file, sizeof(file), "%s/pi.jsonl", home);
  put(file, "{\"type\":\"session_info\",\"name\":\"first\"}\n{\"type\":"
            "\"message\",\"name\":\"not a "
            "title\"}\n{\"type\":\"session_info\",\"name\":\"last\"}\n");
  TEST_ASSERT(agent_title_read("pi", "test-id", file, out) &&
              !strcmp(out, "last"));
  unlink(file);
  snprintf(file, sizeof(file), "%s/summary.json", home);
  put(file,
      "{\"info\":{\"id\":\"test-id\"},\"generated_title\":\"Grok title\"}");
  TEST_ASSERT(agent_title_read("grok", "test-id", file, out) &&
              !strcmp(out, "Grok title"));
  TEST_ASSERT(!agent_title_read("grok", "other", file, out));
  unlink(file);
}
static void sessions(void) {
  agent_sessions_reset();
  TEST_ASSERT(agent_sessions_apply(1, "claude", AGENT_EVENT_START, 0, 1000, 5,
                                   NULL) == 0);
  TEST_ASSERT(agent_sessions_apply(2, "codex", AGENT_EVENT_START, 0, 1001, 5,
                                   NULL) == 0);
  TEST_ASSERT(agent_sessions_set_name(2, "repo") == 0 &&
              agent_sessions_set_name(1, "repo") == 0);
  TEST_ASSERT(agent_sessions_set_title(1, "Private title") == 0);
  TEST_ASSERT(agent_sessions_set_id(1, "test-id") == 0);
  TEST_ASSERT(agent_sessions_set_title(1, "Private title") == 0);
  TEST_ASSERT(agent_sessions_id_command("sid 0000000000000001 test-id") == 0);
  TEST_ASSERT(agent_sessions_id_command("sid 0000000000000001 ../bad") != 0);
  TEST_ASSERT(agent_sessions_id_command("sid 1 test-id") != 0);
  TEST_ASSERT(agent_sessions_set_title(1, "bad\nline") < 0);
  agent_session_record_t records[32];
  TEST_ASSERT(agent_sessions_export(records, 32) == 2);
  agent_sessions_reset();
  for (int i = 0; i < 2; i++)
    TEST_ASSERT(agent_sessions_restore(&records[i], 2000, 5) == 0);
  agent_session_view_t views[32];
  TEST_ASSERT(agent_sessions_snapshot(views, 32) == 2);
  TEST_ASSERT(!strcmp(views[0].title, "Private title") &&
              !strcmp(views[0].session_id, "test-id"));
  char list[1024];
  agent_sessions_format(list, sizeof(list), 2000);
  TEST_ASSERT(strstr(list, "title=Private title"));
}
static void copilot_yaml(void) {
  const char *inputs[] = {
      "summary: Old summary\nname: New name\nuser_named: true\n",
      "summary: Summary only",
      "name: ''\nsummary: fallback\n",
      "name: \"quoted title\" # comment\n",
      "name: 'single '' quote'\n",
      "name: plain title # comment\n",
      "name: \"\\u4f60\\u597d\"\n",
      "nested:\n  name: ignored\nsummary: Valid summary\n",
      "name: |\n  ignored\nsummary: >-\n  ignored\n",
      "name: [one, two]\nsummary: {title: map}\n",
      "name: &anchor text\n",
      "name: null\nsummary: ~\n",
      "name: \"unfinished\n",
      "name: 'unfinished\n",
      "name: \"bad\\nline\"\n",
      "name: \"bad\\u0000title\"\n",
      "name: \"\\ud800\"\n",
      "name: \"ok\"garbage\n",
      " name: nested\n"};
  const char *expected[] = {"New name",
                            "Summary only",
                            "fallback",
                            "quoted title",
                            "single ' quote",
                            "plain title",
                            "你好",
                            "Valid summary",
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL};
  for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); i++) {
    char out[97];
    TEST_ASSERT(agent_title_copilot_yaml(inputs[i], strlen(inputs[i]), out) ==
                (expected[i] != NULL));
    TEST_ASSERT(!strcmp(out, expected[i] ? expected[i] : ""));
  }
  char line[256];
  memset(line, 'x', sizeof(line));
  memcpy(line, "name: ", 6);
  line[sizeof(line) - 1] = 0;
  char out[97];
  TEST_ASSERT(!agent_title_copilot_yaml(line, strlen(line), out));
  TEST_ASSERT(!agent_title_copilot_yaml("name: x\0bad", 11, out));
  char root[1024], bucket[1100], session[1200], file[1300];
  snprintf(root, sizeof(root), "%s/.copilot", home);
  TEST_ASSERT(mkdir(root, 0700) == 0);
  snprintf(bucket, sizeof(bucket), "%s/session-state", root);
  TEST_ASSERT(mkdir(bucket, 0700) == 0);
  snprintf(session, sizeof(session), "%s/test-id", bucket);
  TEST_ASSERT(mkdir(session, 0700) == 0);
  snprintf(file, sizeof(file), "%s/workspace.yaml", session);
  put(file, "name: Copilot title\nsummary: Old summary\n");
  unsetenv("COPILOT_HOME");
  TEST_ASSERT(agent_title_read("copilot", "test-id", NULL, out) &&
              !strcmp(out, "Copilot title"));
  agent_sessions_reset();
  TEST_ASSERT(agent_sessions_apply(9, "copilot", AGENT_EVENT_START, 0, 1, 5,
                                   NULL) == 0);
  TEST_ASSERT(agent_sessions_set_id(9, "test-id") == 0);
  agent_session_record_t record;
  TEST_ASSERT(agent_sessions_export(&record, 1) == 1 &&
              !strcmp(record.title, "Copilot title") &&
              !record.title_temporary);
  TEST_ASSERT(setenv("COPILOT_HOME", "/etc", 1) == 0);
  TEST_ASSERT(!agent_title_read("copilot", "test-id", NULL, out));
  TEST_ASSERT(setenv("COPILOT_HOME", root, 1) == 0);
  TEST_ASSERT(!agent_title_read("copilot", "../bad", NULL, out));
  unlink(file);
  TEST_ASSERT(symlink("/etc/passwd", file) == 0);
  TEST_ASSERT(!agent_title_read("copilot", "test-id", NULL, out));
  unlink(file);
  TEST_ASSERT(mkfifo(file, 0600) == 0);
  TEST_ASSERT(!agent_title_read("copilot", "test-id", NULL, out));
  unlink(file);
  int fd = open(file, O_CREAT | O_WRONLY, 0600);
  TEST_ASSERT(fd >= 0 && ftruncate(fd, AGENT_TITLE_TAIL_MAX + 1) == 0);
  close(fd);
  TEST_ASSERT(!agent_title_read("copilot", "test-id", NULL, out));
  unlink(file);
  rmdir(session);
  rmdir(bucket);
  rmdir(root);
}
static void prompt_titles(void) {
  agent_sessions_reset();
  TEST_ASSERT(
      agent_sessions_apply(1, "claude", AGENT_EVENT_START, 0, 1, 5, NULL) == 0);
  TEST_ASSERT(agent_sessions_set_prompt(1, "first prompt") == 0);
  uint64_t generation = agent_sessions_generation();
  TEST_ASSERT(agent_sessions_set_prompt(1, "second prompt") == 0);
  TEST_ASSERT(agent_sessions_generation() == generation);
  agent_sessions_refresh_title(1);
  TEST_ASSERT(agent_sessions_generation() == generation);
  agent_session_record_t record;
  TEST_ASSERT(agent_sessions_export(&record, 1) == 1);
  TEST_ASSERT(record.title_temporary && !strcmp(record.title, "first prompt"));
  char list[256];
  agent_sessions_format(list, sizeof(list), 2);
  TEST_ASSERT(strstr(list, "title~=first prompt"));
  agent_sessions_reset();
  TEST_ASSERT(agent_sessions_restore(&record, 2, 5) == 0);
  TEST_ASSERT(agent_sessions_title_command("ttl 0000000000000001 real title") ==
              0);
  TEST_ASSERT(agent_sessions_title_command("ask 0000000000000001 ignored") ==
              0);
  TEST_ASSERT(agent_sessions_set_title(1, "") == 0);
  agent_sessions_refresh_title(1);
  TEST_ASSERT(agent_sessions_export(&record, 1) == 1);
  TEST_ASSERT(!record.title_temporary && !strcmp(record.title, "real title"));
  agent_sessions_format(list, sizeof(list), 2);
  TEST_ASSERT(strstr(list, "title=real title") && !strstr(list, "title~="));
  TEST_ASSERT(agent_sessions_title_command("ask 1 bad") != 0);
  TEST_ASSERT(agent_sessions_title_command("ask 0000000000000000 bad") != 0);
  TEST_ASSERT(agent_sessions_title_command("ask 0000000000000001 /help") != 0);
  TEST_ASSERT(agent_sessions_title_command("ask 0000000000000001 ") != 0);
  TEST_ASSERT(agent_sessions_title_command("ask 0000000000000001    ") != 0);
  TEST_ASSERT(agent_sessions_set_prompt(1, " /help") < 0);
  TEST_ASSERT(agent_sessions_title_command("ask 0000000000000001 bad\nline") !=
              0);
  TEST_ASSERT(agent_sessions_title_command("ttl 0000000000000001 bad\tline") !=
              0);
  agent_sessions_reset();
  TEST_ASSERT(agent_sessions_restore(&record, 3, 5) == 0);
  TEST_ASSERT(agent_sessions_export(&record, 1) == 1 &&
              !record.title_temporary);
}
int main(void) {
  TEST_ASSERT(mkdtemp(home) && setenv("HOME", home, 1) == 0);
  parser();
  tail_and_paths();
  other_sources();
  sessions();
  prompt_titles();
  copilot_yaml();
  rmdir(home);
  puts("title parser, bounded tails, paths, IDs and records passed");
  return 0;
}
