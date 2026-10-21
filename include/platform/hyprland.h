#ifndef HYPRLAND_H
#define HYPRLAND_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

// =============================================================================
// HYPRLAND-SPECIFIC TYPES
// =============================================================================

typedef struct {
  int monitor_id;  // monitor number in Hyprland
  int x, y;
  int width, height;
  bool fullscreen;
} window_info_t;

// =============================================================================
// HYPRLAND HELPER FUNCTIONS
// =============================================================================

struct wl_output;
void hypr_poll(void);
int hypr_poll_fd(void);
int hypr_timeout(void);
bool hypr_fullscreen_for_output(struct wl_output *object);
void hypr_cleanup(void);

/// Execute a command and capture its stdout into buf. Returns bytes read or -1.
/// Uses posix_spawnp with a one-second deadline and rejects truncation.
ssize_t safe_exec_read(const char *const argv[], char *buf, size_t buf_size);

#endif  // HYPRLAND_H
