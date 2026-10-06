#include "graphics/sign_names.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

static void name(const sign_input_t *in, int i, const char *expected) {
  char out[128];
  sign_session_name(in, &in->sessions[i], out, NULL);
  TEST_ASSERT(!strcmp(out, expected));
}
int main(void) {
  agent_session_view_t s[3] = {
      {.key = 1,
       .order = 1,
       .name = "repo",
       .title = "First task",
       .state = AGENT_STATE_WAITING},
      {.key = 2,
       .order = 2,
       .name = "repo",
       .title = "Second task",
       .state = AGENT_STATE_WORKING},
      {.key = 3,
       .order = 3,
       .name = "other",
       .title = "Third task",
       .state = AGENT_STATE_IDLE   }
  };
  sign_input_t in = {.sessions = s,
                     .count = 3,
                     .name = SIGN_NAME_AUTO,
                     .idle = SIGN_IDLE_NEVER,
                     .title_length = 16};
  // The compatibility mode always selects projects, including collisions.
  name(&in, 0, "repo");
  name(&in, 1, "repo");
  name(&in, 2, "other");
  s[1].title[0] = 0;
  name(&in, 1, "repo");
  strcpy(s[2].name, "repo");
  name(&in, 2, "repo");
  s[0].state = AGENT_STATE_IDLE;
  s[1].state = AGENT_STATE_IDLE;
  name(&in, 2, "repo");
  in.sessions = s + 1;
  in.count = 2;
  name(&in, 1, "repo");
  in.sessions = s;
  in.count = 3;
  in.name = SIGN_NAME_PROJECT;
  name(&in, 0, "repo");
  name(&in, 1, "repo");
  in.name = SIGN_NAME_TITLE;
  name(&in, 0, "First task");
  name(&in, 1, "repo");
  char title[100];
  sign_title_truncate("abcdef", 3, title);
  TEST_ASSERT(!strcmp(title, "abc…"));
  sign_title_truncate("一二三四五", 3, title);
  TEST_ASSERT(!strcmp(title, "一二三…"));
  sign_title_truncate("一二三四五", 0, title);
  TEST_ASSERT(!strcmp(title, "一二三四五"));
  sign_title_truncate("😺x", 1, title);
  TEST_ASSERT(!strcmp(title, "😺…"));
  sign_title_truncate("abc", 3, title);
  TEST_ASSERT(!strcmp(title, "abc"));
  puts("project/title selection, compatibility mode and Unicode truncation "
       "passed");
  return 0;
}
