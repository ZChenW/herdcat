// The hook client's incremental scanner: one payload from an agent, fed in
// pieces the way a pipe delivers it, then every question asked of the result.
// Byte 0 picks the adapter, byte 1 the piece size.
#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "fuzz.h"

static int fuzz_hook(const uint8_t *data, size_t size) {
  if (size < 2 || size > FUZZ_MAX_INPUT)
    return 0;
  const agent_adapter_t *adapter =
      agent_adapter_at(data[0] % agent_adapter_count());
  size_t piece = data[1] ? data[1] : size;
  data += 2;
  size -= 2;
  char *payload = fuzz_text(data, size);
  static agent_hook_scanner_t scanner;
  agent_hook_scan_init(&scanner);
  agent_hook_scan_adapter(&scanner, adapter);
  for (size_t at = 0; at < size; at += piece)
    agent_hook_scan_feed(&scanner, payload + at,
                         size - at < piece ? size - at : piece);
  (void)agent_hook_scan_finish(&scanner);
  agent_event_t event;
  bool metadata;
  char path[AGENT_TRANSCRIPT_PATH_MAX + 1], title[AGENT_TITLE_MAX + 1];
  char prompt[AGENT_TITLE_MAX + 1];
  (void)agent_hook_event(&scanner, &event);
  (void)agent_hook_event_override(&scanner, scanner.event, &event, &metadata);
  (void)agent_hook_key(adapter->name, &scanner);
  (void)agent_hook_transcript(&scanner, path);
  (void)agent_hook_title(&scanner, title);
  (void)agent_hook_prompt(&scanner, scanner.event, prompt);
  free(payload);
  return 0;
}
FUZZ_ENTRY(fuzz_hook)
