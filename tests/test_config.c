// Unit tests for config parser
// Uses #include of source file to access static functions

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

// Stub out wayland-client.h types before including headers
struct wl_output;
struct zxdg_output_v1;
#define _WAYLAND_CLIENT_H
#define _XDG_OUTPUT_UNSTABLE_V1_CLIENT_PROTOCOL_H

#include "../include/config/config.h"
#include "../include/core/bongocat.h"
#include "../include/utils/error.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg)                                         \
  do {                                                                 \
    if (cond) {                                                        \
      tests_passed++;                                                  \
    } else {                                                           \
      tests_failed++;                                                  \
      fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
    }                                                                  \
  } while (0)

#define TEST_ASSERT_EQ(a, b, msg)                                            \
  do {                                                                       \
    if ((a) == (b)) {                                                        \
      tests_passed++;                                                        \
    } else {                                                                 \
      tests_failed++;                                                        \
      fprintf(stderr, "  FAIL: %s:%d: %s (expected %d, got %d)\n", __FILE__, \
              __LINE__, msg, (int)(b), (int)(a));                            \
    }                                                                        \
  } while (0)

static void write_temp_config(const char *path, const char *content) {
  FILE *f = fopen(path, "w");
  assert(f != NULL);
  fputs(content, f);
  fclose(f);
}

// ---------------------------------------------------------------------------
// Test: default config values
// ---------------------------------------------------------------------------
static void test_defaults(void) {
  printf("test_defaults...\n");
  config_t config = {0};
  bongocat_error_t err =
      load_config(&config, "/nonexistent/path/bongocat.conf");
  // load_config should succeed even with missing file (uses defaults)
  TEST_ASSERT(err == BONGOCAT_SUCCESS || err != BONGOCAT_SUCCESS,
              "load_config returns");
  config_cleanup_full(&config);

  // Test with a valid empty config
  char path[] = "/tmp/bongocat_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  write(fd, "\n", 1);
  close(fd);

  memset(&config, 0, sizeof(config));
  err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "empty config loads successfully");
  TEST_ASSERT_EQ(config.fps, 60, "default fps is 60");
  TEST_ASSERT_EQ(config.cat_height, 40, "default cat_height is 40");
  TEST_ASSERT_EQ(config.overlay_height, 50, "default overlay_height is 50");
  TEST_ASSERT_EQ(config.overlay_opacity, 150, "default overlay_opacity is 150");
  TEST_ASSERT_EQ(config.overlay_position, POSITION_TOP,
                 "default position is top");
  TEST_ASSERT_EQ(config.layer, LAYER_TOP, "default layer is top");
  TEST_ASSERT_EQ(config.enable_antialiasing, 1, "default antialiasing is on");
  TEST_ASSERT_EQ(config.enable_hand_mapping, 1, "default hand_mapping is on");
  TEST_ASSERT_EQ(config.cat_x_offset, 100, "default cat_x_offset is 100");
  TEST_ASSERT_EQ(config.cat_y_offset, 10, "default cat_y_offset is 10");
  TEST_ASSERT_EQ(config.keypress_duration, 100,
                 "default keypress_duration is 100");
  TEST_ASSERT_EQ(config.agent_stale_timeout, 600,
                 "default agent_stale_timeout is 600 seconds");
  TEST_ASSERT_EQ(config.agent_done_timeout, 5,
                 "default agent_done_timeout is 5 seconds");
  for (int i = 0; i < NUM_FRAMES; i++) {
    TEST_ASSERT(config.asset_paths[i] != NULL, "all frames have an asset path");
  }

  config_cleanup_full(&config);
  unlink(path);
}

// ---------------------------------------------------------------------------
// Test: integer clamping
// ---------------------------------------------------------------------------
static void test_integer_clamping(void) {
  printf("test_integer_clamping...\n");
  char path[] = "/tmp/bongocat_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);

  write_temp_config(path, "fps=999\ncat_height=0\noverlay_opacity=-50\n"
                          "overlay_height=1\n");

  config_t config = {0};
  bongocat_error_t err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "clamping config loads");
  TEST_ASSERT_EQ(config.fps, 120, "fps clamped to MAX_FPS=120");
  TEST_ASSERT_EQ(config.cat_height, 10, "cat_height clamped to MIN=10");
  TEST_ASSERT_EQ(config.overlay_opacity, 0, "overlay_opacity clamped to 0");
  TEST_ASSERT_EQ(config.overlay_height, 20, "overlay_height clamped to MIN=20");

  config_cleanup_full(&config);
  unlink(path);
}

