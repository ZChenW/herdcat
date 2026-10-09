// One line as it arrives on the control socket from a hook client, and a
// /proc/<pid>/stat line: the parsers behind src/core/control.c.
#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "fuzz.h"

static int fuzz_requests(const uint8_t *data, size_t size) {
  if (size > 4096)
    return 0;
  char *request = fuzz_text(data, size);
  uint64_t key;
  char agent[9];
  agent_event_t event;
  pid_t pid, candidate, owner, parent;
  bool metadata;
  agent_sessions_reset();
  if (agent_event_request(request, &key, agent, &event, &pid, &candidate,
                          &metadata))
    (void)agent_sessions_apply(key, agent, event, pid, 1000, 30, &metadata);
  (void)agent_event_owner_request(request, &key, agent, &event, &pid,
                                  &candidate, &metadata, &owner);
  (void)agent_sessions_title_command(request);
  (void)agent_sessions_id_command(request);
  (void)agent_sessions_cwd_command(request);
  (void)agent_event_parse(request, &event);
  (void)agent_hook_valid_agent(request);
  char comm[32];
  unsigned long tty;
  (void)agent_hook_parse_stat(request, comm, sizeof(comm), &parent);
  (void)agent_hook_stat_tty(request, &tty);
  agent_sessions_reset();
  free(request);
  return 0;
}
FUZZ_ENTRY(fuzz_requests)
