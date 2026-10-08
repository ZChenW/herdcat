#define _GNU_SOURCE
#include "core/agent_sessions.h"
#include "core/agent_title.h"
#include "platform/transcript_watch.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void parses(const char *line, const char *expected) {
  char out[AGENT_TITLE_MAX + 1];
  TEST_ASSERT(agent_prompt_line("agy", line, strlen(line), out) ==
              (expected != NULL));
  TEST_ASSERT(!strcmp(out, expected ? expected : ""));
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--transcript-prompt"))
    return agent_prompt_main(argc, argv);
  const char *first =
      "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\",\"content\":"
      "\"outside<USER_REQUEST>\\n  lakes   and \\\"rivers\\\" \\u4e2d\\n"
      "private second line\\n</USER_REQUEST><OTHER>private tail</OTHER>\"}";
  parses(first, "lakes and \"rivers\" 中");
  const char *invalid[] = {
      "{\"type\":\"USER_INPUT\",\"source\":\"SYSTEM\",\"content\":"
      "\"<USER_REQUEST>wrong</USER_REQUEST>\"}",
      "{\"type\":\"ASSISTANT\",\"source\":\"USER_EXPLICIT\",\"content\":"
      "\"<USER_REQUEST>wrong</USER_REQUEST>\"}",
      "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\",\"content\":"
      "\"unwrapped\"}",
      "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\",\"content\":"
      "\"<USER_REQUEST>unclosed\"}",
      "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\",\"content\":"
      "\"<USER_REQUEST></USER_REQUEST><OTHER>wrong</OTHER>\"}",
      "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\",\"content\":"
      "\"<USER_REQUEST>\\u0000</USER_REQUEST>\"}",
      "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\",\"content\":"
      "\"<USER_REQUEST>\\ud800</USER_REQUEST>\"}",
      "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\",\"content\":"
      "\"<USER_REQUEST>/command</USER_REQUEST>\"}",
      "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\",\"content\":4}",
      "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\",}",
      "{}",
      "[]",
      "{"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
    parses(invalid[i], NULL);
  char long_text[301], line[1024], expected[AGENT_TITLE_MAX + 1];
  memset(long_text, 'x', 300);
  long_text[300] = 0;
  memset(expected, 'x', AGENT_TITLE_MAX);
  expected[AGENT_TITLE_MAX] = 0;
  snprintf(line, sizeof(line),
           "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\","
           "\"content\":\"<USER_REQUEST>%s</USER_REQUEST>\"}",
           long_text);
  parses(line, expected);
  // Truncation must retain complete multibyte characters.
  long_text[0] = 0;
  expected[0] = 0;
  for (int i = 0; i < 40; i++) {
    strcat(long_text, "湖");
    if (i < 32)
      strcat(expected, "湖");
  }
  snprintf(line, sizeof(line),
           "{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\","
           "\"content\":\"<USER_REQUEST>%s</USER_REQUEST>\"}",
           long_text);
  parses(line, expected);

  char home[] = "/tmp/hc-agy-title-XXXXXX", path[256], log[256], out[97];
  TEST_ASSERT(mkdtemp(home));
  TEST_ASSERT(!setenv("HOME", home, 1));
  snprintf(path, sizeof(path), "%s/transcript.jsonl", home);
  snprintf(log, sizeof(log), "%s/cli-test.log", home);
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file);
  fprintf(file, "{}\n%s\n%s\n", first, line);
  TEST_ASSERT(!fclose(file));
  TEST_ASSERT(agent_prompt_read("agy", path, out));
  TEST_ASSERT(!strcmp(out, "lakes and \"rivers\" 中"));
  file = fopen(path, "w");
  TEST_ASSERT(file);
  fprintf(file, "%s\n%s\n", invalid[7], first);
  TEST_ASSERT(!fclose(file));
  // An unusable first explicit input must not promote a later input.
  TEST_ASSERT(!agent_prompt_read("agy", path, out) && !out[0]);
  file = fopen(path, "w");
  TEST_ASSERT(file);
  fprintf(file, "%s\n", first);
  TEST_ASSERT(!fclose(file));
  TEST_ASSERT(agent_prompt_read("agy", path, out));
  agent_sessions_reset();
  TEST_ASSERT(
      !agent_sessions_apply(1, "agy", AGENT_EVENT_WORKING, 0, 100, 5, NULL));
  TEST_ASSERT(!agent_sessions_set_id(1, "test-id"));
  transcript_watch_path(1, path, 100);
  agent_session_record_t record;
  TEST_ASSERT(agent_sessions_next_prompt(&record));
  TEST_ASSERT(!strcmp(record.transcript, path));
  transcript_watch_path(1, log, 100);
  TEST_ASSERT(agent_sessions_export(&record, 1) == 1);
  TEST_ASSERT(!strcmp(record.transcript, path));
  agent_sessions_recovered_prompt(record.key, record.order, out);
  agent_sessions_recovered_prompt(record.key, record.order, "second input");
  agent_session_view_t view;
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1);
  TEST_ASSERT(view.title_temporary && !strcmp(view.title, out));
  TEST_ASSERT(!agent_sessions_next_prompt(&record));
  transcript_watch_cleanup();
  file = fopen(path, "w");
  TEST_ASSERT(file);
  fputs("{\"type\":\"ASSISTANT\"}\n", file);
  TEST_ASSERT(!fclose(file));
  TEST_ASSERT(!agent_prompt_read("agy", path, out) && !out[0]);
  file = fopen(path, "w");
  TEST_ASSERT(file);
  fputs(first, file);  // An incomplete final record is ignored.
  TEST_ASSERT(!fclose(file));
  TEST_ASSERT(!agent_prompt_read("agy", path, out));
  TEST_ASSERT(!agent_prompt_read("agy", "/tmp/outside.jsonl", out));
  file = fopen(path, "w");
  TEST_ASSERT(file);
  fputs("{\"type\":\"USER_INPUT\",\"source\":\"USER_EXPLICIT\","
        "\"content\":\"<USER_REQUEST>",
        file);
  for (int i = 0; i < 5000; i++)
    fputc('x', file);
  fprintf(file, "</USER_REQUEST>\"}\n%s\n", first);
  TEST_ASSERT(!fclose(file));
  TEST_ASSERT(!agent_prompt_read("agy", path, out));
  file = fopen(path, "w");
  TEST_ASSERT(file);
  for (int i = 0; i < 256 * 1024; i++)
    fputc(' ', file);
  fprintf(file, "\n%s\n", first);
  TEST_ASSERT(!fclose(file));
  TEST_ASSERT(!agent_prompt_read("agy", path, out));
  TEST_ASSERT(!unlink(path));
  TEST_ASSERT(!rmdir(home));
  puts("Antigravity prompt tests passed");
  return 0;
}
