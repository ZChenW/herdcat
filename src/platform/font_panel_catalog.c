#define _GNU_SOURCE
#include "font_panel_internal.h"
#include "graphics/font_panel.h"
#include "graphics/text.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

font_panel_face_t catalog[FONT_PANEL_CAP];
int catalog_count, catalog_lang;

// Recently chosen families lead the grid, newest first, so the few faces a
// user moves between are one glance away. Kept beside the other state files.
#define RECENT_MAX 4
static char recent[RECENT_MAX][128];
static bool recent_loaded;
static bool recent_path(char *out, size_t cap, bool create) {
  const char *state = getenv("XDG_STATE_HOME");
  const char *home = getenv("HOME");
  char dir[PATH_MAX];
  int length;
  if (state && state[0] == '/')
    length = snprintf(dir, sizeof(dir), "%s/herdcat", state);
  else if (home && home[0] == '/')
    length = snprintf(dir, sizeof(dir), "%s/.local/state/herdcat", home);
  else
    return false;
  if (length < 0 || (size_t)length >= sizeof(dir))
    return false;
  if (create && mkdir(dir, 0700) < 0 && errno != EEXIST)
    return false;
  length = snprintf(out, cap, "%s/fonts-recent", dir);
  return length > 0 && (size_t)length < cap;
}
static bool recent_name_ok(const char *name) {
  size_t length = strlen(name);
  if (!length || length >= sizeof(recent[0]))
    return false;
  for (const unsigned char *p = (const unsigned char *)name; *p; p++)
    if (*p < 0x20 || *p == 0x7f)
      return false;
  return true;
}
static void load_recent(void) {
  if (recent_loaded)
    return;
  recent_loaded = true;
  char path[PATH_MAX];
  if (!recent_path(path, sizeof(path), false))
    return;
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return;
  struct stat st;
  char text[RECENT_MAX * 130 + 1];
  ssize_t got = -1;
  if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid())
    got = read(fd, text, sizeof(text) - 1);
  close(fd);
  if (got <= 0)
    return;
  text[got] = '\0';
  int count = 0;
  char *save = NULL;
  for (char *line = strtok_r(text, "\n", &save); line && count < RECENT_MAX;
       line = strtok_r(NULL, "\n", &save))
    if (recent_name_ok(line))
      snprintf(recent[count++], sizeof(recent[0]), "%s", line);
}
static void save_recent(void) {
  char path[PATH_MAX], temp[PATH_MAX];
  if (!recent_path(path, sizeof(path), true))
    return;
  int length = snprintf(temp, sizeof(temp), "%s.tmp", path);
  if (length < 0 || (size_t)length >= sizeof(temp))
    return;
  int fd =
      open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0)
    return;
  bool ok = true;
  for (int i = 0; i < RECENT_MAX && ok; i++) {
    if (!recent[i][0])
      continue;
    size_t size = strlen(recent[i]);
    ok = write(fd, recent[i], size) == (ssize_t)size && write(fd, "\n", 1) == 1;
  }
  ok = close(fd) == 0 && ok;
  if (!ok || rename(temp, path) < 0)
    unlink(temp);
}
// Move the recent families to the front of the catalog, keeping the rest in
// their sorted order.
static void lift_recent(void) {
  load_recent();
  int front = 0;
  for (int r = 0; r < RECENT_MAX; r++) {
    if (!recent[r][0])
      continue;
    for (int i = front; i < catalog_count; i++) {
      if (strcmp(catalog[i].name, recent[r]) != 0)
        continue;
      font_panel_face_t found = catalog[i];
      memmove(&catalog[front + 1], &catalog[front],
              (size_t)(i - front) * sizeof(catalog[0]));
      catalog[front++] = found;
      break;
    }
  }
}
// The default face has no family name and is always the first cell.
void note_recent(const char *family) {
  if (!family || !recent_name_ok(family))
    return;
  load_recent();
  int at = RECENT_MAX - 1;
  for (int i = 0; i < RECENT_MAX; i++)
    if (!strcmp(recent[i], family)) {
      at = i;
      break;
    }
  memmove(&recent[1], &recent[0], (size_t)at * sizeof(recent[0]));
  snprintf(recent[0], sizeof(recent[0]), "%s", family);
  save_recent();
}
void load_faces(bool english) {
  int lang = english ? 1 : 2;
  if (catalog_lang == lang)
    return;
  const char *names[FONT_PANEL_CAP];
  int count = text_families(english ? "en" : "zh-cn", names, FONT_PANEL_CAP);
  catalog_count = 0;
  if (count < 0)
    count = 0;
  for (int i = 0; i < count && catalog_count < FONT_PANEL_CAP; i++) {
    if (!names[i] || !names[i][0])
      continue;
    snprintf(catalog[catalog_count].name, sizeof(catalog[catalog_count].name),
             "%s", names[i]);
    // spacing >= 90 (dual, mono, charcell). Names are not inspected.
    int spacing = text_family_spacing(names[i]);
    catalog[catalog_count].mono = text_spacing_mono(spacing);
    catalog_count++;
  }
  catalog_lang = lang;
  lift_recent();
}
