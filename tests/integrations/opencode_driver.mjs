import readline from "node:readline";
import plugin from "../../integrations/opencode/index.js";
const sessions = new Map();
const lines = readline.createInterface({ input: process.stdin });
const stream = async function* () {
  for await (const line of lines) {
    const { event, info } = JSON.parse(line);
    if (info) sessions.set(event.data.sessionID, info);
    yield event;
  }
};
await plugin.setup({
  location: { directory: "/tmp/bridge-test" },
  event: { subscribe: () => stream() },
  session: { get: async ({ sessionID }) => {
    if (!sessions.has(sessionID)) throw new Error("Unavailable");
    return sessions.get(sessionID);
  } },
});
console.log("ready");
