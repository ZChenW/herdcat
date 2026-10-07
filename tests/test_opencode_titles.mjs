import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { readFile } from "node:fs/promises";

// Replace process spawning, keeping the actual bridge logic and event stream.
// Payloads are synthetic and stay in memory; no socket or service is required.
const captured = [];
globalThis.titleTestSpawn = (command, args, options) => {
  assert.equal(command, "herdcat");
  assert.deepEqual(options.stdio, ["pipe", "ignore", "ignore"]);
  const child = new EventEmitter();
  child.stdin = new EventEmitter();
  child.stdin.end = body => {
    captured.push({ event: args.at(-1), payload: JSON.parse(body) });
    queueMicrotask(() => child.emit("close", 0));
  };
  child.unref = child.stdin.unref = () => {};
  return child;
};
const source = (await readFile(new URL("../integrations/opencode/index.js", import.meta.url), "utf8"))
  .replace('import { spawn } from "node:child_process";',
           'const spawn = globalThis.titleTestSpawn;');
const { default: plugin } = await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);

const info = new Map();
const directory = "/tmp/bridge-test";
const steps = [
  ["session.created", "main", { location: { directory }, title: "New session - synthetic" }],
  ["session.inbox.enqueued", "main", { location: { directory }, title: "First title" }],
  ["session.execution.started", "main", { location: { directory }, title: "First title" }],
  ["session.tool.called", "main"],
  ["session.execution.succeeded", "main", { location: { directory }, title: "Updated title" }],
  ["session.created", "child", { location: { directory }, parentID: "main", title: "Child title" }],
  ["session.execution.succeeded", "child"],
  ["session.created", "foreign", { location: { directory: "/tmp/other" }, title: "Foreign title" }],
  ["session.execution.started", "missing"],
  ["session.created", "long", { location: { directory }, title: "猫".repeat(33) }],
  ["session.execution.succeeded", "long", { location: { directory }, title: "猫".repeat(32) }],
  ["session.step.failed", "main"],
  ["session.deleted", "main"],
];
let finish;
const finished = new Promise(resolve => { finish = resolve; });
const lookups = [];
await plugin.setup({
  location: { directory },
  event: { subscribe: () => (async function* () {
    for (const [type, sessionID, record] of steps) {
      if (record) info.set(sessionID, record);
      yield { type, data: { sessionID, content: "SYNTHETIC_DO_NOT_FORWARD" } };
    }
    finish();
  })() },
  session: { get: async ({ sessionID }) => {
    lookups.push(sessionID);
    if (!info.has(sessionID)) throw new Error("Unavailable synthetic record");
    return info.get(sessionID);
  } },
});
await finished;
await new Promise(resolve => setImmediate(resolve));
assert.deepEqual(captured.map(item => item.event), [
  "session.created", "session.inbox.enqueued", "session.execution.started",
  "session.tool.called", "session.execution.succeeded", "session.created",
  "session.execution.succeeded", "session.deleted",
]);
assert.deepEqual(captured.map(item => item.payload.title), [
  "New session - synthetic", "First title", "First title", "First title",
  "Updated title", undefined, "猫".repeat(32), "Updated title",
]);
assert.equal(lookups.filter(id => id === "main").length, 4);
for (const { payload } of captured) {
  assert.equal(payload.cwd, directory);
  assert.ok(!("content" in payload) && !("agent_pid" in payload));
  assert.ok(!JSON.stringify(payload).includes("SYNTHETIC_DO_NOT_FORWARD"));
}
delete globalThis.titleTestSpawn;
console.log("opencode title refresh, filtering and payload privacy passed");
