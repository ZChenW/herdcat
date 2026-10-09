#define _POSIX_C_SOURCE 200809L
#include "config/config.h"
#include "test_helpers.h"

#include <string.h>
#include <unistd.h>

typedef struct {
  int count;
  int first_line;
  config_severity_t severity;
} diagnostics_t;

static void remember_diagnostic(const config_diagnostic_t *item, void *data) {
  diagnostics_t *seen = data;
  if (seen->count == 0)
    seen->first_line = item->line;
  seen->count++;
  seen->severity = item->severity;
}

static void write_config(const char *path, const char *text) {
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file != NULL);
  TEST_ASSERT(fputs(text, file) >= 0);
  TEST_ASSERT(fclose(file) == 0);
}

static void expect_recovery(const char *path, const char *text) {
  write_config(path, text);
  for (int strict = 0; strict <= 1; strict++) {
    config_t config;
    diagnostics_t seen = {0};
    TEST_ASSERT(load_config_report(&config, path, strict != 0,
                                   remember_diagnostic, &seen) ==
                (strict ? HERDCAT_ERROR_CONFIG : HERDCAT_SUCCESS));
    TEST_ASSERT(seen.count > 0 && seen.first_line == 1);
    TEST_ASSERT(seen.severity == (strict ? CONFIG_ERROR : CONFIG_WARNING));
    // A malformed first record must not swallow a valid following setting.
    TEST_ASSERT(config.fps == 37);
    config_cleanup_full(&config);
  }
}

int main(void) {
  char path[] = "/tmp/hc-config-recovery-XXXXXX";
  int fd = mkstemp(path);
  TEST_ASSERT(fd >= 0);
  close(fd);
  const char *invalid[] = {"fps=\n",
                           "fps=999999999999999999999999999999999999\n",
                           "fps=2147483648\n",
                           "fps=-2147483649\n",
                           "fps=12junk\n",
                           "sleep_begin=24:00\n",
                           "sleep_end=12:60\n",
                           "sleep_begin=12:x0\n",
                           "sleep_end=12:0x\n",
                           "sleep_begin=1x:00\n",
                           "sleep_end=x1:00\n",
                           "sign_font=bad\x7f"
                           "font\n",
                           "[monitor:]\n",
                           "[unknown]\n",
                           "[monitor:missing-bracket\n",
                           "=42\n",
                           "missing-equals\n",
                           "keyboard_device=/etc/passwd\n",
                           "keyboard_device=/dev/input/../x\n"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    char text[1024];
    snprintf(text, sizeof(text), "%sfps=37\n", invalid[i]);
    expect_recovery(path, text);
  }
  // Enum errors abort strict loading; tolerant loading diagnoses and falls
  // back.
  const char *bad_enums[] = {"layer=invalid\n", "overlay_position=invalid\n",
                             "cat_align=invalid\n"};
  for (size_t i = 0; i < sizeof(bad_enums) / sizeof(bad_enums[0]); i++) {
    char text[128];
    snprintf(text, sizeof(text), "%sfps=37\n", bad_enums[i]);
    write_config(path, text);
    for (int strict = 0; strict <= 1; strict++) {
      diagnostics_t diagnostic = {0};
      config_t parsed;
      TEST_ASSERT(load_config_report(&parsed, path, strict != 0,
                                     remember_diagnostic, &diagnostic) ==
                  (strict ? HERDCAT_ERROR_CONFIG : HERDCAT_SUCCESS));
      TEST_ASSERT(diagnostic.count == 1 && diagnostic.first_line == 1);
      TEST_ASSERT(diagnostic.severity ==
                  (strict ? CONFIG_ERROR : CONFIG_WARNING));
      TEST_ASSERT(parsed.fps == (strict ? 60 : 37));
      TEST_ASSERT(parsed.layer == LAYER_TOP);
      TEST_ASSERT(parsed.overlay_position == POSITION_TOP);
      TEST_ASSERT(parsed.cat_align == ALIGN_CENTER);
      config_cleanup_full(&parsed);
    }
  }
  char long_line[1024];
  memset(long_line, 'x', 900);
  // A plausible setting embedded in an overlong line must not be parsed.
  memcpy(long_line + 511, "fps=99", 6);
  strcpy(long_line + 900, "\nfps=37\n");
  expect_recovery(path, long_line);
  memset(long_line, 'x', 900);
  long_line[900] = '\0';
  write_config(path, long_line);
  config_t config;
  diagnostics_t seen = {0};
  TEST_ASSERT(load_config_report(&config, path, true, remember_diagnostic,
                                 &seen) == HERDCAT_ERROR_CONFIG);
  TEST_ASSERT(config.fps == 60 && seen.count == 1 && seen.first_line == 1);
  config_cleanup_full(&config);

  write_config(path, "monitor=old\nmonitor= , \t ,\nfps=37\n");
  TEST_ASSERT(load_config_strict(&config, path) == HERDCAT_SUCCESS);
  TEST_ASSERT(config.num_output_names == 0 && config.output_name == NULL);
  config_cleanup_full(&config);
  write_config(path, "fps= \t37\t # later # earlier\n");
  TEST_ASSERT(load_config_strict(&config, path) == HERDCAT_SUCCESS);
  TEST_ASSERT(config.fps == 37);
  config_cleanup_full(&config);
  // Linux permits fopen(directory), but reading must propagate FILE_IO.
  TEST_ASSERT(load_config_strict(&config, "/tmp") == HERDCAT_ERROR_FILE_IO);
  config_cleanup_full(&config);
  TEST_ASSERT(unlink(path) == 0);
  puts("Configuration rejection, diagnostics and line recovery passed.");
  return 0;
}
