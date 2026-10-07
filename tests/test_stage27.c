#define _GNU_SOURCE
#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "graphics/text.h"
#include "platform/agent_watch.h"
#include "platform/font_panel.h"
#include "platform/overlay_signs.h"
#include "platform/session_store.h"
#include "platform/transcript_watch.h"
#include "test_helpers.h"
#include "utils/path_wire.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// The separate font surface is outside this settled-sign regression.
bool font_panel_surface_is_open(void) {
  return false;
}
void font_panel_surface_close(void) {}
void font_panel_surface_sync(size_t index, const config_t *config,
                             font_panel_anchor_t card, int height, int64_t now,
                             int *timeout, bool toggle) {
  (void)index;
  (void)config;
  (void)card;
  (void)height;
  (void)now;
  (void)timeout;
  (void)toggle;
}
void font_panel_surface_activity(int64_t now) {
  (void)now;
}
bool font_panel_surface_covers(size_t index) {
  (void)index;
  return false;
}
bool font_panel_surface_armed(void) {
  return false;
}
void font_panel_surface_wheel(int n) {
  (void)n;
}
bool font_panel_surface_button(uint32_t button, uint32_t state) {
  (void)button;
  (void)state;
  return false;
}
const char *font_panel_surface_hover(void) {
  return NULL;
}
bool font_panel_surface_take_choice(char *out, size_t cap) {
  (void)out;
  (void)cap;
  return false;
}
void font_panel_surface_left(void) {}
void font_panel_surface_select(const char *family) {
  (void)family;
}
void font_panel_surface_language(bool english) {
  (void)english;
}

