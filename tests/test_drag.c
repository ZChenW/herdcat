#define _GNU_SOURCE
#include "platform/drag.h"
#include "test_helpers.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void geometry(void) {
  config_t config = {.cat_x_offset = 20, .cat_align = ALIGN_LEFT};
  TEST_ASSERT(drag_default_x(&config, 1000, 200) == 20);
  config.cat_align = ALIGN_CENTER;
  TEST_ASSERT(drag_default_x(&config, 1000, 200) == 420);
  config.cat_align = ALIGN_RIGHT;
  TEST_ASSERT(drag_default_x(&config, 1000, 200) == 780);
  config.cat_x_offset = INT_MIN;
  TEST_ASSERT(drag_default_x(&config, INT_MAX, 100) == INT_MAX);
  int x = -1, y = -1;
  drag_clamp(&x, &y, 1000, 200, 800, 100);
  TEST_ASSERT(x == 0 && y == 0);
  x = y = INT_MAX;
  drag_clamp(&x, &y, 1000, 200, 800, 100);
  TEST_ASSERT(x == 800 && y == 700);
  drag_clamp(&x, &y, 100, 200, 50, 100);
  TEST_ASSERT(x == 0 && y == 0);
  config.cat_y_offset = 10;
  drag_rect_t rect = drag_cat_rect(30, &config, 200, 80, 100);
  TEST_ASSERT(rect.x == 30 && rect.y == 20 && rect.width == 200 &&
              rect.height == 80);
  config.cat_y_offset = -40;
  rect = drag_cat_rect(0, &config, 200, 80, 100);
  TEST_ASSERT(rect.y == 0 && rect.height == 50);
  config.cat_y_offset = INT_MAX;
  TEST_ASSERT(drag_cat_rect(0, &config, 200, 80, 100).height == 0);
  TEST_ASSERT(!drag_exceeds_threshold(10, 10, 12, 12));
  TEST_ASSERT(drag_exceeds_threshold(10, 10, 14, 10));
  TEST_ASSERT(drag_exceeds_threshold(10, 10, 7, 7));
}
// Coordinates stay in the press-time frame during a grab, so the margin is
// absolute: skipping motions or repeating one must not change the result.
static void follow_tests(void) {
  TEST_ASSERT(drag_margin_follow(10, 50, 50, false) == 10);
  TEST_ASSERT(drag_margin_follow(10, 50, 30, false) == 30);
  TEST_ASSERT(drag_margin_follow(10, 50, 30, true) == 0);
  TEST_ASSERT(drag_margin_follow(10, 50, 70, true) == 30);
  TEST_ASSERT(drag_margin_follow(5, 50, 90, false) == 0);
  int margin = 0;
  for (int step = 1; step <= 400; step++) {
    if (step % 8 == 0) {
      margin = drag_margin_follow(0, 50.0, 50.0 - (2.0 * step), false);
      TEST_ASSERT(margin == 2 * step);
      TEST_ASSERT(drag_margin_follow(0, 50.0, 50.0 - (2.0 * step), false) ==
                  margin);
    }
  }
  TEST_ASSERT(margin == 800);
}
static void write_file(const char *path, const char *text) {
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file != NULL);
  TEST_ASSERT(fputs(text, file) >= 0);
  TEST_ASSERT(fclose(file) == 0);
}
int main(void) {
  follow_tests();
  geometry();
  char root[] = "/tmp/bongocat-drag-XXXXXX";
  TEST_ASSERT(mkdtemp(root) != NULL);
  TEST_ASSERT(setenv("XDG_STATE_HOME", root, 1) == 0);
  int x = -1, y = -1;
  TEST_ASSERT(drag_position_load("DP-1", &x, &y) == 1);
  TEST_ASSERT(drag_position_reset(NULL) == 0);
  TEST_ASSERT(drag_position_save("DP-1", 120, 300) == 0);
  TEST_ASSERT(drag_position_save("eDP-1", 20, 30) == 0);
  TEST_ASSERT(drag_position_save("DP-1", 140, 320) == 0);
  TEST_ASSERT(drag_position_load("DP-1", &x, &y) == 0 && x == 140 && y == 320);
  TEST_ASSERT(drag_position_load("eDP-1", &x, &y) == 0 && x == 20 && y == 30);
  char path[512], dir[512];
  snprintf(dir, sizeof(dir), "%s/bongocat", root);
  snprintf(path, sizeof(path), "%s/bongocat/position", root);
  struct stat st;
  TEST_ASSERT(stat(dir, &st) == 0 && (st.st_mode & 0777) == 0700);
  TEST_ASSERT(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
  TEST_ASSERT(drag_position_reset("DP-1") == 0);
  TEST_ASSERT(drag_position_load("DP-1", &x, &y) == 1);
  TEST_ASSERT(drag_position_load("eDP-1", &x, &y) == 0 && x == 20);
  write_file(path, "bad\nDP/2 1 2\nDP-2 -1 2\nDP-2 99999999999999 2\nDP-2 1 2 "
                   "extra\nDP-1 42 99");
  TEST_ASSERT(drag_position_load("DP-1", &x, &y) == 0 && x == 42 && y == 99);
  TEST_ASSERT(drag_position_load("DP-2", &x, &y) == 1);
  TEST_ASSERT(drag_position_save("bad name", 0, 0) == -1);
  TEST_ASSERT(drag_position_save("DP-1", -1, 0) == -1);
  char long_line[400];
  memset(long_line, 'a', 300);
  strcpy(long_line + 300, "\nDP-1 3 4\n");
  write_file(path, long_line);
  TEST_ASSERT(drag_position_load("DP-1", &x, &y) == 0 && x == 3 && y == 4);
  TEST_ASSERT(drag_position_reset(NULL) == 0);
  TEST_ASSERT(symlink("/dev/null", path) == 0);
  TEST_ASSERT(drag_position_load("DP-1", &x, &y) == -1);
  TEST_ASSERT(drag_position_save("DP-1", 0, 0) == -1);
  TEST_ASSERT(drag_position_reset(NULL) == -1);
  TEST_ASSERT(unlink(path) == 0);
  TEST_ASSERT(mkfifo(path, 0600) == 0);
  TEST_ASSERT(drag_position_load("DP-1", &x, &y) == -1);
  TEST_ASSERT(unlink(path) == 0);
  for (int i = 0; i < MAX_OUTPUTS; i++) {
    char name[32];
    snprintf(name, sizeof(name), "DP-%d", i);
    TEST_ASSERT(drag_position_save(name, i, i) == 0);
  }
  TEST_ASSERT(drag_position_save("extra", 0, 0) == -1);
  TEST_ASSERT(drag_position_reset(NULL) == 0);
  TEST_ASSERT(rmdir(dir) == 0);
  TEST_ASSERT(symlink(root, dir) == 0);
  TEST_ASSERT(drag_position_save("DP-1", 0, 0) == -1);
  TEST_ASSERT(unlink(dir) == 0);
  TEST_ASSERT(unsetenv("XDG_STATE_HOME") == 0);
  TEST_ASSERT(setenv("HOME", root, 1) == 0);
  TEST_ASSERT(drag_position_save("DP-1", 50, 60) == 0);
  TEST_ASSERT(drag_position_load("DP-1", &x, &y) == 0 && x == 50 && y == 60);
  TEST_ASSERT(drag_position_reset(NULL) == 0);
  snprintf(dir, sizeof(dir), "%s/.local/state/bongocat", root);
  TEST_ASSERT(rmdir(dir) == 0);
  snprintf(dir, sizeof(dir), "%s/.local/state", root);
  TEST_ASSERT(rmdir(dir) == 0);
  snprintf(dir, sizeof(dir), "%s/.local", root);
  TEST_ASSERT(rmdir(dir) == 0);
  TEST_ASSERT(rmdir(root) == 0);
  return 0;
}
