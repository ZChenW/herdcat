#ifndef AGENT_HOOK_H
#define AGENT_HOOK_H

#include "core/agent_sessions.h"

#define AGENT_HOOK_MAX_DEPTH 128

typedef struct {
  char event[64];
  char session_id[128];
  char notification[64];
  char cwd[256];
  unsigned valid_fields;
  bool stop_present;
  bool stop_hook_active;
  // Incremental JSON parser storage, independent of total input length.
  unsigned char stack[AGENT_HOOK_MAX_DEPTH];
  unsigned depth;
  unsigned token, field, number_state, literal_pos;
  unsigned unicode_left, utf8_left, utf8_min, utf8_max;
  bool started, complete, failed, key, escaped, escape_next, overflow;
  char text[256];
  size_t text_length;
  const char *literal;
} agent_hook_scanner_t;

void agent_hook_scan_init(agent_hook_scanner_t *scanner);
void agent_hook_scan_feed(agent_hook_scanner_t *scanner, const char *data,
                          size_t length);
bool agent_hook_scan_finish(agent_hook_scanner_t *scanner);
bool agent_hook_event(const agent_hook_scanner_t *scanner,
                      agent_event_t *event);
bool agent_hook_valid_agent(const char *agent);
uint64_t agent_hook_key(const char *agent, const agent_hook_scanner_t *scanner);
bool agent_hook_name(const agent_hook_scanner_t *scanner, char name[41]);
int agent_hook_parse_stat(const char *line, char *comm, size_t capacity,
                          pid_t *parent);
// Quiet, bounded hook client. Only invalid CLI agent names return nonzero.
int agent_hook_run(const char *agent);

#endif  // AGENT_HOOK_H
