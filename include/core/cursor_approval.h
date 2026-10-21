#ifndef HERDCAT_CURSOR_APPROVAL_H
#define HERDCAT_CURSOR_APPROVAL_H

#include <stdbool.h>
#include <stddef.h>

// Strip terminal escapes and trailing blank rows, then inspect the last 20
// visible rows. Interior blank rows still count towards the window.
bool cursor_approval_screen(const char *text, size_t length);

#endif
