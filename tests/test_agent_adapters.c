#include "core/agent_hook.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

static const agent_hook_alias_t ALIASES[] = {
    {"event",           HOOK_FIELD_EVENT,   0, false},
    {"conversation_id", HOOK_FIELD_SESSION, 0, false},
    {"session_id",      HOOK_FIELD_SESSION, 1, false},
    {"workspace_roots", HOOK_FIELD_CWD,     0, true },
    {"status",          HOOK_FIELD_STATUS,  0, false},
};
static const agent_hook_rule_t RULES[] = {
    {"finish", HOOK_FIELD_STATUS, "completed", AGENT_EVENT_DONE,    false},
    {"finish", HOOK_FIELD_STATUS, "aborted",   AGENT_EVENT_IDLE,    false},
    {"begin",  HOOK_FIELD_NONE,   NULL,        AGENT_EVENT_WORKING, true },
};
static const agent_adapter_t FIXTURE = {.name = "fixture",
                                        .display_name = "Fixture",
                                        .aliases = ALIASES,
                                        .alias_count = 5,
                                        .rules = RULES,
                                        .rule_count = 3,
                                        .json_stdout = true};

static void check(const char *json, const char *override, int expected,
                  const char *session, const char *name) {
  for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
    agent_hook_scanner_t s;
    agent_hook_scan_adapter(&s, &FIXTURE);
    size_t length = strlen(json);
    for (size_t i = 0; i < length; i += chunk)
      agent_hook_scan_feed(&s, json + i,
                           length - i < chunk ? length - i : chunk);
    TEST_ASSERT(agent_hook_scan_finish(&s));
    agent_event_t event;
    bool metadata = false;
    TEST_ASSERT(agent_hook_event_override(&s, override, &event, &metadata) ==
                (expected >= 0));
    if (expected >= 0) {
      TEST_ASSERT((int)event == expected);
      TEST_ASSERT(metadata == (event == AGENT_EVENT_WORKING));
    }
    if (session)
      TEST_ASSERT(!strcmp(s.session_id, session));
    char actual[41];
    TEST_ASSERT(agent_hook_name(&s, actual) == (name != NULL));
    if (name)
      TEST_ASSERT(!strcmp(actual, name));
  }
}

static void adapter_check(const char *agent, const char *json,
                          const char *override, int expected) {
  for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
    agent_hook_scanner_t s;
    agent_hook_scan_adapter(&s, agent_adapter_find(agent));
    size_t length = strlen(json);
    for (size_t i = 0; i < length; i += chunk)
      agent_hook_scan_feed(&s, json + i,
                           length - i < chunk ? length - i : chunk);
    TEST_ASSERT(agent_hook_scan_finish(&s));
    agent_event_t event;
    TEST_ASSERT(agent_hook_event_override(&s, override, &event, NULL) ==
                (expected >= 0));
    if (expected >= 0)
      TEST_ASSERT((int)event == expected);
  }
}

static void test_grok(void) {
  TEST_ASSERT(agent_adapter_find("grok")->json_stdout);
  adapter_check(
      "grok",
      "{\"hook_event_name\":\"SessionEnd\",\"subagentType\":\"worker\"}", NULL,
      -1);
  adapter_check("grok", "{\"hookEventName\":\"user_prompt_submit\"}", NULL,
                AGENT_EVENT_WORKING);
  adapter_check("grok", "{\"hookEventName\":\"stop_cancelled\"}", NULL,
                AGENT_EVENT_INTERRUPT);
  adapter_check("grok", "{\"hook_event_name\":\"StopFailure\"}", NULL,
                AGENT_EVENT_INTERRUPT);
  adapter_check("grok", "{\"hookEventName\":\"stop\",\"stopHookActive\":true}",
                NULL, -1);
  adapter_check("grok", "{\"hookEventName\":\"stop\",\"stopHookActive\":false}",
                NULL, AGENT_EVENT_DONE);
  adapter_check("grok",
                "{\"hook_event_name\":\"Notification\",\"notificationType\":"
                "\"permission_prompt\"}",
                NULL, AGENT_EVENT_WAITING);
  agent_hook_scanner_t s;
  agent_hook_scan_adapter(&s, agent_adapter_find("grok"));
  const char *json =
      "{\"sessionId\":\"test\",\"workspaceRoot\":\"/tmp/project\"}";
  agent_hook_scan_feed(&s, json, strlen(json));
  TEST_ASSERT(agent_hook_scan_finish(&s));
  TEST_ASSERT(!strcmp(s.session_id, "test"));
  char name[41];
  TEST_ASSERT(agent_hook_name(&s, name) && !strcmp(name, "project"));
}

