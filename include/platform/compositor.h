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
  int (*parse_windows)(const char *text, size_t length, focus_window_t *out,
                       size_t capacity);
  bool (*focus_window)(uint64_t id, const char **args, char *text, size_t size);
} compositor_ops_t;
extern const compositor_ops_t COMPOSITOR_NIRI;
extern const compositor_ops_t COMPOSITOR_HYPRLAND;
extern const compositor_ops_t COMPOSITOR_SWAY;
// Selection is side-effect free. Experimental backends require opt-in.
const compositor_ops_t *compositor_detect(bool experimental);
const compositor_ops_t *compositor_selected(void);
void compositor_configure(bool experimental);

int compositor_hyprland_event(const char *text, size_t length,
                              focus_watch_event_t *event);
int compositor_hyprland_windows(const char *text, size_t length,
                                focus_window_t *out, size_t capacity);
bool compositor_hyprland_address(const char *text, uint64_t *id);
bool compositor_hyprland_path(char *path, size_t size);
int compositor_sway_event(const char *text, size_t length,
                          focus_watch_event_t *event);
int compositor_sway_tree(const char *text, size_t length, focus_window_t *out,
                         size_t capacity);
// Header/payload framing is native-endian i3 IPC (14-byte header).
size_t compositor_sway_message(uint32_t type, const char *payload, void *out,
                               size_t size);
int compositor_sway_header(const void *data, size_t size, uint32_t *length,
                           uint32_t *type);
#endif
