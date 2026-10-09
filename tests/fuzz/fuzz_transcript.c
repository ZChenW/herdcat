// One line of a session record or of an agent's own log, read as each
// agent in turn: titles, first prompts, cancelled and failed turns, the
// Antigravity log lines, and Copilot's workspace.yaml.
#include "core/agent_adapters.h"
#include "core/agent_title.h"
#include "core/agent_transcript.h"
#include "fuzz.h"

static int fuzz_transcript(const uint8_t *data, size_t size) {
  static const char id[] = "cdb7f3b6-794a-4429-b3b0-f293d8cfa90a";
  if (size > FUZZ_MAX_INPUT)
    return 0;
  char *line = fuzz_text(data, size);
  char out[AGENT_TITLE_MAX + 1];
  for (size_t i = 0; i < agent_adapter_count(); i++) {
    const char *agent = agent_adapter_at(i)->name;
    (void)agent_title_line(agent, id, line, size, out);
    (void)agent_prompt_line(agent, line, size, out);
    (void)agent_transcript_interrupted(agent, line, size);
    (void)agent_transcript_failed(agent, line, size);
  }
  (void)agent_transcript_agy_cancelled(id, line, size);
  (void)agent_transcript_agy_confirmation(id, line, size);
  (void)agent_transcript_agy_confirmation("", line, size);
  (void)agent_title_copilot_yaml(line, size, out);
  json_span_t root;
  if (json_document(line, size, &root))
    (void)agent_title_scalar(root, out, sizeof(out));
  free(line);
  return 0;
}
FUZZ_ENTRY(fuzz_transcript)
