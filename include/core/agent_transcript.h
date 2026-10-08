#ifndef AGENT_TRANSCRIPT_H
#define AGENT_TRANSCRIPT_H

#include <stdbool.h>
#include <stddef.h>

#define AGENT_TRANSCRIPT_PATH_MAX 1024
#define AGENT_TRANSCRIPT_LINE_MAX 4096

// Match only a complete bounded log line for the supplied conversation.
bool agent_transcript_agy_cancelled(const char *id, const char *line,
                                    size_t length);

// Structured, bounded JSONL recognition; never logs or retains content.
bool agent_transcript_interrupted(const char *agent, const char *line,
                                  size_t length);
// True when that line reports a turn that ended on an error, not a cancel.
bool agent_transcript_failed(const char *agent, const char *line,
                             size_t length);

#endif
