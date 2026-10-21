#include "platform/compositor.h"

#include "platform/focus.h"
#include "platform/focus_watch.h"

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
    return &COMPOSITOR_SWAY;
  return NULL;
}
const compositor_ops_t *compositor_selected(void) {
  return compositor_detect(experimental_enabled);
}
void compositor_configure(bool experimental) {
  if (experimental == experimental_enabled)
    return;
  const compositor_ops_t *previous = compositor_selected();
  experimental_enabled = experimental;
  if (previous == compositor_selected())
    return;
  focus_watch_cleanup();
  focus_cleanup();
}
