#define _GNU_SOURCE
#include "config/config.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static char path[] = "/tmp/herdcat-name-config-XXXXXX";
static void count_diagnostic(const config_diagnostic_t *diagnostic,
                             void *data) {
  (void)diagnostic;
  (*(int *)data)++;
}
static void put(const char *value) {
  FILE *f = fopen(path, "w");
  TEST_ASSERT(f);
  fputs(value, f);
  fclose(f);
}
static void configs(void) {
  config_t c;
  put("");
  TEST_ASSERT(load_config_strict(&c, path) == HERDCAT_SUCCESS);
  TEST_ASSERT(c.sign_name == SIGN_NAME_PROJECT &&
              c.sign_name_extra == SIGN_EXTRA_INLINE &&
              c.sign_title_length == 16 && !c.sign_nameplate[0]);
  config_cleanup_full(&c);
  put("sign_name=auto\n");
  int diagnostics = 0;
  TEST_ASSERT(load_config_report(&c, path, true, count_diagnostic,
                                 &diagnostics) == HERDCAT_SUCCESS);
  TEST_ASSERT(c.sign_name == SIGN_NAME_PROJECT && diagnostics == 0);
  config_cleanup_full(&c);
  const char *names[] = {"auto", "project", "title"},
             *extras[] = {"off", "inline", "end", "above", "below"};
  for (int n = 0; n < 3; n++)
    for (int e = 0; e < 5; e++) {
      char data[256];
      snprintf(data, sizeof(data),
               "sign_name=%s\nsign_name_extra=%s\nsign_title_length=64\nsign_"
               "nameplate=**{name}** · {state}\\n{title}\n",
               names[n], extras[e]);
      put(data);
      TEST_ASSERT(load_config_strict(&c, path) == HERDCAT_SUCCESS);
      TEST_ASSERT((int)c.sign_name == (n == 0 ? SIGN_NAME_PROJECT : n) &&
                  (int)c.sign_name_extra == e && c.sign_title_length == 64 &&
                  strstr(c.sign_nameplate, "\\n"));
      config_cleanup_full(&c);
    }
  const char *bad[] = {"sign_name=bad\n",
                       "sign_name_extra=bad\n",
                       "sign_title_length=-1\n",
                       "sign_title_length=65\n",
                       "sign_title_length=1x\n",
                       "sign_nameplate=**{name}\n",
                       "sign_nameplate={unknown}\n",
                       "sign_nameplate=a\\nb\\nc\n"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    put(bad[i]);
    TEST_ASSERT(load_config_strict(&c, path) != HERDCAT_SUCCESS);
  }
  put("sign_name=title\nsign_name_extra=below\nsign_title_length=0\n");
  TEST_ASSERT(load_config_strict(&c, path) == HERDCAT_SUCCESS);
  TEST_ASSERT(c.sign_title_length == 0);
  config_cleanup_full(&c);
  put("sign_nameplate=**broken\nsign_name_extra=end\n");
  TEST_ASSERT(load_config(&c, path) == HERDCAT_SUCCESS);
  TEST_ASSERT(!c.sign_nameplate[0] && c.sign_name_extra == SIGN_EXTRA_END);
  config_cleanup_full(&c);
}
int main(void) {
  int fd = mkstemp(path);
  TEST_ASSERT(fd >= 0);
  close(fd);
  configs();
  unlink(path);
  puts("name configuration defaults, values, strict reload and startup "
       "fallback passed");
  return 0;
}
