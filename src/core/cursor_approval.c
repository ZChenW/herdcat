#include "core/cursor_approval.h"

#include <string.h>

typedef struct {
  const char *question, *approve, *reject;
} prompt_t;

// Cursor 2026.10.19-6bbf2c4, components/prompt/decision-logic.ts.
static const prompt_t PROMPTS[] = {
    {"Run this command?",                     "Run (once) (y)",
     "Skip & tell the agent what to do instead (esc or n)"                                       },
    {"Run this command outside the sandbox?", "Run outside sandbox (once) (y)",
     "Skip & tell the agent what to do instead (esc or n)"                                       },
    {"Run this MCP tool?",                    "Run (once) (y)",                 "Skip (esc or n)"},
    {"Write to this file?",                   "Proceed (y)",
     "Reject & propose changes (esc or n or p)"                                                  },
    {"Delete this file?",                     "Delete (y)",                     "Keep (n)"       },
    {"Read this file?",                       "Allow once (y)",                 "Skip (esc or n)"},
    {"Allow this web search?",                "Allow search (y)",               "Skip (esc or n)"},
    {"Allow this web fetch?",                 "Fetch (y)",                      "Skip (esc or n)"},
    {"Proceed with this edit?",               "Proceed (y)",
     "Reject & propose changes (esc or n or p)"                                                  },
};

typedef struct {
  int question;
  unsigned approves, rejects;
  bool selected;
} screen_line_t;

static bool ends_with(const char *text, const char *suffix) {
  size_t n = strlen(text), m = strlen(suffix);
  return n >= m && !strcmp(text + n - m, suffix);
}

static screen_line_t classify(char *line) {
  screen_line_t result = {.question = -1};
  bool arrow = false;
  if (!strncmp(line, "→ ", strlen("→ "))) {
    arrow = true;
    line += strlen("→ ");
  }
  for (size_t i = 0; i < sizeof(PROMPTS) / sizeof(PROMPTS[0]); i++) {
    if (!strcmp(line, PROMPTS[i].question))
      result.question = (int)i;
    if (!strcmp(line, PROMPTS[i].approve))
      result.approves |= 1U << i;
    if (!strcmp(line, PROMPTS[i].reject))
      result.rejects |= 1U << i;
  }
  // Computer-use MCP tools substitute an application name in this option.
  if (!strncmp(line, "Allow ", 6) && (ends_with(line, " once (y)") ||
                                      ends_with(line, " for this session (y)")))
    result.approves |= 1U << 2;
  result.selected =
      arrow && (result.approves || result.rejects ||
                ends_with(line, " (tab)") || ends_with(line, " (shift+tab)"));
  return result;
}

typedef enum {
  TEXT,
  ESCAPE,
  CSI,
  STRING,
  STRING_ESCAPE
} escape_t;

static bool escaped(unsigned char c, escape_t *state) {
  switch (*state) {
  case TEXT:
    if (c != 0x1b)
      return false;
    *state = ESCAPE;
    break;
  case ESCAPE:
    if (c == '[')
      *state = CSI;
    else if (c == ']' || c == 'P' || c == '^' || c == '_')
      *state = STRING;
    else if (c < 0x20 || c > 0x2f)
      *state = TEXT;
    break;
  case CSI:
    if (c >= 0x40 && c <= 0x7e)
      *state = TEXT;
    break;
  case STRING:
    if (c == 0x07)
      *state = TEXT;
    else if (c == 0x1b)
      *state = STRING_ESCAPE;
    break;
  case STRING_ESCAPE:
    *state = c == '\\' || c == 0x07 ? TEXT : STRING;
    break;
  }
  return true;
}

static void remember_line(screen_line_t lines[20], size_t *count,
                          size_t *blank_rows, char *line, bool overflow) {
  if (!line[0] && !overflow) {
    if (*blank_rows < 20)
      (*blank_rows)++;
    return;
  }
  // Defer blank rows until more content follows. Interior blanks count
  // towards the window, but bottom padding cannot evict the approval menu.
  while (*blank_rows) {
    lines[(*count)++ % 20] = (screen_line_t){.question = -1};
    (*blank_rows)--;
  }
  lines[(*count)++ % 20] =
      overflow ? (screen_line_t){.question = -1} : classify(line);
}

bool cursor_approval_screen(const char *text, size_t length) {
  if (!text || !length || length > 65536 || memchr(text, 0, length))
    return false;
  screen_line_t lines[20];
  char line[512];
  size_t used = 0, count = 0, blank_rows = 0;
  bool overflow = false;
  escape_t state = TEXT;
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)text[i];
    if (escaped(c, &state))
      continue;
    if (c == '\r') {
      // Kitty --add-wrap-markers preserves physical screen rows with CR.
      c = '\n';
      if (i + 1 < length && text[i + 1] == '\n')
        i++;
    }
    if (c != '\n') {
      if (c == ' ' || c == '\t') {
        if (!used || line[used - 1] == ' ')
          continue;
        c = ' ';
      }
      if (used < sizeof(line) - 1)
        line[used++] = (char)c;
      else
        overflow = true;
    }
    if (c == '\n' || i == length - 1) {
      if (used && line[used - 1] == ' ')
        used--;
      line[used] = 0;
      remember_line(lines, &count, &blank_rows, line, overflow);
      used = 0;
      overflow = false;
    }
  }
  // The final visible row may end in SGR/OSC rather than a newline or text.
  if (used || overflow) {
    if (used && line[used - 1] == ' ')
      used--;
    line[used] = 0;
    remember_line(lines, &count, &blank_rows, line, overflow);
  }
  unsigned mask = 0;
  bool approve = false, reject = false, selected = false;
  for (size_t i = count > 20 ? count - 20 : 0; i < count; i++) {
    const screen_line_t *row = &lines[i % 20];
    if (row->question >= 0) {
      mask = 1U << row->question;
      approve = reject = selected = false;
    }
    approve |= (row->approves & mask) != 0;
    reject |= (row->rejects & mask) != 0;
    selected |= row->selected;
  }
  return approve && reject && selected;
}
