#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "test_helpers.h"

#include <string.h>

static void event(const char *agent, const char *name, const char *extra,
                  int64_t now, agent_state_t expected) {
  char json[512];
  snprintf(json, sizeof(json),
           "{\"hook_event_name\":\"%s\",\"session_id\":\"qwen-test\"%s}", name,
           extra);
  agent_hook_scanner_t scanner;
  agent_hook_scan_adapter(&scanner, agent_adapter_find(agent));
  agent_hook_scan_feed(&scanner, json, strlen(json));
  TEST_ASSERT(agent_hook_scan_finish(&scanner));
  agent_event_t action;
  TEST_ASSERT(agent_hook_event(&scanner, &action));
  TEST_ASSERT(agent_sessions_apply(1, agent, action, 0, now, 5, NULL) == 0);
  char prompt[AGENT_TITLE_MAX + 1];
  if (agent_hook_prompt(&scanner, NULL, prompt))
    TEST_ASSERT(agent_sessions_set_prompt(1, prompt) == 0);
  TEST_ASSERT(agent_sessions_resolve() == expected);
}

int main(void) {
  agent_sessions_reset();
  event("qwen", "SessionStart", "", 0, AGENT_STATE_IDLE);
  event("qwen", "UserPromptSubmit", ",\"prompt\":\"first prompt\"", 1,
        AGENT_STATE_WORKING);
  event("qwen", "PermissionRequest", "", 2, AGENT_STATE_WAITING);
  event("qwen", "Notification", ",\"notification_type\":\"permission_prompt\"",
        3, AGENT_STATE_WAITING);
  const char *tool[] = {"PreToolUse", "PostToolUse", "PostToolBatch",
                        "PostToolUseFailure"};
  for (int i = 0; i < 4; i++)
    event("qwen", tool[i], "", 4 + i, AGENT_STATE_WORKING);
  event("qwen", "UserPromptSubmit", ",\"prompt\":\"\"", 8, AGENT_STATE_WORKING);
  agent_session_view_t view;
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1);
  TEST_ASSERT(!strcmp(view.title, "first prompt"));
  event("qwen", "Stop", "", 9, AGENT_STATE_DONE);
  event("qwen", "Notification", ",\"notification_type\":\"idle_prompt\"", 10,
        AGENT_STATE_DONE);
  TEST_ASSERT(agent_sessions_snapshot(&view, 1) == 1 && view.unread);
  event("qwen", "StopFailure", "", 25, AGENT_STATE_DONE);
  // Both early and mid-reply cancellations use this notification sequence.
  event("qwen", "UserPromptSubmit", "", 1000, AGENT_STATE_WORKING);
  event("qwen", "Notification", ",\"notification_type\":\"idle_prompt\"", 1500,
        AGENT_STATE_IDLE);
  event("qwen", "StopFailure", "", 1515, AGENT_STATE_ERROR);
  event("qwen", "UserPromptSubmit", "", 2000, AGENT_STATE_WORKING);
  event("qwen", "PermissionRequest", "", 2100, AGENT_STATE_WAITING);
  event("qwen", "Notification", ",\"notification_type\":\"idle_prompt\"", 2200,
        AGENT_STATE_IDLE);
  event("qwen", "StopFailure", "", 2451, AGENT_STATE_IDLE);
  event("qwen", "UserPromptSubmit", "", 3000, AGENT_STATE_WORKING);
  event("qwen", "Notification", ",\"notification_type\":\"idle_prompt\"", 3500,
        AGENT_STATE_IDLE);
  // Another event consumes the grace period; idle failures cannot revive work.
  event("qwen", "SessionStart", "", 3501, AGENT_STATE_IDLE);
  event("qwen", "StopFailure", "", 3515, AGENT_STATE_IDLE);
  event("qwen", "SessionEnd", "", 4000, AGENT_STATE_IDLE);
  TEST_ASSERT(agent_sessions_count() == 0);
  for (int i = 0; i < 2; i++) {
    const char *agent = i ? "codex" : "claude";
    agent_sessions_reset();
    event(agent, "UserPromptSubmit", "", 1000, AGENT_STATE_WORKING);
    event(agent, "Notification", ",\"notification_type\":\"idle_prompt\"", 1500,
          AGENT_STATE_IDLE);
    event(agent, "StopFailure", "", 1515, AGENT_STATE_IDLE);
  }
  return 0;
}