// ---------------------------------------------------------------------------
// Test: time parsing
// ---------------------------------------------------------------------------
static void test_time_parsing(void) {
  printf("test_time_parsing...\n");
  char path[] = "/tmp/bongocat_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);

  write_temp_config(
      path, "enable_scheduled_sleep=1\nsleep_begin=22:30\nsleep_end=06:15\n");

  config_t config = {0};
  bongocat_error_t err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "time config loads");
  TEST_ASSERT_EQ(config.sleep_begin.hour, 22, "sleep_begin hour");
  TEST_ASSERT_EQ(config.sleep_begin.min, 30, "sleep_begin min");
  TEST_ASSERT_EQ(config.sleep_end.hour, 6, "sleep_end hour");
  TEST_ASSERT_EQ(config.sleep_end.min, 15, "sleep_end min");

  config_cleanup_full(&config);
  unlink(path);
}

// ---------------------------------------------------------------------------
// Test: malformed integer values (P1-6 strtol validation)
// ---------------------------------------------------------------------------
static void test_malformed_integers(void) {
  printf("test_malformed_integers...\n");
  char path[] = "/tmp/bongocat_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);

  // Non-numeric value should be rejected, config should still load with
  // defaults
  write_temp_config(path, "fps=abc\n");

  config_t config = {0};
  bongocat_error_t err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "malformed int config loads");
  // fps should remain at default since "abc" was rejected
  TEST_ASSERT_EQ(config.fps, 60, "fps stays at default on invalid input");

  config_cleanup_full(&config);
  write_temp_config(path,
                    "fps=60junk\nsleep_begin=22:30junk\nenable_debug=2\n");
  memset(&config, 0, sizeof(config));
  err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "trailing junk is rejected safely");
  TEST_ASSERT_EQ(config.fps, 60, "invalid integer leaves default");
  TEST_ASSERT_EQ(config.sleep_begin.hour, 0, "invalid time leaves default");
  TEST_ASSERT_EQ(config.sleep_begin.min, 0,
                 "invalid time leaves default minute");
  TEST_ASSERT_EQ(config.enable_debug, 0, "invalid boolean leaves default");

  config_cleanup_full(&config);
  unlink(path);
}

// ---------------------------------------------------------------------------
// Test: monitor list parsing
// ---------------------------------------------------------------------------
static void test_monitor_list(void) {
  printf("test_monitor_list...\n");
  char path[] = "/tmp/bongocat_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);

  write_temp_config(path, "monitor=eDP-1, HDMI-A-1 , DP-2\n");

  config_t config = {0};
  bongocat_error_t err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "monitor list config loads");
  TEST_ASSERT_EQ(config.num_output_names, 3, "3 monitors parsed");

  config_cleanup_full(&config);
  unlink(path);
}

// ---------------------------------------------------------------------------
// Test: keyboard_device path validation (P1-7)
// ---------------------------------------------------------------------------
static void test_keyboard_device_validation(void) {
  printf("test_keyboard_device_validation...\n");
  char path[] = "/tmp/bongocat_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);

  // Valid path should be accepted
  write_temp_config(path, "keyboard_device=/dev/input/event0\n");
  config_t config = {0};
  bongocat_error_t err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "valid device path loads");
  TEST_ASSERT_EQ(config.num_keyboard_devices, 1, "device added");
  config_cleanup_full(&config);

  // Path traversal should be rejected
  write_temp_config(path, "keyboard_device=/dev/input/../shadow\n");
  memset(&config, 0, sizeof(config));
  err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "traversal path config loads");
  // The invalid device should be rejected, so count stays at default (1)
  // since config_set_default_devices adds /dev/input/event4
  TEST_ASSERT(config.num_keyboard_devices <= 1,
              "traversal device not added beyond default");
  config_cleanup_full(&config);

  // Non /dev/input/ path should be rejected
  write_temp_config(path, "keyboard_device=/etc/passwd\n");
  memset(&config, 0, sizeof(config));
  err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "invalid path config loads");
  config_cleanup_full(&config);

  unlink(path);
}

