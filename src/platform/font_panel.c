#define _GNU_SOURCE
#include "platform/font_panel.h"

#include "font_panel_internal.h"
#include "graphics/font_panel.h"
#include "graphics/sign_palette.h"
#include "graphics/text.h"
#include "platform/overlay_geometry.h"
#include "platform/overlay_signs.h"
#include "platform/shm_buffer.h"
#include "platform/wayland.h"
#include "utils/error.h"
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "viewporter-client-protocol.h"
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

static font_panel_t panel;
static char selected_name[128];
static char choice[128];
static bool has_choice;
static size_t owner;
static int placed_margin, placed_margin_x, card_margin_x;
static int panel_scale_120 = 120;
static int logical_w, logical_h;
static int origin_x, origin_y;
static int output_w, output_h;
static bool panel_ready, armed, need_draw, follow_up;
#define BATCH_START 8
static int batch = BATCH_START;
static bool stood_in;

static struct wl_surface *panel_surface;
static struct zwlr_layer_surface_v1 *panel_layer;
static struct wp_viewport *panel_viewport;
static struct wl_callback *panel_frame;
static shm_buffer_t *buffers[2];
static int buffer_w, buffer_h;
static double pointer_x, pointer_y;

static int elapsed_ms(const struct timespec *start,
                      const struct timespec *end) {
  int64_t sec = (int64_t)end->tv_sec - (int64_t)start->tv_sec;
  int64_t nsec = end->tv_nsec - start->tv_nsec;
  int64_t ms = sec * 1000 + nsec / 1000000;
  if (ms < 0)
    return 0;
  if (ms > INT_MAX)
    return INT_MAX;
  return (int)ms;
}
static void merge_timeout(int *timeout_ms, int wait) {
  if (!timeout_ms || wait <= 0)
    return;
  if (*timeout_ms < 0 || wait < *timeout_ms)
    *timeout_ms = wait;
}
static bool output_size(int *width, int *height) {
  if (!output || !width || !height)
    return false;
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    if (outputs[i].wl_output != output)
      continue;
    if (outputs[i].screen_width <= 0 || outputs[i].screen_height <= 0)
      return false;
    *width = outputs[i].screen_width;
    *height = outputs[i].screen_height;
    return true;
  }
  return false;
}
static uint32_t layer_value(layer_type_t layer) {
  switch (layer) {
  case LAYER_BACKGROUND:
    return ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
  case LAYER_BOTTOM:
    return ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
  case LAYER_OVERLAY:
    return ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  default:
    return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  }
}
static void release_frame(void) {
  if (!panel_frame)
    return;
  wl_callback_destroy(panel_frame);
  panel_frame = NULL;
}
static void retire_buffers(void) {
  shm_buffer_retire(buffers[0]);
  shm_buffer_retire(buffers[1]);
  buffers[0] = buffers[1] = NULL;
  buffer_w = buffer_h = 0;
}
static void destroy_surface(void) {
  release_frame();
  if (panel_layer) {
    zwlr_layer_surface_v1_destroy(panel_layer);
    panel_layer = NULL;
  }
  if (panel_viewport) {
    wp_viewport_destroy(panel_viewport);
    panel_viewport = NULL;
  }
  if (panel_surface) {
    wl_surface_destroy(panel_surface);
    panel_surface = NULL;
  }
  retire_buffers();
  panel_ready = false;
  armed = false;
  logical_w = logical_h = 0;
}
// Faces past this page stay unprepared. Marking them early would make the
// next scroll open every newly visible face in one frame.
static int visible_limit(void) {
  font_panel_layout_t layout;
  font_panel_layout(&panel, &layout);
  int count = font_panel_count(&panel);
  if (layout.cell_count <= 0)
    return 0;
  int end = layout.first_row * FONT_PANEL_COLS + layout.cell_count;
  return end < count ? end : count;
}
static bool reveal_left(void) {
  if (!panel.open)
    return false;
  if (!stood_in || !panel.real_preview)
    return true;
  int prepared = font_panel_prepared(&panel);
  return prepared >= 0 && prepared < visible_limit();
}
// Opening a family costs about a millisecond now that faces are looked up
// once, so a whole page usually fits in one frame. The slow tiers stay for
// machines where it does not.
static void tune_batch(int ms) {
  if (ms > 110)
    batch = 1;
  else if (ms > 40)
    batch = 2;
  else if (ms > 12)
    batch = BATCH_START;
  else
    batch = 30;
}
static void note_visible(int ms) {
  if (font_panel_opacity(&panel) <= 0)
    return;
  if (!stood_in) {
    stood_in = true;
    herdcat_log_info("Font panel first paint %d ms for %d families, names "
                     "in the main face",
                     ms, catalog_count);
    font_panel_set_real_preview(&panel, true);
    follow_up = true;
    return;
  }
  tune_batch(ms);
  int prepared = font_panel_prepared(&panel);
  int limit = visible_limit();
  if (prepared < 0 || prepared >= limit)
    return;
  int next = prepared + batch;
  if (next > limit)
    next = limit;
  font_panel_set_prepared(&panel, next);
  follow_up = true;
}
static bool ensure_buffers(void) {
  int scale = panel_scale_120 > 0 ? panel_scale_120 : 120;
  int width = 0, height = 0;
  font_panel_pixels(&panel, scale, &width, &height);
  if (buffers[0] && buffers[1] && buffer_w == width && buffer_h == height)
    return true;
  shm_buffer_t *first = shm_buffer_create(shm, width, height);
  shm_buffer_t *second = first ? shm_buffer_create(shm, width, height) : NULL;
  if (!first || !second) {
    shm_buffer_retire(first);
    shm_buffer_retire(second);
    return false;
  }
  retire_buffers();
  buffers[0] = first;
  buffers[1] = second;
  buffer_w = width;
  buffer_h = height;
  return true;
}
static shm_buffer_t *spare_buffer(void) {
  for (int i = 0; i < 2; i++)
    if (buffers[i] && !buffers[i]->busy)
      return buffers[i];
  return NULL;
}
static void on_frame(void *data, struct wl_callback *callback, uint32_t time);
static const struct wl_callback_listener FRAME_LISTENER = {.done = on_frame};
static void on_frame(void *data, struct wl_callback *callback, uint32_t time) {
  (void)data;
  (void)time;
  if (callback == panel_frame)
    panel_frame = NULL;
  wl_callback_destroy(callback);
  if (panel_surface)
    need_draw = true;
}
static void bind_frame(void) {
  if (panel_frame || !panel_surface)
    return;
  panel_frame = wl_surface_frame(panel_surface);
  wl_callback_add_listener(panel_frame, &FRAME_LISTENER, NULL);
}
static void apply_scale(void) {
  if (!panel_surface)
    return;
  int scale = panel_scale_120 > 0 ? panel_scale_120 : 120;
  if (panel_viewport) {
    wl_surface_set_buffer_scale(panel_surface, 1);
    if (logical_w > 0 && logical_h > 0)
      wp_viewport_set_destination(panel_viewport, logical_w, logical_h);
    return;
  }
  if (scale % 120 == 0)
    wl_surface_set_buffer_scale(panel_surface, scale / 120);
}
static void apply_input(void) {
  if (!compositor || !panel_surface || logical_w <= 0 || logical_h <= 0)
    return;
  struct wl_region *region = wl_compositor_create_region(compositor);
  wl_region_add(region, 0, 0, logical_w, logical_h);
  wl_surface_set_input_region(panel_surface, region);
  wl_region_destroy(region);
}
static int clamp_margin(int value) {
  return value < 0 ? 0 : value;
}
static void apply_margins(void) {
  if (!panel_layer)
    return;
  int top = clamp_margin(origin_y);
  int left = clamp_margin(origin_x);
  int right = clamp_margin(output_w - left - logical_w);
  int bottom = clamp_margin(output_h - top - logical_h);
  zwlr_layer_surface_v1_set_margin(panel_layer, top, right, bottom, left);
}
static void paint(bool animate) {
  if (!panel_surface || !panel_ready || !shm)
    return;
  if (!ensure_buffers())
    return;
  shm_buffer_t *buffer = spare_buffer();
  if (!buffer) {
    need_draw = true;
    return;
  }
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  memset(buffer->pixels, 0, buffer->size);
  int scale = panel_scale_120 > 0 ? panel_scale_120 : 120;
  font_panel_draw(&panel, buffer->pixels, buffer_w, buffer_h, scale);
  clock_gettime(CLOCK_MONOTONIC, &end);
  follow_up = false;
  note_visible(elapsed_ms(&start, &end));
  apply_scale();
  apply_input();
  buffer->busy = true;
  wl_surface_attach(panel_surface, buffer->object, 0, 0);
  wl_surface_damage_buffer(panel_surface, 0, 0, buffer_w, buffer_h);
  if (animate || follow_up || reveal_left())
    bind_frame();
  wl_surface_commit(panel_surface);
  need_draw = follow_up;
}
static void on_configure(void *data, struct zwlr_layer_surface_v1 *layer,
                         uint32_t serial, uint32_t width, uint32_t height) {
  (void)data;
  (void)width;
  (void)height;
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  panel_ready = true;
  paint(reveal_left() || font_panel_opacity(&panel) < 1);
}
static void on_closed(void *data, struct zwlr_layer_surface_v1 *layer) {
  (void)data;
  (void)layer;
  font_panel_close(&panel);
  destroy_surface();
  need_draw = false;
}
static const struct zwlr_layer_surface_v1_listener LAYER_LISTENER = {
    .configure = on_configure, .closed = on_closed};
