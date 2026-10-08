#define _GNU_SOURCE
#include "core/agent_adapters.h"
#include "core/agent_quiet.h"
#include "platform/agent_output.h"
#include "test_helpers.h"

#include <sys/stat.h>
#include <unistd.h>

static uint64_t output;
static bool readable;
static unsigned reads;

// The process I/O boundary supplies deterministic terminal/counter samples.
bool agent_output_wchar(pid_t pid, uint64_t *wchar) {
  TEST_ASSERT(pid == 123 || pid == 124);
  reads++;
  *wchar = output;
  return readable;
}

static void start(const char *agent) {
  agent_quiet_reset();
  agent_sessions_reset();
  output = 0;
  reads = 0;
  readable = true;
  TEST_ASSERT(agent_sessions_apply(1, agent, AGENT_EVENT_WORKING, 123, 100, 5,
                                   NULL) == 0);
  agent_quiet_sync(true, 100);
}

static agent_state_t state(void) {
  agent_session_view_t row;
  TEST_ASSERT(agent_sessions_snapshot(&row, 1) == 1);
  return row.state;
}

static void sample(int64_t now, uint64_t bytes) {
  output += bytes;
  agent_quiet_sync(true, now);
}

static void quiet_contract(const char *agent) {
  start(agent);
  TEST_ASSERT(agent_quiet_deadline() == 1100 && reads == 1);
  sample(1099, 0);
  TEST_ASSERT(reads == 1 && state() == AGENT_STATE_WORKING);
  sample(1100, 8);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(2100, 8);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  TEST_ASSERT(agent_sessions_apply(1, agent, AGENT_EVENT_WORKING, 123, 2200, 5,
                                   NULL) == 0);
  agent_quiet_sync(true, 2200);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  TEST_ASSERT(agent_quiet_deadline() == 3200);

  start(agent);
  sample(1100, 8);
  sample(2100, 8000);  // The measured cancellation burst resets the streak.
  sample(3100, 8);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(4100, 8);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);
  TEST_ASSERT(agent_quiet_deadline() == 0);

  start(agent);
  sample(1100, 256);
  sample(2100, 256);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);

  const agent_event_t events[] = {AGENT_EVENT_WAITING, AGENT_EVENT_DONE,
                                  AGENT_EVENT_IDLE};
  for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
    start(agent);
    TEST_ASSERT(agent_sessions_apply(1, agent, events[i], 123, 200, 5, NULL) ==
                0);
    agent_quiet_sync(true, 200);
    TEST_ASSERT(agent_quiet_deadline() == 0);
    unsigned before = reads;
    sample(10000, 0);
    TEST_ASSERT(reads == before);
    TEST_ASSERT(state() ==
                (events[i] == AGENT_EVENT_WAITING ? AGENT_STATE_WAITING
                 : events[i] == AGENT_EVENT_DONE  ? AGENT_STATE_DONE
                                                  : AGENT_STATE_IDLE));
  }

  start(agent);
  readable = false;  // The boundary also rejects non-terminal stdout.
  sample(1100, 0);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  unsigned before = reads;
  sample(10000, 0);
  TEST_ASSERT(reads == before);
}