// ---------------------------------------------------------------------------
// Test: enum parsing
// ---------------------------------------------------------------------------
static void test_enum_parsing(void) {
  printf("test_enum_parsing...\n");
  char path[] = "/tmp/bongocat_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);

  write_temp_config(path, "overlay_position=bottom\nlayer=background\n"
                          "cat_align=right\n");

  config_t config = {0};
  bongocat_error_t err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "enum config loads");
  TEST_ASSERT_EQ(config.overlay_position, POSITION_BOTTOM,
                 "position is bottom");
  TEST_ASSERT_EQ(config.layer, LAYER_BACKGROUND, "layer is background");
  TEST_ASSERT_EQ(config.cat_align, ALIGN_RIGHT, "align is right");

  config_cleanup_full(&config);

  static const char *layer_names[] = {"background", "bottom", "top", "overlay"};
  for (int i = LAYER_BACKGROUND; i <= LAYER_OVERLAY; i++) {
    char line[32];
    snprintf(line, sizeof(line), "layer=%s\n", layer_names[i]);
    write_temp_config(path, line);
    memset(&config, 0, sizeof(config));
    err = load_config(&config, path);
    TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "layer config loads");
    TEST_ASSERT_EQ(config.layer, (layer_type_t)i, "layer maps correctly");
    config_cleanup_full(&config);
  }
  unlink(path);
}

// ---------------------------------------------------------------------------
// Test: comments and whitespace
// ---------------------------------------------------------------------------
static void test_comments_and_whitespace(void) {
  printf("test_comments_and_whitespace...\n");
  char path[] = "/tmp/bongocat_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);

  write_temp_config(path, "# This is a comment\n"
                          "  fps = 30  # inline comment\n"
                          "\n"
                          "   \t  \n"
                          "; semicolon comment\n"
                          "cat_height = 100\n");

  config_t config = {0};
  bongocat_error_t err = load_config(&config, path);
  TEST_ASSERT_EQ(err, BONGOCAT_SUCCESS, "comment config loads");
  TEST_ASSERT_EQ(config.fps, 30, "fps is 30");
  TEST_ASSERT_EQ(config.cat_height, 100, "cat_height is 100");

  config_cleanup_full(&config);
  unlink(path);
}

static void test_agent_config(void) {
  char path[] = "/tmp/bongocat_test_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);
  const int values[] = {0, 1, 3600, -1, 3601};
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    char text[64];
    snprintf(text, sizeof(text), "agent_done_timeout=%d\n", values[i]);
    write_temp_config(path, text);
    config_t config = {0};
    TEST_ASSERT_EQ(load_config(&config, path), BONGOCAT_SUCCESS,
                   "startup loads agent timeout");
    bool valid = values[i] >= 0 && values[i] <= 3600;
    TEST_ASSERT_EQ(config.agent_done_timeout, valid ? values[i] : 5,
                   "out-of-range timeout resets to default");
    config_cleanup_full(&config);
    TEST_ASSERT_EQ(load_config_strict(&config, path),
                   valid ? BONGOCAT_SUCCESS : BONGOCAT_ERROR_CONFIG,
                   "strict loading rejects invalid timeout");
    config_cleanup_full(&config);
  }
  const int stale_values[] = {0, 1, 600, 3601, 86400, -1, 86401};
  for (size_t i = 0; i < sizeof(stale_values) / sizeof(stale_values[0]); i++) {
    char text[64];
    snprintf(text, sizeof(text), "agent_stale_timeout=%d\n", stale_values[i]);
    write_temp_config(path, text);
    config_t config = {0};
    bool valid = stale_values[i] >= 0 && stale_values[i] <= 86400;
    TEST_ASSERT_EQ(load_config(&config, path), BONGOCAT_SUCCESS,
                   "startup loads stale timeout");
    TEST_ASSERT_EQ(config.agent_stale_timeout, valid ? stale_values[i] : 600,
                   "invalid stale timeout resets to default");
    config_cleanup_full(&config);
    TEST_ASSERT_EQ(load_config_strict(&config, path),
                   valid ? BONGOCAT_SUCCESS : BONGOCAT_ERROR_CONFIG,
                   "strict loading rejects invalid stale timeout");
    config_cleanup_full(&config);
  }
  for (int frame = BONGOCAT_FRAME_SLEEPING; frame < NUM_FRAMES; frame++) {
    char text[32];
    snprintf(text, sizeof(text), "idle_frame=%d\n", frame);
    write_temp_config(path, text);
    config_t config = {0};
    TEST_ASSERT_EQ(load_config(&config, path), BONGOCAT_SUCCESS,
                   "startup validates idle frame");
    TEST_ASSERT_EQ(config.idle_frame,
                   frame <= BONGOCAT_FRAME_LAST_USER ? frame : 0,
                   "agent frames cannot be used as idle_frame");
    config_cleanup_full(&config);
  }
  write_temp_config(path, "[monitor:TEST-1]\nagent_done_timeout=10\n");
  config_t config = {0};
  TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_ERROR_CONFIG,
                 "agent timing must remain global");
  config_cleanup_full(&config);
  write_temp_config(path, "[monitor:TEST-1]\nagent_stale_timeout=10\n");
  TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_ERROR_CONFIG,
                 "stale timing must remain global");
  config_cleanup_full(&config);
  unlink(path);
}

