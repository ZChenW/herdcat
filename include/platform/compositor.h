#ifndef HERDCAT_COMPOSITOR_H
#define HERDCAT_COMPOSITOR_H
#include "platform/focus.h"
#include "platform/focus_watch.h"

typedef struct {
  const char *name;
  bool (*detect)(void);
  int (*connect)(void);
  void (*events)(void);
  void (*ready)(uint32_t token);
  int (*timeout)(void);
  bool (*available)(void);
  void (*cleanup)(void);
  // argv has eight slots; command storage must outlive job_start.
  void (*windows)(const char **argv);
  int (*parse_windows)(const char *, size_t, focus_window_t *, size_t);
  bool (*focus_window)(uint64_t, const char **, char *, size_t);
} compositor_ops_t;
extern const compositor_ops_t COMPOSITOR_NIRI;
extern const compositor_ops_t COMPOSITOR_HYPRLAND;
extern const compositor_ops_t COMPOSITOR_SWAY;
// Selection is side-effect free. Experimental backends require opt-in.
const compositor_ops_t *compositor_detect(bool experimental);
const compositor_ops_t *compositor_selected(void);
void compositor_configure(bool experimental);

int compositor_hyprland_event(const char *, size_t, focus_watch_event_t *);
int compositor_hyprland_windows(const char *, size_t, focus_window_t *, size_t);
bool compositor_hyprland_address(const char *, uint64_t *);
bool compositor_hyprland_path(char *, size_t);
int compositor_sway_event(const char *, size_t, focus_watch_event_t *);
int compositor_sway_tree(const char *, size_t, focus_window_t *, size_t);
// Header/payload framing is native-endian i3 IPC (14-byte header).
size_t compositor_sway_message(uint32_t, const char *, void *, size_t);
int compositor_sway_header(const void *, size_t, uint32_t *, uint32_t *);
#endif