static unsigned builds;
void __real_signs_frame(signs_t *, const sign_input_t *, sign_frame_t *);
void __wrap_signs_frame(signs_t *, const sign_input_t *, sign_frame_t *);
void __wrap_signs_frame(signs_t *model, const sign_input_t *input,
                        sign_frame_t *frame) {
  builds++;
  __real_signs_frame(model, input, frame);
}
static char root[] = "/tmp/herdcat-stage27-XXXXXX";
static agent_session_view_t view(uint64_t key) {
  agent_session_view_t rows[AGENT_SESSIONS_MAX];
  int n = agent_sessions_snapshot(rows, AGENT_SESSIONS_MAX);
  for (int i = 0; i < n; i++)
    if (rows[i].key == key)
      return rows[i];
  TEST_ASSERT(false);
  return (agent_session_view_t){0};
}
static void add(uint64_t key, const char *agent, pid_t pid) {
  TEST_ASSERT(
      !agent_sessions_apply(key, agent, AGENT_EVENT_START, pid, 1000, 5, NULL));
}
static void cwd_names(void) {
  agent_sessions_reset();
  add(1, "claude", 0);
  TEST_ASSERT(!agent_sessions_set_cwd_name(1, "/work/Projects/", "Projects"));
  TEST_ASSERT(
      !agent_sessions_set_cwd_name(1, "/work/Projects/herdcat", "herdcat"));
  const char *outside[] = {"/work/Projects", "/work", "/tmp",
                           "/work/Projects-other", "/work/Projects/../other"};
  for (size_t i = 0; i < sizeof(outside) / sizeof(*outside); i++) {
    TEST_ASSERT(!agent_sessions_set_cwd_name(1, outside[i], "wrong"));
    TEST_ASSERT(!strcmp(view(1).name, "herdcat"));
  }
  TEST_ASSERT(!agent_sessions_set_cwd_name(1, "/work/Projects/other", "other"));
  // Legacy name-only hooks keep their previous semantics.
  TEST_ASSERT(!agent_sessions_set_name(1, "legacy"));
  TEST_ASSERT(!strcmp(view(1).name, "legacy"));
  char path[256] = "/work/Projects/space \nline", encoded[511], message[576];
  path_hex(path, encoded);
  snprintf(message, sizeof(message), "cwd %016x %s escaped", 1, encoded);
  TEST_ASSERT(!agent_sessions_cwd_command(message));
  TEST_ASSERT(!strcmp(view(1).name, "escaped"));
  TEST_ASSERT(agent_sessions_cwd_command("cwd 0000000000000001 2f00 nope"));
  // Use the real private store across reset, including newline in start cwd.
  add(2, "codex", 0);
  TEST_ASSERT(!agent_sessions_set_cwd_name(2, path, "start"));
  TEST_ASSERT(!setenv("XDG_RUNTIME_DIR", root, 1));
  session_store_flush(agent_sessions_generation(), 1000, true);
  agent_sessions_reset();
  session_store_load(2000, 5);
  TEST_ASSERT(!agent_sessions_set_cwd_name(1, "/work/Projects", "wrong"));
  TEST_ASSERT(!strcmp(view(1).name, "escaped"));
  TEST_ASSERT(!agent_sessions_set_cwd_name(2, path, "wrong"));
  TEST_ASSERT(!strcmp(view(2).name, "start"));
  strcat(path, "/child");
  TEST_ASSERT(!agent_sessions_set_cwd_name(2, path, "child"));
  TEST_ASSERT(!strcmp(view(2).name, "child"));
}
static void parse_prompt(void) {
  char out[AGENT_TITLE_MAX + 1];
  const char *valid[] = {
      "{\"type\":\"user\",\"isMeta\":false,\"message\":{\"role\":\"user\","
      "\"content\":\"  hello   world\\nignored\"}}",
      "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":[{"
      "\"type\":\"tool_result\",\"content\":\"wrong\"},{\"type\":\"text\","
      "\"text\":\"hello world\"}]}}"};
  for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
    TEST_ASSERT(agent_prompt_line("claude", valid[i], strlen(valid[i]), out));
    TEST_ASSERT(!strcmp(out, "hello world"));
  }
  const char *invalid[] = {
      "{\"type\":\"user\",\"isMeta\":true,\"message\":{\"role\":\"user\","
      "\"content\":\"secret\"}}",
      "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\" "
      "/command\"}}",
      "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":[{"
      "\"type\":\"tool_result\",\"content\":\"secret\"}]}}",
      "{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":"
      "\"wrong\"}}",
      "{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":"
      "\"\\ud800\"}}"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
    TEST_ASSERT(
        !agent_prompt_line("claude", invalid[i], strlen(invalid[i]), out));
  const char *codex = "{\"type\":\"event_msg\",\"payload\":{\"type\":\"user_"
                      "message\",\"message\":\"codex prompt\"}}";
  TEST_ASSERT(agent_prompt_line("codex", codex, strlen(codex), out));
  TEST_ASSERT(!strcmp(out, "codex prompt"));
}
static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void ready(uint32_t token) {
  transcript_watch_ready(token, now_ms());
}
static void recovery(void) {
  char path[512];
  snprintf(path, sizeof(path), "%s/transcript.jsonl", root);
  TEST_ASSERT(!setenv("HOME", root, 1));
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file);
  fputs("{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":"
        "\"recovered first line\\nprivate second line\"}}\n",
        file);
  TEST_ASSERT(!fclose(file));
  agent_sessions_reset();
  add(1, "claude", 0);
  TEST_ASSERT(!agent_sessions_set_transcript(1, path));
  TEST_ASSERT(!agent_watch_init());
  agent_watch_on_ready(ready);
  // Works even with interruption detection disabled and an idle session.
  transcript_watch_sync(false, now_ms());
  int64_t end = now_ms() + 2500;
  while (!view(1).title[0] && now_ms() < end) {
    struct pollfd fd = {.fd = agent_watch_fd(), .events = POLLIN};
    poll(&fd, 1, 20);
    agent_watch_process(NULL);
    transcript_prompt_poll();
  }
  TEST_ASSERT(!strcmp(view(1).title, "recovered first line"));
  TEST_ASSERT(view(1).title_temporary);
  agent_session_record_t record;
  TEST_ASSERT(!agent_sessions_next_prompt(&record));
  TEST_ASSERT(!agent_sessions_set_title(1, "real title"));
  agent_sessions_recovered_prompt(1, view(1).order, "late result");
  TEST_ASSERT(!strcmp(view(1).title, "real title"));
  TEST_ASSERT(!view(1).title_temporary);
  transcript_watch_cleanup();
  agent_watch_cleanup();
  // Only the beginning is read. A prompt beyond 256 KiB cannot be recovered.
  file = fopen(path, "w");
  TEST_ASSERT(file);
  for (int i = 0; i < 256 * 1024; i++)
    fputc(' ', file);
  fputs("\n{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"too "
        "late\"}}\n",
        file);
  TEST_ASSERT(!fclose(file));
  char out[AGENT_TITLE_MAX + 1];
  TEST_ASSERT(!agent_prompt_read("claude", path, out));
  TEST_ASSERT(!unlink(path));
}
static void process(pid_t pid, pid_t parent, const char *env) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%ld", root, (long)pid);
  TEST_ASSERT(!mkdir(path, 0700) || errno == EEXIST);
  snprintf(path, sizeof(path), "%s/%ld/stat", root, (long)pid);
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file);
  fprintf(file, "%ld (agent) S %ld 1 1 0 0\n", (long)pid, (long)parent);
  TEST_ASSERT(!fclose(file));
  snprintf(path, sizeof(path), "%s/%ld/environ", root, (long)pid);
  file = fopen(path, "w");
  TEST_ASSERT(file);
  TEST_ASSERT(fwrite(env, 1, strlen(env) + 1, file) == strlen(env) + 1);
  TEST_ASSERT(!fclose(file));
}
static void detached(void) {
  agent_sessions_reset();
  process(100, 1, "");
  process(200, 1, "CLAUDE_PID=100");
  add(1, "claude", 100);
  unsetenv("CLAUDE_PID");
  TEST_ASSERT(agent_hook_owner_pid(root, 200) == 100);
  TEST_ASSERT(!setenv("CLAUDE_PID", "300", 1));
  TEST_ASSERT(agent_hook_owner_pid(root, 200) == 300);
  TEST_ASSERT(!setenv("CLAUDE_PID", "100oops", 1));
  TEST_ASSERT(!agent_hook_owner_pid(root, 200));
  unsetenv("CLAUDE_PID");
  TEST_ASSERT(!agent_sessions_apply_owned(2, "codex", AGENT_EVENT_WORKING, 0,
                                          200, true, 100, root, 1000, 5));
  TEST_ASSERT(view(2).parent == 1 && view(2).pid == 200);
  agent_session_view_t rows[32], selected[32];
  int n = agent_sessions_snapshot(rows, 32);
  TEST_ASSERT(agent_sessions_select(rows, (size_t)n, selected, 32) == 1);
  for (int i = 0; i < 3; i++) {
    agent_sessions_reset();
    add(1, i == 2 ? "codex" : "claude", i == 1 ? 200 : 100);
    TEST_ASSERT(!agent_sessions_apply_owned(2, "kimi", AGENT_EVENT_START, 0,
                                            200, true,
                                            i == 0   ? 999
                                            : i == 1 ? 200
                                                     : 100,
                                            root, 1000, 5));
    TEST_ASSERT(!view(2).parent);
  }
  char agent[9];
  uint64_t key;
  agent_event_t event;
  pid_t pid, candidate, owner;
  bool metadata;
  TEST_ASSERT(agent_event_owner_request(
      "ev codex start 0000000000000002 0 200 1 100", &key, agent, &event, &pid,
      &candidate, &metadata, &owner));
  TEST_ASSERT(owner == 100 && candidate == 200 && metadata);
}
static void motions(void) {
  TEST_ASSERT(!text_init("sans"));
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    agent_sessions_reset();
    overlay_signs_cleanup();
    add(1, "claude", 0);
    config_t config = {0};
    config.cat_height = 110;
    config.overlay_height = 120;
    config.sign_style = (sign_style_t)style;
    config.sign_idle = SIGN_IDLE_ALWAYS;
    config.sign_animations = SIGN_ANIM_OFF;
    config.sign_max = 5;
    config.sign_font_size = 13;
    int height = overlay_signs_height(&config);
    overlay_signs_step(0, &config, 100, 198, height, false, 1000);
    const sign_frame_t *frame = overlay_signs_frame(0);
    TEST_ASSERT(frame && frame->hit_count);
    sign_hit_t hit = frame->hits[0];
    TEST_ASSERT(overlay_signs_pointer(0, hit.x + hit.w / 2, hit.y + hit.h / 2));
    overlay_signs_step(0, &config, 100, 198, height, false, 1001);
    unsigned before = builds, redraws = 0;
    for (int i = 0; i < 1000; i++) {
      TEST_ASSERT(overlay_signs_pointer(0, hit.x + hit.w / 2 + (i % 2) * .1,
                                        hit.y + hit.h / 2));
      overlay_signs_step_t step =
          overlay_signs_step(0, &config, 100, 198, height, false, 1002 + i);
      redraws += step.redraw;
      TEST_ASSERT(!step.frame);
    }
    TEST_ASSERT(!redraws && builds == before);
    // Crossing targets and later session changes still invalidate the cache.
    overlay_signs_pointer(0, -100, -100);
    TEST_ASSERT(
        overlay_signs_step(0, &config, 100, 198, height, false, 3000).redraw);
  }
  // Full-rate entrance animation used to redraw on each unrelated motion
  // wake even while its existing Wayland frame callback was pending.
  overlay_signs_cleanup();
  agent_sessions_reset();
  add(1, "claude", 0);
  config_t config = {.cat_height = 110,
                     .overlay_height = 120,
                     .sign_style = SIGN_STYLE_POST,
                     .sign_idle = SIGN_IDLE_ALWAYS,
                     .sign_animations = SIGN_ANIM_FULL,
                     .sign_max = 5,
                     .sign_font_size = 13};
  int height = overlay_signs_height(&config);
  overlay_signs_pointer(0, 150, height - 50);
  TEST_ASSERT(
      overlay_signs_step(0, &config, 100, 198, height, false, 1000).frame);
  overlay_signs_frame_wait(0, true);
  unsigned before = builds;
  for (int i = 0; i < 1000; i++) {
    overlay_signs_pointer(0, 150 + i % 2, height - 50);
    overlay_signs_step_t step =
        overlay_signs_step(0, &config, 100, 198, height, false, 1001 + i);
    TEST_ASSERT(!step.redraw && !step.frame);
  }
  TEST_ASSERT(builds == before);
  overlay_signs_frame_wait(0, false);
  TEST_ASSERT(
      overlay_signs_step(0, &config, 100, 198, height, false, 2001).redraw);
  overlay_signs_cleanup();
  text_cleanup();
}
// A hovered board slides away from the pole. The pole-side edge it leaves
// must stay part of its target, or hover would switch off and on forever.
static void hover_edge(void) {
  agent_session_view_t session = view(1);
  session.state = AGENT_STATE_WORKING;
  sign_input_t in = {.sessions = &session,
                     .count = 1,
                     .style = SIGN_STYLE_POST,
                     .idle = SIGN_IDLE_ALWAYS,
                     .animations = SIGN_ANIM_FULL,
                     .open = true,
                     .cat_x = 300,
                     .cat_y = 300,
                     .cat_height = 110,
                     .now_ms = 1000};
  signs_t model = {0};
  sign_frame_t rest, hovered;
  for (int i = 0; i < 100; i++, in.now_ms += 16)
    signs_frame(&model, &in, &rest);
  TEST_ASSERT(rest.hit_count == 1);
  in.has_hover = true;
  in.hover_key = 1;
  for (int i = 0; i < 100; i++, in.now_ms += 16)
    signs_frame(&model, &in, &hovered);
  TEST_ASSERT(hovered.hit_count == 1);
  sign_hit_t a = rest.hits[0], b = hovered.hits[0];
  TEST_ASSERT(b.x <= a.x && b.x + b.w >= a.x + a.w);
  TEST_ASSERT(b.x + b.w > a.x + a.w || b.x < a.x);  // It did slide.
}
int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--transcript-prompt"))
    return agent_prompt_main(argc, argv);
  TEST_ASSERT(mkdtemp(root));
  cwd_names();
  parse_prompt();
  recovery();
  detached();
  motions();
  hover_edge();
  char path[512];
  for (int pid = 100; pid <= 200; pid += 100) {
    snprintf(path, sizeof(path), "%s/%d/stat", root, pid);
    TEST_ASSERT(!unlink(path));
    snprintf(path, sizeof(path), "%s/%d/environ", root, pid);
    TEST_ASSERT(!unlink(path));
    snprintf(path, sizeof(path), "%s/%d", root, pid);
    TEST_ASSERT(!rmdir(path));
  }
  snprintf(path, sizeof(path), "%s/herdcat/sessions", root);
  TEST_ASSERT(!unlink(path));
  snprintf(path, sizeof(path), "%s/herdcat", root);
  TEST_ASSERT(!rmdir(path));
  TEST_ASSERT(!rmdir(root));
  puts("Stage 27 cwd, prompt recovery, 1000 motions and detached owners "
       "passed.");
  return 0;
}
