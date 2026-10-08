#include "graphics/signs.h"
#include "test_helpers.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
static double top(const sign_frame_t *f) {
  double y = 1000;
  for (int i = 0; i < f->shape_count; i++) {
    const sign_shape_t *s = &f->shapes[i];
    double r = s->rotation * acos(-1) / 180,
           cx = s->orbit ? s->origin_x : s->x + s->w / 2,
           cy = s->orbit ? s->origin_y : s->y + s->h / 2;
    for (int j = 0; j < 4; j++) {
      double x = s->x + (j & 1 ? s->w : 0) - cx,
             dy = s->y + (j & 2 ? s->h : 0) - cy;
      y = fmin(y, cy + x * sin(r) + dy * cos(r));
    }
  }
  for (int i = 0; i < f->text_count; i++) {
    const sign_text_t *t = &f->texts[i];
    double h = sign_tag_height(t), scale = t->tag_scale;
    double ty =
        t->tag_scale > 0 ? t->anchor_y - h / 2 - h * scale / 2 : t->line_top;
    y = fmin(y, ty);
  }
  return y;
}
int main(void) {
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
    for (int cap = 1; cap <= 10; cap++) {
      double reach = 0;
      for (int n = 1; n <= cap; n++)
        for (int state = 0; state < AGENT_STATE_COUNT; state++)
          for (int hover = -1; hover < n; hover++)
            for (int two = 0; two < 2; two++) {
              agent_session_view_t ss[10] = {0};
              for (int i = 0; i < n; i++) {
                ss[i].key = i + 1;
                ss[i].order = i + 1;
                ss[i].state = state;
                ss[i].unread = true;
                ss[i].child_count = 3;
                strcpy(ss[i].agent, i % 2 ? "codex" : "claude");
                strcpy(ss[i].name, "repository");
                strcpy(ss[i].title, "secondary");
              }
              sign_input_t in = {.style = style,
                                 .sessions = ss,
                                 .count = n,
                                 .cat_height = 110,
                                 .cat_y = 500,
                                 .cat_x = 500,
                                 .idle = SIGN_IDLE_ALWAYS,
                                 .animations = SIGN_ANIM_FULL,
                                 .open = true,
                                 .has_hover = hover >= 0,
                                 .hover_key = hover + 1,
                                 .font_size = 13};
              if (two)
                strcpy(in.nameplate, "**{name}**\\n{state}");
              signs_t model = {0};
              sign_frame_t f;
              for (int t = 0; t <= 1600; t += 4) {
                in.now_ms = t;
                signs_frame(&model, &in, &f);
                reach = fmax(reach, 500 - top(&f));
              }
            }
      printf("style=%d cap=%d reach=%.3f threshold=%.0f\n", style, cap, reach,
             ceil(reach + 8));
      TEST_ASSERT(reach + 8 <= sign_reach((sign_style_t)style, 110, cap));
    }
}
