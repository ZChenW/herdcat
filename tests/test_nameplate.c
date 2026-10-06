#include "config/nameplate.h"
#include "graphics/nameplate_layout.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "signs_nanosvg.h"
#include "test_helpers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void invalid(const char *source, const char *reason, size_t where) {
  size_t at;
  const char *error = nameplate_validate(source, &at);
  TEST_ASSERT(error && strstr(error, reason) && at == where);
}
static void parser(void) {
  size_t at;
  TEST_ASSERT(
      !nameplate_validate("**{name}**  {agent} · {state}\\n{title}", &at));
  invalid("**{name}", "bold", 0);
  invalid("abc {unknown}", "unknown", 4);
  invalid("{name", "unclosed", 0);
  invalid("abc}", "closing", 3);
  invalid("a\\nb\\nc", "two lines", 4);
  invalid("a\nb", "control", 1);
  char long_text[162];
  memset(long_text, 'a', 161);
  long_text[161] = 0;
  invalid(long_text, "160", 160);
  nameplate_fields_t f = {"repo", "repo", "title", "Claude", "Waiting"};
  nameplate_t a, b;
  for (int i = 0; i < 5; i++) {
    nameplate_expand(nameplate_builtin((sign_name_extra_t)i, false), &f, &a);
    char custom[161];
    snprintf(custom, sizeof(custom), "%s",
             nameplate_builtin((sign_name_extra_t)i, false));
    nameplate_expand(custom, &f, &b);
    TEST_ASSERT(!memcmp(&a, &b, sizeof(a)));
    TEST_ASSERT(a.lines == (i >= 3 ? 2 : 1));
  }
  f.title = "";
  nameplate_expand("**{name}** · {title} · {agent}", &f, &a);
  TEST_ASSERT(!strcmp(a.text, "repo · Claude"));
  nameplate_expand("{title} · {name}\\n{title}", &f, &a);
  TEST_ASSERT(!strcmp(a.text, "repo") && a.lines == 1 && a.runs[0].line == 0);
  nameplate_expand("{title}\\n**{name}**", &f, &a);
  TEST_ASSERT(a.lines == 1 && a.runs[0].line == 0);
  nameplate_expand(nameplate_builtin(SIGN_EXTRA_INLINE, false), &f, &a);
  TEST_ASSERT(!strcmp(a.text, "repo  Claude · Waiting"));
  f.title = "repo";
  nameplate_expand("**{name}** · {title}", &f, &a);
  TEST_ASSERT(!strcmp(a.text, "repo"));
  f.agent = "";
  f.title = "title";
  nameplate_expand("{agent} · {title}", &f, &a);
  TEST_ASSERT(!strcmp(a.text, "title"));
  nameplate_expand("{title} · {agent}", &f, &a);
  TEST_ASSERT(!strcmp(a.text, "title"));
}
static sign_frame_t scene(int extra, bool below, const char *custom) {
  agent_session_view_t s = {.key = 1,
                            .order = 1,
                            .agent = "claude",
                            .name = "repo",
                            .title = "Session title",
                            .state = AGENT_STATE_WAITING};
  sign_input_t in = {.sessions = &s,
                     .count = 1,
                     .style = SIGN_STYLE_FAN,
                     .animations = SIGN_ANIM_OFF,
                     .idle = SIGN_IDLE_ALWAYS,
                     .name = SIGN_NAME_PROJECT,
                     .name_extra = (sign_name_extra_t)extra,
                     .title_length = 16,
                     .cat_x = 250,
                     .cat_y = 300,
                     .cat_height = 110,
                     .has_hover = true,
                     .hover_key = 1,
                     .open = true,
                     .orientation = below ? SIGN_BELOW : SIGN_ABOVE};
  if (custom)
    snprintf(in.nameplate, sizeof(in.nameplate), "%s", custom);
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  return frame;
}
static void layout(void) {
  TEST_ASSERT(text_init("DejaVu Sans") == 0);
  sign_frame_t off = scene(0, false, NULL);
  double height = sign_tag_height(&off.texts[0]);
  for (int e = 0; e < 5; e++) {
    sign_frame_t a = scene(e, false, NULL),
                 b = scene(e, false,
                           nameplate_builtin((sign_name_extra_t)e, false));
    TEST_ASSERT(!memcmp(&a, &b, sizeof(a)));
    sign_frame_t down = scene(e, true, NULL);
    TEST_ASSERT(a.text_count == 1 && down.text_count == 1);
    TEST_ASSERT(sign_tag_height(&a.texts[0]) ==
                height + (e >= 3 ? a.texts[0].meta_px * 1.2 : 0));
    TEST_ASSERT(fabs(down.texts[0].anchor_y - (710 - a.texts[0].anchor_y +
                                               sign_tag_height(&a.texts[0]))) <
                .001);
    sign_frame_t copy = a;
    signs_reflect(&copy, 355);
    signs_reflect(&copy, 355);
    TEST_ASSERT(fabs(copy.texts[0].anchor_y - a.texts[0].anchor_y) < .001);
    TEST_ASSERT(copy.bounds_y == a.bounds_y && copy.bounds_h == a.bounds_h);
    if (e >= 3) {
      const nameplate_t *m = &a.texts[0].nameplate;
      TEST_ASSERT(m->lines == 2 && m->runs[0].line == 0);
      TEST_ASSERT(m->runs[0].bold == (e == 4));
    }
    uint8_t p[640 * 800 * 4] = {0}, q[640 * 800 * 4] = {0};
    sign_draw(p, 640, 800, 120, &a, SIGN_DRAW_OVER);
    sign_draw(q, 640, 800, 120, &b, SIGN_DRAW_OVER);
    TEST_ASSERT(!memcmp(p, q, sizeof(p)));
  }
  for (int scale = 120; scale <= 180; scale += 30) {
    sign_frame_t legacy = off;
    memset(&legacy.texts[0].nameplate, 0, sizeof(legacy.texts[0].nameplate));
    legacy.texts[0].templated = false;
    uint8_t *p = calloc(960 * 960, 4), *q = calloc(960 * 960, 4);
    TEST_ASSERT(p && q);
    sign_draw(p, 960, 960, scale, &off, SIGN_DRAW_OVER);
    sign_draw(q, 960, 960, scale, &legacy, SIGN_DRAW_OVER);
    TEST_ASSERT(!memcmp(p, q, 960 * 960 * 4));
    free(p);
    free(q);
  }
  sign_frame_t empty = scene(0, false, "{name} · {title}");
  TEST_ASSERT(empty.texts[0].nameplate.lines == 1);
  sign_frame_t same = scene(0, false, "{project}");
  TEST_ASSERT(!strcmp(same.texts[0].nameplate.text, "repo"));
  sign_frame_t blank = scene(0, false, "{name} · {title}");
  nameplate_fields_t no_title = {"repo", "repo", "", "Claude", "Waiting"};
  nameplate_expand("{title}\\n{title}", &no_title, &blank.texts[0].nameplate);
  nameplate_layout_t nothing;
  nameplate_layout(&blank.texts[0], 1, &nothing);
  TEST_ASSERT(!nothing.lines && !nothing.count && !nothing.width);
  // Exercise priority: secondary runs truncate before the bold name.
  sign_text_t t = off.texts[0];
  t.max_width = 150;
  t.surface_width = 150;
  nameplate_layout_t fitted;
  nameplate_layout(&t, 1, &fitted);
  TEST_ASSERT(fitted.width <= 126 && !strcmp(fitted.runs[0].text, "repo"));
  TEST_ASSERT(strstr(fitted.runs[2].text, "…") || !fitted.runs[2].text[0]);
  t.max_width = 35;
  nameplate_layout(&t, 1, &fitted);
  TEST_ASSERT(fitted.width <= 11);
  // Advanced runs retain state emphasis and bold primary spans.
  sign_frame_t custom = scene(0, false, "{title} · **{project}** · {state}");
  TEST_ASSERT(custom.texts[0].nameplate.count >= 5);
  TEST_ASSERT(custom.texts[0].nameplate.runs[2].bold);
  TEST_ASSERT(custom.texts[0].nameplate.runs[4].state);
  // A removed trailing segment takes its separator with it.
  sign_text_t trailing = custom.texts[0];
  nameplate_layout(&trailing, 1, &fitted);
  double prefix =
      fitted.runs[0].width + fitted.runs[1].width + fitted.runs[2].width;
  trailing.max_width = prefix + 24;
  nameplate_layout(&trailing, 1, &fitted);
  TEST_ASSERT(!fitted.runs[3].text[0] && !fitted.runs[4].text[0]);
  TEST_ASSERT(!strcmp(fitted.runs[2].text, "repo"));
  sign_frame_t above = scene(SIGN_EXTRA_ABOVE, false, NULL);
  nameplate_layout(&above.texts[0], 1, &fitted);
  TEST_ASSERT(fitted.runs[1].bold &&
              (double)fitted.runs[1].px == above.texts[0].px);
  for (int below = 0; below < 2; below++) {
    agent_session_view_t s = {.key = 1,
                              .name = "repo",
                              .title = "Title",
                              .agent = "claude",
                              .state = AGENT_STATE_WAITING};
    sign_input_t in = {.sessions = &s,
                       .count = 1,
                       .style = SIGN_STYLE_FAN,
                       .name = SIGN_NAME_PROJECT,
                       .name_extra = SIGN_EXTRA_BELOW,
                       .animations = SIGN_ANIM_OFF,
                       .cat_x = 220,
                       .cat_y = 200,
                       .cat_height = 110,
                       .surface_height = 620,
                       .surface_width = 100,
                       .orientation = below ? SIGN_BELOW : SIGN_ABOVE,
                       .has_hover = true,
                       .hover_key = 1};
    signs_t model = {0};
    sign_frame_t frame;
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(frame.texts[0].nameplate.lines == 2);
    TEST_ASSERT(frame.texts[0].surface_width == 100);
    uint8_t pixels[100 * 620 * 4] = {0};
    sign_draw(pixels, 100, 620, 120, &frame, SIGN_DRAW_OVER);
    bool ink = false;
    for (size_t i = 3; i < sizeof(pixels); i += 4)
      ink |= pixels[i] != 0;
    TEST_ASSERT(ink);
    in.cat_y = below ? 20 : 90;
    in.surface_height = below ? 220 : 300;
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(frame.texts[0].nameplate.lines == 1);
  }
  text_cleanup();
  sign_draw_cleanup();
}
int main(void) {
  parser();
  layout();
  puts("templates, empty segments, layouts, reflection, fit and pixels passed");
  return 0;
}
