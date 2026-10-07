// Split-post examples use the production model, rasterizer, text and cat.
#include "graphics/animation.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "platform/input.h"
#include "platform/wayland.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SC 2
atomic_uint *pending_paws;
void wayland_request_current_redraw(void) {}
void wayland_request_redraw(void) {}
int64_t input_timestamp(void) {
  return 0;
}
static const char *output_dir;
static void shot(const char *name, sign_input_t *in, int width, int height) {
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, in, &frame);
  int w = width * SC, h = height * SC;
  uint8_t *pixels = malloc((size_t)w * h * 4);
  if (!pixels)
    exit(1);
  uint32_t bg = in->theme == SIGN_THEME_DARK ? 0x1a1b26 : 0xf1f2f6;
  for (int i = 0; i < w * h; i++) {
    pixels[i * 4] = bg & 255;
    pixels[i * 4 + 1] = (bg >> 8) & 255;
    pixels[i * 4 + 2] = (bg >> 16) & 255;
    pixels[i * 4 + 3] = 255;
  }
  animation_cache_frames((int)(in->cat_height * 1.8) * SC, in->cat_height * SC,
                         0, 0, 1);
  sign_draw(pixels, w, h, SC * 120, &frame, SIGN_DRAW_UNDER);
  const cached_frame_t *cat = &anim_cached_frames[0];
  blit_cached_frame(pixels, w, h, cat->data, cat->width, cat->height,
                    (int)(in->cat_x * SC),
                    (int)((in->cat_y - frame.cat_lift) * SC));
  sign_draw(pixels, w, h, SC * 120, &frame, SIGN_DRAW_OVER);
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s.ppm", output_dir, name);
  FILE *file = fopen(path, "wb");
  if (!file)
    exit(1);
  fprintf(file, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++) {
    fputc(pixels[i * 4 + 2], file);
    fputc(pixels[i * 4 + 1], file);
    fputc(pixels[i * 4], file);
  }
  if (fclose(file))
    exit(1);
  free(pixels);
  sign_draw_cleanup();
}
static sign_input_t input(agent_session_view_t *sessions, int count) {
  return (sign_input_t){.sessions = sessions,
                        .count = (size_t)count,
                        .style = SIGN_STYLE_POST,
                        .name = SIGN_NAME_PROJECT,
                        .name_extra = SIGN_EXTRA_INLINE,
                        .title_length = 16,
                        .animations = SIGN_ANIM_OFF,
                        .idle = SIGN_IDLE_ALWAYS,
                        .english = true,
                        .font_size = 13,
                        .now_ms = 180000,
                        .cat_x = 311,
                        .cat_y = 365,
                        .cat_height = 110,
                        .surface_width = 820,
                        .surface_height = 490};
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  output_dir = argv[1];
  config_t config = {.cat_height = 110, .fps = 60};
  if (text_init("Noto Sans") || animation_init(&config) != HERDCAT_SUCCESS)
    return 1;
  const char *agents[] = {"claude", "codex", "grok"};
  const char *states[] = {"Idle", "Working", "Waiting", "Done", "Error"};
  agent_session_view_t sessions[10] = {0};
  for (int theme = 0; theme < 2; theme++) {
    for (int agent = 0; agent < 3; agent++) {
      for (int i = 0; i < 10; i++) {
        sessions[i] = (agent_session_view_t){.key = (uint64_t)i + 1,
                                             .order = (uint64_t)i + 1,
                                             .state = (agent_state_t)(i / 2),
                                             .unread = i / 2 >= 3};
        strcpy(sessions[i].agent, agents[agent]);
        snprintf(sessions[i].name, sizeof(sessions[i].name), "%s",
                 states[i / 2]);
        strcpy(sessions[i].title, "Layout review");
      }
      sign_input_t in = input(sessions, 10);
      in.open = true;
      in.theme = (sign_theme_t)theme;
      char name[64];
      snprintf(name, sizeof(name), "%s-states-%s", agents[agent],
               theme ? "dark" : "light");
      shot(name, &in, 820, 490);
    }
    for (int i = 0; i < 10; i++) {
      sessions[i].state = (agent_state_t)(i / 2);
      strcpy(sessions[i].agent, agents[(i / 2) % 3]);
    }
    sign_input_t in = input(sessions, 10);
    in.theme = (sign_theme_t)theme;
    char name[64];
    snprintf(name, sizeof(name), "collapsed-%s", theme ? "dark" : "light");
    shot(name, &in, 820, 490);
    for (int i = 0; i < 10; i++) {
      sessions[i].state = AGENT_STATE_IDLE;
      strcpy(sessions[i].agent, agents[(i / 2) % 3]);
    }
    snprintf(name, sizeof(name), "compact-shapes-%s", theme ? "dark" : "light");
    shot(name, &in, 820, 490);
    for (int i = 0; i < 10; i++)
      sessions[i].state = (agent_state_t)(i / 2);
    in.open = true;
    in.has_hover = true;
    in.hover_key = 3;
    snprintf(name, sizeof(name), "hover-ten-%s", theme ? "dark" : "light");
    shot(name, &in, 820, 490);
    in.hover_key = 4;
    snprintf(name, sizeof(name), "hover-left-ten-%s", theme ? "dark" : "light");
    shot(name, &in, 820, 490);
    in.orientation = SIGN_BELOW;
    in.cat_y = 14;
    snprintf(name, sizeof(name), "below-ten-%s", theme ? "dark" : "light");
    shot(name, &in, 820, 490);
  }
  // README style comparison preserves the existing production fan fixture.
  for (int i = 0; i < 3; i++) {
    sessions[i] =
        (agent_session_view_t){.key = (uint64_t)i + 1,
                               .order = (uint64_t)i + 1,
                               .state = (agent_state_t)(i == 0   ? 1
                                                        : i == 1 ? 2
                                                                 : 0)};
    strcpy(sessions[i].agent, agents[i]);
    strcpy(sessions[i].name, i == 0   ? "herdcat"
                             : i == 1 ? "api-server"
                                      : "notes");
    if (i < 2)
      strcpy(sessions[i].title, i == 0 ? "Fix sign layout" : "Rate limiting");
  }
  sign_input_t in = input(sessions, 3);
  in.cat_height = 66;
  in.cat_x = 260;
  in.cat_y = 215;
  in.surface_width = 640;
  in.surface_height = 300;
  in.style = SIGN_STYLE_FAN;
  in.has_hover = true;
  in.hover_key = 1;
  shot("readme-fan", &in, 640, 300);
  in.style = SIGN_STYLE_POST;
  in.open = true;
  in.has_hover = false;
  shot("readme-style-post", &in, 640, 300);
  in.cat_x = 234;
  in.cat_y = 160;
  in.surface_width = 620;
  in.surface_height = 240;
  sessions[0].state = AGENT_STATE_IDLE;
  sessions[1].state = AGENT_STATE_DONE;
  sessions[1].unread = true;
  shot("readme-post", &in, 620, 240);
  animation_cleanup();
  sign_draw_cleanup();
  text_cleanup();
  return 0;
}
