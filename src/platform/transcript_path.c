#define _GNU_SOURCE
#include "core/agent_title.h"
#include "core/agent_transcript.h"
#include "platform/transcript_watch.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int private_open(const char *path) {
  const char *home = getenv("HOME");
  if (!path || !home || home[0] != '/' || path[0] != '/')
    return -1;
  size_t n = strlen(path), hn = strlen(home);
  while (hn > 1 && home[hn - 1] == '/')
    hn--;
  if (hn <= 1 || n > AGENT_TRANSCRIPT_PATH_MAX || n <= hn + 1 ||
      strncmp(path, home, hn) || path[hn] != '/')
    return -1;
  char copy[AGENT_TRANSCRIPT_PATH_MAX + 1];
  memcpy(copy, path, n + 1);
  // Walk from / to reject symlinked ancestors as well as the final component.
  int parent = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (parent < 0)
    return -1;
  char *save = NULL, *part = strtok_r(copy + 1, "/", &save);
  while (part) {
    char *next = strtok_r(NULL, "/", &save);
    if (!strcmp(part, "..")) {
      close(parent);
      return -1;
    }
    int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
    if (next)
      flags |= O_DIRECTORY;
    int fd = openat(parent, part, flags);
    close(parent);
    if (fd < 0)
      return -1;
    parent = fd;
    part = next;
  }
  struct stat st;
  if (fstat(parent, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid()) {
    close(parent);
    return -1;
  }
  return parent;
}

int agent_title_open(const char *path) {
  size_t n = path ? strlen(path) : 0;
  if (n < 6 || (strcmp(path + n - 6, ".jsonl") &&
                strcmp(path + n - 5, ".json") && strcmp(path + n - 5, ".yaml")))
    return -1;
  return private_open(path);
}

int transcript_watch_open(const char *path) {
  size_t n = path ? strlen(path) : 0;
  return n >= 6 && (!strcmp(path + n - 6, ".jsonl") ||
                    !strcmp(path + n - 4, ".log"))
             ? private_open(path)
             : -1;
}
