#include "utils/text_lines.h"

#include <stdarg.h>
#include <stdio.h>

bool text_lines_add(char *text, size_t capacity, size_t *used,
                    const char *format, ...) {
  if (*used >= capacity) {
    return false;
  }
  va_list args;
  va_start(args, format);
  int n = vsnprintf(text + *used, capacity - *used, format, args);
  va_end(args);
  if (n < 0 || (size_t)n >= capacity - *used) {
    text[*used] = '\0';
    return false;
  }
  *used += (size_t)n;
  return true;
}

void text_lines_more(char *text, size_t capacity, size_t *used, size_t start,
                     int left, const char *noun, bool terminated) {
  while (left > 0 && *used < capacity) {
    // Separated text needs a newline before the count.
    bool open = *used > 0 && text[*used - 1] != '\n';
    if (text_lines_add(text, capacity, used, "%s... %d more %s%s%s",
                       open ? "\n" : "", left, noun, left == 1 ? "" : "s",
                       terminated ? "\n" : "")) {
      return;
    }
    if (*used <= start) {
      return;
    }
    // Drop the last line; the newline that ended the one before stays.
    size_t end = *used - 1;
    while (end > start && text[end - 1] != '\n') {
      end--;
    }
    *used = end;
    text[end] = '\0';
    left++;
  }
}
