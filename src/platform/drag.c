#define _GNU_SOURCE
#include "platform/drag.h"

#include "utils/error.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define POSITION_LINE_MAX 256
#define POSITION_FILE_MAX (MAX_OUTPUTS * POSITION_LINE_MAX)

typedef struct {
  char output[128];
  int x, y;
} position_t;

static int bounded(int64_t value, int64_t low, int64_t high) {
  return (int)(value < low ? low : value > high ? high : value);
}
int drag_default_x(const config_t *config, int surface_width, int cat_width) {
  int64_t x = config->cat_x_offset;
  if (config->cat_align == ALIGN_CENTER) {
    x += ((int64_t)surface_width - cat_width) / 2;
  } else if (config->cat_align == ALIGN_RIGHT) {
    x = (int64_t)surface_width - cat_width - x;
  }
  return bounded(x, INT_MIN, INT_MAX);
}
void drag_clamp(int *x, int *y, int surface_width, int cat_width,
                int output_height, int overlay_height) {
  int64_t max_x = (int64_t)surface_width - cat_width;
  int64_t max_y = (int64_t)output_height - overlay_height;
  *x = bounded(*x, 0, max_x > 0 ? max_x : 0);
  *y = bounded(*y, 0, max_y > 0 ? max_y : 0);
}
drag_rect_t drag_cat_rect(int x, const config_t *config, int cat_width,
                          int cat_height, int overlay_height) {
  int64_t top =
      ((int64_t)overlay_height - cat_height) / 2 + config->cat_y_offset;
  int64_t bottom = top + cat_height;
  int y = bounded(top, 0, overlay_height > 0 ? overlay_height : 0);
  int end = bounded(bottom, y, overlay_height > y ? overlay_height : y);
  return (drag_rect_t){x, y, cat_width > 0 ? cat_width : 0, end - y};
}
bool drag_exceeds_threshold(double start_x, double start_y, double x,
                            double y) {
  double dx = x - start_x;
  double dy = y - start_y;
  return dx * dx + dy * dy >= 16.0;
}
int drag_margin_follow(int press_margin, double grab_y, double pointer_y,
                       bool top) {
  double error = pointer_y - grab_y;
  if (!(error > -(double)INT_MAX && error < (double)INT_MAX)) {
    return press_margin;
  }
  int64_t value =
      (int64_t)press_margin + (top ? llround(error) : -llround(error));
  return value < 0 ? 0 : value > INT_MAX ? INT_MAX : (int)value;
}
static bool valid_output(const char *output) {
  if (!output || !*output ||
      strlen(output) >= sizeof(((position_t *)0)->output)) {
    return false;
  }
  for (const char *p = output; *p; p++) {
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
          (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) {
      return false;
    }
  }
  return true;
}
// Walk directory descriptors so no component can redirect through a symlink.
static int state_dir(bool create) {
  const char *base = getenv("XDG_STATE_HOME");
  char path[PATH_MAX];
  int length;
  if (base && base[0] == '/') {
    length = snprintf(path, sizeof(path), "%s/herdcat", base);
  } else {
    base = getenv("HOME");
    if (!base || base[0] != '/') {
      errno = EINVAL;
      return -1;
    }
    length = snprintf(path, sizeof(path), "%s/.local/state/herdcat", base);
  }
  if (length < 0 || (size_t)length >= sizeof(path)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  int dir = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  char *save = NULL;
  for (char *part = strtok_r(path, "/", &save); dir >= 0 && part;
       part = strtok_r(NULL, "/", &save)) {
    int next =
        openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT && create) {
      if (mkdirat(dir, part, 0700) == 0 || errno == EEXIST) {
        next =
            openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      }
    }
    int saved_errno = errno;
    close(dir);
    dir = next;
    errno = saved_errno;
  }
  if (dir >= 0) {
    struct stat st;
    if (fstat(dir, &st) || st.st_uid != getuid() ||
        (create && fchmod(dir, 0700))) {
      close(dir);
      errno = EACCES;
      return -1;
    }
  }
  return dir;
}
static bool parse_number(const char *text, int *value) {
  if (!text || !*text || *text < '0' || *text > '9') {
    return false;
  }
  char *end;
  errno = 0;
  long number = strtol(text, &end, 10);
  if (errno || *end || number < 0 || number > INT_MAX) {
    return false;
  }
  *value = (int)number;
  return true;
}
static int read_positions(int dir, position_t *entries, size_t *count) {
  *count = 0;
  int fd =
      openat(dir, "position", O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    return errno == ENOENT ? 0 : -1;
  }
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
      st.st_size > POSITION_FILE_MAX) {
    close(fd);
    errno = EINVAL;
    return -1;
  }
  FILE *file = fdopen(fd, "r");
  if (!file) {
    close(fd);
    return -1;
  }
  char contents[POSITION_FILE_MAX + 1];
  size_t bytes = fread(contents, 1, sizeof(contents), file);
  bool failed = ferror(file) || bytes == sizeof(contents) ||
                memchr(contents, '\0', bytes);
  fclose(file);
  if (failed) {
    errno = EFBIG;
    return -1;
  }
  contents[bytes] = '\0';
  for (char *line = contents, *next; line; line = next) {
    next = strchr(line, '\n');
    if (next) {
      *next++ = '\0';
    }
    if (!*line) {
      continue;
    }
    bool too_long = strlen(line) >= POSITION_LINE_MAX;
    char *save = NULL;
    char *name = strtok_r(line, " \t\r\n", &save);
    char *x = strtok_r(NULL, " \t\r\n", &save);
    char *y = strtok_r(NULL, " \t\r\n", &save);
    position_t entry = {0};
    if (too_long || !valid_output(name) || !parse_number(x, &entry.x) ||
        !parse_number(y, &entry.y) || strtok_r(NULL, " \t\r\n", &save)) {
      herdcat_log_warning("Skipping malformed drag position line");
      continue;
    }
    size_t index = 0;
    while (index < *count && strcmp(entries[index].output, name)) {
      index++;
    }
    if (index == MAX_OUTPUTS) {
      herdcat_log_warning("Ignoring extra drag position entry");
      continue;
    }
    snprintf(entry.output, sizeof(entry.output), "%s", name);
    entries[index] = entry;
    if (index == *count) {
      (*count)++;
    }
  }
  return 0;
}
static int write_positions(int dir, const position_t *entries, size_t count) {
  static unsigned sequence;
  char temp[64];
  int fd = -1;
  for (int i = 0; i < 16; i++) {
    snprintf(temp, sizeof(temp), ".position.%ld.%u", (long)getpid(),
             sequence++);
    fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                0600);
    if (fd >= 0 || errno != EEXIST) {
      break;
    }
  }
  if (fd < 0) {
    return -1;
  }
  int result = fchmod(fd, 0600);
  for (size_t i = 0; !result && i < count; i++) {
    if (dprintf(fd, "%s %d %d\n", entries[i].output, entries[i].x,
                entries[i].y) < 0) {
      result = -1;
    }
  }
  if (!result) {
    result = fsync(fd);
  }
  if (close(fd)) {
    result = -1;
  }
  if (!result) {
    result = renameat(dir, temp, dir, "position");
  }
  if (result) {
    unlinkat(dir, temp, 0);
  }
  return result;
}
int drag_position_load(const char *output, int *x, int *y) {
  if (!valid_output(output) || !x || !y) {
    errno = EINVAL;
    return -1;
  }
  int dir = state_dir(false);
  if (dir < 0) {
    return errno == ENOENT ? 1 : -1;
  }
  position_t entries[MAX_OUTPUTS];
  size_t count;
  int result = read_positions(dir, entries, &count);
  close(dir);
  if (result) {
    return -1;
  }
  for (size_t i = 0; i < count; i++) {
    if (!strcmp(output, entries[i].output)) {
      *x = entries[i].x;
      *y = entries[i].y;
      return 0;
    }
  }
  return 1;
}
static int change_position(const char *output, int x, int y, bool remove) {
  if ((output && !valid_output(output)) ||
      (!remove && (!output || x < 0 || y < 0))) {
    errno = EINVAL;
    return -1;
  }
  int dir = state_dir(!remove);
  if (dir < 0) {
    return remove && errno == ENOENT ? 0 : -1;
  }
  position_t entries[MAX_OUTPUTS];
  size_t count;
  int result = read_positions(dir, entries, &count);
  if (!result && !output) {
    result = unlinkat(dir, "position", 0);
    if (result && errno == ENOENT) {
      result = 0;
    }
  } else if (!result) {
    size_t index = 0;
    while (index < count && strcmp(output, entries[index].output)) {
      index++;
    }
    if (remove && index < count) {
      entries[index] = entries[--count];
    } else if (!remove && index < MAX_OUTPUTS) {
      snprintf(entries[index].output, sizeof(entries[index].output), "%s",
               output);
      entries[index].x = x;
      entries[index].y = y;
      if (index == count) {
        count++;
      }
    } else if (!remove) {
      errno = ENOSPC;
      result = -1;
    }
    if (!result) {
      result = write_positions(dir, entries, count);
    }
  }
  close(dir);
  return result;
}
int drag_position_save(const char *output, int x, int y) {
  return change_position(output, x, y, false);
}
int drag_position_reset(const char *output) {
  return change_position(output, 0, 0, true);
}
