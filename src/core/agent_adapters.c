#include "core/agent_adapters.h"

#include "core/agent_sessions.h"

#include <string.h>

// clang-format off
#define COUNT(items)      (sizeof(items) / sizeof((items)[0]))
#define RULE(name, event) {name, HOOK_FIELD_NONE, NULL, event, false}
#define META(name, event) {name, HOOK_FIELD_NONE, NULL, event, true}
#define WHEN(name, field, value, event) {name, field, value, event, false}
// clang-format on

static const agent_hook_alias_t CLAUDE_FIELDS[] = {
    {"prompt",            HOOK_FIELD_PROMPT,       0, false},
    {"transcript_path",   HOOK_FIELD_TRANSCRIPT,   0, false},
    {"hook_event_name",   HOOK_FIELD_EVENT,        0, false},
    {"session_id",        HOOK_FIELD_SESSION,      0, false},
    {"notification_type", HOOK_FIELD_NOTIFICATION, 0, false},
    {"stop_hook_active",  HOOK_FIELD_STOP,         0, false},
    {"cwd",               HOOK_FIELD_CWD,          0, false},
};
static const agent_hook_rule_t CLAUDE_RULES[] = {
    META("SessionStart", AGENT_EVENT_START),
    META("UserPromptSubmit", AGENT_EVENT_WORKING),
    RULE("PreToolUse", AGENT_EVENT_WORKING),
    RULE("PostToolUse", AGENT_EVENT_WORKING),
    RULE("PostToolUseFailure", AGENT_EVENT_WORKING),
    RULE("PermissionRequest", AGENT_EVENT_WAITING),
    RULE("Stop", AGENT_EVENT_DONE),
    RULE("StopFailure", AGENT_EVENT_FAIL),
    RULE("Interrupt", AGENT_EVENT_IDLE),
    RULE("SessionEnd", AGENT_EVENT_END),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "idle_prompt",
         AGENT_EVENT_REST),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "permission_prompt",
         AGENT_EVENT_WAITING),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "elicitation_dialog",
         AGENT_EVENT_WAITING),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "agent_needs_input",
         AGENT_EVENT_WAITING),
};
static const agent_hook_rule_t CODEX_RULES[] = {
    META("SessionStart", AGENT_EVENT_START),
    META("UserPromptSubmit", AGENT_EVENT_WORKING),
    RULE("PreToolUse", AGENT_EVENT_WORKING),
    RULE("PostToolUse", AGENT_EVENT_WORKING),
    RULE("PostToolUseFailure", AGENT_EVENT_WORKING),
    RULE("PermissionRequest", AGENT_EVENT_WAITING),
    RULE("Stop", AGENT_EVENT_DONE),
    RULE("StopFailure", AGENT_EVENT_FAIL),
    RULE("Interrupt", AGENT_EVENT_INTERRUPT),
    RULE("SessionEnd", AGENT_EVENT_END),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "idle_prompt",
         AGENT_EVENT_REST),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "permission_prompt",
         AGENT_EVENT_WAITING),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "elicitation_dialog",
         AGENT_EVENT_WAITING),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "agent_needs_input",
         AGENT_EVENT_WAITING),
};
static const agent_hook_alias_t GROK_FIELDS[] = {
    {"prompt",            HOOK_FIELD_PROMPT,       0, false},
    {"subagentType",      HOOK_FIELD_PARENT,       0, false},
    {"hook_event_name",   HOOK_FIELD_EVENT,        0, false},
    {"hookEventName",     HOOK_FIELD_EVENT,        1, false},
    {"session_id",        HOOK_FIELD_SESSION,      0, false},
    {"sessionId",         HOOK_FIELD_SESSION,      1, false},
    {"cwd",               HOOK_FIELD_CWD,          0, false},
    {"workspaceRoot",     HOOK_FIELD_CWD,          1, false},
    {"notification_type", HOOK_FIELD_NOTIFICATION, 0, false},
    {"notificationType",  HOOK_FIELD_NOTIFICATION, 1, false},
    {"stop_hook_active",  HOOK_FIELD_STOP,         0, false},
    {"stopHookActive",    HOOK_FIELD_STOP,         1, false},
};
static const agent_hook_rule_t GROK_RULES[] = {
    META("SessionStart", AGENT_EVENT_START),
    META("session_start", AGENT_EVENT_START),
    META("UserPromptSubmit", AGENT_EVENT_WORKING),
    META("user_prompt_submit", AGENT_EVENT_WORKING),
    RULE("PreToolUse", AGENT_EVENT_WORKING),
    RULE("pre_tool_use", AGENT_EVENT_WORKING),
    RULE("PostToolUse", AGENT_EVENT_WORKING),
    RULE("post_tool_use", AGENT_EVENT_WORKING),
    RULE("PostToolUseFailure", AGENT_EVENT_WORKING),
    RULE("post_tool_use_failure", AGENT_EVENT_WORKING),
    RULE("PermissionRequest", AGENT_EVENT_WAITING),
    RULE("permission_request", AGENT_EVENT_WAITING),
    RULE("Stop", AGENT_EVENT_DONE),
    RULE("stop", AGENT_EVENT_DONE),
    RULE("StopFailure", AGENT_EVENT_FAIL),
    RULE("stop_failure", AGENT_EVENT_FAIL),
    RULE("StopCancelled", AGENT_EVENT_INTERRUPT),
    RULE("stop_cancelled", AGENT_EVENT_INTERRUPT),
    RULE("Interrupt", AGENT_EVENT_INTERRUPT),
    RULE("interrupt", AGENT_EVENT_INTERRUPT),
    RULE("SessionEnd", AGENT_EVENT_END),
    RULE("session_end", AGENT_EVENT_END),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "idle_prompt",
         AGENT_EVENT_REST),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "permission_prompt",
         AGENT_EVENT_WAITING),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "elicitation_dialog",
         AGENT_EVENT_WAITING),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "agent_needs_input",
         AGENT_EVENT_WAITING),
    WHEN("notification", HOOK_FIELD_NOTIFICATION, "idle_prompt",
         AGENT_EVENT_REST),
    WHEN("notification", HOOK_FIELD_NOTIFICATION, "permission_prompt",
         AGENT_EVENT_WAITING),
    WHEN("notification", HOOK_FIELD_NOTIFICATION, "elicitation_dialog",
         AGENT_EVENT_WAITING),
    WHEN("notification", HOOK_FIELD_NOTIFICATION, "agent_needs_input",
         AGENT_EVENT_WAITING),
};
static const agent_hook_rule_t KIMI_RULES[] = {
    META("SessionStart", AGENT_EVENT_START),
    META("UserPromptSubmit", AGENT_EVENT_WORKING),
    RULE("PreToolUse", AGENT_EVENT_WORKING),
    RULE("PostToolUse", AGENT_EVENT_WORKING),
    RULE("PostToolUseFailure", AGENT_EVENT_WORKING),
    RULE("PermissionResult", AGENT_EVENT_WORKING),
    RULE("PermissionRequest", AGENT_EVENT_WAITING),
    RULE("Stop", AGENT_EVENT_DONE),
    RULE("StopFailure", AGENT_EVENT_FAIL),
    RULE("Interrupt", AGENT_EVENT_INTERRUPT),
    RULE("SessionEnd", AGENT_EVENT_END),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "idle_prompt",
         AGENT_EVENT_REST),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "permission_prompt",
         AGENT_EVENT_WAITING),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "elicitation_dialog",
         AGENT_EVENT_WAITING),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "agent_needs_input",
         AGENT_EVENT_WAITING),
};
static const agent_hook_alias_t CURSOR_FIELDS[] = {
    {"prompt",          HOOK_FIELD_PROMPT,  0, false},
    {"hook_event_name", HOOK_FIELD_EVENT,   0, false},
    {"conversation_id", HOOK_FIELD_SESSION, 0, false},
    {"session_id",      HOOK_FIELD_SESSION, 1, false},
    {"workspace_roots", HOOK_FIELD_CWD,     0, true },
    {"cwd",             HOOK_FIELD_CWD,     1, false},
    {"status",          HOOK_FIELD_STATUS,  0, false},
};
static const agent_hook_rule_t CURSOR_RULES[] = {
    META("sessionStart", AGENT_EVENT_START),
    META("beforeSubmitPrompt", AGENT_EVENT_WORKING),
    RULE("preToolUse", AGENT_EVENT_WORKING),
    RULE("beforeShellExecution", AGENT_EVENT_WORKING),
    RULE("postToolUse", AGENT_EVENT_WORKING),
    WHEN("stop", HOOK_FIELD_STATUS, "completed", AGENT_EVENT_DONE),
    WHEN("stop", HOOK_FIELD_STATUS, "aborted", AGENT_EVENT_INTERRUPT),
    WHEN("stop", HOOK_FIELD_STATUS, "error", AGENT_EVENT_FAIL),
    RULE("sessionEnd", AGENT_EVENT_END),
    // afterShellExecution/postToolUseFailure can follow stop(aborted).
};
static const agent_hook_alias_t COPILOT_FIELDS[] = {
    {"prompt",            HOOK_FIELD_PROMPT,       0, false},
    {"hook_event_name",   HOOK_FIELD_EVENT,        0, false},
    {"hookName",          HOOK_FIELD_EVENT,        1, false},
    {"sessionId",         HOOK_FIELD_SESSION,      0, false},
    {"cwd",               HOOK_FIELD_CWD,          0, false},
    {"stopReason",        HOOK_FIELD_STATUS,       0, false},
    {"notification_type", HOOK_FIELD_NOTIFICATION, 0, false},
};
static const agent_hook_rule_t COPILOT_RULES[] = {
    META("sessionStart", AGENT_EVENT_START),
    META("userPromptSubmitted", AGENT_EVENT_WORKING),
    RULE("preToolUse", AGENT_EVENT_WORKING),
    RULE("postToolUse", AGENT_EVENT_WORKING),
    RULE("postToolUseFailure", AGENT_EVENT_WORKING),
    WHEN("agentStop", HOOK_FIELD_STATUS, "end_turn", AGENT_EVENT_DONE),
    WHEN("agentStop", HOOK_FIELD_STATUS, "error", AGENT_EVENT_FAIL),
    WHEN("agentStop", HOOK_FIELD_STATUS, "aborted", AGENT_EVENT_INTERRUPT),
    WHEN("agentStop", HOOK_FIELD_STATUS, "interrupted", AGENT_EVENT_INTERRUPT),
    RULE("errorOccurred", AGENT_EVENT_FAIL),
    RULE("sessionEnd", AGENT_EVENT_END),
    WHEN("notification", HOOK_FIELD_NOTIFICATION, "permission_prompt",
         AGENT_EVENT_WAITING),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "permission_prompt",
         AGENT_EVENT_WAITING),
    // permissionRequest also fires for auto-allowed tools. It is not waiting.
};
static const agent_hook_alias_t PI_FIELDS[] = {
    {"transcript_path", HOOK_FIELD_TRANSCRIPT, 0, false},
    {"hook_event_name", HOOK_FIELD_EVENT,      0, false},
    {"session_id",      HOOK_FIELD_SESSION,    0, false},
    {"cwd",             HOOK_FIELD_CWD,        0, false},
    {"stopReason",      HOOK_FIELD_STATUS,     0, false},
    {"agent_pid",       HOOK_FIELD_PID,        0, false},
    {"parent_session",  HOOK_FIELD_PARENT,     0, false},
};
static const agent_hook_rule_t PI_RULES[] = {
    META("session_start", AGENT_EVENT_START),
    META("before_agent_start", AGENT_EVENT_WORKING),
    RULE("agent_start", AGENT_EVENT_WORKING),
    RULE("tool_call", AGENT_EVENT_WORKING),
    RULE("tool_result", AGENT_EVENT_WORKING),
    WHEN("agent_end", HOOK_FIELD_STATUS, "stop", AGENT_EVENT_DONE),
    WHEN("agent_end", HOOK_FIELD_STATUS, "error", AGENT_EVENT_FAIL),
    WHEN("agent_end", HOOK_FIELD_STATUS, "aborted", AGENT_EVENT_INTERRUPT),
    WHEN("agent_end", HOOK_FIELD_STATUS, "length", AGENT_EVENT_FAIL),
    RULE("session_shutdown", AGENT_EVENT_END),
};
static const agent_hook_alias_t OPENCODE_FIELDS[] = {
    {"title",           HOOK_FIELD_TITLE,   0, false},
    {"hook_event_name", HOOK_FIELD_EVENT,   0, false},
    {"session_id",      HOOK_FIELD_SESSION, 0, false},
    {"cwd",             HOOK_FIELD_CWD,     0, false},
    {"parent_session",  HOOK_FIELD_PARENT,  0, false},
};
static const agent_hook_rule_t OPENCODE_RULES[] = {
    META("session.created", AGENT_EVENT_START),
    META("session.inbox.enqueued", AGENT_EVENT_WORKING),
    META("session.execution.started", AGENT_EVENT_WORKING),
    RULE("session.tool.called", AGENT_EVENT_WORKING),
    RULE("session.tool.success", AGENT_EVENT_WORKING),
    RULE("permission.asked", AGENT_EVENT_WAITING),
    RULE("permission.replied", AGENT_EVENT_WORKING),
    RULE("session.execution.succeeded", AGENT_EVENT_DONE),
    RULE("session.execution.interrupted", AGENT_EVENT_INTERRUPT),
    RULE("session.execution.failed", AGENT_EVENT_FAIL),
    RULE("session.deleted", AGENT_EVENT_END),
};
static const agent_hook_rule_t QWEN_RULES[] = {
    META("SessionStart", AGENT_EVENT_START),
    META("UserPromptSubmit", AGENT_EVENT_WORKING),
    RULE("PreToolUse", AGENT_EVENT_WORKING),
    RULE("PostToolUse", AGENT_EVENT_WORKING),
    RULE("PostToolUseFailure", AGENT_EVENT_WORKING),
    RULE("PostToolBatch", AGENT_EVENT_WORKING),
    RULE("PermissionRequest", AGENT_EVENT_WAITING),
    RULE("Stop", AGENT_EVENT_DONE),
    RULE("StopFailure", AGENT_EVENT_FAIL),
    RULE("SessionEnd", AGENT_EVENT_END),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "idle_prompt",
         AGENT_EVENT_REST),
    WHEN("Notification", HOOK_FIELD_NOTIFICATION, "permission_prompt",
         AGENT_EVENT_WAITING),
};
static const agent_hook_alias_t AGY_FIELDS[] = {
    {"conversationId", HOOK_FIELD_SESSION, 0, false},
    {"workspacePaths", HOOK_FIELD_CWD,     0, true },
    // Classify the error string without retaining its private contents.
    {"error",          HOOK_FIELD_STATUS,  0, false},
};
static const agent_hook_rule_t AGY_RULES[] = {
    META("PreInvocation", AGENT_EVENT_WORKING),
    RULE("PostInvocation", AGENT_EVENT_WORKING),
    RULE("PreToolUse", AGENT_EVENT_WORKING),
    RULE("PostToolUse", AGENT_EVENT_WORKING),
    WHEN("Stop", HOOK_FIELD_STATUS, "", AGENT_EVENT_DONE),
    WHEN("Stop", HOOK_FIELD_STATUS, "error", AGENT_EVENT_FAIL),
};
static const agent_adapter_t ADAPTERS[] = {
    {.name = "qwen",
     .display_name = "Qwen",
     .interrupt_source = AGENT_SIGNAL_HOOK,
     .error_source = AGENT_SIGNAL_HOOK,
     .aliases = CLAUDE_FIELDS,
     .alias_count = COUNT(CLAUDE_FIELDS),
     .rules = QWEN_RULES,
     .rule_count = COUNT(QWEN_RULES),
     .stop_guard = true},
    {.name = "agy",
     .display_name = "Antigravity",
     .interrupt_source = AGENT_SIGNAL_TRANSCRIPT,
     .error_source = AGENT_SIGNAL_HOOK,
     .aliases = AGY_FIELDS,
     .alias_count = COUNT(AGY_FIELDS),
     .rules = AGY_RULES,
     .rule_count = COUNT(AGY_RULES)},
    {.name = "opencode",
     .interrupt_source = AGENT_SIGNAL_HOOK,
     .error_source = AGENT_SIGNAL_HOOK,
     .display_name = "opencode",
     .aliases = OPENCODE_FIELDS,
     .alias_count = COUNT(OPENCODE_FIELDS),
     .rules = OPENCODE_RULES,
     .rule_count = COUNT(OPENCODE_RULES),
     .no_pid = true},
    {.name = "claude",
     .interrupt_source = AGENT_SIGNAL_TRANSCRIPT,
     .error_source = AGENT_SIGNAL_HOOK,
     .display_name = "Claude",
     .aliases = CLAUDE_FIELDS,
     .alias_count = COUNT(CLAUDE_FIELDS),
     .rules = CLAUDE_RULES,
     .rule_count = COUNT(CLAUDE_RULES),
     .json_stdout = false,
     .stop_guard = true,
     .explicit_pid = false,
     .continuous_output = true,
     .process_name = "claude",
     .owner_pid_env = "CLAUDE_PID"},
    {.name = "codex",
     .interrupt_source = AGENT_SIGNAL_HOOK,
     .error_source = AGENT_SIGNAL_TRANSCRIPT,
     .display_name = "Codex",
     .aliases = CLAUDE_FIELDS,
     .alias_count = COUNT(CLAUDE_FIELDS),
     .rules = CODEX_RULES,
     .rule_count = COUNT(CODEX_RULES),
     .json_stdout = false,
     .stop_guard = true,
     .explicit_pid = false,
     .process_name = "codex"},
    {.name = "grok",
     .interrupt_source = AGENT_SIGNAL_HOOK,
     .error_source = AGENT_SIGNAL_HOOK,
     .display_name = "Grok",
     .continuous_output = true,
     .aliases = GROK_FIELDS,
     .alias_count = COUNT(GROK_FIELDS),
     .rules = GROK_RULES,
     .rule_count = COUNT(GROK_RULES),
     .json_stdout = true,
     .stop_guard = true,
     .explicit_pid = false,
     .process_name = "grok"},
    {.name = "kimi",
     .interrupt_source = AGENT_SIGNAL_HOOK,
     .error_source = AGENT_SIGNAL_HOOK,
     .display_name = "Kimi",
     .aliases = CLAUDE_FIELDS,
     .alias_count = COUNT(CLAUDE_FIELDS),
     .rules = KIMI_RULES,
     .rule_count = COUNT(KIMI_RULES),
     .json_stdout = false,
     .stop_guard = true,
     .explicit_pid = false,
     .process_name = "kimi"},
    {.name = "cursor",
     .interrupt_source = AGENT_SIGNAL_HOOK,
     .error_source = AGENT_SIGNAL_HOOK,
     .display_name = "Cursor",
     .aliases = CURSOR_FIELDS,
     .alias_count = COUNT(CURSOR_FIELDS),
     .rules = CURSOR_RULES,
     .rule_count = COUNT(CURSOR_RULES),
     .json_stdout = true,
     .stop_guard = false,
     .explicit_pid = false},
    {.name = "copilot",
     .interrupt_source = AGENT_SIGNAL_NONE,
     .error_source = AGENT_SIGNAL_HOOK,
     .display_name = "Copilot",
     .continuous_output = true,
     .aliases = COPILOT_FIELDS,
     .alias_count = COUNT(COPILOT_FIELDS),
     .rules = COPILOT_RULES,
     .rule_count = COUNT(COPILOT_RULES),
     .json_stdout = true,
     .stop_guard = false,
     .explicit_pid = false},
    {.name = "pi",
     .interrupt_source = AGENT_SIGNAL_HOOK,
     .error_source = AGENT_SIGNAL_HOOK,
     .display_name = "Pi",
     .aliases = PI_FIELDS,
     .alias_count = COUNT(PI_FIELDS),
     .rules = PI_RULES,
     .rule_count = COUNT(PI_RULES),
     .json_stdout = false,
     .stop_guard = false,
     .explicit_pid = true},
};

