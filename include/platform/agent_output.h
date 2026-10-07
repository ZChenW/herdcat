#ifndef AGENT_OUTPUT_H
#define AGENT_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

// Only stdout on /dev/pts/<number> qualifies. Read no terminal contents.
bool agent_output_wchar(pid_t pid, uint64_t *wchar);

#endif
