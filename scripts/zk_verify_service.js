#!/usr/bin/env node
"use strict";

const http = require("http");
const fs = require("fs");
const path = require("path");
const { performance } = require("perf_hooks");

let snarkjs;
try {
  snarkjs = require("snarkjs");
} catch (err) {
  console.error("[zk-verify-service] failed to require snarkjs:", err && err.message ? err.message : String(err));
  process.exit(1);
}

const HOST = process.env.DIDZK_VERIFY_SERVICE_HOST || "127.0.0.1";
const PORT = Number(process.env.DIDZK_VERIFY_SERVICE_PORT || "3400");
const MAX_BODY_BYTES = Number(process.env.DIDZK_VERIFY_SERVICE_MAX_BODY_BYTES || "1048576");

const vkCache = new Map();

function nowMs() {
  return performance.now();
}

function loadJsonFile(p) {
  return JSON.parse(fs.readFileSync(p, "utf8"));
}

function getVk(vkPath) {
  const full = path.resolve(vkPath);
  const st = fs.statSync(full);
  const key = `${full}:${st.mtimeMs}:${st.size}`;
  const cached = vkCache.get(key);
  if (cached) return cached;
  const vk = loadJsonFile(full);
  vkCache.clear(); // simple cache discipline; one active vk is typical here
  vkCache.set(key, vk);
  return vk;
}

async function verifyPayload(payload) {
  const t0 = nowMs();

  const vkPath = typeof payload.vkPath === "string" ? payload.vkPath.trim() : "";
  const proofPath = typeof payload.proofPath === "string" ? payload.proofPath.trim() : "";
  const publicPath = typeof payload.publicPath === "string" ? payload.publicPath.trim() : "";

  if (!vkPath) throw new Error("missing_vkPath");

  let proof = payload.proof;
  let publicSignals = payload.publicSignals;

  if (!proof && !proofPath) throw new Error("missing_proof_or_proofPath");
  if (!publicSignals && !publicPath) throw new Error("missing_publicSignals_or_publicPath");

  const vk = getVk(vkPath);
  if (!proof) proof = loadJsonFile(proofPath);
  if (!publicSignals) publicSignals = loadJsonFile(publicPath);

  const ok = await snarkjs.groth16.verify(vk, publicSignals, proof);
  const t1 = nowMs();

  return {
    ok: !!ok,
    verifyMs: t1 - t0,
    stdoutText: ok ? "OK" : "Invalid proof",
    backend: "persistent_node_service",
  };
}

function sendJson(res, statusCode, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
    "Connection": "close",
  });
  res.end(body);
}

const server = http.createServer((req, res) => {
  if (req.method === "GET" && req.url === "/health") {
    return sendJson(res, 200, {
      ok: true,
      backend: "persistent_node_service",
      host: HOST,
      port: PORT,
      vkCacheEntries: vkCache.size,
    });
  }

  if (req.method !== "POST" || req.url !== "/verify") {
    return sendJson(res, 404, { ok: false, error: "not_found" });
  }

  let body = "";
  let tooLarge = false;
  req.setEncoding("utf8");
  req.on("data", (chunk) => {
    body += chunk;
    if (body.length > MAX_BODY_BYTES) {
      tooLarge = true;
      req.destroy(new Error("body_too_large"));
    }
  });
  req.on("error", (err) => {
    if (tooLarge) {
      return sendJson(res, 413, { ok: false, error: "body_too_large" });
    }
    return sendJson(res, 400, { ok: false, error: err && err.message ? err.message : String(err) });
  });
  req.on("end", async () => {
    if (tooLarge) {
      return sendJson(res, 413, { ok: false, error: "body_too_large" });
    }

    const startedAt = nowMs();
    try {
      const payload = body ? JSON.parse(body) : {};
      const result = await verifyPayload(payload);
      const elapsed = nowMs() - startedAt;
      console.error("[zk-verify-service] verify", JSON.stringify({
        ok: result.ok,
        elapsed_ms: elapsed,
        vkPath: payload.vkPath || "",
        proofPath: payload.proofPath || "",
        publicPath: payload.publicPath || "",
      }));
      return sendJson(res, 200, result);
    } catch (err) {
      const elapsed = nowMs() - startedAt;
      const message = err && err.message ? err.message : String(err);
      console.error("[zk-verify-service] verify-error", JSON.stringify({
        error: message,
        elapsed_ms: elapsed,
      }));
      return sendJson(res, 500, {
        ok: false,
        error: message,
        backend: "persistent_node_service",
      });
    }
  });
});

server.listen(PORT, HOST, () => {
  console.error(`[zk-verify-service] listening on http://${HOST}:${PORT}`);
});
