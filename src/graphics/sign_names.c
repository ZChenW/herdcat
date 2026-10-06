#include "graphics/sign_names.h"

#include "utils/utf8.h"

#include <stdio.h>
#include <string.h>

void sign_title_truncate(const char *title, int limit,
                         char out[AGENT_TITLE_MAX + 4]) {
  size_t used = 0;
  int count = 0;
  while (title && *title && (!limit || count < limit)) {
    uint32_t cp;
    size_t n = utf8_decode(title, &cp);
    if (!n || used + n > AGENT_TITLE_MAX)
      break;
    memcpy(out + used, title, n);
    used += n;
    title += n;
    count++;
  }
  if (title && *title) {
    memcpy(out + used, "…", 3);
    used += 3;
  }
  out[used] = 0;
}
void sign_session_name(const sign_input_t *in, const agent_session_view_t *s,
                       char out[128], bool *title_main) {
  bool use_title = s->title[0] && in->name == SIGN_NAME_TITLE;
  if (use_title) {
    char title[AGENT_TITLE_MAX + 4];
    sign_title_truncate(s->title, in->title_length, title);
    snprintf(out, 128, "%s", title);
  } else {
    snprintf(out, 128, "%s", s->name);
  }
  if (title_main)
    *title_main = use_title;
}
