#define _GNU_SOURCE
#include "platform/compositor.h"
#include "platform/focus.h"
#include "test_helpers.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int parse(const char *s, focus_window_t *out) {
  return focus_parse_windows(s, strlen(s), out, 8);
}
int main(int argc, char **argv) {
  if (argc == 2 || argc == 3) {
    if (argc == 3 && !strcmp(argv[2], "hyprland"))
      compositor_configure(true);
    if (focus_session_window((pid_t)atoi(argv[1])) < 0)
      return 2;
    TEST_ASSERT(focus_session_window((pid_t)atoi(argv[1])) < 0);
    while (focus_timeout() >= 0) {
      struct pollfd fd = {.fd = focus_poll_fd(), .events = POLLIN};
      poll(&fd, 1, focus_timeout());
      focus_poll();
    }
    printf("%d\n", focus_take_result());
    TEST_ASSERT(focus_take_result() == FOCUS_PENDING);
    focus_cleanup();
    return 0;
  }
  focus_window_t windows[8];
  const char *json =
      "[{\"title\":\"quote \\\" brace } {\",\"pid\":42,\"id\":7,"
      "\"extra\":{\"a\":[null,true,{},[1.2e-3]]}}, {\"id\":8,\"pid\":null}]";
  TEST_ASSERT(parse(json, windows) == 1);
  TEST_ASSERT(windows[0].id == 7 && windows[0].pid == 42);
  for (size_t i = 0; i < strlen(json); i++)
    TEST_ASSERT(focus_parse_windows(json, i, windows, 8) == -1);
  const char *bad[] = {"",
                       "{}",
                       "[{}]",
                       "[{\"id\":1}]",
                       "[null]",
                       "[]x",
                       "[{\"id\":1,\"pid\":2,}]",
                       "[{\"id\":1,\"pid\":-2}]",
                       "[{\"id\":01,\"pid\":2}]",
                       "[{\"id\":1,\"pid\":2.5}]",
                       "[{\"id\":18446744073709551616,\"pid\":2}]",
                       "[{\"id\":1,\"pid\":2147483648}]",
                       "[{\"id\":1,\"pid\":2,\"id\":2}]"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    TEST_ASSERT(parse(bad[i], windows) == -1);
  TEST_ASSERT(parse(" [] \n", windows) == 0);
  TEST_ASSERT(focus_parse_windows(json, strlen(json), windows, 0) == -1);
  uint64_t id = 0;
  windows[0] = (focus_window_t){.id = 100, .pid = getppid()};
  windows[1] = (focus_window_t){.id = 200, .pid = getpid()};
  TEST_ASSERT(focus_find_window(getpid(), windows, 2, &id) && id == 200);
  TEST_ASSERT(focus_find_window(getpid(), windows, 1, &id) && id == 100);
  TEST_ASSERT(!focus_find_window(getpid(), windows, 0, &id));
  TEST_ASSERT(!focus_find_window(2147483647, windows, 1, &id));
  char match[32];
  TEST_ASSERT(
      focus_kitty_target("007", "unix:/tmp/kitty.sock", match, sizeof(match)));
  TEST_ASSERT(!strcmp(match, "id:7"));
  TEST_ASSERT(
      focus_kitty_target("0", "unix:/run/kitty", match, sizeof(match)) &&
      !strcmp(match, "id:0"));
  TEST_ASSERT(focus_kitty_target("18446744073709551615", "unix:/run/kitty",
                                 match, sizeof(match)));
  TEST_ASSERT(!strcmp(match, "id:18446744073709551615"));
  const char *rejected[] = {NULL,
                            "",
                            "12a",
                            "+3",
                            "-1",
                            " 3",
                            "18446744073709551616",
                            "123456789012345678901"};
  for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
    TEST_ASSERT(!focus_kitty_target(rejected[i], "unix:/run/kitty", match,
                                    sizeof(match)));
  TEST_ASSERT(!focus_kitty_target("3", NULL, match, sizeof(match)));
  TEST_ASSERT(
      !focus_kitty_target("3", "tcp:127.0.0.1:9", match, sizeof(match)));
  TEST_ASSERT(
      !focus_kitty_target("3", "UNIX:/run/kitty", match, sizeof(match)));
  TEST_ASSERT(
      !focus_kitty_target("3", "unix:/tmp/bad\n", match, sizeof(match)));
  TEST_ASSERT(
      !focus_kitty_target("3", "unix:/tmp/bad\x7f", match, sizeof(match)));
  char long_socket[160];
  memset(long_socket, 'a', sizeof(long_socket));
  memcpy(long_socket, "unix:", 5);
  long_socket[128] = '\0';
  TEST_ASSERT(!focus_kitty_target("3", long_socket, match, sizeof(match)));
  long_socket[127] = '\0';
  TEST_ASSERT(focus_kitty_target("3", long_socket, match, sizeof(match)));
  TEST_ASSERT(!focus_kitty_target("3", "unix:/run/kitty", match, 4));
  unsetenv("NIRI_SOCKET");
  TEST_ASSERT(!focus_available() && focus_session_window(getpid()) < 0);
  TEST_ASSERT(focus_timeout() == -1 && focus_poll_fd() == -1);
  focus_cleanup();
  return 0;
}
