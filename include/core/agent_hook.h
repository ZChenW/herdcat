#ifndef AGENT_HOOK_H
#define AGENT_HOOK_H

#include "core/agent_adapters.h"
#include "core/agent_transcript.h"

#define AGENT_HOOK_MAX_DEPTH 128

typedef struct {
  char event[64];
  char session_id[128];
  char notification[64];
  char cwd[256];
  char transcript[AGENT_TRANSCRIPT_PATH_MAX * 6 + 1];
  char status[64];
  char title[AGENT_TITLE_MAX * 6 + 1];
  char prompt[AGENT_TITLE_MAX + 1];
  size_t prompt_length;
  uint32_t prompt_cp, prompt_high;
  bool prompt_space, prompt_done, prompt_invalid;
  bool child_session;
  pid_t pid;
  const agent_adapter_t *adapter;
  unsigned ranks[HOOK_FIELD_COUNT];
  bool array_field, array_taken;
  unsigned valid_fields;
  bool stop_present;
  bool stop_hook_active;
  // Incremental JSON parser storage, independent of total input length.
  unsigned char stack[AGENT_HOOK_MAX_DEPTH];
  unsigned depth;
  unsigned token, field, number_state, literal_pos;
  unsigned unicode_left, utf8_left, utf8_min, utf8_max;
  bool started, complete, failed, key, escaped, escape_next, overflow;
  char text[AGENT_TRANSCRIPT_PATH_MAX * 6 + 1];
  size_t text_length;
  const char *literal;
} agent_hook_scanner_t;

void agent_hook_scan_init(agent_hook_scanner_t *scanner);
void agent_hook_scan_adapter(agent_hook_scanner_t *scanner,
                             const agent_adapter_t *adapter);
bool agent_hook_event_override(const agent_hook_scanner_t *scanner,
                               const char *name, agent_event_t *event,
                               bool *metadata);
void agent_hook_scan_feed(agent_hook_scanner_t *scanner, const char *data,
                          size_t length);
bool agent_hook_scan_finish(agent_hook_scanner_t *scanner);
bool agent_hook_event(const agent_hook_scanner_t *scanner,
                      agent_event_t *event);
bool agent_hook_valid_agent(const char *agent);
uint64_t agent_hook_key(const char *agent, const agent_hook_scanner_t *scanner);
bool agent_hook_transcript(const agent_hook_scanner_t *scanner,
                           char path[AGENT_TRANSCRIPT_PATH_MAX + 1]);
bool agent_hook_title(const agent_hook_scanner_t *scanner,
                      char out[AGENT_TITLE_MAX + 1]);
bool agent_hook_prompt(const agent_hook_scanner_t *scanner,
                       const char *event_name, char out[AGENT_TITLE_MAX + 1]);
bool agent_hook_name(const agent_hook_scanner_t *scanner, char name[41]);
// Repository root's final component, or the directory's own. No git command.
bool agent_hook_place_name(const char *dir, char name[41]);
int agent_hook_parse_stat(const char *line, char *comm, size_t capacity,
                          pid_t *parent);
// 1 when tty_nr is nonzero, 0 when it is 0, -1 when the field is absent.
int agent_hook_stat_tty(const char *line, unsigned long *tty_nr);
// Same codes for a live process. -1 means /proc/<pid>/stat could not be read.
int agent_process_tty(pid_t pid);
// The one process under root (normally "/proc") named comm that has a
// controlling terminal and runs in cwd. 0 when there is none or more than
// one. Codex without --no-daemon runs its hooks in a background server; this
// finds the terminal program the session belongs to.
pid_t agent_hook_owner_pid(const char *proc_root, pid_t pid);
pid_t agent_hook_front_process(const char *root, const char *comm,
                               const char *cwd);
// Quiet, bounded hook client. Only invalid CLI agent names return nonzero.
int agent_hook_run(const char *agent, const char *event_name);
// Explicit adapter entry point also supports isolated client-policy tests.
int agent_hook_run_adapter(const char *agent, const char *event_name,
                           const agent_adapter_t *adapter);

#endif  // AGENT_HOOK_H
