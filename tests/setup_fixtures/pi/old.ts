// Legacy herdcat --hook pi --old bridge
export default function old(pi) { pi.on("session_start", () => "bongocat --hook pi"); }
