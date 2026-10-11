#!/usr/bin/env node
/**
 * Zero-dependency local CodeArts Hook bridge.
 * The hook ingress is ALWAYS loopback-only; LAN devices have read-only access.
 * Do not expose directly to the public Internet or untrusted Wi-Fi.
 */
import http from "node:http";
import net from "node:net";
import { timingSafeEqual } from "node:crypto";
import { createStateStore } from "./core.mjs";

export const DEFAULT_PORT = 17373;
export const MAX_BODY_BYTES = 512;

function safeEqual(actual, expected) {
  if (typeof actual !== "string" || typeof expected !== "string") return false;
  const a = Buffer.from(actual);
  const b = Buffer.from(expected);
  return a.length === b.length && timingSafeEqual(a, b);
}

function isLoopback(address = "") {
  return address === "::1" || address === "127.0.0.1" ||
    address.startsWith("::ffff:127.");
}

function jsonResponse(res, code, data) {
  res.writeHead(code, {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store",
    "X-Content-Type-Options": "nosniff",
    "Access-Control-Allow-Origin": "null"
  });
  res.end(JSON.stringify(data));
}

async function boundedBody(req) {
  const declared = req.headers["content-length"];
  if (declared && Number(declared) > MAX_BODY_BYTES) throw new Error("too_large");
  let length = 0;
  const chunks = [];
  for await (const chunk of req) {
    length += chunk.length;
    if (length > MAX_BODY_BYTES) throw new Error("too_large");
    chunks.push(chunk);
  }
  return Buffer.concat(chunks).toString("utf8");
}

function assertToken(name, value) {
  if (typeof value !== "string" || value.length < 16 || value.length > 256) {
    throw new Error(name + " must be a 16..256 character random token");
  }
}

export function createBridge({
  hookToken, petToken, clock = Date.now, ttlMs = 10000
} = {}) {
  assertToken("PIXELPET_HOOK_TOKEN", hookToken);
  assertToken("PIXELPET_PET_TOKEN", petToken);
  if (hookToken === petToken) throw new Error("Use distinct hook and pet tokens");
  const store = createStateStore({ clock, ttlMs });

  const server = http.createServer(async (req, res) => {
    try {
      const route = req.url;
      const method = req.method;
      const remote = req.socket.remoteAddress || "";
      if (method === "GET" && route === "/healthz") {
        if (!isLoopback(remote)) return jsonResponse(res, 403, { error: "forbidden" });
        return jsonResponse(res, 200, { ok: true });
      }

      const bearer = req.headers.authorization;
      if (method === "POST" && route === "/v1/events") {
        // Never accept raw CodeArts hooks from other LAN machines.
        if (!isLoopback(remote)) return jsonResponse(res, 403, { error: "forbidden" });
        if (!safeEqual(bearer, "Bearer " + hookToken)) {
          return jsonResponse(res, 401, { error: "unauthorized" });
        }
        if (req.headers["content-type"]?.split(";")[0]?.trim().toLowerCase() !==
            "application/json") {
          return jsonResponse(res, 415, { error: "content_type" });
        }
        const payload = JSON.parse(await boundedBody(req));
        store.ingest(payload);
        return jsonResponse(res, 202, { accepted: true });
      }
      if (method === "GET" && route === "/v1/state") {
        if (!safeEqual(bearer, "Bearer " + petToken)) {
          return jsonResponse(res, 401, { error: "unauthorized" });
        }
        return jsonResponse(res, 200, store.snapshot());
      }
      return jsonResponse(res, 404, { error: "not_found" });
    } catch (error) {
      const code = error.message === "too_large" ? 413 : 400;
      return jsonResponse(res, code, { error: code === 413 ? "too_large" : "invalid_event" });
    }
  });
  server.requestTimeout = 3000;
  server.headersTimeout = 3000;
  server.keepAliveTimeout = 1500;
  return { server, store };
}

export async function startBridge({
  host = "127.0.0.1",
  port = DEFAULT_PORT,
  hookToken = process.env.PIXELPET_HOOK_TOKEN,
  petToken = process.env.PIXELPET_PET_TOKEN,
  clock,
  ttlMs
} = {}) {
  if (typeof host !== "string" || !net.isIP(host)) {
    throw new Error("PIXELPET_BIND must be an IP address, not a hostname");
  }
  if (!Number.isInteger(port) || port < 0 || port > 65535) {
    throw new Error("PIXELPET_PORT must be 0..65535");
  }
  if (host !== "127.0.0.1" && host !== "::1" &&
      !process.env.PIXELPET_LAN_ACK && process.env.NODE_ENV !== "test") {
    throw new Error("LAN access needs explicit PIXELPET_LAN_ACK=1 (read-only LAN, no TLS)");
  }
  const bridge = createBridge({ hookToken, petToken, clock, ttlMs });
  await new Promise((resolve, reject) => {
    bridge.server.once("error", reject);
    bridge.server.listen(port, host, () => {
      bridge.server.off("error", reject);
      resolve();
    });
  });
  return bridge;
}

if (process.argv[1] && import.meta.url === new URL("file://" + process.argv[1]).href) {
  const rawPort = process.env.PIXELPET_PORT || String(DEFAULT_PORT);
  const port = Number(rawPort);
  startBridge({
    host: process.env.PIXELPET_BIND || "127.0.0.1",
    port, ttlMs: Number(process.env.PIXELPET_TTL_MS || 10000)
  }).then(({ server }) => {
    const addr = server.address();
    console.log("[PixelPet] bridge listening on " + addr.address + ":" + addr.port);
    console.log("[PixelPet] hook ingress: loopback only; LAN state endpoint: token-protected");
    for (const sig of ["SIGINT", "SIGTERM"]) {
      process.once(sig, () => server.close(() => process.exit(0)));
    }
  }).catch((error) => {
    console.error("[PixelPet] " + error.message);
    process.exitCode = 1;
  });
}
