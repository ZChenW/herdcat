#include "core/agent_hook.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

static agent_hook_scanner_t scan(const char *json, size_t chunk, bool valid) {
  agent_hook_scanner_t s;
  agent_hook_scan_init(&s);
  size_t length = strlen(json);
  for (size_t i = 0; i < length; i += chunk) {
    size_t n = length - i < chunk ? length - i : chunk;
    agent_hook_scan_feed(&s, json + i, n);
  }
  TEST_ASSERT(agent_hook_scan_finish(&s) == valid);
  return s;
}

static void expect_event(const char *json, int expected) {
  for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
    agent_hook_scanner_t s = scan(json, chunk, true);
    agent_event_t event = AGENT_EVENT_COUNT;
    bool found = agent_hook_event(&s, &event);
    TEST_ASSERT(found == (expected >= 0));
    if (found) {
      TEST_ASSERT((int)event == expected);
    }
  }
}

static void test_mapping(void) {
  const char *names[] = {
      "SessionStart",       "UserPromptSubmit",  "PreToolUse",   "PostToolUse",
      "PostToolUseFailure", "PermissionRequest", "Stop",         "StopFailure",
      "Interrupt",          "SessionEnd",        "SubagentStop", "unknown"};
  const int events[] = {AGENT_EVENT_START,
                        AGENT_EVENT_WORKING,
                        AGENT_EVENT_WORKING,
                        AGENT_EVENT_WORKING,
                        AGENT_EVENT_WORKING,
                        AGENT_EVENT_WAITING,
                        AGENT_EVENT_DONE,
                        AGENT_EVENT_IDLE,
                        AGENT_EVENT_IDLE,
                        AGENT_EVENT_END,
                        -1,
                        -1};
  char json[256];
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    snprintf(json, sizeof(json), "{\"hook_event_name\":\"%s\"}", names[i]);
    expect_event(json, events[i]);
  }
  expect_event("{\"stop_hook_active\":true,\"hook_event_name\":\"Stop\"}", -1);
  expect_event("{\"hook_event_name\":\"Stop\",\"stop_hook_active\":false}",
               AGENT_EVENT_DONE);
  expect_event("{\"hook_event_name\":\"Stop\",\"stop_hook_active\":\"false\"}",
               -1);
  const char *notifications[] = {"permission_prompt", "elicitation_dialog",
                                 "agent_needs_input", "idle_prompt", "other"};
  for (size_t i = 0; i < 5; i++) {
    snprintf(json, sizeof(json),
             "{\"hook_event_name\":\"Notification\","
             "\"notification_type\":\"%s\"}",
             notifications[i]);
    expect_event(json, i < 3    ? AGENT_EVENT_WAITING
                       : i == 3 ? AGENT_EVENT_REST
                                : -1);
  }
  expect_event("{\"hook_event_name\":\"Notification\"}", -1);
  expect_event("{\"hook_event_name\":\"St\\u006fp\"}", -1);
  expect_event("{\"hook_event_name\":true}", -1);
  expect_event("{\"nested\":{\"hook_event_name\":\"Stop\"}}", -1);
  expect_event("{\"nested\":{\"hook_event_name\":\"Stop\","
               "\"stop_hook_active\":true},\"hook_event_name\":\"PreToolUse\"}",
               AGENT_EVENT_WORKING);
  expect_event("{\"hook_event_name\":\"Stop\",\"extra\":[true,false,null,"
               "{},[],[1,-2.5e+3,0,-0.0,1E-9]],\"stop_hook_active\":false}",
               AGENT_EVENT_DONE);
  expect_event("{\"text\":\"brace { quote \\\" slash \\\\ unicode \\u1234\","
               "\"hook_event_name\":\"Stop\"}",
               AGENT_EVENT_DONE);
  expect_event("{\"hook_event_name\":\"Stop\",\"text\":\"猫😀\"}",
               AGENT_EVENT_DONE);
}

static void test_invalid_json(void) {
  const char *invalid[] = {"",
                           "[]",
                           "null",
                           "{}{}",
                           "{",
                           "{\"hook_event_name\":\"Stop\"",
                           "{\"x\":true false}",
                           "{\"x\":tru}",
                           "{\"x\":nulll}",
                           "{\"x\":01}",
                           "{\"x\":-}",
                           "{\"x\":.1}",
                           "{\"x\":1.}",
                           "{\"x\":1e}",
                           "{\"x\":1e+}",
                           "{\"x\":+1}",
                           "{\"x\":1,}",
                           "{\"x\":[1,]}",
                           "{\"x\":[1}}",
                           "{\"x\" 1}",
                           "{\"x\":\"\\q\"}",
                           "{\"x\":\"\\u123x\"}",
                           "{\"x\":\"\n\"}",
                           "{\"x\":\"\xc0\xaf\"}",
                           "{\"x\":\"\xed\xa0\x80\"}",
                           "{\"x\":\"\xf4\x90\x80\x80\"}"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
      agent_hook_scanner_t s = scan(invalid[i], chunk, false);
      agent_event_t event;
      TEST_ASSERT(!agent_hook_event(&s, &event));
    }
  }
  agent_hook_scanner_t s;
  agent_hook_scan_init(&s);
  const char nul[] = "{\"x\":\"\\\0\",\"hook_event_name\":\"Stop\"}";
  agent_hook_scan_feed(&s, nul, sizeof(nul) - 1);
  TEST_ASSERT(!agent_hook_scan_finish(&s));
  agent_hook_scan_init(&s);
  for (int i = 0; i <= AGENT_HOOK_MAX_DEPTH; i++) {
    agent_hook_scan_feed(&s, "{\"x\":", 5);
  }
  TEST_ASSERT(s.failed);
}

