#ifndef HERDCAT_AGENT_TITLE_H
#define HERDCAT_AGENT_TITLE_H
#include <stdbool.h>
#include <stddef.h>
#define AGENT_TITLE_MAX      96
#define AGENT_SESSION_ID_MAX 64
#define AGENT_TITLE_TAIL_MAX (256 * 1024)
#include "utils/json.h"

#include <string.h>
static inline bool agent_session_id_valid(const char *id) {
  return id && *id && strlen(id) <= AGENT_SESSION_ID_MAX &&
         strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "0123456789-_") == strlen(id);
}
bool agent_title_scalar(json_span_t value, char *out, size_t capacity);
bool agent_title_copilot_yaml(const char *data, size_t length,
                              char out[AGENT_TITLE_MAX + 1]);
bool agent_title_copilot(const char *id, char out[AGENT_TITLE_MAX + 1]);
int agent_title_open(const char *path);
bool agent_title_kimi(const char *id, char out[AGENT_TITLE_MAX + 1]);
// Never logs source data. Rejected input leaves the output empty.
bool agent_title_line(const char *agent, const char *id, const char *line,
                      size_t length, char out[AGENT_TITLE_MAX + 1]);
bool agent_prompt_line(const char *agent, const char *line, size_t length,
                       char out[AGENT_TITLE_MAX + 1]);
bool agent_prompt_read(const char *agent, const char *path,
                       char out[AGENT_TITLE_MAX + 1]);
int agent_prompt_main(int argc, char **argv);
bool agent_title_read(const char *agent, const char *id, const char *path,
                      char out[AGENT_TITLE_MAX + 1]);
#endif