size_t agent_adapter_count(void) {
  return COUNT(ADAPTERS);
}

const agent_adapter_t *agent_adapter_at(size_t index) {
  return index < COUNT(ADAPTERS) ? &ADAPTERS[index] : NULL;
}

const agent_adapter_t *agent_adapter_find(const char *name) {
  for (size_t i = 0; name && i < COUNT(ADAPTERS); i++) {
    if (!strcmp(name, ADAPTERS[i].name))
      return &ADAPTERS[i];
  }
  return agent_adapter_find("claude");
}

const agent_hook_alias_t *agent_adapter_alias(const agent_adapter_t *adapter,
                                              const char *key) {
  for (size_t i = 0; i < adapter->alias_count; i++) {
    if (!strcmp(key, adapter->aliases[i].key))
      return &adapter->aliases[i];
  }
  return NULL;
}

bool agent_adapter_event(const agent_adapter_t *adapter, const char *name,
                         const char *notification, const char *status,
                         bool stop_present, bool stop_valid, bool stop_active,
                         agent_event_t *event, bool *metadata) {
  if (!name || !event)
    return false;
  if (adapter->stop_guard && (!strcmp(name, "Stop") || !strcmp(name, "stop")) &&
      stop_present && (!stop_valid || stop_active))
    return false;
  for (size_t i = 0; i < adapter->rule_count; i++) {
    const agent_hook_rule_t *rule = &adapter->rules[i];
    const char *value =
        rule->condition == HOOK_FIELD_NOTIFICATION ? notification : status;
    if (strcmp(name, rule->name) || (rule->condition != HOOK_FIELD_NONE &&
                                     (!value || strcmp(value, rule->value))))
      continue;
    *event = rule->event;
    if (metadata)
      *metadata = rule->metadata;
    return true;
  }
  return false;
}

const char *agent_adapter_display(const char *name, char fallback[9]) {
  const agent_adapter_t *adapter = agent_adapter_find(name);
  if (!strcmp(name, adapter->name))
    return adapter->display_name;
  size_t n = strlen(name);
  if (n > 8)
    n = 8;
  memcpy(fallback, name, n);
  fallback[n] = '\0';
  if (fallback[0] >= 'a' && fallback[0] <= 'z')
    fallback[0] -= 'a' - 'A';
  return fallback;
}
