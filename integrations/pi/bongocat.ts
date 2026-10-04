/*
Adapted from OpenPets packages/pi/src/runtime.ts event registration.
https://github.com/OpenPetsHQ/openpets

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

// Fixed-size dispatch queue preserves lifecycle ordering without blocking Pi.
export default function bongocat(pi) {
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
      const child = spawn("bongocat", ["--hook", "pi", "--event", item.event], {
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
  for (const name of ["session_start", "before_agent_start", "agent_start",
                     "tool_call", "tool_result", "agent_end", "session_shutdown"]) {
    pi.on(name, (event, ctx) => {
      try {
        const session_id = ctx.sessionManager.getSessionId();
        if (typeof session_id !== "string" || !session_id || session_id.length > 127)
          return;
        // Forked/child sessions carry a parent in the session header.
        if (ctx.sessionManager.getHeader?.()?.parentSession) return;
        const payload = { session_id, cwd: ctx.cwd, agent_pid: process.pid };
        if (name === "agent_end") {
          // agent_end wraps messages; it has no top-level stopReason in Pi.
          const last = event.messages?.findLast(message => message.role === "assistant");
          payload.stopReason = last?.stopReason;
        }
        if (queue.length >= 128) return;
        queue.push({ event: name, payload });
        pump();
      } catch { /* Never affect the agent or print conversation data. */ }
    });
  }
}
