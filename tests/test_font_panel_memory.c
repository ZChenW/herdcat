#define _GNU_SOURCE
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdouble-promotion"
#  pragma GCC diagnostic ignored "-Wmissing-prototypes"
#  pragma GCC diagnostic ignored "-Wstrict-prototypes"
#  pragma GCC diagnostic ignored "-Wold-style-definition"
#  pragma GCC diagnostic ignored "-Wshadow"
#endif
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg.h>
#include <nanosvgrast.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

#include "graphics/font_panel.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "test_helpers.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef STAGE14_BASELINE
#  define text_preview_begin() ((void)0)
#  define text_preview_end()   ((void)0)
#endif

#define W 700
#define H 440
static font_panel_t panel;
static char chosen[128] = "Noto Sans";
static uint8_t canvas[W * H * 4], before[W * H * 4];
typedef struct {
  size_t allocated, arena, heap_kb, heap_size_kb;
  int font_maps;
} memory_t;

static memory_t memory(const char *phase) {
  struct mallinfo2 info = mallinfo2();
  memory_t out = {.allocated = info.uordblks + info.hblkhd,
                  .arena = info.arena + info.hblkhd};
  FILE *file = fopen("/proc/self/smaps", "r");
  TEST_ASSERT(file);
  char line[1024];
  bool heap = false;
  static char mapped[128][1024];
  int mapped_count = 0;
  while (fgets(line, sizeof(line), file)) {
    unsigned long start, end;
    if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
      heap = strstr(line, "[heap]") != NULL;
      char *path = strchr(line, '/');
      if (path && (strstr(path, ".ttf") || strstr(path, ".otf") ||
                   strstr(path, ".ttc"))) {
        bool known = false;
        for (int i = 0; i < mapped_count; i++)
          if (!strcmp(mapped[i], path))
            known = true;
        if (!known) {
          TEST_ASSERT(mapped_count < 128);
          snprintf(mapped[mapped_count++], sizeof(mapped[0]), "%s", path);
        }
      }
    } else if (heap) {
      size_t size;
      if (sscanf(line, "Pss: %zu kB", &size) == 1)
        out.heap_kb += size;
      if (sscanf(line, "Size: %zu kB", &size) == 1)
        out.heap_size_kb += size;
    }
  }
  out.font_maps = mapped_count;
  TEST_ASSERT(out.font_maps > 0);
  TEST_ASSERT(fclose(file) == 0);
  printf("%s allocated=%zu arena=%zu heap_pss_kb=%zu heap_size_kb=%zu "
         "font_maps=%d glyphs=%d\n",
         phase, out.allocated, out.arena, out.heap_kb, out.heap_size_kb,
         out.font_maps, text_glyph_count());
  fflush(stdout);
  return out;
}
static void scene_pixels(sign_style_t style, sign_theme_t theme, int scale) {
  memset(canvas, 0, sizeof(canvas));
  text_set_scale(scale);
  signs_t model = {0};
  agent_session_view_t session = {
      .key = 1, .order = 1, .state = AGENT_STATE_WAITING};
  snprintf(session.agent, sizeof(session.agent), "claude");
  snprintf(session.name, sizeof(session.name), "herdcat 测量");
  sign_input_t input = {.sessions = &session,
                        .count = 1,
                        .style = style,
                        .theme = theme,
                        .animations = SIGN_ANIM_OFF,
                        .idle = SIGN_IDLE_ALWAYS,
                        .open = true,
                        .cat_x = 210,
                        .cat_y = 210,
                        .cat_height = 110,
                        .now_ms = 100000};
  sign_frame_t frame;
  signs_frame(&model, &input, &frame);
  sign_draw(canvas, W, H, scale, &frame, SIGN_DRAW_UNDER);
  sign_draw(canvas, W, H, scale, &frame, SIGN_DRAW_OVER);
}
static void sign_pixels(void) {
  scene_pixels(SIGN_STYLE_POST, SIGN_THEME_LIGHT, 120);
}
static void list_faces(void) {
  const char *names[FONT_PANEL_CAP];
  font_panel_face_t listed[FONT_PANEL_CAP];
  int count = text_families("en", names, FONT_PANEL_CAP);
  TEST_ASSERT(count > 0);
  if (count > FONT_PANEL_CAP)
    count = FONT_PANEL_CAP;
  for (int i = 0; i < count; i++) {
    snprintf(listed[i].name, sizeof(listed[i].name), "%s", names[i]);
    listed[i].mono = text_spacing_mono(text_family_spacing(names[i]));
  }
  font_panel_set_faces(&panel, listed, count);
}
static double elapsed_ms(struct timespec begin, struct timespec end) {
  return (double)(end.tv_sec - begin.tv_sec) * 1000 +
         (double)(end.tv_nsec - begin.tv_nsec) / 1000000;
}
static void browse(int64_t now) {
  struct timespec begin, end;
  TEST_ASSERT(clock_gettime(CLOCK_MONOTONIC, &begin) == 0);
  text_set_scale(120);
  font_panel_reset(&panel);
  list_faces();
  font_panel_set_prepared(&panel, 0);
  font_panel_set_real_preview(&panel, false);
  font_panel_open(&panel, true, 1, SIGN_ANIM_OFF, chosen, now);
  memset(canvas, 0, sizeof(canvas));
  font_panel_draw(&panel, canvas, W, H, 120);
  TEST_ASSERT(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
  printf("first_paint now=%lld ms=%.3f\n", (long long)now,
         elapsed_ms(begin, end));
  font_panel_set_real_preview(&panel, true);
  font_panel_set_prepared(&panel, -1);
  for (int page = 0; page < FONT_PANEL_CAP; page++) {
    font_panel_layout_t layout;
    font_panel_layout(&panel, &layout);
    for (int cell = 0; cell < layout.cell_count; cell++) {
      font_panel_pointer(&panel, layout.cells[cell].x + 8,
                         layout.cells[cell].y + 8, now + page);
      memset(canvas, 0, sizeof(canvas));
      font_panel_draw(&panel, canvas, W, H, 120);
    }
    int row = panel.first_row;
    font_panel_wheel(&panel, 8, now + page);
    if (panel.first_row == row)
      break;
  }
}
int main(int argc, char **argv) {
  bool measure = argc == 2 && !strcmp(argv[1], "--measure");
  TEST_ASSERT(argc == 1 || measure);
  TEST_ASSERT(text_init("Noto Sans") == 0);
  sign_pixels();
  memcpy(before, canvas, sizeof(before));
  memory_t baseline = memory("before");
  list_faces();
  memory("listed");
  browse(0);
  memory("open");
  font_panel_close(&panel);
  memory_t closed = memory("closed");
  sign_pixels();
  TEST_ASSERT(!memcmp(before, canvas, sizeof(before)));
  // mallinfo2 live allocations are the heap acceptance metric. smaps also
  // records allocator fragmentation, which trim cannot always unmap.
  if (!measure) {
    TEST_ASSERT(closed.allocated <= baseline.allocated * 3 / 2 + 131072);
    TEST_ASSERT(closed.font_maps <= baseline.font_maps);
  }
  size_t previous = closed.allocated;
  bool all_increased = true;
  for (int cycle = 1; cycle < 20; cycle++) {
    browse(cycle * 20000);
    if (cycle == 19)
      font_panel_step(&panel, panel.idle_at);
    else
      font_panel_close(&panel);
    TEST_ASSERT(!font_panel_is_open(&panel));
    char phase[32];
    snprintf(phase, sizeof(phase), "closed-%02d", cycle + 1);
    memory_t next = memory(phase);
    if (next.allocated <= previous)
      all_increased = false;
    previous = next.allocated;
    sign_pixels();
    TEST_ASSERT(!memcmp(before, canvas, sizeof(before)));
    if (!measure) {
      TEST_ASSERT(next.allocated <= baseline.allocated * 3 / 2 + 131072);
      TEST_ASSERT(next.font_maps <= baseline.font_maps);
    }
  }
  if (!measure)
    TEST_ASSERT(!all_increased);
  const char *names[2];
  TEST_ASSERT(text_families("en", names, 2) >= 2);
  char tried[128];
  snprintf(tried, sizeof(tried), "%s", names[0]);
  // A closed panel can still have a hover family installed until the menu
  // callback restores it. Its original CJK fallback must survive that gap.
  browse(400000);
  text_preview_begin();
  TEST_ASSERT(text_set_family(tried) == 0);
  sign_pixels();
  font_panel_close(&panel);
  TEST_ASSERT(text_set_family("Noto Sans") == 0);
  text_preview_end();
  int matches = text_match_count();
  (void)matches;
  sign_pixels();
  TEST_ASSERT(!memcmp(before, canvas, sizeof(before)));
#ifndef STAGE14_BASELINE
  TEST_ASSERT(text_match_count() == matches);
#endif
  memory_t restored = memory("hover-restored");
  if (!measure)
    TEST_ASSERT(restored.font_maps <= baseline.font_maps);
  // Accept a different family, then keep its own fallback set after closing.
  snprintf(chosen, sizeof(chosen), "%s", tried);
  TEST_ASSERT(text_set_family(tried) == 0);
  sign_pixels();
  memcpy(before, canvas, sizeof(before));
  memory_t selected = memory("selected-before");
  browse(420000);
  text_preview_begin();
  TEST_ASSERT(text_set_family("Noto Sans") == 0);
  sign_pixels();
  font_panel_close(&panel);
  TEST_ASSERT(text_set_family(tried) == 0);
  text_preview_end();
  sign_pixels();
  TEST_ASSERT(!memcmp(before, canvas, sizeof(before)));
  memory_t accepted = memory("selected-closed");
  if (!measure) {
    TEST_ASSERT(accepted.font_maps <= selected.font_maps);
    TEST_ASSERT(accepted.allocated <= selected.allocated * 3 / 2 + 131072);
  }
  // Compare complete plates, including their text, at both themes/styles
  // and integer/fractional buffer scales. Freeze each source scene first.
  static uint8_t references[8][sizeof(canvas)];
  for (int i = 0; i < 8; i++) {
    scene_pixels(i & 1 ? SIGN_STYLE_FAN : SIGN_STYLE_POST,
                 i & 2 ? SIGN_THEME_DARK : SIGN_THEME_LIGHT, i & 4 ? 180 : 120);
    memcpy(references[i], canvas, sizeof(canvas));
  }
  browse(440000);
  font_panel_close(&panel);
  for (int i = 0; i < 8; i++) {
    scene_pixels(i & 1 ? SIGN_STYLE_FAN : SIGN_STYLE_POST,
                 i & 2 ? SIGN_THEME_DARK : SIGN_THEME_LIGHT, i & 4 ? 180 : 120);
    TEST_ASSERT(!memcmp(references[i], canvas, sizeof(canvas)));
  }
  printf("pixel_scenes=8 unchanged; explicit/idle/hover/selection checked\n");
  sign_draw_cleanup();
  text_cleanup();
  return 0;
}
