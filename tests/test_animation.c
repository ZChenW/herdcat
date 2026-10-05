#define _POSIX_C_SOURCE 200809L
#include "graphics/animation.h"
#include "graphics/paw_frame.h"
#include "platform/input.h"
#include "platform/wayland.h"
#include "test_helpers.h"

#include <limits.h>
#include <time.h>
atomic_uint *pending_paws;
static unsigned redraws;
static unsigned all_redraws;
void wayland_request_current_redraw(void) {
  redraws++;
}
void wayland_request_redraw(void) {
  all_redraws++;
}
int64_t input_timestamp(void) {
  return 0;
}
static int key_hooks;
static void count_key(void) {
  key_hooks++;
}
static void delay(int ms) {
  struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
  while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}

static void agent_tests(config_t *config, void *first, void *second) {
  animation_overlay_activate(first, config);
  config->fps = 1;
  unsigned before = all_redraws;
  animation_set_agent_state(AGENT_STATE_WORKING);
  TEST_ASSERT(all_redraws == before + 1);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_AGENT_WORKING);
  animation_tick(PAW_LEFT);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_LEFT_DOWN);
  // Changing agent state must not clear an existing paw hold.
  animation_set_agent_state(AGENT_STATE_WAITING);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_LEFT_DOWN);
  animation_set_agent_state(AGENT_STATE_WORKING);
  delay(110);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_AGENT_WORKING);
  animation_overlay_activate(second, config);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_AGENT_WORKING);

  config->idle_sleep_timeout_sec = 1;
  delay(1100);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_AGENT_WORKING);
  animation_set_agent_state(AGENT_STATE_WAITING);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_AGENT_WAITING);
  // Equal endpoints cover the full day without depending on wall-clock time.
  config->enable_scheduled_sleep = 1;
  config->sleep_begin = config->sleep_end = (config_time_t){0, 0};
  animation_tick(PAW_LEFT);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_SLEEPING);
  config->enable_scheduled_sleep = 0;
  animation_set_agent_state(AGENT_STATE_IDLE);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_SLEEPING);
  // Paw holds still win over an already-expired idle sleep deadline.
  config->keypress_duration = 1500;
  animation_tick(PAW_LEFT);
  delay(1100);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_LEFT_DOWN);
  config->keypress_duration = 100;
  config->idle_sleep_timeout_sec = 0;
  animation_set_paused(true);
  animation_set_agent_state(AGENT_STATE_WORKING);
  animation_tick(PAW_RIGHT);
  TEST_ASSERT(anim_index == config->idle_frame);
  TEST_ASSERT(animation_get_agent_state() == AGENT_STATE_WORKING);
  animation_set_paused(false);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_AGENT_WORKING);

  // Session deadlines live outside animation, including while paused.
  animation_set_agent_state(AGENT_STATE_DONE);
  before = all_redraws;
  animation_set_agent_state(AGENT_STATE_DONE);
  TEST_ASSERT(all_redraws == before);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_AGENT_DONE);
  animation_set_paused(true);
  animation_set_agent_state(AGENT_STATE_IDLE);
  TEST_ASSERT(animation_tick(0) == -1);
  TEST_ASSERT(anim_index == config->idle_frame);
  animation_set_agent_state(AGENT_STATE_WAITING);
  animation_set_paused(false);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_AGENT_WAITING);
  animation_set_agent_state(AGENT_STATE_COUNT);
  TEST_ASSERT(animation_get_agent_state() == AGENT_STATE_WAITING);
  animation_set_agent_state(AGENT_STATE_IDLE);
  animation_tick(0);
  TEST_ASSERT(anim_index == config->idle_frame);
}
int main(void) {
  config_t config = {.fps = 1,
                     .keypress_duration = 100,
                     .idle_frame = 0,
                     .enable_hand_mapping = 1};
  TEST_ASSERT(animation_init(&config) == BONGOCAT_SUCCESS);
  void *first = animation_overlay_create(&config);
  void *second = animation_overlay_create(&config);
  TEST_ASSERT(first && second);
  animation_overlay_activate(first, &config);
  TEST_ASSERT(animation_tick(0) == -1);
  unsigned before = redraws;
  int deadline = animation_tick(PAW_LEFT);
  TEST_ASSERT(deadline > 0 && deadline <= 1000);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_LEFT_DOWN && redraws == before);
  config.fps = 120;
  animation_tick(PAW_LEFT);
  TEST_ASSERT(redraws > before);
  animation_overlay_activate(second, &config);
  animation_tick(PAW_RIGHT);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_RIGHT_DOWN);
  animation_overlay_activate(first, &config);
  animation_tick(0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_LEFT_DOWN);
  animation_set_paused(true);
  animation_tick(PAW_RIGHT);
  TEST_ASSERT(anim_index == 0);
  animation_set_paused(false);
  animation_tick(0);
  TEST_ASSERT(anim_index == 0);
  delay(110);
  animation_overlay_activate(second, &config);
  animation_tick(0);
  TEST_ASSERT(anim_index == 0);
  animation_overlay_cache(4, 4);
  for (int i = 0; i < NUM_FRAMES; i++) {
    TEST_ASSERT(anim_cached_frames[i].data);
  }
  uint8_t *cache = anim_cached_frames[0].data;
  TEST_ASSERT(cache);
  animation_overlay_cache(4, 4);
  TEST_ASSERT(cache == anim_cached_frames[0].data);
  uint8_t source[] = {0, 0, 255, 255};
  uint8_t dest[16] = {0};
  blit_cached_frame(dest, 2, 2, source, 1, 1, INT_MIN, INT_MAX);
  for (size_t i = 0; i < sizeof(dest); i++)
    TEST_ASSERT(dest[i] == 0);
  blit_cached_frame(dest, 2, 2, source, 1, 1, 1, 1);
  TEST_ASSERT(dest[14] == 255 && dest[15] == 255);
  agent_tests(&config, first, second);
  animation_set_key_hook(count_key);
  config.mirror_x = 1;
  unsigned drawn = redraws;
  animation_tap(PAW_LEFT, 220);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_RIGHT_DOWN);
  TEST_ASSERT(key_hooks == 0 && redraws == drawn + 1);
  animation_tap(PAW_LEFT, 0);
  TEST_ASSERT(anim_index == BONGOCAT_FRAME_RIGHT_DOWN && key_hooks == 0);
  config.mirror_x = 0;
  animation_overlay_destroy(first);
  animation_overlay_destroy(second);
  animation_set_agent_state(AGENT_STATE_DONE);
  TEST_ASSERT(animation_get_agent_state() == AGENT_STATE_DONE);
  animation_cleanup();
  return 0;
}
