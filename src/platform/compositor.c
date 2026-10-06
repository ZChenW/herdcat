#include "platform/compositor.h"

#include <stdlib.h>
static bool experimental_enabled;
const compositor_ops_t *compositor_detect(bool experimental) {
  const char *value = getenv("NIRI_SOCKET");
  if (value && *value)
    return &COMPOSITOR_NIRI;
  value = getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (value && *value)
    return experimental ? &COMPOSITOR_HYPRLAND : NULL;
  value = getenv("SWAYSOCK");
  if (value && *value)
    return experimental ? &COMPOSITOR_SWAY : NULL;
  return NULL;
}
const compositor_ops_t *compositor_selected(void) {
  return compositor_detect(experimental_enabled);
}
void compositor_configure(bool experimental) {
  if (experimental == experimental_enabled)
    return;
  focus_watch_cleanup();
  focus_cleanup();
  experimental_enabled = experimental;
}
