#define _GNU_SOURCE
#include "graphics/sign_palette.h"
#include "platform/agent_watch.h"
#include "platform/theme_watch.h"
#include "test_helpers.h"

#include <poll.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>

static unsigned spawn_calls;
int __real_posix_spawnp(pid_t *pid, const char *file,
                        const posix_spawn_file_actions_t *actions,
                        const posix_spawnattr_t *attributes, char *const args[],
                        char *const environment[]);
int __wrap_posix_spawnp(pid_t *pid, const char *file,
                        const posix_spawn_file_actions_t *actions,
                        const posix_spawnattr_t *attributes, char *const args[],
                        char *const environment[]);
int __wrap_posix_spawnp(pid_t *pid, const char *file,
                        const posix_spawn_file_actions_t *actions,
                        const posix_spawnattr_t *attributes, char *const args[],
                        char *const environment[]) {
  spawn_calls++;
  return __real_posix_spawnp(pid, file, actions, attributes, args, environment);
}
static bool parse(const char *s, bool signal, sign_theme_t *theme) {
  return theme_parse(s, strlen(s), signal, theme);
}
int main(int argc, char **argv) {
  sign_theme_t theme;
  if (argc > 1) {
    TEST_ASSERT(agent_watch_init() == 0);
    agent_watch_on_ready(theme_watch_ready);
    theme_watch_configure(SIGN_THEME_LIGHT);
    theme_watch_configure(SIGN_THEME_DARK);
    TEST_ASSERT(theme_watch_timeout() == -1);
    TEST_ASSERT(spawn_calls == 0);
    theme_watch_configure(SIGN_THEME_AUTO);
    bool missing = !strcmp(argv[1], "missing");
    TEST_ASSERT(theme_tool_available() != missing);
    if (missing)
      TEST_ASSERT(spawn_calls == 0);
    bool reconnect = !strcmp(argv[1], "reconnect");
    for (int i = 0; i < (reconnect ? 230 : 60); i++) {
      struct pollfd fd = {.fd = agent_watch_fd(), .events = POLLIN};
      poll(&fd, 1, 10);
      agent_watch_process(NULL);
      theme_watch_poll();
      if (i == 10 && !missing && !reconnect)
        TEST_ASSERT(sign_theme_effective(SIGN_THEME_AUTO) == SIGN_THEME_DARK);
    }
    TEST_ASSERT(sign_theme_effective(SIGN_THEME_AUTO) == SIGN_THEME_LIGHT);
    theme_watch_configure(SIGN_THEME_LIGHT);
    TEST_ASSERT(theme_watch_timeout() == -1);
    theme_watch_cleanup();
    agent_watch_cleanup();
    return 0;
  }
  const char *read = "{\"type\":\"v\",\"data\":[{\"type\":\"u\",\"data\":1}]}";
  TEST_ASSERT(parse(read, false, &theme) && theme == SIGN_THEME_DARK);
  TEST_ASSERT(parse("{\"type\":\"v\",\"data\":[{\"type\":\"v\",\"data\":[{"
                    "\"type\":\"u\",\"data\":2}]}]}",
                    false, &theme) &&
              theme == SIGN_THEME_LIGHT);
  for (size_t i = 0; i < strlen(read); i++)
    TEST_ASSERT(!theme_parse(read, i, false, &theme));
  const char *signal =
      "{\"type\":\"signal\",\"interface\":\"org.freedesktop.portal.Settings\","
      "\"member\":\"SettingChanged\",\"payload\":{\"type\":\"ssv\",\"data\":["
      "\"org.freedesktop.appearance\",\"color-scheme\",{\"type\":\"u\","
      "\"data\":1}]}}";
  TEST_ASSERT(parse(signal, true, &theme) && theme == SIGN_THEME_DARK);
  for (size_t i = 0; i < strlen(signal); i++)
    TEST_ASSERT(!theme_parse(signal, i, true, &theme));
  char buffer[1024];
  for (int value = 0; value <= 3; value++) {
    strcpy(buffer, signal);
    char *digit = strstr(buffer, "\"data\":1");
    digit[7] = (char)('0' + value);
    TEST_ASSERT(parse(buffer, true, &theme) == (value < 3));
    if (value < 3)
      TEST_ASSERT(theme == (value == 1 ? SIGN_THEME_DARK : SIGN_THEME_LIGHT));
  }
  strcpy(buffer, signal);
  strstr(buffer, "appearance")[0] = 'x';
  TEST_ASSERT(!parse(buffer, true, &theme));
  strcpy(buffer, signal);
  strstr(buffer, "SettingChanged")[0] = 'x';
  TEST_ASSERT(!parse(buffer, true, &theme));
  TEST_ASSERT(!parse("{\"type\":\"u\",\"data\":-1}", false, &theme));
  TEST_ASSERT(!parse("{\"type\":\"u\",\"data\":1,\"data\":2}", false, &theme));
  char *huge = malloc(65537);
  memset(huge, ' ', 65537);
  TEST_ASSERT(!theme_parse(huge, 65537, true, &theme));
  free(huge);
  puts("Theme parsing passed");
  return 0;
}