static void test_hash_and_large_payload(void) {
  agent_hook_scanner_t s = scan("{\"session_id\":\"test\"}", 1, true);
  TEST_ASSERT(strcmp(s.session_id, "test") == 0);
  TEST_ASSERT(agent_hook_key("claude", &s) == UINT64_C(0xe430d22bdbbe8583));
  TEST_ASSERT(agent_hook_key("codex", &s) == UINT64_C(0x0f886b5c86d51f1a));
  s = scan("{}", 1, true);
  uint64_t fallback = agent_hook_key("claude", &s);
  TEST_ASSERT(fallback == UINT64_C(0x8cd5fc330ae6ec20));
  s = scan("{\"session_id\":\"te\\u0073t\"}", 1, true);
  TEST_ASSERT(agent_hook_key("claude", &s) == fallback);
  char value[256], json[512];
  memset(value, 'x', sizeof(value) - 1);
  value[sizeof(value) - 1] = '\0';
  snprintf(json, sizeof(json), "{\"session_id\":\"%s\"}", value);
  s = scan(json, 1, true);
  TEST_ASSERT(agent_hook_key("claude", &s) == fallback);
  snprintf(json, sizeof(json), "{\"hook_event_name\":\"%s\"}", value);
  expect_event(json, -1);
  s = scan("{\"session_id\":false}", 1, true);
  TEST_ASSERT(agent_hook_key("claude", &s) == fallback);
  // A multi-megabyte ignored string must not impose a total input limit.
  agent_hook_scan_init(&s);
  const char *prefix = "{\"tool_input\":{\"content\":\"";
  agent_hook_scan_feed(&s, prefix, strlen(prefix));
  char chunk[4096];
  memset(chunk, 'x', sizeof(chunk));
  for (int i = 0; i < 1024; i++) {
    agent_hook_scan_feed(&s, chunk, sizeof(chunk));
  }
  const char *suffix = "\"},\"hook_event_name\":\"PreToolUse\","
                       "\"session_id\":\"test\"}";
  agent_hook_scan_feed(&s, suffix, strlen(suffix));
  TEST_ASSERT(agent_hook_scan_finish(&s));
  agent_event_t event;
  TEST_ASSERT(agent_hook_event(&s, &event) && event == AGENT_EVENT_WORKING);
  TEST_ASSERT(agent_hook_key("claude", &s) == UINT64_C(0xe430d22bdbbe8583));
}

static void test_stat_and_agent(void) {
  const char *valid[] = {"a", "claude", "codex", "opencode"};
  const char *invalid[] = {NULL, "", "Claude", "a-b", "a1", "toooolong", "a b"};
  for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
    TEST_ASSERT(agent_hook_valid_agent(valid[i]));
  }
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    TEST_ASSERT(!agent_hook_valid_agent(invalid[i]));
  }
  char comm[64];
  pid_t parent;
  TEST_ASSERT(agent_hook_parse_stat("42 (odd ) ( name) S 123 0 0", comm,
                                    sizeof(comm), &parent) == 0);
  TEST_ASSERT(strcmp(comm, "odd ) ( name") == 0 && parent == 123);
  TEST_ASSERT(
      agent_hook_parse_stat("42 (sh) S 1 0", comm, sizeof(comm), &parent) == 0);
  const char *bad[] = {"",
                       "42 (sh)",
                       "42 (sh) ",
                       "42 (sh) S",
                       "42 (sh) S ",
                       "42 (sh) S -1",
                       "42 (sh) S 9999999999999999999999",
                       "42 sh S 1",
                       "42 (sh) S 1garbage"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    TEST_ASSERT(agent_hook_parse_stat(bad[i], comm, sizeof(comm), &parent) ==
                -1);
  }
  TEST_ASSERT(agent_hook_parse_stat("42 (bash) S 1", comm, 2, &parent) == -1);
}

int main(void) {
  test_mapping();
  test_invalid_json();
  test_hash_and_large_payload();
  test_stat_and_agent();
  return 0;
}
