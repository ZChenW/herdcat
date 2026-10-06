#include "platform/focus.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
  pid_t pid = getpid();
  // clang-format off: versions disagree on aligning non-ASCII text.
  focus_window_t windows[4] = {
      {.id = 1, .pid = pid,        .title = "repo — unrelated"           },
      {.id = 2, .pid = pid,        .title = "Task alpha — terminal"      },
      {.id = 3, .pid = pid,        .title = "work — Task beta — terminal"},
      {.id = 4, .pid = pid + 1000, .title = "Task beta"                  }
  };
  // clang-format on
  agent_terminal_t t = {.kind = TERMINAL_GHOSTTY};
  uint64_t id = 0;
  TEST_ASSERT(focus_terminal_window_title(pid, &t, "repo", "Task beta", windows,
                                          4, &id) &&
              id == 3);
  TEST_ASSERT(focus_terminal_window_title(pid, &t, "repo", "missing", windows,
                                          4, &id) &&
              id == 1);
  TEST_ASSERT(
      focus_terminal_window_title(pid, &t, "repo", NULL, windows, 4, &id) &&
      id == 1);
  t.kind = TERMINAL_WEZTERM;
  t.window = 1;
  strcpy(t.title, "repo — unrelated");
  TEST_ASSERT(focus_terminal_window_title(pid, &t, "repo", "Task alpha",
                                          windows, 4, &id) &&
              id == 2);
  TEST_ASSERT(focus_terminal_window_title(pid, &t, "repo", "Task beta", windows,
                                          4, &id) &&
              id == 3);
  strcpy(windows[1].title, "Task beta duplicate");
  TEST_ASSERT(focus_terminal_window_title(pid, &t, "repo", "Task beta", windows,
                                          4, &id) &&
              id == 1);
  t.kind = TERMINAL_KITTY;
  t.window = 1;
  strcpy(t.title, "repo — unrelated");
  TEST_ASSERT(focus_terminal_window_title(pid, &t, "repo", "Task beta", windows,
                                          4, &id) &&
              id == 1);
  puts("Ghostty/WezTerm session-title priority, ambiguity and fallback passed");
  return 0;
}
