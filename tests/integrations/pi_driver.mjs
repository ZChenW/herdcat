import readline from "node:readline";
import extension from "../../integrations/pi/herdcat.ts";
const handlers = new Map();
extension({ on: (name, handler) => handlers.set(name, handler) });
const ctx = {
  cwd: "/tmp/bridge-test",
  sessionManager: {
    getSessionId: () => "bridge-test",
    getHeader: () => ({}),
  },
};
console.log(process.pid);
for await (const line of readline.createInterface({ input: process.stdin })) {
  const { name, event = {}, child = false } = JSON.parse(line);
  ctx.sessionManager.getHeader = () => child ? { parentSession: "parent" } : {};
  handlers.get(name)?.(event, ctx);
}
