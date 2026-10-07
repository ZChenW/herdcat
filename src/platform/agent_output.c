#define _GNU_SOURCE
#include "platform/agent_output.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool agent_output_wchar(pid_t pid, uint64_t *wchar) {
  if (pid <= 0 || !wchar)
    return false;
  char path[64], target[128];
  snprintf(path, sizeof(path), "/proc/%jd/fd/1", (intmax_t)pid);
  ssize_t length = readlink(path, target, sizeof(target) - 1);
  if (length <= 0 || length >= (ssize_t)sizeof(target) - 1)
    return false;
  target[length] = '\0';
  const char *number = target + strlen("/dev/pts/");
  if (strncmp(target, "/dev/pts/", strlen("/dev/pts/")) || !*number ||
      strspn(number, "0123456789") != strlen(number))
    return false;
  snprintf(path, sizeof(path), "/proc/%jd/io", (intmax_t)pid);
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return false;
  char buffer[1024];
  ssize_t used;
  do {
    used = read(fd, buffer, sizeof(buffer) - 1);
  } while (used < 0 && errno == EINTR);
  close(fd);
  if (used <= 0 || used == (ssize_t)sizeof(buffer) - 1)
    return false;
  buffer[used] = '\0';
  // Ignore every other field; no output contents are opened or retained.
  for (char *line = buffer; line && *line;) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    if (!strncmp(line, "wchar: ", 7)) {
      const char *value = line + 7;
      if (!*value || strspn(value, "0123456789") != strlen(value))
        return false;
      errno = 0;
      char *end;
      unsigned long long parsed = strtoull(value, &end, 10);
      if (errno || *end || parsed > UINT64_MAX)
        return false;
      *wchar = (uint64_t)parsed;
      return true;
    }
    line = next;
  }
  return false;
}