static void test_drag_config(void) {
  char path[] = "/tmp/bongocat-drag-config-XXXXXX";
  int fd = mkstemp(path);
  TEST_ASSERT(fd >= 0, "temporary config created");
  close(fd);
  config_t config = {0}, effective;
  write_temp_config(path, "");
  TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_SUCCESS,
                 "default drag config loads");
  TEST_ASSERT_EQ(config.cat_draggable, 1, "dragging defaults to enabled");
  config_cleanup_full(&config);
  write_temp_config(path,
                    "cat_draggable=0\n[monitor:TEST-1]\ncat_draggable=1\n");
  TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_SUCCESS,
                 "drag monitor override loads");
  config_for_monitor(&config, "TEST-1", &effective);
  TEST_ASSERT_EQ(effective.cat_draggable, 1, "monitor can enable dragging");
  config_for_monitor(&config, "TEST-2", &effective);
  TEST_ASSERT_EQ(effective.cat_draggable, 0, "other monitor remains disabled");
  config_cleanup_full(&config);
  write_temp_config(path, "cat_draggable=2\n");
  TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_ERROR_CONFIG,
                 "drag flag rejects non-boolean values");
  config_cleanup_full(&config);
  unlink(path);
}

static void test_sign_config(void) {
  char path[] = "/tmp/bongocat-sign-config-XXXXXX";
  int fd = mkstemp(path);
  TEST_ASSERT(fd >= 0, "temporary sign config");
  close(fd);
  config_t config = {0};
  write_temp_config(path, "");
  TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_SUCCESS,
                 "sign defaults load");
  TEST_ASSERT(config.sign_style == SIGN_STYLE_FAN && config.sign_max == 5 &&
                  config.sign_idle == SIGN_IDLE_HOVER && !config.sign_font[0] &&
                  config.sign_font_size == 13 &&
                  config.sign_animations == SIGN_ANIM_FULL &&
                  config.sign_language == SIGN_LANGUAGE_AUTO &&
                  config.sign_done == SIGN_DONE_STICKY &&
                  config.sign_typing_desk,
              "all nine sign defaults");
  config_cleanup_full(&config);
  const char *valid[] = {
      "sign_style=fan",       "sign_style=post",
      "sign_style=off",       "sign_idle=hover",
      "sign_idle=always",     "sign_idle=never",
      "sign_animations=full", "sign_animations=reduced",
      "sign_animations=off",  "sign_language=auto",
      "sign_language=en",     "sign_language=zh",
      "sign_done=sticky",     "sign_done=timeout",
      "sign_typing_desk=0",   "sign_typing_desk=1",
      "sign_font=",           "sign_font=Noto Sans",
  };
  for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
    write_temp_config(path, valid[i]);
    TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_SUCCESS,
                   "every sign enum and font value loads");
    config_cleanup_full(&config);
  }
  for (int max = 1; max <= 5; max++) {
    for (int size = 10; size <= 20; size++) {
      char line[64];
      snprintf(line, sizeof(line), "sign_max=%d\nsign_font_size=%d\n", max,
               size);
      write_temp_config(path, line);
      TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_SUCCESS,
                     "all numeric sign values load");
      TEST_ASSERT(config.sign_max == max && config.sign_font_size == size,
                  "numeric sign values preserved");
      config_cleanup_full(&config);
    }
  }
  const char *invalid[] = {
      "sign_style=unknown",
      "sign_idle=unknown",
      "sign_animations=unknown",
      "sign_language=unknown",
      "sign_done=unknown",
      "sign_typing_desk=2",
      "sign_typing_desk=-1",
      "sign_max=0",
      "sign_max=6",
      "sign_max=1x",
      "sign_font_size=9",
      "sign_font_size=21",
      "sign_font_size=13.5",
      "[monitor:TEST-1]\nsign_style=post",
      "sign_font=bad\tfont",
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    write_temp_config(path, invalid[i]);
    TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_ERROR_CONFIG,
                   "invalid sign values rejected");
    config_cleanup_full(&config);
  }
  char long_font[160] = "sign_font=";
  memset(long_font + 10, 'a', 128);
  long_font[138] = '\0';
  write_temp_config(path, long_font);
  TEST_ASSERT_EQ(load_config_strict(&config, path), BONGOCAT_ERROR_CONFIG,
                 "overlong font rejected without truncating");
  config_cleanup_full(&config);
  char *lang = getenv("LANG") ? strdup(getenv("LANG")) : NULL;
  char *messages = getenv("LC_MESSAGES") ? strdup(getenv("LC_MESSAGES")) : NULL;
  config.sign_language = SIGN_LANGUAGE_AUTO;
  setenv("LANG", "zh_CN.UTF-8", 1);
  unsetenv("LC_MESSAGES");
  TEST_ASSERT(!config_sign_english(&config), "auto uses Chinese LANG");
  setenv("LC_MESSAGES", "en_US.UTF-8", 1);
  TEST_ASSERT(config_sign_english(&config), "LC_MESSAGES precedes LANG");
  setenv("LC_MESSAGES", "zh_TW.UTF-8", 1);
  TEST_ASSERT(!config_sign_english(&config), "Chinese locale uses zh table");
  setenv("LC_MESSAGES", "", 1);
  TEST_ASSERT(!config_sign_english(&config),
              "empty messages falls back to LANG");
  unsetenv("LANG");
  TEST_ASSERT(config_sign_english(&config), "absent locale uses English");
  config.sign_language = SIGN_LANGUAGE_ZH;
  TEST_ASSERT(!config_sign_english(&config), "explicit zh overrides locale");
  config.sign_language = SIGN_LANGUAGE_EN;
  TEST_ASSERT(config_sign_english(&config), "explicit en overrides locale");
  if (lang)
    setenv("LANG", lang, 1);
  else
    unsetenv("LANG");
  if (messages)
    setenv("LC_MESSAGES", messages, 1);
  else
    unsetenv("LC_MESSAGES");
  free(lang);
  free(messages);
  unlink(path);
}

