#ifndef AGENT_ADAPTERS_H
#define AGENT_ADAPTERS_H

#include "core/agent_sessions.h"

typedef enum {
  HOOK_FIELD_NONE,
  HOOK_FIELD_EVENT,
  HOOK_FIELD_SESSION,
  HOOK_FIELD_NOTIFICATION,
  HOOK_FIELD_STOP,
  HOOK_FIELD_CWD,
  HOOK_FIELD_TRANSCRIPT,
  HOOK_FIELD_STATUS,
  HOOK_FIELD_PARENT,
  HOOK_FIELD_PID,
  HOOK_FIELD_PROMPT,
  HOOK_FIELD_TITLE,
  HOOK_FIELD_COUNT
} agent_hook_field_t;

typedef struct {
  const char *key;
  agent_hook_field_t field;
  // Smaller ranks take precedence, independent of JSON member order.
  unsigned rank;
  bool array;
} agent_hook_alias_t;

typedef struct {
  const char *name;
  agent_hook_field_t condition;
  const char *value;
  agent_event_t event;
  bool metadata;
} agent_hook_rule_t;

typedef enum {
  AGENT_SIGNAL_NONE,
  AGENT_SIGNAL_HOOK,
  AGENT_SIGNAL_TRANSCRIPT
} agent_signal_t;

typedef struct {
  const char *name, *display_name;
  agent_signal_t interrupt_source, error_source;
  const agent_hook_alias_t *aliases;
  size_t alias_count;
  const agent_hook_rule_t *rules;
  size_t rule_count;
  bool json_stdout;
  bool stop_guard;
  bool explicit_pid;
  bool no_pid;
  // The terminal title starts with "✳" whenever the agent is not working.
  bool rest_title;
  // Exact /proc comm. NULL when that process cannot be identified.
  const char *process_name;
  // Inherited owner PID exported by this agent to its children.
  const char *owner_pid_env;
} agent_adapter_t;

// Return the display name, or capitalize an unknown name in fallback.
const char *agent_adapter_display(const char *name, char fallback[9]);

// Unknown names retain the legacy Claude mapping.
const agent_adapter_t *agent_adapter_find(const char *name);
size_t agent_adapter_count(void);
const agent_adapter_t *agent_adapter_at(size_t index);
const agent_hook_alias_t *agent_adapter_alias(const agent_adapter_t *adapter,
                                              const char *key);
bool agent_adapter_event(const agent_adapter_t *adapter, const char *name,
                         const char *notification, const char *status,
                         bool stop_present, bool stop_valid, bool stop_active,
                         agent_event_t *event, bool *metadata);

#endif  // AGENT_ADAPTERS_H