int main(void) {
  unsigned before;
  TEST_ASSERT(setenv("HERDCAT_TEST_TIMING", "fast", 1) == 0);
  start("claude");
  TEST_ASSERT(agent_quiet_deadline() == 350 && reads == 1);
  sample(349, 0);
  TEST_ASSERT(reads == 1 && state() == AGENT_STATE_WORKING);
  sample(350, 8);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(600, 8);
  TEST_ASSERT(state() == AGENT_STATE_IDLE && agent_quiet_deadline() == 0);
  start("claude");
  sample(350, 8);
  sample(600, 8000);
  sample(850, 8);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(1100, 8);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);
  // Preserve the rate threshold and reject dispatch gaps at scaled timing.
  start("copilot");
  sample(350, 64);
  sample(600, 64);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(1200, 0);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(1450, 8);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(1700, 8);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);
  TEST_ASSERT(unsetenv("HERDCAT_TEST_TIMING") == 0);
  const char *quiet_agents[] = {"claude", "grok", "copilot"};
  for (size_t i = 0; i < sizeof(quiet_agents) / sizeof(quiet_agents[0]); i++)
    quiet_contract(quiet_agents[i]);
  // Copilot's measured thinking output: 300 bytes every half second for 20s.
  start("copilot");
  for (int64_t now = 600; now <= 20100; now += 500) {
    sample(now, 300);
    TEST_ASSERT(state() == AGENT_STATE_WORKING);
    TEST_ASSERT(agent_quiet_deadline() ==
                100 + ((now - 100) / 1000 + 1) * 1000);
  }
  sample(21100, 8);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(22100, 8);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  start("claude");
  readable = false;
  agent_quiet_reset();
  agent_quiet_sync(true, 100);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  before = reads;
  sample(10000, 0);
  TEST_ASSERT(reads == before);
  start("claude");
  TEST_ASSERT(agent_quiet_deadline() == 1100);
  sample(1099, 0);
  TEST_ASSERT(reads == 1 && state() == AGENT_STATE_WORKING);
  sample(1100, 16);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(2100, 16);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  agent_quiet_record(1, 2150);
  agent_quiet_sync(true, 2150);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  TEST_ASSERT(agent_quiet_deadline() == 3150);
  // An ordinary hook self-heals a quiet classification.
  TEST_ASSERT(agent_sessions_apply(1, "claude", AGENT_EVENT_WORKING, 123, 2200,
                                   5, NULL) == 0);
  agent_quiet_sync(true, 2200);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  TEST_ASSERT(agent_quiet_deadline() == 3200);

  start("grok");
  sample(1100, 40);
  sample(2100, 7000);  // A burst breaks the quiet streak.
  sample(3100, 16);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(4100, 40);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);

  start("claude");
  sample(1100, 256);
  sample(2100, 256);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);

  // Long turns retain the same adjacent one-second cancellation windows.
  for (size_t i = 0; i < sizeof(quiet_agents) / sizeof(quiet_agents[0]); i++) {
    start(quiet_agents[i]);
    for (int64_t now = 1100; now <= 60100; now += 1000) {
      sample(now, 369);  // Reviewer's minimum working half-second output.
      TEST_ASSERT(state() == AGENT_STATE_WORKING);
      TEST_ASSERT(agent_quiet_deadline() == now + 1000);
      before = reads;
      sample(now + 999, 0);
      TEST_ASSERT(reads == before);
    }
    sample(61100, 16);
    TEST_ASSERT(state() == AGENT_STATE_WORKING);
    TEST_ASSERT(agent_quiet_deadline() == 62100);
    sample(62100, 16);
    TEST_ASSERT(state() == AGENT_STATE_IDLE);
    TEST_ASSERT(agent_quiet_deadline() == 0);
  }

  const agent_event_t events[] = {AGENT_EVENT_WAITING, AGENT_EVENT_DONE,
                                  AGENT_EVENT_IDLE};
  for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
    start("claude");
    TEST_ASSERT(
        agent_sessions_apply(1, "claude", events[i], 123, 200, 5, NULL) == 0);
    agent_quiet_sync(true, 200);
    TEST_ASSERT(agent_quiet_deadline() == 0);
    before = reads;
    sample(10000, 0);
    TEST_ASSERT(reads == before);
    TEST_ASSERT(state() ==
                (events[i] == AGENT_EVENT_WAITING ? AGENT_STATE_WAITING
                 : events[i] == AGENT_EVENT_DONE  ? AGENT_STATE_DONE
                                                  : AGENT_STATE_IDLE));
  }
  for (size_t i = 0; i < agent_adapter_count(); i++) {
    const char *name = agent_adapter_at(i)->name;
    if (!strcmp(name, "claude") || !strcmp(name, "grok") ||
        !strcmp(name, "copilot"))
      continue;
    start(name);
    TEST_ASSERT(agent_quiet_deadline() == 0 && reads == 0);
  }
  start("unknown");
  TEST_ASSERT(agent_quiet_deadline() == 0 && reads == 0);
  start("claude");
  readable = false;  // Unreadable I/O or stdout no longer a terminal.
  sample(1100, 0);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  before = reads;
  sample(10000, 0);
  TEST_ASSERT(reads == before);
  start("claude");
  agent_quiet_sync(false, 200);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  start("claude");
  sample(1100, 16);
  // Repeated working hook discards the old quiet window and restarts the guard.
  agent_sessions_apply(1, "claude", AGENT_EVENT_WORKING, 123, 1150, 5, NULL);
  agent_quiet_sync(true, 1150);
  sample(2150, 16);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(3150, 16);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);
  start("claude");
  sample(1100, 16);
  sample(4000, 16);  // Delayed dispatch cannot count as a one-second window.
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(5000, 16);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(6000, 16);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);
  start("claude");
  for (int64_t now = 1100; now <= 16100; now += 1000)
    sample(now, 15000);
  agent_quiet_record(1, 16200);
  agent_quiet_sync(true, 16200);
  TEST_ASSERT(agent_quiet_deadline() == 17200);
  sample(17200, 16);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  sample(18200, 16);
  TEST_ASSERT(state() == AGENT_STATE_IDLE);
  agent_sessions_reset();
  agent_quiet_sync(true, 6100);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  start("claude");
  sample(1100, 16);
  agent_sessions_apply(1, "claude", AGENT_EVENT_WORKING, 124, 1200, 5, NULL);
  agent_quiet_sync(true, 1200);
  TEST_ASSERT(agent_quiet_deadline() == 2200);
  sample(2200, 16);
  TEST_ASSERT(state() == AGENT_STATE_WORKING);
  agent_sessions_apply(1, "claude", AGENT_EVENT_END, 0, 2300, 5, NULL);
  agent_quiet_sync(true, 2300);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  // An idle parent with working children is derived display state only.
  char directory[] = "/tmp/hq-unit-XXXXXX", path[256];
  TEST_ASSERT(mkdtemp(directory));
  snprintf(path, sizeof(path), "%s/124", directory);
  TEST_ASSERT(mkdir(path, 0700) == 0);
  snprintf(path, sizeof(path), "%s/124/stat", directory);
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file);
  TEST_ASSERT(fputs("124 (codex) S 123 1 1 0 0\n", file) >= 0);
  TEST_ASSERT(fclose(file) == 0);
  agent_sessions_apply(1, "claude", AGENT_EVENT_START, 123, 2400, 5, NULL);
  agent_sessions_apply(2, "codex", AGENT_EVENT_WORKING, 124, 2400, 5, NULL);
  agent_sessions_process(2, 124, false, directory);
  agent_quiet_sync(true, 2400);
  TEST_ASSERT(agent_quiet_deadline() == 0);
  agent_session_view_t rows[2];
  TEST_ASSERT(agent_sessions_snapshot(rows, 2) == 2);
  TEST_ASSERT(rows[0].state == AGENT_STATE_IDLE && rows[0].child_count == 1);
  TEST_ASSERT(rows[1].state == AGENT_STATE_WORKING && rows[1].parent == 1);
  TEST_ASSERT(unlink(path) == 0);
  snprintf(path, sizeof(path), "%s/124", directory);
  TEST_ASSERT(rmdir(path) == 0 && rmdir(directory) == 0);
  agent_sessions_reset();
  agent_quiet_reset();
  return 0;
}
