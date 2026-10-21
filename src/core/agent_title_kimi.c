#define _GNU_SOURCE
#include "core/agent_title.h"
#include "core/agent_transcript.h"
#include "utils/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool agent_title_kimi(const char *id, char out[AGENT_TITLE_MAX + 1]) {
  out[0] = 0;
  const char *home = getenv("HOME"), *base = getenv("KIMI_CODE_HOME");
  if (!home || !agent_session_id_valid(id))
    return false;
  char root[AGENT_TRANSCRIPT_PATH_MAX + 1], path[AGENT_TRANSCRIPT_PATH_MAX + 1];
  int n = base && *base ? snprintf(root, sizeof(root), "%s", base)
                        : snprintf(root, sizeof(root), "%s/.kimi-code", home);
  if (n < 0 || (size_t)n >= sizeof(root))
    return false;
  n = snprintf(path, sizeof(path), "%s/session_index.jsonl", root);
  if (n < 0 || (size_t)n >= sizeof(path))
    return false;
  int fd = agent_title_open(path);
  if (fd < 0)
    return false;
  struct stat st;
  if (fstat(fd, &st) || st.st_size <= 0) {
    close(fd);
    return false;
  }
  size_t want = st.st_size > AGENT_TITLE_TAIL_MAX ? AGENT_TITLE_TAIL_MAX
                                                  : (size_t)st.st_size;
  off_t offset = st.st_size - (off_t)want;
  char *tail = malloc(want + 1);
  if (!tail) {
    close(fd);
    return false;
  }
  ssize_t got = pread(fd, tail, want, offset);
  close(fd);
  bool found = false;
  if (got == (ssize_t)want) {
    size_t end = want;
    if (tail[end - 1] != '\n')
      while (end && tail[end - 1] != '\n')
        end--;
    while (end && !found) {
      size_t stop = end - 1, start = stop;
      while (start && tail[start - 1] != '\n')
        start--;
      json_span_t doc, sid, dir;
      if ((start || !offset) && stop - start <= 4096 &&
          json_document(tail + start, stop - start, &doc) &&
          json_field(doc, "sessionId", &sid) && json_equal(sid, id) &&
          json_field(doc, "sessionDir", &dir) &&
          dir.end - dir.p <= AGENT_TRANSCRIPT_PATH_MAX &&
          json_text(dir, path, sizeof(path)))
        found = true;
      end = start;
    }
  }
  free(tail);
  if (!found)
    return false;
  // The index can only reference sessions within this same data root.
  char session[AGENT_TRANSCRIPT_PATH_MAX + 1];
  size_t rn = strlen(root);
  if (strncmp(path, root, rn) || strncmp(path + rn, "/sessions/", 10))
    return false;
  n = snprintf(session, sizeof(session), "%s/state.json", path);
  if (n < 0 || (size_t)n >= sizeof(session))
    return false;
  fd = agent_title_open(session);
  if (fd < 0)
    return false;
  if (fstat(fd, &st) || st.st_size <= 0 || st.st_size > 4096) {
    close(fd);
    return false;
  }
  char data[4097];
  got = pread(fd, data, (size_t)st.st_size, 0);
  close(fd);
  return got == st.st_size &&
         agent_title_line("kimi", id, data, (size_t)got, out);
}