static bool place_of(const config_t *config, font_panel_anchor_t card,
                     int surface_h, double *x, double *y) {
  if (!output_size(&output_w, &output_h))
    return false;
  bool top = config->overlay_position == POSITION_TOP;
  drag_rect_t absolute =
      overlay_card_rect((drag_rect_t){card.x, card.y, card.w, card.h},
                        card_margin_x, placed_margin, top, output_h, surface_h);
  font_panel_box_t card_box = {absolute.x, absolute.y, absolute.width,
                               absolute.height};
  double scale = panel.scale > 0 ? panel.scale : 1;
  font_panel_size_t size = {font_panel_width(scale),
                            font_panel_height(font_panel_count(&panel), scale)};
  font_panel_size_t screen = {output_w, output_h};
  font_panel_place(&card_box, &size, &screen, FONT_PANEL_GAP * scale, x, y);
  return true;
}
static void remember_size(double width, double height, double x, double y) {
  int next_w = width < 1 ? 1 : (int)(width + 0.5);
  int next_h = height < 1 ? 1 : (int)(height + 0.5);
  int next_x = (int)(x + 0.5);
  int next_y = (int)(y + 0.5);
  if (next_x < 0)
    next_x = 0;
  if (next_y < 0)
    next_y = 0;
  if (next_w == logical_w && next_h == logical_h && next_x == origin_x &&
      next_y == origin_y)
    return;
  logical_w = next_w;
  logical_h = next_h;
  origin_x = next_x;
  origin_y = next_y;
  if (!panel_layer)
    return;
  zwlr_layer_surface_v1_set_size(panel_layer, (uint32_t)logical_w,
                                 (uint32_t)logical_h);
  apply_margins();
  need_draw = true;
}
static bool create_surface(const config_t *config) {
  if (!compositor || !shm || !layer_shell || !output)
    return false;
  if (!output_size(&output_w, &output_h))
    return false;
  panel_scale_120 = wayland_phys_dim(120);
  if (panel_scale_120 <= 0)
    panel_scale_120 = 120;
  panel_surface = wl_compositor_create_surface(compositor);
  if (!panel_surface)
    return false;
  panel_layer = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, panel_surface, output, layer_value(config->layer),
      "herdcat-font-panel");
  if (!panel_layer) {
    wl_surface_destroy(panel_surface);
    panel_surface = NULL;
    return false;
  }
  zwlr_layer_surface_v1_add_listener(panel_layer, &LAYER_LISTENER, NULL);
  panel_viewport = wayland_viewport_for(panel_surface);
  uint32_t anchor =
      ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  zwlr_layer_surface_v1_set_anchor(panel_layer, anchor);
  zwlr_layer_surface_v1_set_size(panel_layer, (uint32_t)logical_w,
                                 (uint32_t)logical_h);
  apply_margins();
  zwlr_layer_surface_v1_set_exclusive_zone(panel_layer, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(panel_layer, 0);
  apply_input();
  wl_surface_commit(panel_surface);
  return true;
}
static void open_model(const config_t *config, int64_t now_ms) {
  bool english = config_sign_english(config);
  // Rebuild the order each time the panel opens so the latest picks lead.
  // It is not reordered while open: cells must not move under the pointer.
  catalog_lang = 0;
  load_faces(english);
  font_panel_reset(&panel);
  font_panel_set_theme(&panel, sign_theme_effective(config->sign_theme));
  font_panel_set_faces(&panel, catalog, catalog_count);
  font_panel_set_prepared(&panel, 0);
  font_panel_set_real_preview(&panel, false);
  stood_in = false;
  batch = BATCH_START;
  follow_up = false;
  double scale = config->cat_height > 0 ? config->cat_height / 110.0 : 1;
  font_panel_open(&panel, english, scale, config->sign_animations,
                  selected_name, now_ms);
}
static void retarget(const config_t *config) {
  if (!panel.open)
    return;
  if (panel.theme != sign_theme_effective(config->sign_theme)) {
    font_panel_set_theme(&panel, sign_theme_effective(config->sign_theme));
    need_draw = true;
  }
  bool english = config_sign_english(config);
  if (panel.english != english) {
    load_faces(english);
    font_panel_set_faces(&panel, catalog, catalog_count);
    font_panel_set_language(&panel, english);
    font_panel_set_prepared(&panel, 0);
    font_panel_set_real_preview(&panel, false);
    stood_in = false;
    batch = BATCH_START;
    need_draw = true;
  }
  if (strcmp(panel.selected, selected_name) != 0)
    font_panel_set_selected(&panel, selected_name);
}
static void refresh_geometry(const config_t *config, font_panel_anchor_t card,
                             int surface_h) {
  double x = 0, y = 0;
  if (!place_of(config, card, surface_h, &x, &y))
    return;
  double scale = panel.scale > 0 ? panel.scale : 1;
  remember_size(font_panel_width(scale),
                font_panel_height(font_panel_count(&panel), scale), x, y);
}
void font_panel_surface_close(void) {
  font_panel_close(&panel);
  destroy_surface();
  need_draw = false;
  follow_up = false;
}
bool font_panel_surface_is_open(void) {
  return panel.open;
}
void font_panel_surface_activity(int64_t now_ms) {
  font_panel_activity(&panel, now_ms);
}
bool font_panel_surface_covers(size_t index) {
  return armed && panel_surface && owner == index;
}
bool font_panel_surface_armed(void) {
  return armed && panel_surface;
}
void font_panel_surface_wheel(int discrete) {
  if (!armed)
    return;
  font_panel_wheel(&panel, discrete, overlay_signs_now());
  need_draw = true;
}
bool font_panel_surface_button(uint32_t button, uint32_t state) {
  if (!armed)
    return false;
  int64_t now = overlay_signs_now();
  font_panel_activity(&panel, now);
  if (button == 0x110 && state == 0) {
    char picked[128];
    font_panel_filter_t before = panel.filter;
    if (font_panel_click(&panel, pointer_x, pointer_y, now, picked,
                         sizeof(picked))) {
      snprintf(choice, sizeof(choice), "%s", picked);
      has_choice = true;
      note_recent(picked);
      need_draw = true;
    } else {
      need_draw = true;
      // The new list is a different set of faces. Start again at the main
      // face so one click cannot open every row.
      if (panel.filter != before) {
        font_panel_set_prepared(&panel, 0);
        batch = BATCH_START;
      }
    }
  }
  return true;
}
const char *font_panel_surface_hover(void) {
  return armed ? font_panel_hover_family(&panel) : NULL;
}
bool font_panel_surface_take_choice(char *out, size_t cap) {
  if (!has_choice)
    return false;
  has_choice = false;
  if (out && cap)
    snprintf(out, cap, "%s", choice);
  return true;
}
bool font_panel_surface_enter(struct wl_surface *target, double x, double y) {
  if (!panel_surface || target != panel_surface)
    return false;
  armed = true;
  pointer_x = x;
  pointer_y = y;
  overlay_signs_track_panel(owner);
  font_panel_pointer(&panel, x, y, overlay_signs_now());
  need_draw = true;
  return true;
}
bool font_panel_surface_motion(double x, double y) {
  if (!armed)
    return false;
  pointer_x = x;
  pointer_y = y;
  font_panel_pointer(&panel, x, y, overlay_signs_now());
  need_draw = true;
  return true;
}
void font_panel_surface_left(void) {
  if (!armed)
    return;
  armed = false;
  font_panel_leave(&panel, overlay_signs_now());
  need_draw = true;
}
void font_panel_surface_output_gone(size_t index) {
  if (panel_surface && owner == index)
    font_panel_surface_close();
}
void font_panel_surface_select(const char *family) {
  snprintf(selected_name, sizeof(selected_name), "%s", family ? family : "");
  if (!panel.open || strcmp(panel.selected, selected_name) == 0)
    return;
  font_panel_set_selected(&panel, selected_name);
  need_draw = true;
}
void font_panel_surface_language(bool english) {
  if (!panel.open || panel.english == english)
    return;
  load_faces(english);
  font_panel_set_faces(&panel, catalog, catalog_count);
  font_panel_set_language(&panel, english);
  font_panel_set_prepared(&panel, 0);
  font_panel_set_real_preview(&panel, false);
  stood_in = false;
  batch = BATCH_START;
  need_draw = true;
}
void font_panel_surface_margin(int margin_x, int margin_y) {
  placed_margin_x = margin_x;
  placed_margin = margin_y;
}
static void toggle_panel(size_t index, const config_t *config, int64_t now_ms) {
  if (panel.open && owner == index) {
    font_panel_surface_close();
    return;
  }
  if (panel.open)
    font_panel_surface_close();
  owner = index;
  open_model(config, now_ms);
}
void font_panel_surface_sync(size_t index, const config_t *config,
                             font_panel_anchor_t card, int surface_h,
                             int64_t now_ms, int *timeout_ms, bool toggle) {
  if (toggle) {
    // The card is hidden while browsing. Retain its opening output-space
    // anchor when the main surface is repositioned by a scale/size change.
    card_margin_x = placed_margin_x;
    toggle_panel(index, config, now_ms);
  }
  if (!panel.open || owner != index) {
    if (!panel.open)
      destroy_surface();
    return;
  }
  int scale = wayland_phys_dim(120);
  if (scale > 0 && scale != panel_scale_120) {
    panel_scale_120 = scale;
    retire_buffers();
    need_draw = true;
  }
  retarget(config);
  refresh_geometry(config, card, surface_h);
  if (!panel_surface &&
      (logical_w <= 0 || logical_h <= 0 || !create_surface(config))) {
    font_panel_surface_close();
    return;
  }
  font_panel_wake_t wake = font_panel_step(&panel, now_ms);
  if (!panel.open) {
    destroy_surface();
    need_draw = false;
    return;
  }
  if (wake.redraw)
    need_draw = true;
  if (panel_ready && need_draw)
    paint(wake.frame);
  else if (panel_ready && (wake.frame || reveal_left()) && !panel_frame) {
    bind_frame();
    wl_surface_commit(panel_surface);
  }
  merge_timeout(timeout_ms, wake.timeout_ms);
}
