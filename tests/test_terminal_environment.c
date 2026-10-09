#define _POSIX_C_SOURCE 200809L
#include "platform/agent_terminal.h"
#include "test_helpers.h"

#include <string.h>

int main(void) {
  const char *keys[] = {"TMUX", "TMUX_PANE", "WEZTERM_PANE",
                        "WEZTERM_UNIX_SOCKET", "TERM_PROGRAM"};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    TEST_ASSERT(unsetenv(keys[i]) == 0);
  agent_terminal_t terminal = {.kind = TERMINAL_NONE, .pane = 123};
  TEST_ASSERT(!agent_terminal_environment(&terminal));
  TEST_ASSERT(terminal.kind == TERMINAL_NONE && terminal.pane == 123);
  TEST_ASSERT(setenv("TERM_PROGRAM", "ghostty", 1) == 0);
  TEST_ASSERT(agent_terminal_environment(&terminal));
  TEST_ASSERT(terminal.kind == TERMINAL_GHOSTTY && terminal.pane == 0);

  // Pack the 1024-byte environment buffer exactly, including both NULs.
  // TMUX alone does not select tmux; TERM_PROGRAM still selects Ghostty.
  const size_t maximum =
      1024 - sizeof("TMUX=") - sizeof("TERM_PROGRAM=ghostty");
  char long_value[1100];
  memset(long_value, 'x', sizeof(long_value));
  long_value[maximum] = '\0';
  TEST_ASSERT(setenv("TMUX", long_value, 1) == 0);
  TEST_ASSERT(agent_terminal_environment(&terminal));
  TEST_ASSERT(terminal.kind == TERMINAL_GHOSTTY);
  const size_t oversized[] = {maximum + 1, 1018, 1019, 1099};
  for (size_t i = 0; i < sizeof(oversized) / sizeof(oversized[0]); i++) {
    memset(long_value, 'x', sizeof(long_value));
    long_value[oversized[i]] = '\0';
    TEST_ASSERT(setenv("TMUX", long_value, 1) == 0);
    terminal.kind = TERMINAL_NONE;
    terminal.pane = 123;
    TEST_ASSERT(!agent_terminal_environment(&terminal));
    TEST_ASSERT(terminal.kind == TERMINAL_NONE && terminal.pane == 123);
  }
  TEST_ASSERT(unsetenv("TMUX") == 0);
  TEST_ASSERT(agent_terminal_environment(&terminal));
  TEST_ASSERT(terminal.kind == TERMINAL_GHOSTTY);
  puts("Terminal environment length boundaries and recovery passed.");
  return 0;
}
