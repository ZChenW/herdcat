#ifndef HERDCAT_TEXT_LINES_H
#define HERDCAT_TEXT_LINES_H
#include <stdbool.h>
#include <stddef.h>

// Control replies are built from whole lines. A line that does not fit is
// left out, never cut, and the text then ends by saying how many were.

// Appends formatted text at text + *used only if all of it fits.
__attribute__((format(printf, 4, 5))) bool
text_lines_add(char *text, size_t capacity, size_t *used, const char *format,
               ...);
// Ends text that left `left` lines out with "... N more <noun>s". Lines
// after `start` are dropped from the end, whole, until that line fits.
// Terminated lines end with a newline; otherwise newlines separate them.
void text_lines_more(char *text, size_t capacity, size_t *used, size_t start,
                     int left, const char *noun, bool terminated);
#endif
