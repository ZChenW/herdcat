/*
OpenPets event-bridge reference: packages/opencode/src/opencode-plugin-runtime.ts
https://github.com/OpenPetsHQ/openpets
Rewritten for the observed opencode 2.x API (the reference uses v1 hooks).

MIT License

Copyright (c) 2026 OpenPets

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
import { spawn } from "node:child_process";

export default {
  id: "bongocat.sessions",
  async setup(ctx) {
    const directory = ctx.location?.directory;
    if (typeof directory !== "string") return;
    const sessions = new Map();
    const queue = [];
    let busy = false;
    const pump = () => {
      if (busy || !queue.length) return;
      const item = queue.shift();
      busy = true;
      let finished = false;
      const finish = () => {
        if (finished) return;
        finished = true;
        busy = false;
        pump();
      };
      try {
        const child = spawn("bongocat", ["--hook", "opencode", "--event", item.event], {
          stdio: ["pipe", "ignore", "ignore"],
        });
        child.on("error", finish);
        child.on("close", finish);
        child.stdin.on("error", () => {});
        child.stdin.end(JSON.stringify(item.payload));
        child.unref();
        child.stdin.unref?.();
      } catch { finish(); }
    };
    const events = new Set([
      "session.created", "session.inbox.enqueued", "session.execution.started",
      "session.tool.called", "session.tool.success", "permission.asked",
      "permission.replied", "session.execution.succeeded",
      "session.execution.interrupted", "session.execution.failed", "session.deleted",
    ]);
    try {
      const subscription = ctx.event.subscribe();
      const consume = async () => {
        for await (const event of subscription) {
          try {
            if (event.type === "location.shutdown" &&
                event.location?.directory === directory) break;
            if (!events.has(event.type)) continue;
            const session_id = event.data?.sessionID;
            if (typeof session_id !== "string" || !session_id || session_id.length > 127)
              continue;
            if (!sessions.has(session_id)) {
              // Resolve canonical directory and parent metadata once; never poll. On a
              // failed lookup do not risk showing a child as a separate sign.
              if (event.type === "session.deleted") continue;
              const info = await ctx.session.get({ sessionID: session_id });
              const cwd = info?.location?.directory;
              if (sessions.size >= 128) sessions.delete(sessions.keys().next().value);
              sessions.set(session_id, info?.parentID || cwd !== directory ? null : cwd);
            }
            const cwd = sessions.get(session_id);
            if (event.type === "session.deleted") sessions.delete(session_id);
            if (!cwd || queue.length >= 128) continue;
            // process.pid belongs to the background service, not the terminal.
            queue.push({ event: event.type, payload: { session_id, cwd } });
            pump();
          } catch { /* Format changes and inaccessible sessions are ignored. */ }
        }
        sessions.clear();
      };
      void consume().catch(() => {});
    } catch { /* Unsupported plugin APIs must not break the service. */ }
  },
};