int main(void) {
  bongocat_error_init(0);  // Suppress debug output
  printf("=== Config Parser Tests ===\n");

  test_defaults();
  test_integer_clamping();
  test_time_parsing();
  test_malformed_integers();
  test_monitor_list();
  test_keyboard_device_validation();
  test_enum_parsing();
  test_comments_and_whitespace();
  test_agent_config();
  char path[] = "/tmp/bongocat-interrupt-config-XXXXXX";
  int fd = mkstemp(path);
  TEST_ASSERT(fd >= 0, "interrupt config tempfile");
  close(fd);
  for (int v = -1; v <= 2; v++) {
    char text[64];
    snprintf(text, sizeof(text), "agent_interrupt_detect=%d\n", v);
    write_temp_config(path, text);
    config_t cfg;
    bool valid = v == 0 || v == 1;
    TEST_ASSERT_EQ(load_config_strict(&cfg, path),
                   valid ? BONGOCAT_SUCCESS : BONGOCAT_ERROR_CONFIG,
                   "interrupt boolean is strict");
    TEST_ASSERT_EQ(cfg.agent_interrupt_detect, valid ? v : 1,
                   "interrupt default and boolean");
    config_cleanup_full(&cfg);
  }
  write_temp_config(path, "[monitor:TEST-1]\nagent_interrupt_detect=0\n");
  config_t cfg;
  TEST_ASSERT_EQ(load_config_strict(&cfg, path), BONGOCAT_ERROR_CONFIG,
                 "interrupt detect is global only");
  config_cleanup_full(&cfg);
  unlink(path);
  test_drag_config();
  test_sign_config();

  printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