static void test_cursor(void) {
  adapter_check("cursor",
                "{\"hook_event_name\":\"stop\",\"status\":\"completed\"}", NULL,
                AGENT_EVENT_DONE);
  adapter_check("cursor",
                "{\"hook_event_name\":\"stop\",\"status\":\"aborted\"}", NULL,
                AGENT_EVENT_INTERRUPT);
  adapter_check("cursor", "{\"hook_event_name\":\"stop\",\"status\":\"error\"}",
                NULL, AGENT_EVENT_INTERRUPT);
  adapter_check("cursor", "{\"hook_event_name\":\"stop\"}", NULL, -1);
  adapter_check("cursor", "{\"hook_event_name\":\"stop\",\"status\":false}",
                NULL, -1);
  adapter_check("cursor", "{\"hook_event_name\":\"postToolUseFailure\"}", NULL,
                -1);
  adapter_check("cursor", "{\"hook_event_name\":\"afterShellExecution\"}", NULL,
                -1);
  adapter_check("cursor", "{\"hook_event_name\":\"PermissionRequest\"}", NULL,
                -1);
  adapter_check("cursor", "{}", "beforeSubmitPrompt", AGENT_EVENT_WORKING);
  agent_hook_scanner_t s;
  agent_hook_scan_adapter(&s, agent_adapter_find("cursor"));
  const char *json =
      "{\"conversation_id\":\"first\",\"session_id\":\"second\",\"workspace_"
      "roots\":[\"/tmp/project\",\"/tmp/other\"],\"cwd\":\"/tmp/fallback\"}";
  agent_hook_scan_feed(&s, json, strlen(json));
  TEST_ASSERT(agent_hook_scan_finish(&s));
  TEST_ASSERT(!strcmp(s.session_id, "first"));
  char name[41];
  TEST_ASSERT(agent_hook_name(&s, name) && !strcmp(name, "project"));
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--client"))
    return agent_hook_run_adapter("fixture", NULL, &FIXTURE);
  test_grok();
  test_cursor();
  TEST_ASSERT(agent_adapter_find("opencode")->no_pid);
  adapter_check("opencode", "{}", "session.created", AGENT_EVENT_START);
  adapter_check("opencode", "{}", "permission.asked", AGENT_EVENT_WAITING);
  adapter_check("opencode", "{}", "permission.replied", AGENT_EVENT_WORKING);
  adapter_check("opencode", "{}", "session.execution.succeeded",
                AGENT_EVENT_DONE);
  adapter_check("opencode", "{}", "session.execution.interrupted",
                AGENT_EVENT_INTERRUPT);
  adapter_check("opencode", "{}", "session.execution.failed",
                AGENT_EVENT_INTERRUPT);
  adapter_check("opencode", "{}", "session.deleted", AGENT_EVENT_END);
  adapter_check("opencode", "{}", "session.step.failed", -1);
  adapter_check("opencode", "{}", "session.status", -1);
  adapter_check("pi", "{\"stopReason\":\"stop\"}", "agent_end",
                AGENT_EVENT_DONE);
  adapter_check("pi", "{\"stopReason\":\"error\"}", "agent_end",
                AGENT_EVENT_INTERRUPT);
  adapter_check("pi", "{\"stopReason\":\"aborted\"}", "agent_end",
                AGENT_EVENT_INTERRUPT);
  adapter_check("pi", "{\"stopReason\":\"length\"}", "agent_end",
                AGENT_EVENT_INTERRUPT);
  adapter_check("pi", "{}", "agent_end", -1);
  adapter_check("pi", "{\"parent_session\":\"parent\"}", "agent_start", -1);
  agent_hook_scanner_t pid_scan;
  agent_hook_scan_adapter(&pid_scan, agent_adapter_find("pi"));
  const char *pid_json = "{\"agent_pid\":12345}";
  agent_hook_scan_feed(&pid_scan, pid_json, strlen(pid_json));
  TEST_ASSERT(agent_hook_scan_finish(&pid_scan) && pid_scan.pid == 12345);
  const char *bad_pids[] = {"-1",  "0",   "1",    "2147483648",
                            "1.5", "1e3", "true", "\"12345\""};
  for (size_t i = 0; i < sizeof(bad_pids) / sizeof(bad_pids[0]); i++) {
    char json[64];
    snprintf(json, sizeof(json), "{\"agent_pid\":%s}", bad_pids[i]);
    agent_hook_scan_adapter(&pid_scan, agent_adapter_find("pi"));
    agent_hook_scan_feed(&pid_scan, json, strlen(json));
    TEST_ASSERT(agent_hook_scan_finish(&pid_scan) && pid_scan.pid == 0);
  }
  adapter_check("copilot", "{\"sessionId\":\"test\"}", "userPromptSubmitted",
                AGENT_EVENT_WORKING);
  adapter_check("copilot", "{\"hookName\":\"permissionRequest\"}", NULL, -1);
  adapter_check("copilot", "{}", "permissionRequest", -1);
  adapter_check("copilot",
                "{\"hook_event_name\":\"Notification\",\"notification_type\":"
                "\"permission_prompt\"}",
                "notification", AGENT_EVENT_WAITING);
  adapter_check("copilot",
                "{\"hook_event_name\":\"Notification\",\"notification_type\":"
                "\"permission_prompt\"}",
                NULL, AGENT_EVENT_WAITING);
  adapter_check("copilot", "{\"notification_type\":\"unknown\"}",
                "notification", -1);
  adapter_check("copilot", "{\"stopReason\":\"end_turn\"}", "agentStop",
                AGENT_EVENT_DONE);
  adapter_check("copilot", "{\"stopReason\":\"error\"}", "agentStop",
                AGENT_EVENT_INTERRUPT);
  adapter_check("copilot", "{}", "agentStop", -1);
  adapter_check("copilot", "{}", "errorOccurred", AGENT_EVENT_INTERRUPT);
  adapter_check("copilot", "{}", "sessionEnd", AGENT_EVENT_END);
  TEST_ASSERT(!agent_adapter_find("kimi")->json_stdout);
  adapter_check("kimi", "{\"hook_event_name\":\"PermissionRequest\"}", NULL,
                AGENT_EVENT_WAITING);
  adapter_check(
      "kimi",
      "{\"hook_event_name\":\"PermissionResult\",\"decision\":\"approved\"}",
      NULL, AGENT_EVENT_WORKING);
  adapter_check(
      "kimi",
      "{\"hook_event_name\":\"PermissionResult\",\"decision\":\"rejected\"}",
      NULL, AGENT_EVENT_WORKING);
  adapter_check("kimi", "{\"hook_event_name\":\"Interrupt\"}", NULL,
                AGENT_EVENT_INTERRUPT);
  adapter_check("kimi", "{\"hook_event_name\":\"SessionEnd\"}", NULL,
                AGENT_EVENT_END);
  adapter_check("kimi",
                "{\"hook_event_name\":\"Stop\",\"stop_hook_active\":true}",
                NULL, -1);
  adapter_check("kimi", "{\"hook_event_name\":\"Stop\"}", NULL,
                AGENT_EVENT_DONE);
  TEST_ASSERT(!strcmp(agent_adapter_find("claude")->display_name, "Claude"));
  TEST_ASSERT(!strcmp(agent_adapter_find("codex")->display_name, "Codex"));
  TEST_ASSERT(agent_adapter_find("unknown") == agent_adapter_find("claude"));
  check("{\"event\":\"finish\",\"status\":\"completed\"}", NULL,
        AGENT_EVENT_DONE, NULL, NULL);
  check("{\"event\":\"finish\",\"status\":\"aborted\"}", NULL, AGENT_EVENT_IDLE,
        NULL, NULL);
  check("{\"event\":\"finish\",\"status\":\"new-value\"}", NULL, -1, NULL,
        NULL);
  check("{\"event\":\"finish\"}", NULL, -1, NULL, NULL);
  check("{\"event\":\"finish\"}", "begin", AGENT_EVENT_WORKING, NULL, NULL);
  check("{}", "begin", AGENT_EVENT_WORKING, NULL, NULL);
  check("{\"conversation_id\":\"primary\",\"session_id\":\"secondary\"}", NULL,
        -1, "primary", NULL);
  check("{\"session_id\":\"secondary\",\"conversation_id\":\"primary\"}", NULL,
        -1, "primary", NULL);
  check("{\"workspace_roots\":[\"/tmp/first\",\"/tmp/last\"]}", NULL, -1, NULL,
        "first");
  check("{\"workspace_roots\":[{},[\"/tmp/nested\"],false,\"/tmp/first\"]}",
        NULL, -1, NULL, "first");
  check("{\"workspace_roots\":[\"/tmp/\\u9879\\u76ee\"]}", NULL, -1, NULL,
        "项目");
  check("{\"workspace_roots\":[]}", NULL, -1, NULL, NULL);
  check("{\"workspace_roots\":\"/tmp/wrong-type\"}", NULL, -1, NULL, NULL);
  check("{\"nested\":{\"workspace_roots\":[\"/tmp/wrong-depth\"]}}", NULL, -1,
        NULL, NULL);
  char long_json[1024];
  memset(long_json, 'x', sizeof(long_json));
  const char *prefix = "{\"workspace_roots\":[\"/tmp/";
  memcpy(long_json, prefix, strlen(prefix));
  strcpy(long_json + 900, "\",\"/tmp/second\"]}");
  check(long_json, NULL, -1, NULL, NULL);
  return 0;
}
