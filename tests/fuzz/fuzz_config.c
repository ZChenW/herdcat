// config/config_parse.c through the public loader, lenient and strict.
#define _GNU_SOURCE
#include "config/config.h"
#include "fuzz.h"

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

static void fuzz_config_quiet(const config_diagnostic_t *diagnostic,
                              void *data) {
  (void)data;
  // Touch what a caller would print.
  if (diagnostic->message)
    (void)strlen(diagnostic->message);
  if (diagnostic->key)
    (void)strlen(diagnostic->key);
}

static int fuzz_config(const uint8_t *data, size_t size) {
  if (size > FUZZ_MAX_INPUT)
    return 0;
  int fd = memfd_create("herdcat-fuzz-config", MFD_CLOEXEC);
  if (fd < 0)
    abort();
  if (size && write(fd, data, size) != (ssize_t)size)
    abort();
  char path[64];
  snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
  for (int strict = 0; strict < 2; strict++) {
    config_t config = {0};
    herdcat_error_t result =
        load_config_report(&config, path, strict, fuzz_config_quiet, NULL);
    (void)result;
    config_cleanup_full(&config);
  }
  close(fd);
  return 0;
}
FUZZ_ENTRY(fuzz_config)
