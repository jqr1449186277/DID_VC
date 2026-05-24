#!/usr/bin/env node
"use strict";

/**
 * bb_service_zk.js
 *
 * Phase 1:
 *  - remove direct rebuild from event listeners
 *  - cooperative rebuild (yield to event loop)
 *  - GET endpoints read cache only / fast-fail when not ready
 *  - async debounced state flush
 *
 * Phase 2:
 *  - active leaves + zero folding (no full 2^depth materialization)
 *  - partial path preheat (affected ids + open ops + small warm set)
 *  - incremental mirror sync by block range
 */

const fs = require("fs");
const path = require("path");
const express = require("express");

let ethersPkg;
try {
  ethersPkg = require("ethers");
} catch {
  console.error("[bb_service_zk] missing dependency: ethers. Run: npm i ethers");
  process.exit(1);
}

let circomlib;
try {
  circomlib = require("circomlibjs");
} catch {
  console.error("[bb_service_zk] missing dependency: circomlibjs. Run: npm i circomlibjs");
  process.exit(1);
}

const JsonRpcProvider = ethersPkg.JsonRpcProvider || (ethersPkg.providers && ethersPkg.providers.JsonRpcProvider);
const WebSocketProvider = ethersPkg.WebSocketProvider || (ethersPkg.providers && ethersPkg.providers.WebSocketProvider);
const Wallet = ethersPkg.Wallet;
const Contract = ethersPkg.Contract;
const getAddress = ethersPkg.getAddress || (ethersPkg.utils && ethersPkg.utils.getAddress);
const isHexString = ethersPkg.isHexString || (ethersPkg.utils && ethersPkg.utils.isHexString);
const parseEther = ethersPkg.parseEther || (ethersPkg.utils && ethersPkg.utils.parseEther);
const idFn = ethersPkg.id || (ethersPkg.utils && ethersPkg.utils.id);

const RPC_URL = process.env.RPC_URL || "http://127.0.0.1:8545";
const CONTRACT_ADDR = process.env.CONTRACT;
const PORT = parseInt(process.env.PORT || "3000", 10);
const TREE_DEPTH = parseInt(process.env.TREE_DEPTH || "20", 10);
const POLL_MS = parseInt(process.env.POLL_MS || "200", 10);
const AUTO_REBUILD = (process.env.AUTO_REBUILD || "1") !== "0";
const SANITY_MS = parseInt(process.env.SANITY_MS || "3000", 10);
const OP_TERMINAL_RETENTION_MS = Math.max(60000, parseInt(process.env.OP_TERMINAL_RETENTION_MS || "180000", 10));
const REBUILD_CHUNK = Math.max(8, parseInt(process.env.REBUILD_CHUNK || "256", 10));
const PREHEAT_LIMIT = Math.max(4, parseInt(process.env.PREHEAT_LIMIT || "32", 10));
const ROOT_UPDATER_PRIVKEY = process.env.ROOT_UPDATER_PRIVKEY ||
  "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";
const STATE_FILE = process.env.STATE_FILE || path.join(__dirname, "bb_state_zk.json");

if (!CONTRACT_ADDR) {
  console.error("[bb_service_zk] Please set CONTRACT env var (DIDBulletinBoardZK address).");
  process.exit(1);
}

const ABI = [
  "function records(bytes32) view returns (bytes32 cid, address owner, address recovery, uint64 version, bool active, bytes32 pkNormHash, bytes32 pkRecHash)",
  "function activeRoot() view returns (bytes32)",
  "function rootEpoch() view returns (uint64)",
  "function rootUpdater() view returns (address)",
  "function ttssVkSetHashOf(bytes32) view returns (bytes32)",
  "function ttssMetaHashOf(bytes32) view returns (bytes32)",
  "function ttssMetaVersionOf(bytes32) view returns (uint64)",
  "function ttssMetaEpochOf(bytes32) view returns (uint64)",
  "function lastTraceAccusedSetHashOf(bytes32) view returns (bytes32)",
  "function lastTraceProofHashOf(bytes32) view returns (bytes32)",
  "function lastTraceDigestOf(bytes32) view returns (bytes32)",
  "function lastTraceVersionOf(bytes32) view returns (uint64)",
  "function lastTraceEpochOf(bytes32) view returns (uint64)",
  "function getTTSSMeta(bytes32 idHash) view returns (bytes32 vkSetHash, bytes32 metaHash, uint64 ver, uint64 epoch)",
  "function getTraceAnchor(bytes32 idHash) view returns (bytes32 accusedSetHash, bytes32 proofHash, bytes32 traceDigest, uint64 ver, uint64 epoch)",
  "function setTTSSMeta(bytes32 idHash, uint64 ver, uint64 epoch, bytes32 vkSetHash, bytes32 metaHash)",
  "function publishTraceResult(bytes32 idHash, uint64 ver, uint64 epoch, bytes32 accusedSetHash, bytes32 proofHash, bytes32 traceDigest)",
  "function registerZK(bytes32 idHash, bytes32 cid, bytes32 pkNormHash, bytes32 pkRecHash, address owner, address recovery)",
  "function applyUpdateZK(bytes32 idHash, bytes32 newCid, bytes32 pkNormHash, bytes32 pkRecHash, uint64 newVersion)",
  "function applyRecoveryRotateZK(bytes32 idHash, bytes32 newCid, bytes32 pkNormHash, bytes32 pkRecHash, uint64 newVersion, address newOwner, address newRecovery)",
  "function setActiveRoot(bytes32 newRoot, uint64 newEpoch)",
  "event RegisteredZK(bytes32 indexed idHash, bytes32 cid, bytes32 pkNormHash, bytes32 pkRecHash, address owner, address recovery)",
  "event TTSSMetaUpdated(bytes32 indexed idHash, uint64 indexed ver, uint64 indexed epoch, bytes32 vkSetHash, bytes32 metaHash)",
  "event TraceResultPublished(bytes32 indexed idHash, uint64 indexed ver, uint64 indexed epoch, bytes32 accusedSetHash, bytes32 proofHash, bytes32 traceDigest)",
  "event UpdatedZK(bytes32 indexed idHash, bytes32 newCid, bytes32 pkNormHash, bytes32 pkRecHash, uint64 newVersion)",
  "event RecoveredZK(bytes32 indexed idHash, bytes32 newCid, bytes32 pkNormHash, bytes32 pkRecHash, uint64 newVersion, address newOwner, address newRecovery)",
  "event RootUpdated(bytes32 indexed newRoot, uint64 indexed newEpoch)"
];

let provider;
if (/^wss?:\/\//.test(RPC_URL)) {
  provider = new WebSocketProvider(RPC_URL);
  console.log(`[bb_service_zk] WebSocketProvider ${RPC_URL}`);
} else {
  provider = new JsonRpcProvider(RPC_URL);
  provider.pollingInterval = POLL_MS;
  console.log(`[bb_service_zk] JsonRpcProvider ${RPC_URL}, polling=${POLL_MS}ms`);
}

const contract = new Contract(CONTRACT_ADDR, ABI, provider);
const rootUpdaterWallet = new Wallet(ROOT_UPDATER_PRIVKEY, provider);
const rootUpdaterContract = contract.connect(rootUpdaterWallet);

function bodyString(v) { return typeof v === "string" ? v.trim() : ""; }
function nowMs() { return Date.now(); }
function idToKey(idStr) { return idFn(String(idStr)); }
function semicolonKV(obj) { return Object.entries(obj).map(([k, v]) => `${k}=${v}`).join(";"); }
function ensure0x64(v) { const s = bodyString(v); return isHexString && isHexString(s) && s.length === 66 ? s : ""; }
function bn(v) { return BigInt(v.toString()); }
function hexToField(hex) { return bn(hex); }
function fieldToHex(x) {
  let h = bn(x).toString(16);
  if (h.length > 64) throw new Error("field_overflow_256");
  while (h.length < 64) h = "0" + h;
  return "0x" + h;
}
function poseidonHash(inputs) { const out = poseidon(inputs.map((v) => bn(v))); return bn(F.toString(out)); }
function parseEthersError(e) {
  if (!e) return "unknown";
  const short = e.shortMessage || e.reason || e.message || String(e);
  return String(short).replace(/\s+/g, " ").trim().slice(0, 320);
}
function walletFromSeedHex(seedHex) { return new Wallet(seedHex, provider); }

function traceStepEnter(step, meta = {}) {
  const startedAt = nowMs();
  console.error(`[trace-enter] ${step} ${JSON.stringify({ ...meta, startedAt })}`);
  return startedAt;
}
function traceStepExit(step, startedAt, meta = {}) {
  const endedAt = nowMs();
  console.error(`[trace-exit] ${step} ${JSON.stringify({ ...meta, endedAt, elapsed_ms: endedAt - startedAt })}`);
}
function traceStepError(step, startedAt, err, meta = {}) {
  const endedAt = nowMs();
  console.error(`[trace-error] ${step} ${JSON.stringify({ ...meta, endedAt, elapsed_ms: endedAt - startedAt, error: parseEthersError(err) })}`);
}
function logInfo(tag, meta = {}) { console.error(`[${tag}] ${JSON.stringify(meta)}`); }

function loadState() {
  try {
    const raw = fs.readFileSync(STATE_FILE, "utf8");
    const st = JSON.parse(raw);
    if (!st || typeof st !== "object") throw new Error("bad_state");
    return {
      keys: st.keys || {},
      mirror: st.mirror || {},
      idByHash: st.idByHash || {},
      ops: st.ops || {},
      requestToOp: st.requestToOp || {},
      meta: st.meta || {},
      ttssMeta: st.ttssMeta || {},
      traceAnchors: st.traceAnchors || {},
    };
  } catch {
    return { keys: {}, mirror: {}, idByHash: {}, ops: {}, requestToOp: {}, meta: {}, ttssMeta: {}, traceAnchors: {} };
  }
}

const state = loadState();
state.meta.lastSyncedBlock = Number(state.meta.lastSyncedBlock || 0);
state.meta.rebuildSeq = Number(state.meta.rebuildSeq || 0);
state.rootCache = state.rootCache || { root: "0x" + "00".repeat(32), epoch: 0, depth: TREE_DEPTH, builtAt: 0 };
state.chainCache = state.chainCache || { root: "0x" + "00".repeat(32), epoch: 0, checkedAt: 0, block: 0 };
state.pathCache = state.pathCache || {};
state.leafCache = state.leafCache || {};
state.cacheBootstrapped = state.cacheBootstrapped || false;
state.cacheStale = state.cacheStale !== undefined ? !!state.cacheStale : true;
state.rebuildInFlight = false;
state.rebuildDirty = false;
state.fullSyncRequested = false;
state.pendingAffectedIds = [];
state.pendingOpIds = [];
state.ttssMeta = state.ttssMeta || {};
state.traceAnchors = state.traceAnchors || {};

function snapshotPersistentState() {
  return JSON.stringify({
    keys: state.keys,
    mirror: state.mirror,
    idByHash: state.idByHash,
    ops: state.ops,
    requestToOp: state.requestToOp,
    meta: state.meta,
    ttssMeta: state.ttssMeta,
    traceAnchors: state.traceAnchors,
    rootCache: state.rootCache,
    chainCache: state.chainCache,
    cacheBootstrapped: state.cacheBootstrapped,
    cacheStale: state.cacheStale,
  }, null, 2);
}

let flushTimer = null;
let flushInFlight = false;
let flushPending = false;
function saveState() { scheduleStateFlush(); }
function scheduleStateFlush() {
  flushPending = true;
  if (flushTimer) return;
  flushTimer = setTimeout(async () => {
    flushTimer = null;
    if (flushInFlight || !flushPending) return;
    flushPending = false;
    flushInFlight = true;
    try {
      await fs.promises.writeFile(STATE_FILE, snapshotPersistentState());
    } catch (e) {
      console.warn(`[bb_service_zk] state flush failed: ${parseEthersError(e)}`);
      flushPending = true;
    } finally {
      flushInFlight = false;
      if (flushPending) scheduleStateFlush();
    }
  }, 150);
}

function rememberId(id) {
  const raw = bodyString(id);
  if (!raw) return "";
  const idHash = idToKey(raw);
  state.idByHash[idHash] = raw;
  return idHash;
}
function maybeRememberId(id, idHash) {
  const raw = bodyString(id);
  if (raw && idHash) state.idByHash[idHash] = raw;
}
function makeOpId() { return `op_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 10)}`; }
function defaultRequestKey(kind, id, explicitRequestId, extra = "") {
  const explicit = bodyString(explicitRequestId);
  if (explicit) return explicit;
  const rawId = bodyString(id);
  const suffix = extra ? `:${extra}` : "";
  return `${kind}:${rawId}${suffix}`;
}
function getOpByRequestKey(requestKey) {
  const key = bodyString(requestKey);
  if (!key) return null;
  const opId = state.requestToOp[key];
  return opId ? (state.ops[opId] || null) : null;
}
function getOpByLookup(opId, requestKey) {
  const id = bodyString(opId);
  if (id && state.ops[id]) return state.ops[id];
  return getOpByRequestKey(requestKey);
}
function opResponseKV(op, extra = {}) {
  const out = {
    ok: 1,
    accepted: 1,
    opId: op.opId,
    requestKey: op.requestKey,
    kind: op.kind,
    id: op.id,
    status: op.status,
    ready: op.ready ? 1 : 0,
    txHash: op.txHash || "",
    submit_ms: op.submitMs || 0,
    confirm_ms: op.confirmMs || 0,
    owner: op.owner || "",
    recovery: op.recovery || "",
    version: op.targetVersion ?? op.currentVersion ?? op.minVersion ?? 0,
    currentVersion: op.currentVersion ?? 0,
  };
  if (op.currentRoot) out.root = op.currentRoot;
  if (op.currentEpoch !== undefined) out.epoch = op.currentEpoch;
  if (op.lastError) out.lastError = op.lastError;
  return { ...out, ...extra };
}
function createOperation(kind, id, requestKey, extra = {}) {
  const opId = makeOpId();
  const op = {
    opId,
    requestKey,
    kind,
    id: bodyString(id),
    idHash: extra.idHash || "",
    status: extra.status || "ACCEPTED",
    ready: false,
    acceptedAt: nowMs(),
    updatedAt: nowMs(),
    txHash: extra.txHash || "",
    submitMs: extra.submitMs || 0,
    confirmMs: extra.confirmMs || 0,
    owner: extra.owner || "",
    recovery: extra.recovery || "",
    minVersion: Number(extra.minVersion || 0),
    targetVersion: Number(extra.targetVersion || 0),
    currentVersion: Number(extra.currentVersion || 0),
    currentRoot: extra.currentRoot || "",
    currentEpoch: extra.currentEpoch !== undefined ? Number(extra.currentEpoch) : undefined,
    lastError: extra.lastError || "",
  };
  state.ops[opId] = op;
  state.requestToOp[requestKey] = opId;
  saveState();
  logInfo("op-create", { opId, requestKey, id: op.id, status: op.status, ready: op.ready, currentRoot: op.currentRoot || "", currentEpoch: op.currentEpoch ?? null });
  return op;
}
function updateOperation(opId, patch = {}) {
  const op = state.ops[opId];
  if (!op) return null;
  Object.assign(op, patch);
  op.updatedAt = nowMs();
  state.ops[opId] = op;
  if (op.requestKey) state.requestToOp[op.requestKey] = opId;
  saveState();
  return op;
}
function failOperation(opId, errText) {
  return updateOperation(opId, { status: "FAILED", ready: false, lastError: String(errText || "unknown") });
}
function isTerminalOp(op) {
  const status = bodyString(op && op.status);
  return status === "READY" || status === "FAILED";
}
function shouldSkipTerminalOpRefresh(op, now = nowMs()) {
  if (!op || !isTerminalOp(op)) return false;
  const updatedAt = Number(op.updatedAt || op.acceptedAt || 0);
  if (!Number.isFinite(updatedAt) || updatedAt <= 0) return false;
  return (now - updatedAt) > OP_TERMINAL_RETENTION_MS;
}
function pruneOldTerminalOps(now = nowMs()) {
  const removeOpIds = [];
  for (const [opId, op] of Object.entries(state.ops || {})) {
    if (shouldSkipTerminalOpRefresh(op, now)) removeOpIds.push(opId);
  }
  if (removeOpIds.length === 0) return 0;
  const removeSet = new Set(removeOpIds);
  for (const opId of removeOpIds) delete state.ops[opId];
  for (const [requestKey, mappedOpId] of Object.entries(state.requestToOp || {})) {
    if (removeSet.has(mappedOpId) || !state.ops[mappedOpId]) delete state.requestToOp[requestKey];
  }
  saveState();
  logInfo("ops-pruned", { removed: removeOpIds.length, remaining: Object.keys(state.ops || {}).length, retentionMs: OP_TERMINAL_RETENTION_MS });
  return removeOpIds.length;
}
const DEFERRED_TTSS_ATTACH_START_DELAY_MS = 750;
let deferredTTSSAttachInFlight = 0;
function hasNonTerminalOpKinds(kinds = []) {
  const want = new Set((kinds || []).map((k) => bodyString(k)).filter(Boolean));
  if (want.size === 0) return false;
  for (const op of Object.values(state.ops || {})) {
    if (!op || isTerminalOp(op)) continue;
    if (want.has(bodyString(op.kind))) return true;
  }
  return false;
}
function shouldDegradeDeferredTTSSAttach(reason = "") {
  const st = getAddrLockStats(rootUpdaterWallet.address);
  const queueBusy = Number(st.active || 0) > 0 || Number(st.queued || 0) > 0;
  const recoveryBusy = hasNonTerminalOpKinds(["recovery", "ttss_meta_rotate"]);
  const tooManyDeferred = deferredTTSSAttachInFlight > 0;
  return {
    degrade: queueBusy || recoveryBusy || tooManyDeferred,
    queueBusy,
    recoveryBusy,
    tooManyDeferred,
    active: Number(st.active || 0),
    queued: Number(st.queued || 0),
    reason: bodyString(reason || ""),
  };
}

function listOpenOpIdsForId(rawId) {
  const id = bodyString(rawId);
  return Object.values(state.ops || {})
    .filter((op) => bodyString(op.id) === id && bodyString(op.status) !== "FAILED")
    .map((op) => op.opId);
}

function parseRecordTuple(r) {
  return {
    cid: r.cid ?? r[0],
    owner: getAddress(r.owner ?? r[1]),
    recovery: getAddress(r.recovery ?? r[2]),
    version: Number(r.version ?? r[3]),
    active: Boolean(r.active ?? r[4]),
    pkNormHash: r.pkNormHash ?? r[5],
    pkRecHash: r.pkRecHash ?? r[6],
  };
}
async function getRecordOnchain(id) {
  const tuple = await contract.records(idToKey(id));
  return parseRecordTuple(tuple);
}
async function getRecordByIdHashOnchain(idHash) {
  const tuple = await contract.records(bodyString(idHash));
  return parseRecordTuple(tuple);
}
async function getTTSSMetaOnchain(idHash) {
  const tuple = await contract.getTTSSMeta(bodyString(idHash));
  return {
    vkSetHash: tuple.vkSetHash ?? tuple[0],
    metaHash: tuple.metaHash ?? tuple[1],
    ver: Number(tuple.ver ?? tuple[2]),
    epoch: Number(tuple.epoch ?? tuple[3]),
  };
}
async function getTraceAnchorOnchain(idHash) {
  const tuple = await contract.getTraceAnchor(bodyString(idHash));
  return {
    accusedSetHash: tuple.accusedSetHash ?? tuple[0],
    proofHash: tuple.proofHash ?? tuple[1],
    traceDigest: tuple.traceDigest ?? tuple[2],
    ver: Number(tuple.ver ?? tuple[3]),
    epoch: Number(tuple.epoch ?? tuple[4]),
  };
}
function zeroHex32(v) {
  return bodyString(v) || ("0x" + "00".repeat(32));
}
function resolveIdAndHash(body = {}, query = {}) {
  const id = bodyString(body.id || query.id || "");
  const idHashRaw = bodyString(body.idHash || query.idHash || "");
  const idHash = id ? rememberId(id) : idHashRaw;
  if (id && idHashRaw) maybeRememberId(id, idHashRaw);
  return { id, idHash: idHash || idHashRaw };
}
function storeTTSSMetaLocal(entry) {
  state.ttssMeta[entry.idHash] = { ...entry, updatedAt: nowMs() };
  saveState();
}
function storeTraceAnchorLocal(entry) {
  state.traceAnchors[entry.idHash] = { ...entry, updatedAt: nowMs() };
  saveState();
}
async function getPendingNonceRaw(addr) {
  const hexNonce = await provider.send("eth_getTransactionCount", [getAddress(addr), "pending"]);
  return Number(BigInt(hexNonce));
}
const addrLock = new Map();
const addrLockStats = new Map();
function getAddrLockStats(addr) {
  const a = getAddress(addr);
  const st = addrLockStats.get(a) || { queued: 0, active: 0, lastWaitMs: 0, lastHoldMs: 0 };
  return { ...st };
}
async function withAddrLock(addr, fn) {
  const a = getAddress(addr);
  const prev = addrLock.get(a) || Promise.resolve();
  const prevStats = addrLockStats.get(a) || { queued: 0, active: 0, lastWaitMs: 0, lastHoldMs: 0 };
  addrLockStats.set(a, { ...prevStats, queued: Number(prevStats.queued || 0) + 1 });
  let release;
  const next = new Promise((r) => { release = r; });
  addrLock.set(a, prev.then(() => next));
  const queuedAt = nowMs();
  await prev;
  const acquiredAt = nowMs();
  const beforeRun = addrLockStats.get(a) || { queued: 1, active: 0, lastWaitMs: 0, lastHoldMs: 0 };
  addrLockStats.set(a, {
    ...beforeRun,
    queued: Math.max(0, Number(beforeRun.queued || 0) - 1),
    active: Number(beforeRun.active || 0) + 1,
    lastWaitMs: acquiredAt - queuedAt,
  });
  try {
    return await fn(acquiredAt - queuedAt);
  } finally {
    const releasedAt = nowMs();
    const endStats = addrLockStats.get(a) || { queued: 0, active: 1, lastWaitMs: acquiredAt - queuedAt, lastHoldMs: 0 };
    addrLockStats.set(a, {
      ...endStats,
      active: Math.max(0, Number(endStats.active || 0) - 1),
      lastHoldMs: releasedAt - acquiredAt,
    });
    release();
  }
}
async function sendContractTxLocked(wallet, sendFn, meta = {}) {
  const label = bodyString(meta.label || "tx");
  const isRootUpdater = getAddress(wallet.address) === getAddress(rootUpdaterWallet.address);
  const enqueuedAt = nowMs();
  const preStats = getAddrLockStats(wallet.address);
  return withAddrLock(wallet.address, async (lockWaitMs) => {
    if (isRootUpdater) {
      console.log(`[bb_service_zk] rootUpdaterTx queue label=${label || "tx"} wait_ms=${lockWaitMs} queued_before=${Number(preStats.queued || 0)} active_before=${Number(preStats.active || 0)}`);
    }
    let lastErr = null;
    const sendStart = nowMs();
    for (let attempt = 0; attempt < 3; attempt++) {
      const nonce = await getPendingNonceRaw(wallet.address);
      try {
        const tx = await sendFn({ nonce });
        if (isRootUpdater) {
          console.log(`[bb_service_zk] rootUpdaterTx sent label=${label || "tx"} lock_wait_ms=${lockWaitMs} send_ms=${nowMs() - sendStart} total_queue_ms=${nowMs() - enqueuedAt} nonce=${nonce} hash=${tx && tx.hash ? String(tx.hash) : ""}`);
        }
        return tx;
      } catch (e) {
        lastErr = e;
        const msg = parseEthersError(e).toLowerCase();
        if (msg.includes("nonce too low") || msg.includes("already been used")) {
          await new Promise((r) => setTimeout(r, 25 * (attempt + 1)));
          continue;
        }
        if (isRootUpdater) {
          console.warn(`[bb_service_zk] rootUpdaterTx send_fail label=${label || "tx"} lock_wait_ms=${lockWaitMs} send_ms=${nowMs() - sendStart} total_queue_ms=${nowMs() - enqueuedAt} err=${parseEthersError(e)}`);
        }
        throw e;
      }
    }
    if (isRootUpdater) {
      console.warn(`[bb_service_zk] rootUpdaterTx nonce_retry_exhausted label=${label || "tx"} total_queue_ms=${nowMs() - enqueuedAt}`);
    }
    throw lastErr || new Error("nonce_retry_exhausted");
  });
}
async function hardhatSetBalance(addr, ethStr = "1000") {
  const a = getAddress(addr);
  const wei = parseEther(String(ethStr));
  const hexWei = "0x" + BigInt(wei).toString(16);
  try { await provider.send("hardhat_setBalance", [a, hexWei]); } catch {}
}

let poseidon = null;
let F = null;
const treeCache = {
  zeroes: [],
  activeEntries: [],
  indexById: {},
  idByIndex: {},
  leafById: {},
  leafFieldById: {},
  rootField: 0n,
  rootHex: "0x" + "00".repeat(32),
  levels: [],
  nodeCache: [],
  activeCount: 0,
};
function buildZeroes(depth) {
  const zeroes = new Array(depth + 1);
  zeroes[0] = poseidonHash([0n, 0n, 0n, 0n, 0n]);
  for (let i = 1; i <= depth; i++) zeroes[i] = poseidonHash([zeroes[i - 1], zeroes[i - 1]]);
  return zeroes;
}
function computeLeafFromMirror(rec) {
  return poseidonHash([
    hexToField(rec.cid),
    hexToField(rec.pkNormHash),
    hexToField(rec.pkRecHash),
    BigInt(rec.version),
    1n,
  ]);
}
function yieldImmediate() {
  return new Promise((resolve) => setImmediate(resolve));
}
function treeCapacity() {
  return 1 << TREE_DEPTH;
}
function preferredIndexForIdHash(idHash) {
  const mask = (1n << BigInt(TREE_DEPTH)) - 1n;
  return Number(BigInt(String(idHash)) & mask);
}
function initSparseTreeCache() {
  treeCache.activeEntries = [];
  treeCache.indexById = {};
  treeCache.idByIndex = {};
  treeCache.leafById = {};
  treeCache.leafFieldById = {};
  treeCache.levels = [];
  treeCache.nodeCache = Array.from({ length: TREE_DEPTH + 1 }, () => new Map());
  treeCache.activeCount = 0;
  treeCache.rootField = treeCache.zeroes[TREE_DEPTH];
  treeCache.rootHex = fieldToHex(treeCache.rootField);
  state.pathCache = {};
  state.leafCache = {};
}
function getNodeValue(level, index) {
  const m = treeCache.nodeCache[level];
  if (!m) return treeCache.zeroes[level];
  return m.has(index) ? m.get(index) : treeCache.zeroes[level];
}
function setNodeValue(level, index, value) {
  const m = treeCache.nodeCache[level];
  if (!m) return;
  if (value === treeCache.zeroes[level]) m.delete(index);
  else m.set(index, value);
}
function assignIndexForIdHash(idHash) {
  if (treeCache.indexById[idHash] !== undefined) return treeCache.indexById[idHash];
  const capacity = treeCapacity();
  let idx = preferredIndexForIdHash(idHash);
  for (let steps = 0; steps < capacity; steps++) {
    const occupant = treeCache.idByIndex[idx];
    if (!occupant || occupant === idHash) {
      treeCache.indexById[idHash] = idx;
      treeCache.idByIndex[idx] = idHash;
      return idx;
    }
    idx = (idx + 1) % capacity;
  }
  throw new Error(`tree_capacity_exceeded:no_free_slot_for:${idHash}`);
}
function releaseIndexForIdHash(idHash) {
  const idx = treeCache.indexById[idHash];
  if (idx === undefined) return null;
  delete treeCache.indexById[idHash];
  if (treeCache.idByIndex[idx] === idHash) delete treeCache.idByIndex[idx];
  return idx;
}
function updateSparseLeafAtIndex(index, leafField) {
  if (leafField === null || leafField === undefined) setNodeValue(0, index, treeCache.zeroes[0]);
  else setNodeValue(0, index, leafField);

  let idx = index;
  for (let level = 0; level < TREE_DEPTH; level++) {
    const base = idx & ~1;
    const left = getNodeValue(level, base);
    const right = getNodeValue(level, base + 1);
    const parent = poseidonHash([left, right]);
    idx >>= 1;
    setNodeValue(level + 1, idx, parent);
  }
  treeCache.rootField = getNodeValue(TREE_DEPTH, 0);
  treeCache.rootHex = fieldToHex(treeCache.rootField);
}
function updateLeafCachesForId(rawId, idHash, rec, leafField) {
  const leafHex = fieldToHex(leafField);
  treeCache.leafFieldById[idHash] = leafField;
  treeCache.leafById[idHash] = leafHex;
  state.leafCache[rawId] = {
    idHash,
    cid: rec.cid,
    pkNormHash: rec.pkNormHash,
    pkRecHash: rec.pkRecHash,
    version: rec.version,
    active: 1,
    leaf: leafHex,
  };
}
function removeCachesForId(rawId, idHash) {
  delete treeCache.leafFieldById[idHash];
  delete treeCache.leafById[idHash];
  delete state.leafCache[rawId];
  delete state.pathCache[rawId];
}
function recomputePathCacheForId(rawId) {
  const id = bodyString(rawId);
  if (!id) return null;
  const idHash = rememberId(id);
  const idx0 = treeCache.indexById[idHash];
  if (idx0 === undefined) {
    delete state.pathCache[id];
    return null;
  }
  const leafHex = treeCache.leafById[idHash] || fieldToHex(getNodeValue(0, idx0));
  const pathElements = [];
  const pathIndex = [];
  let idx = idx0;
  for (let level = 0; level < TREE_DEPTH; level++) {
    const sib = idx ^ 1;
    pathElements.push(fieldToHex(getNodeValue(level, sib)));
    pathIndex.push(idx & 1);
    idx >>= 1;
  }
  state.pathCache[id] = {
    idHash,
    depth: TREE_DEPTH,
    index: idx0,
    leaf: leafHex,
    root: treeCache.rootHex,
    epoch: state.chainCache.epoch || state.rootCache.epoch || 0,
    pathElements,
    pathIndex,
  };
  return state.pathCache[id];
}
function refreshActiveEntriesView() {
  const ids = Object.keys(treeCache.indexById).sort((a, b) => treeCache.indexById[a] - treeCache.indexById[b]);
  treeCache.activeEntries = ids.map((idHash) => ({ id: idHash, index: treeCache.indexById[idHash], rec: state.mirror[idHash] })).filter((x) => x.rec && x.rec.active);
  treeCache.activeCount = treeCache.activeEntries.length;
}
async function fullRebuildSparseTreeFromMirror(preheatIds = []) {
  const t = traceStepEnter("rebuildTreeCacheFromMirror", { mode: "full_sparse", activeMirror: Object.keys(state.mirror || {}).length, preheatIds: preheatIds.length, chunk: REBUILD_CHUNK });
  try {
    const activeEntries = Object.entries(state.mirror)
      .filter(([, rec]) => rec && rec.active)
      .sort((a, b) => a[0].localeCompare(b[0]));
    const capacity = treeCapacity();
    if (activeEntries.length > capacity) throw new Error(`tree_capacity_exceeded:${activeEntries.length}>${capacity}`);
    initSparseTreeCache();
    let warmed = 0;
    for (let i = 0; i < activeEntries.length; i++) {
      const [idHash, rec] = activeEntries[i];
      const idx = assignIndexForIdHash(idHash);
      const leaf = computeLeafFromMirror(rec);
      updateSparseLeafAtIndex(idx, leaf);
      const rawId = state.idByHash[idHash] || idHash;
      updateLeafCachesForId(rawId, idHash, rec, leaf);
      if (((i + 1) % REBUILD_CHUNK) === 0) await yieldImmediate();
    }
    refreshActiveEntriesView();
    const idsToPreheat = new Set();
    for (const id of preheatIds) if (bodyString(id)) idsToPreheat.add(bodyString(id));
    for (const rawId of idsToPreheat) {
      if (recomputePathCacheForId(rawId)) warmed += 1;
      if ((warmed % REBUILD_CHUNK) === 0) await yieldImmediate();
    }
    traceStepExit("rebuildTreeCacheFromMirror", t, { mode: "full_sparse", activeCount: treeCache.activeEntries.length, root: treeCache.rootHex, warmed });
    return { activeCount: treeCache.activeEntries.length, warmed, root: treeCache.rootHex };
  } catch (e) {
    traceStepError("rebuildTreeCacheFromMirror", t, e, { mode: "full_sparse" });
    throw e;
  }
}
async function incrementalUpdateTreeCacheFromMirror(affectedIds = [], preheatIds = []) {
  const t = traceStepEnter("rebuildTreeCacheFromMirror", { mode: "incremental_sparse", affectedIds: affectedIds.length, preheatIds: preheatIds.length });
  try {
    if (!state.cacheBootstrapped || !treeCache.nodeCache || treeCache.nodeCache.length === 0) {
      return await fullRebuildSparseTreeFromMirror(preheatIds);
    }
    const affectedRawIds = [...new Set((affectedIds || []).map((x) => bodyString(x)).filter(Boolean))];
    const preheatRawIds = [...new Set((preheatIds || []).map((x) => bodyString(x)).filter(Boolean))];
    for (const rawId of affectedRawIds) {
      const idHash = rememberId(rawId);
      const rec = state.mirror[idHash];
      if (!rec || !rec.active) {
        const oldIdx = releaseIndexForIdHash(idHash);
        if (oldIdx !== null) updateSparseLeafAtIndex(oldIdx, null);
        removeCachesForId(rawId, idHash);
        continue;
      }
      const idx = assignIndexForIdHash(idHash);
      const leaf = computeLeafFromMirror(rec);
      updateSparseLeafAtIndex(idx, leaf);
      updateLeafCachesForId(rawId, idHash, rec, leaf);
    }
    refreshActiveEntriesView();
    for (const rawId of preheatRawIds) recomputePathCacheForId(rawId);
    traceStepExit("rebuildTreeCacheFromMirror", t, { mode: "incremental_sparse", activeCount: treeCache.activeEntries.length, root: treeCache.rootHex, warmed: preheatRawIds.length });
    return { activeCount: treeCache.activeEntries.length, warmed: preheatRawIds.length, root: treeCache.rootHex };
  } catch (e) {
    traceStepError("rebuildTreeCacheFromMirror", t, e, { mode: "incremental_sparse" });
    throw e;
  }
}

async function safeQueryFilter(filter, fromBlock = 0, toBlock = "latest") {
  try {
    const out = await contract.queryFilter(filter, fromBlock, toBlock);
    return Array.isArray(out) ? out : [];
  } catch (e) {
    console.warn(`[bb_service_zk] queryFilter failed: ${parseEthersError(e)}`);
    return [];
  }
}
function applyRegisteredArgsToMirror(a) {
  if (!a) return;
  const idHash = String(a.idHash);
  state.mirror[idHash] = {
    idHash,
    cid: String(a.cid),
    pkNormHash: String(a.pkNormHash),
    pkRecHash: String(a.pkRecHash),
    owner: getAddress(a.owner),
    recovery: getAddress(a.recovery),
    version: 0,
    active: true,
  };
}
function applyUpdatedArgsToMirror(a) {
  if (!a) return;
  const idHash = String(a.idHash);
  const rec = state.mirror[idHash] || { idHash };
  rec.cid = String(a.newCid);
  rec.pkNormHash = String(a.pkNormHash);
  rec.pkRecHash = String(a.pkRecHash);
  rec.version = Number(a.newVersion);
  rec.active = true;
  state.mirror[idHash] = rec;
}
function applyRecoveredArgsToMirror(a) {
  if (!a) return;
  const idHash = String(a.idHash);
  const rec = state.mirror[idHash] || { idHash };
  rec.cid = String(a.newCid);
  rec.pkNormHash = String(a.pkNormHash);
  rec.pkRecHash = String(a.pkRecHash);
  rec.version = Number(a.newVersion);
  rec.owner = getAddress(a.newOwner);
  rec.recovery = getAddress(a.newRecovery);
  rec.active = true;
  state.mirror[idHash] = rec;
}

async function syncMirrorFromEventsIncremental(forceFull = false) {
  const t = traceStepEnter("syncMirrorFromEventsIncremental", { forceFull, lastSyncedBlock: state.meta.lastSyncedBlock || 0 });
  try {
    const latest = Number(await provider.getBlockNumber());
    const fromBlock = forceFull || !state.meta.lastSyncedBlock ? 0 : (Number(state.meta.lastSyncedBlock) + 1);
    const toBlock = latest;
    if (!forceFull && fromBlock > toBlock) {
      traceStepExit("syncMirrorFromEventsIncremental", t, { mode: "noop", latest, fromBlock, toBlock, count: 0 });
      return { mode: "noop", latest, fromBlock, toBlock, count: 0 };
    }

    if (forceFull) state.mirror = {};

    const regEvents = await safeQueryFilter(contract.filters.RegisteredZK(), fromBlock, toBlock);
    const updEvents = await safeQueryFilter(contract.filters.UpdatedZK(), fromBlock, toBlock);
    const recEvents = await safeQueryFilter(contract.filters.RecoveredZK(), fromBlock, toBlock);

    for (const ev of regEvents) { if (ev && ev.args) applyRegisteredArgsToMirror(ev.args); }
    for (const ev of updEvents) { if (ev && ev.args) applyUpdatedArgsToMirror(ev.args); }
    for (const ev of recEvents) { if (ev && ev.args) applyRecoveredArgsToMirror(ev.args); }

    state.meta.lastSyncedBlock = latest;
    saveState();
    const count = regEvents.length + updEvents.length + recEvents.length;
    traceStepExit("syncMirrorFromEventsIncremental", t, { mode: forceFull ? "full" : "incremental", latest, fromBlock, toBlock, count });
    return { mode: forceFull ? "full" : "incremental", latest, fromBlock, toBlock, count };
  } catch (e) {
    traceStepError("syncMirrorFromEventsIncremental", t, e, { forceFull, lastSyncedBlock: state.meta.lastSyncedBlock || 0 });
    throw e;
  }
}

async function refreshMirrorEntryFromChain(id) {
  const raw = bodyString(id);
  if (!raw) throw new Error("missing_id");
  const idHash = rememberId(raw);
  const rec = await getRecordOnchain(raw);
  if (!rec.active) {
    delete state.mirror[idHash];
    delete state.leafCache[raw];
    delete state.pathCache[raw];
    saveState();
    return { idHash, active: false, rec: null };
  }
  state.mirror[idHash] = {
    idHash,
    cid: String(rec.cid),
    pkNormHash: String(rec.pkNormHash),
    pkRecHash: String(rec.pkRecHash),
    owner: getAddress(rec.owner),
    recovery: getAddress(rec.recovery),
    version: Number(rec.version),
    active: true,
  };
  saveState();
  return { idHash, active: true, rec: state.mirror[idHash] };
}
async function refreshKnownMirrorFromChain() {
  const ids = Object.keys(state.keys || {});
  for (const id of ids) {
    try { await refreshMirrorEntryFromChain(id); } catch (e) { console.warn(`[bb_service_zk] refresh known id failed (${id}): ${parseEthersError(e)}`); }
  }
  return { mode: "known_ids", count: ids.length };
}

async function pushRootIfChanged(reason = "auto") {
  const t = traceStepEnter("pushRootIfChanged", { reason, root: treeCache.rootHex });
  try {
    const tEpoch = traceStepEnter("contract.rootEpoch", { where: "pushRootIfChanged", reason });
    const onchainRoot = String(await contract.activeRoot());
    const onchainEpoch = Number(await contract.rootEpoch());
    traceStepExit("contract.rootEpoch", tEpoch, { where: "pushRootIfChanged", reason, onchainRoot, onchainEpoch });

    if (treeCache.rootHex.toLowerCase() === onchainRoot.toLowerCase()) {
      state.chainCache = { root: onchainRoot, epoch: onchainEpoch, checkedAt: nowMs(), block: Number(await provider.getBlockNumber().catch(() => state.chainCache.block || 0)) };
      traceStepExit("pushRootIfChanged", t, { reason, changed: false, root: treeCache.rootHex, epoch: onchainEpoch });
      return { changed: false, root: treeCache.rootHex, epoch: onchainEpoch, reason };
    }

    await hardhatSetBalance(rootUpdaterWallet.address, "1000");
    const tx = await sendContractTxLocked(
      rootUpdaterWallet,
      (ov) => rootUpdaterContract.setActiveRoot(treeCache.rootHex, onchainEpoch + 1, ov),
      { label: "setActiveRoot" }
    );
    const tWait = traceStepEnter("setActiveRoot.wait", { reason, txHash: tx && tx.hash ? String(tx.hash) : "", root: treeCache.rootHex, nextEpoch: onchainEpoch + 1 });
    const receipt = await tx.wait(1);
    traceStepExit("setActiveRoot.wait", tWait, { reason, txHash: tx && tx.hash ? String(tx.hash) : "", root: treeCache.rootHex, nextEpoch: onchainEpoch + 1 });
    state.chainCache = { root: treeCache.rootHex, epoch: onchainEpoch + 1, checkedAt: nowMs(), block: Number(await provider.getBlockNumber().catch(() => state.chainCache.block || 0)) };
    traceStepExit("pushRootIfChanged", t, { reason, changed: true, root: treeCache.rootHex, epoch: onchainEpoch + 1, gas: receipt && receipt.gasUsed ? receipt.gasUsed.toString() : "" });
    return { changed: true, root: treeCache.rootHex, epoch: onchainEpoch + 1, gas: receipt && receipt.gasUsed ? receipt.gasUsed.toString() : "", reason };
  } catch (e) {
    traceStepError("pushRootIfChanged", t, e, { reason, root: treeCache.rootHex });
    throw e;
  }
}

async function updateOperationReadinessFromCache(opId) {
  const op = state.ops[opId];
  if (!op) return null;
  const id = bodyString(op.id);
  const idHash = rememberId(id);
  let leaf = state.leafCache[id] || null;
  if ((!leaf || !leaf.active) && state.cacheBootstrapped && !state.cacheStale) {
    const rec = state.mirror[idHash];
    if (rec && rec.active) {
      const idx = treeCache.indexById[idHash];
      if (idx !== undefined) {
        const leafField = treeCache.leafFieldById[idHash] !== undefined ? treeCache.leafFieldById[idHash] : computeLeafFromMirror(rec);
        updateLeafCachesForId(id, idHash, rec, leafField);
        leaf = state.leafCache[id] || null;
      }
    }
  }
  let pathInfo = state.pathCache[id] || null;
  if (!pathInfo && leaf && leaf.active && state.cacheBootstrapped && !state.cacheStale) {
    pathInfo = recomputePathCacheForId(id);
  }
  const rootOk = !!state.rootCache.root && !!state.chainCache.root &&
    state.rootCache.root.toLowerCase() === state.chainCache.root.toLowerCase() &&
    Number(state.rootCache.epoch) === Number(state.chainCache.epoch);
  const ready = rootOk && !state.cacheStale && !!leaf && !!pathInfo && leaf.active === 1 &&
    Number(leaf.version) >= Number(op.minVersion || 0) &&
    bodyString(pathInfo.root).toLowerCase() === bodyString(state.rootCache.root).toLowerCase();
  const nextStatus = ready ? "READY" : (rootOk ? "ROOT_REBUILT" : (leaf ? "MIRRORED" : op.status));
  logInfo("op-refresh", {
    opId,
    id,
    recActive: leaf ? !!leaf.active : false,
    recVersion: leaf ? Number(leaf.version) : Number(op.currentVersion || 0),
    minVersion: Number(op.minVersion || 0),
    hasPath: !!pathInfo,
    treeRoot: treeCache.rootHex,
    chainEpoch: state.chainCache.epoch || 0,
    nextStatus,
  });
  updateOperation(opId, {
    idHash,
    currentVersion: leaf ? Number(leaf.version) : Number(op.currentVersion || 0),
    owner: leaf ? (state.mirror[idHash]?.owner || op.owner) : op.owner,
    recovery: leaf ? (state.mirror[idHash]?.recovery || op.recovery) : op.recovery,
    currentRoot: state.rootCache.root || treeCache.rootHex,
    currentEpoch: state.rootCache.epoch,
    status: nextStatus,
    ready,
    lastError: ready ? "" : (op.lastError || ""),
  });
  return state.ops[opId];
}
async function updateAllOperationReadinessFromCache() {
  const now = nowMs();
  pruneOldTerminalOps(now);
  for (const opId of Object.keys(state.ops || {})) {
    const op = state.ops[opId];
    if (!op) continue;
    if (shouldSkipTerminalOpRefresh(op, now)) continue;
    await updateOperationReadinessFromCache(opId);
  }
}

async function performRebuildPass(reason, opts = {}) {
  const t = traceStepEnter("performRebuildPass", { reason, fullSync: !!opts.fullSync, affected: (opts.affectedIds || []).length, pendingOps: (opts.opIds || []).length });
  try {
    if (opts.fullSync) {
      const syncInfo = await syncMirrorFromEventsIncremental(true);
      if (syncInfo.count === 0 && Object.keys(state.keys || {}).length > 0) {
        await refreshKnownMirrorFromChain();
      }
      await fullRebuildSparseTreeFromMirror([...(opts.affectedIds || []), ...(opts.opIds || []).map((opId) => (state.ops[opId] || {}).id || "")]);
    } else {
      await syncMirrorFromEventsIncremental(false);
      await incrementalUpdateTreeCacheFromMirror(opts.affectedIds || [], [ ...(opts.affectedIds || []), ...(opts.opIds || []).map((opId) => (state.ops[opId] || {}).id || "") ]);
    }

    const push = await pushRootIfChanged(reason);
    const latestBlock = Number(await provider.getBlockNumber().catch(() => state.chainCache.block || 0));
    if (!push.changed) {
      state.chainCache = { root: push.root, epoch: push.epoch, checkedAt: nowMs(), block: latestBlock };
    }
    state.rootCache = { root: treeCache.rootHex, epoch: Number(state.chainCache.epoch || push.epoch || 0), depth: TREE_DEPTH, builtAt: nowMs() };
    for (const id of Object.keys(state.pathCache || {})) {
      state.pathCache[id].root = state.rootCache.root;
      state.pathCache[id].epoch = state.rootCache.epoch;
    }
    state.cacheBootstrapped = true;
    state.cacheStale = !(state.rootCache.root.toLowerCase() === state.chainCache.root.toLowerCase() && Number(state.rootCache.epoch) === Number(state.chainCache.epoch));
    state.meta.rebuildSeq = Number(state.meta.rebuildSeq || 0) + 1;
    saveState();
    pruneOldTerminalOps();
    await updateAllOperationReadinessFromCache();
    traceStepExit("performRebuildPass", t, { reason, root: state.rootCache.root, epoch: state.rootCache.epoch, cacheStale: state.cacheStale, activeCount: treeCache.activeEntries.length });
    return { root: state.rootCache.root, epoch: state.rootCache.epoch, cacheStale: state.cacheStale };
  } catch (e) {
    traceStepError("performRebuildPass", t, e, { reason, fullSync: !!opts.fullSync });
    throw e;
  }
}

function appendUnique(arr, items) {
  const set = new Set(arr);
  for (const item of items) {
    const s = bodyString(item);
    if (s && !set.has(s)) { set.add(s); arr.push(s); }
  }
}
function scheduleRebuild(reason, affectedIds = [], opIds = [], opts = {}) {
  appendUnique(state.pendingAffectedIds, affectedIds);
  appendUnique(state.pendingOpIds, opIds);
  if (opts.fullSync) state.fullSyncRequested = true;
  if (state.rebuildInFlight) {
    state.rebuildDirty = true;
    logInfo("rebuild-queued", { reason, dirty: true, pendingAffected: state.pendingAffectedIds.length, pendingOpIds: state.pendingOpIds.length, fullSyncRequested: !!state.fullSyncRequested });
    return;
  }
  runRebuildWorker(reason).catch((e) => {
    logError("rebuild-worker-unhandled", e, { reason, pendingAffected: state.pendingAffectedIds.length, pendingOpIds: state.pendingOpIds.length, fullSyncRequested: !!state.fullSyncRequested });
  });
}
async function runRebuildWorker(reason = "manual") {
  if (state.rebuildInFlight) return;
  state.rebuildInFlight = true;
  state.cacheStale = true;
  saveState();
  logInfo("rebuild-start", { reason, pendingAffected: state.pendingAffectedIds.length, pendingOpIds: state.pendingOpIds.length, fullSyncRequested: !!state.fullSyncRequested });
  try {
    do {
      state.rebuildDirty = false;
      const affectedIds = [...new Set(state.pendingAffectedIds.splice(0))];
      const opIds = [...new Set(state.pendingOpIds.splice(0))];
      const fullSync = !!state.fullSyncRequested;
      state.fullSyncRequested = false;
      await performRebuildPass(reason, { fullSync, affectedIds, opIds });
    } while (state.rebuildDirty);
  } finally {
    state.rebuildInFlight = false;
    saveState();
    logInfo("rebuild-done", { reason, cacheStale: state.cacheStale, activeCount: treeCache.activeEntries.length, root: state.rootCache.root, epoch: state.rootCache.epoch });
  }
}
async function waitForRebuildDrain(timeoutMs = 300000) {
  const start = nowMs();
  while (state.rebuildInFlight || state.rebuildDirty) {
    if (nowMs() - start > timeoutMs) throw new Error("rebuild_timeout");
    await new Promise((r) => setTimeout(r, 50));
  }
}
async function waitForActiveRebuildClear(timeoutMs = 300000) {
  const start = nowMs();
  while (state.rebuildInFlight) {
    if (nowMs() - start > timeoutMs) throw new Error("rebuild_timeout");
    await new Promise((r) => setTimeout(r, 20));
  }
}

let inlineReadyRefreshSerial = Promise.resolve();
async function runInlineReadyRefresh(reason, affectedIds = [], opIds = [], opts = {}) {
  const task = async () => {
    await waitForActiveRebuildClear();
    state.cacheStale = true;
    saveState();
    return await performRebuildPass(`${reason}:inline`, { fullSync: !!opts.fullSync, affectedIds, opIds });
  };
  inlineReadyRefreshSerial = inlineReadyRefreshSerial.then(task, task);
  return inlineReadyRefreshSerial;
}

function ttssMetaMatchesLocalEntry(entry, ver, epoch, vkSetHash, metaHash) {
  if (!entry) return false;
  const gotVk = bodyString(entry.vkSetHash).toLowerCase();
  const gotMeta = bodyString(entry.metaHash).toLowerCase();
  return Number(entry.ver) === Number(ver) &&
         Number(entry.epoch) === Number(epoch) &&
         gotVk === bodyString(vkSetHash).toLowerCase() &&
         gotMeta === bodyString(metaHash).toLowerCase();
}
function buildTTSSMetaEffective(entry, fallback = {}) {
  const base = entry || fallback || {};
  return {
    id: bodyString(base.id || fallback.id || ""),
    idHash: bodyString(base.idHash || fallback.idHash || ""),
    ver: Number(base.ver !== undefined ? base.ver : (fallback.ver !== undefined ? fallback.ver : 0)),
    epoch: Number(base.epoch !== undefined ? base.epoch : (fallback.epoch !== undefined ? fallback.epoch : 0)),
    vkSetHash: bodyString(base.vkSetHash || fallback.vkSetHash || ""),
    metaHash: bodyString(base.metaHash || fallback.metaHash || ""),
    txHash: bodyString(base.txHash || fallback.txHash || ""),
    requestKey: bodyString(base.requestKey || fallback.requestKey || ""),
  };
}

async function maybeInlineTTSSMeta({ body, id, idHash, ver, actualEpoch, mergeRequestKind }) {
  const base = { ttssMerged: false, ttssVkSetHash: "", ttssMetaHash: "", ttssMetaTxHash: "", ttssEpoch: 0 };
  const waitInline = String(body.ttssWait ?? "1") === "1";
  if (!waitInline || !body.ttssVkSetHash || !body.ttssMetaHash) return base;

  const ttssVk = ensure0x64(body.ttssVkSetHash);
  const ttssMeta = ensure0x64(body.ttssMetaHash);
  const ttssEpochHint = body.ttssEpochHint !== undefined ? Number(body.ttssEpochHint) : Number(actualEpoch);
  if (!ttssVk || !ttssMeta || !Number.isFinite(actualEpoch) || actualEpoch < 0) return base;
  if (Number.isFinite(ttssEpochHint) && actualEpoch !== ttssEpochHint) return base;

  const existingLocal = state.ttssMeta[idHash] || null;
  const existingVk = existingLocal && existingLocal.vkSetHash ? String(existingLocal.vkSetHash) : "";
  const existingMeta = existingLocal && existingLocal.metaHash ? String(existingLocal.metaHash) : "";
  if (existingLocal && Number(existingLocal.ver) === Number(ver) && Number(existingLocal.epoch) === Number(actualEpoch) &&
      existingVk.toLowerCase() === ttssVk.toLowerCase() && existingMeta.toLowerCase() === ttssMeta.toLowerCase()) {
    return {
      ttssMerged: true,
      ttssVkSetHash: ttssVk,
      ttssMetaHash: ttssMeta,
      ttssMetaTxHash: bodyString(existingLocal.txHash || ""),
      ttssEpoch: Number(actualEpoch),
    };
  }

  const ttssReqKey = defaultRequestKey(mergeRequestKind, id || idHash, body.ttssRequestId, `${ver}`);
  const ttssTx = await sendContractTxLocked(
    rootUpdaterWallet,
    (ov) => rootUpdaterContract.setTTSSMeta(idHash, BigInt(ver), BigInt(actualEpoch), ttssVk, ttssMeta, ov),
    { label: `${mergeRequestKind || "ttss_meta"}:inline` }
  );
  const ttssConfs = Math.max(1, parseInt(String(body.ttssConfirmations ?? "1"), 10));
  const ttssReceipt = await ttssTx.wait(ttssConfs);
  const ttssMetaTxHash = ttssReceipt && ttssReceipt.hash ? String(ttssReceipt.hash) : (ttssTx && ttssTx.hash ? String(ttssTx.hash) : "");
  storeTTSSMetaLocal({
    id: id || state.idByHash[idHash] || "",
    idHash,
    ver: Number(ver),
    epoch: Number(actualEpoch),
    vkSetHash: ttssVk,
    metaHash: ttssMeta,
    txHash: ttssMetaTxHash,
    requestKey: ttssReqKey,
  });
  return {
    ttssMerged: true,
    ttssVkSetHash: ttssVk,
    ttssMetaHash: ttssMeta,
    ttssMetaTxHash,
    ttssEpoch: Number(actualEpoch),
  };
}

function scheduleDeferredTTSSMetaAttach({ body, id, idHash, ver, actualEpoch, mergeRequestKind = "ttss_meta" }) {
  const base = { ttssMerged: false, ttssVkSetHash: "", ttssMetaHash: "", ttssMetaTxHash: "", ttssEpoch: 0, ttssMergeScheduled: 0 };
  const waitInline = String(body.ttssWait ?? "1") === "1";
  if (!waitInline || !body.ttssVkSetHash || !body.ttssMetaHash) return base;

  const ttssVk = ensure0x64(body.ttssVkSetHash);
  const ttssMeta = ensure0x64(body.ttssMetaHash);
  const ttssEpochHint = body.ttssEpochHint !== undefined ? Number(body.ttssEpochHint) : Number(actualEpoch);
  if (!ttssVk || !ttssMeta || !Number.isFinite(actualEpoch) || actualEpoch < 0) return base;
  if (Number.isFinite(ttssEpochHint) && actualEpoch !== ttssEpochHint) return base;

  const ttssConfs = Math.max(1, parseInt(String(body.ttssConfirmations ?? "1"), 10));
  const requestKey = defaultRequestKey(mergeRequestKind, id || idHash, body.ttssRequestId, `${ver}`);
  const existingLocal = state.ttssMeta[idHash] || null;
  if (ttssMetaMatchesLocalEntry(existingLocal, ver, actualEpoch, ttssVk, ttssMeta)) {
    return {
      ttssMerged: true,
      ttssVkSetHash: ttssVk,
      ttssMetaHash: ttssMeta,
      ttssMetaTxHash: bodyString(existingLocal.txHash || ""),
      ttssEpoch: Number(actualEpoch),
      ttssMergeScheduled: 0,
    };
  }

  const existing = getOpByRequestKey(requestKey);
  if (existing && existing.status !== "FAILED") {
    return { ...base, ttssMergeScheduled: 1 };
  }

  const precheck = shouldDegradeDeferredTTSSAttach("schedule");
  if (precheck.degrade) {
    console.log(`[bb_service_zk] deferredTTSS degraded stage=schedule queue_busy=${precheck.queueBusy ? 1 : 0} recovery_busy=${precheck.recoveryBusy ? 1 : 0} deferred_busy=${precheck.tooManyDeferred ? 1 : 0} active=${precheck.active} queued=${precheck.queued}`);
    return { ...base, ttssMergeScheduled: 0 };
  }

  const op = createOperation("ttss_meta", id || idHash, requestKey, {
    status: "ACCEPTED",
    idHash,
    minVersion: Number(ver),
    targetVersion: Number(ver),
    currentVersion: Number(ver),
    currentRoot: state.rootCache.root,
    currentEpoch: state.rootCache.epoch,
  });

  deferredTTSSAttachInFlight += 1;
  void (async () => {
    try {
      if (DEFERRED_TTSS_ATTACH_START_DELAY_MS > 0) {
        await new Promise((r) => setTimeout(r, DEFERRED_TTSS_ATTACH_START_DELAY_MS));
      }
      const recheck = shouldDegradeDeferredTTSSAttach("run");
      if (recheck.queueBusy || recheck.recoveryBusy) {
        updateOperation(op.opId, { status: "FAILED", ready: false, lastError: `deferred_degraded queue_busy=${recheck.queueBusy ? 1 : 0} recovery_busy=${recheck.recoveryBusy ? 1 : 0}` });
        console.log(`[bb_service_zk] deferredTTSS skipped stage=run queue_busy=${recheck.queueBusy ? 1 : 0} recovery_busy=${recheck.recoveryBusy ? 1 : 0} active=${recheck.active} queued=${recheck.queued}`);
        return;
      }
      const tx = await sendContractTxLocked(
        rootUpdaterWallet,
        (ov) => rootUpdaterContract.setTTSSMeta(idHash, BigInt(ver), BigInt(actualEpoch), ttssVk, ttssMeta, ov),
        { label: `${mergeRequestKind || "ttss_meta"}:deferred` }
      );
      updateOperation(op.opId, { txHash: tx && tx.hash ? String(tx.hash) : "", submitMs: 0 });
      const receipt = await tx.wait(ttssConfs);
      const txHash = receipt && receipt.hash ? String(receipt.hash) : (tx && tx.hash ? String(tx.hash) : "");
      storeTTSSMetaLocal({
        id: id || state.idByHash[idHash] || "",
        idHash,
        ver: Number(ver),
        epoch: Number(actualEpoch),
        vkSetHash: ttssVk,
        metaHash: ttssMeta,
        txHash,
        requestKey,
      });
      updateOperation(op.opId, {
        status: "READY",
        ready: true,
        txHash,
        confirmMs: 0,
        currentVersion: Number(ver),
        currentRoot: state.rootCache.root,
        currentEpoch: state.rootCache.epoch,
      });
      logInfo("ttss-meta-deferred-done", { id, idHash, ver: Number(ver), epoch: Number(actualEpoch), txHash, requestKey });
    } catch (e) {
      failOperation(op.opId, parseEthersError(e));
      console.error(`[bb_service_zk] deferred ttss meta attach failed (${id || idHash}): ${parseEthersError(e)}`);
    } finally {
      deferredTTSSAttachInFlight = Math.max(0, deferredTTSSAttachInFlight - 1);
    }
  })();

  return { ...base, ttssMergeScheduled: 1 };
}


async function waitForTxAndRefresh(tx, opts = {}) {
  const confirmations = Math.max(1, Number(opts.confirmations || 1));
  const reason = opts.reason || "tx_wait";
  const id = bodyString(opts.id || "");
  const opId = bodyString(opts.opId || "");
  const onConfirmed = typeof opts.onConfirmed === "function" ? opts.onConfirmed : null;
  const onMirrorPatch = typeof opts.onMirrorPatch === "function" ? opts.onMirrorPatch : null;
  const awaitReady = !!opts.awaitReady;

  const t0 = nowMs();
  const receipt = await tx.wait(confirmations);
  if (opId) {
    const _updated = updateOperation(opId, {
      status: "ONCHAIN_CONFIRMED",
      txHash: receipt && receipt.hash ? String(receipt.hash) : (tx && tx.hash ? String(tx.hash) : ""),
      confirmMs: nowMs() - t0,
    });
    logInfo("op-settle", { opId, id, stage: "ONCHAIN_CONFIRMED", txHash: receipt && receipt.hash ? String(receipt.hash) : (tx && tx.hash ? String(tx.hash) : ""), treeRoot: treeCache.rootHex, currentRoot: _updated && _updated.currentRoot ? _updated.currentRoot : "", currentEpoch: _updated && _updated.currentEpoch !== undefined ? _updated.currentEpoch : null });
  }
  if (onConfirmed) await onConfirmed(receipt);

  if (id) {
    if (onMirrorPatch) {
      await onMirrorPatch(receipt);
    } else {
      await refreshMirrorEntryFromChain(id);
    }
    if (opId) {
      const _updated = updateOperation(opId, {
        status: "MIRRORED",
        currentRoot: treeCache.rootHex,
        currentEpoch: state.rootCache.epoch || state.chainCache.epoch || 0,
      });
      logInfo("op-settle", { opId, id, stage: "MIRRORED", treeRoot: treeCache.rootHex, currentRoot: _updated && _updated.currentRoot ? _updated.currentRoot : "", currentEpoch: _updated && _updated.currentEpoch !== undefined ? _updated.currentEpoch : null });
    }
  }

  let recompute = { root: state.rootCache.root || treeCache.rootHex, epoch: state.rootCache.epoch || state.chainCache.epoch || 0 };
  if (awaitReady) {
    recompute = await runInlineReadyRefresh(reason, id ? [id] : [], opId ? [opId] : [], { fullSync: false });
    if (opId) {
      updateOperation(opId, {
        status: state.ops[opId] && state.ops[opId].ready ? "READY" : (recompute.root ? "ROOT_REBUILT" : state.ops[opId].status),
        currentRoot: recompute.root || state.rootCache.root,
        currentEpoch: recompute.epoch !== undefined ? recompute.epoch : state.rootCache.epoch,
      });
      await updateOperationReadinessFromCache(opId);
    }
  } else {
    scheduleRebuild(reason, id ? [id] : [], opId ? [opId] : [], { fullSync: false });
  }
  recompute = { root: recompute.root || state.rootCache.root || treeCache.rootHex, epoch: recompute.epoch !== undefined ? recompute.epoch : (state.rootCache.epoch || state.chainCache.epoch || 0) };
  return { receipt, recompute };
}
function scheduleTxSettlement(tx, opts = {}) {
  void waitForTxAndRefresh(tx, opts).catch((e) => {
    const reason = opts.reason || "tx_wait";
    console.error(`[bb_service_zk] async tx settle failed (${reason}): ${parseEthersError(e)}`);
    const opId = bodyString(opts.opId || "");
    if (opId) updateOperation(opId, { lastError: parseEthersError(e) });
  });
}

function getOrCreateKeySlot(id) {
  const raw = bodyString(id);
  if (!raw) throw new Error("missing_id");
  state.keys[raw] = state.keys[raw] || {};
  rememberId(raw);
  return state.keys[raw];
}
async function initPoseidon() {
  poseidon = await circomlib.buildPoseidon();
  F = poseidon.F;
  treeCache.zeroes = buildZeroes(TREE_DEPTH);
}
async function bootstrap() {
  await initPoseidon();
  try {
    const onchainUpdater = getAddress(await contract.rootUpdater());
    if (onchainUpdater !== getAddress(rootUpdaterWallet.address)) {
      console.warn(`[bb_service_zk] rootUpdater on-chain is ${onchainUpdater}, local signer is ${rootUpdaterWallet.address}`);
    }
  } catch (e) {
    console.warn(`[bb_service_zk] rootUpdater check failed: ${parseEthersError(e)}`);
  }

  state.fullSyncRequested = true;
  await runRebuildWorker("bootstrap");
  console.log(`[bb_service_zk] ready, root=${treeCache.rootHex}, depth=${TREE_DEPTH}`);

  if (!AUTO_REBUILD) return;

  contract.on(contract.filters.RegisteredZK(), async (...args) => {
    try {
      const ev = args[args.length - 1];
      if (ev && ev.args) {
        applyRegisteredArgsToMirror(ev.args);
        saveState();
        logInfo("event-hint", { type: "RegisteredZK", idHash: String(ev.args.idHash), note: "mirror_applied_no_direct_rebuild" });
      }
    } catch (e) { console.error("[bb_service_zk] event RegisteredZK failed:", e); }
  });
  contract.on(contract.filters.UpdatedZK(), async (...args) => {
    try {
      const ev = args[args.length - 1];
      if (ev && ev.args) {
        applyUpdatedArgsToMirror(ev.args);
        saveState();
        logInfo("event-hint", { type: "UpdatedZK", idHash: String(ev.args.idHash), note: "mirror_applied_no_direct_rebuild" });
      }
    } catch (e) { console.error("[bb_service_zk] event UpdatedZK failed:", e); }
  });
  contract.on(contract.filters.RecoveredZK(), async (...args) => {
    try {
      const ev = args[args.length - 1];
      if (ev && ev.args) {
        applyRecoveredArgsToMirror(ev.args);
        saveState();
        logInfo("event-hint", { type: "RecoveredZK", idHash: String(ev.args.idHash), note: "mirror_applied_no_direct_rebuild" });
      }
    } catch (e) { console.error("[bb_service_zk] event RecoveredZK failed:", e); }
  });

  setInterval(async () => {
    if (state.rebuildInFlight) return;
    try {
      const latest = Number(await provider.getBlockNumber());
      const chainRoot = String(await contract.activeRoot());
      const chainEpoch = Number(await contract.rootEpoch());
      state.chainCache = { root: chainRoot, epoch: chainEpoch, checkedAt: nowMs(), block: latest };
      const behindBlocks = latest > Number(state.meta.lastSyncedBlock || 0);
      const rootMismatch = bodyString(chainRoot).toLowerCase() !== bodyString(state.rootCache.root || treeCache.rootHex).toLowerCase();
      if (behindBlocks || rootMismatch) {
        const fullSync = !!rootMismatch;
        logInfo("sanity-sync", { latest, lastSyncedBlock: state.meta.lastSyncedBlock || 0, behindBlocks, rootMismatch, fullSync, chainRoot, cachedRoot: state.rootCache.root || treeCache.rootHex });
        pruneOldTerminalOps();
        scheduleRebuild("sanity_sync", [], [], { fullSync });
      }
    } catch (e) {
      console.warn(`[bb_service_zk] sanity sync failed: ${parseEthersError(e)}`);
    }
  }, SANITY_MS).unref?.();
}

const app = express();
app.use(express.json({ limit: "2mb" }));

function parseMinVersionInput(raw) {
  const n = Number.parseInt(String(raw ?? "0"), 10);
  return Number.isFinite(n) && n > 0 ? n : 0;
}

function getCachedSnapshotState(rawId, rawMinVersion) {
  const id = bodyString(rawId || "");
  const minVersion = parseMinVersionInput(rawMinVersion);
  if (!id) {
    return {
      id: "",
      idHash: "",
      minVersion,
      root: bodyString(state.rootCache.root || treeCache.rootHex),
      epoch: Number(state.rootCache.epoch || 0),
      depth: TREE_DEPTH,
      pathRoot: "",
      pathElements: [],
      pathIndex: [],
      leaf: null,
      pathInfo: null,
      leafOk: false,
      leafActive: false,
      leafVersion: 0,
      rootMatches: false,
      pathAvailable: false,
      ready: false,
    };
  }

  const idHash = rememberId(id);
  let leaf = state.leafCache[id];
  if ((!leaf || !leaf.active) && state.cacheBootstrapped && !state.cacheStale) {
    const rec = state.mirror[idHash];
    if (rec && rec.active) {
      const idx = treeCache.indexById[idHash];
      if (idx !== undefined) {
        const leafField = treeCache.leafFieldById[idHash] !== undefined ? treeCache.leafFieldById[idHash] : computeLeafFromMirror(rec);
        updateLeafCachesForId(id, idHash, rec, leafField);
        leaf = state.leafCache[id];
      }
    }
  }

  let pathInfo = state.pathCache[id];
  if (!pathInfo && leaf && leaf.active && state.cacheBootstrapped && !state.cacheStale) {
    pathInfo = recomputePathCacheForId(id);
  }

  const root = bodyString(state.rootCache.root || treeCache.rootHex);
  const epoch = Number(state.rootCache.epoch || 0);
  const depth = TREE_DEPTH;
  const pathRoot = pathInfo && bodyString(pathInfo.root) ? bodyString(pathInfo.root) : "";
  const pathElements = pathInfo && Array.isArray(pathInfo.pathElements) ? pathInfo.pathElements : [];
  const pathIndex = pathInfo && Array.isArray(pathInfo.pathIndex) ? pathInfo.pathIndex : [];
  const leafOk = !!leaf && !!bodyString(leaf.leaf);
  const leafActive = leafOk && !!leaf.active;
  const leafVersion = leafOk ? Number(leaf.version || 0) : 0;
  const rootMatches = !!pathRoot && pathRoot.toLowerCase() === root.toLowerCase();
  const pathAvailable = pathElements.length > 0 && pathIndex.length > 0;
  const versionSatisfied = leafVersion >= minVersion;
  const ready = rootMatches && leafOk && leafActive && versionSatisfied && pathAvailable;

  return {
    id,
    idHash,
    minVersion,
    root,
    epoch,
    depth,
    pathRoot,
    pathElements,
    pathIndex,
    leaf,
    pathInfo,
    leafOk,
    leafActive,
    leafVersion,
    rootMatches,
    pathAvailable,
    ready,
  };
}

function snapshotNeedsRepair(cached, rec, rawMinVersion) {
  const minVersion = parseMinVersionInput(rawMinVersion);
  const recordObserved = !!rec;
  const recordActive = recordObserved && !!rec.active;
  const recordVersion = recordObserved ? Number(rec.version || 0) : 0;
  const targetVersion = Math.max(minVersion, recordActive ? recordVersion : 0);
  const cachedVersion = Number(cached && cached.leafVersion ? cached.leafVersion : 0);
  const versionLag = targetVersion > 0 && cachedVersion < targetVersion;
  const cacheMissing = !cached || !cached.leafOk || !cached.leafActive || !cached.pathAvailable;
  const rootMismatch = !!(cached && cached.pathRoot) && bodyString(cached.pathRoot).toLowerCase() !== bodyString(cached.root).toLowerCase();
  const shouldHeal = (!!state.cacheBootstrapped && state.cacheStale) || (recordActive && (versionLag || cacheMissing || rootMismatch));
  return { shouldHeal, targetVersion, recordObserved, recordActive, recordVersion };
}

async function forceRefreshSnapshotForId(rawId, rawMinVersion, reason = "snapshot_repair") {
  const id = bodyString(rawId || "");
  const minVersion = parseMinVersionInput(rawMinVersion);
  if (!id) return null;
  const task = async () => {
    await waitForActiveRebuildClear();
    state.cacheStale = true;
    saveState();
    await refreshMirrorEntryFromChain(id);
    await incrementalUpdateTreeCacheFromMirror([id], [id]);
    const push = await pushRootIfChanged(reason);
    const latestBlock = Number(await provider.getBlockNumber().catch(() => state.chainCache.block || 0));
    if (!push.changed) {
      state.chainCache = { root: push.root, epoch: push.epoch, checkedAt: nowMs(), block: latestBlock };
    }
    state.rootCache = { root: treeCache.rootHex, epoch: Number(state.chainCache.epoch || push.epoch || 0), depth: TREE_DEPTH, builtAt: nowMs() };
    for (const cachedId of Object.keys(state.pathCache || {})) {
      if (cachedId === id) continue;
      state.pathCache[cachedId].root = state.rootCache.root;
      state.pathCache[cachedId].epoch = state.rootCache.epoch;
    }
    const repairedPath = recomputePathCacheForId(id);
    if (repairedPath) {
      repairedPath.root = state.rootCache.root;
      repairedPath.epoch = state.rootCache.epoch;
    }
    state.cacheBootstrapped = true;
    state.cacheStale = !(state.rootCache.root.toLowerCase() === state.chainCache.root.toLowerCase() && Number(state.rootCache.epoch) === Number(state.chainCache.epoch));
    saveState();
    return getCachedSnapshotState(id, minVersion);
  };
  inlineReadyRefreshSerial = inlineReadyRefreshSerial.then(task, task);
  return await inlineReadyRefreshSerial;
}

async function ensureSnapshotFreshForVersion(rawId, rawMinVersion, reason = "snapshot_observe", recOverride = undefined) {
  const id = bodyString(rawId || "");
  const minVersion = parseMinVersionInput(rawMinVersion);
  if (!id) return { rec: null, cached: getCachedSnapshotState(id, minVersion), healed: false, targetVersion: minVersion };

  let rec = recOverride;
  if (rec === undefined) {
    try {
      rec = await getRecordOnchain(id);
    } catch (_) {
      rec = null;
    }
  }

  let cached = getCachedSnapshotState(id, minVersion);
  let check = snapshotNeedsRepair(cached, rec, minVersion);
  let healed = false;
  if (check.shouldHeal) {
    cached = await forceRefreshSnapshotForId(id, check.targetVersion, `${reason}:force_refresh`) || getCachedSnapshotState(id, minVersion);
    healed = true;
    check = snapshotNeedsRepair(cached, rec, minVersion);
    if (check.shouldHeal && !state.cacheStale) {
      cached = await forceRefreshSnapshotForId(id, check.targetVersion, `${reason}:force_refresh_retry`) || getCachedSnapshotState(id, minVersion);
    }
  }
  return { rec, cached: getCachedSnapshotState(id, minVersion), healed, targetVersion: check.targetVersion };
}

async function buildReadySnapshotPayload(rawId, rawMinVersion) {
  const id = bodyString(rawId || "");
  const minVersion = parseMinVersionInput(rawMinVersion);
  if (!id) return { httpStatus: 400, payload: { ok: 0, err: "missing_id" } };
  if (!state.cacheBootstrapped) return { httpStatus: 409, payload: { ok: 0, err: "bootstrap_not_ready", cacheStale: state.cacheStale ? 1 : 0 } };

  const ensured = await ensureSnapshotFreshForVersion(id, minVersion, "http:/readySnapshot");
  if (state.cacheStale) return { httpStatus: 409, payload: { ok: 0, err: "cache_stale", cacheStale: 1 } };

  const rec = ensured.rec;
  const cached = ensured.cached;
  const shortHex = (v) => {
    const s = bodyString(v || "");
    return s.length <= 18 ? s : `${s.slice(0, 10)}...${s.slice(-6)}`;
  };
  const recordObserved = !!rec;
  const recordActive = recordObserved && !!rec.active;
  const recordVersion = recordObserved ? Number(rec.version || 0) : 0;
  const recordVersionSatisfied = recordVersion >= minVersion;
  const diag = [
    `rootMatches=${cached.rootMatches ? 1 : 0}`,
    `leafOk=${cached.leafOk ? 1 : 0}`,
    `leafActive=${cached.leafActive ? 1 : 0}`,
    `leafVersion=${cached.leafVersion}`,
    `minVersion=${minVersion}`,
    `pathAvailable=${cached.pathAvailable ? 1 : 0}`,
    `recordObserved=${recordObserved ? 1 : 0}`,
    `recordActive=${recordActive ? 1 : 0}`,
    `recordVersion=${recordVersion}`,
    `recordVersionSatisfied=${recordVersionSatisfied ? 1 : 0}`,
    `root=${shortHex(cached.root)}`,
    `pathRoot=${shortHex(cached.pathRoot)}`,
    `healed=${ensured.healed ? 1 : 0}`,
  ].join(" ");

  const payload = {
    ok: 1,
    id,
    idHash: cached.idHash,
    ready: cached.ready ? 1 : 0,
    cacheStale: 0,
    minVersion,
    root: cached.root,
    epoch: cached.epoch,
    depth: cached.depth,
    pathRoot: cached.pathRoot,
    pathElements: cached.pathElements,
    pathIndex: cached.pathIndex,
    leafOk: cached.leafOk ? 1 : 0,
    active: cached.leafActive ? 1 : 0,
    version: cached.leafVersion,
    leaf: cached.leafOk ? bodyString(cached.leaf.leaf) : "",
    cid: cached.leaf && cached.leaf.cid ? bodyString(cached.leaf.cid) : "",
    pkNormHash: cached.leaf && cached.leaf.pkNormHash ? bodyString(cached.leaf.pkNormHash) : "",
    pkRecHash: cached.leaf && cached.leaf.pkRecHash ? bodyString(cached.leaf.pkRecHash) : "",
    rootMatches: cached.rootMatches ? 1 : 0,
    recordObserved: recordObserved ? 1 : 0,
    recordActive: recordActive ? 1 : 0,
    recordVersion,
    healed: ensured.healed ? 1 : 0,
    diag,
  };

  if (recordObserved) {
    payload.recordOwner = rec.owner || "";
    payload.recordRecovery = rec.recovery || "";
    payload.recordPkNormHash = rec.pkNormHash || "";
    payload.recordPkRecHash = rec.pkRecHash || "";
  }
  return { httpStatus: 200, payload };
}

function buildInlineReadySnapshotFromCache(rawId, rawMinVersion) {
  const id = bodyString(rawId || "");
  const minVersion = parseMinVersionInput(rawMinVersion);
  if (!id) return { snapshotReady: 0, snapshotDiag: "missing_id" };
  if (!state.cacheBootstrapped) return { snapshotReady: 0, snapshotDiag: "bootstrap_not_ready" };
  if (state.cacheStale) return { snapshotReady: 0, snapshotDiag: "cache_stale" };

  const cached = getCachedSnapshotState(id, minVersion);
  return {
    snapshotReady: cached.ready ? 1 : 0,
    snapshotRoot: cached.root,
    snapshotEpoch: cached.epoch,
    snapshotVersion: cached.leafVersion,
    snapshotLeaf: cached.leafOk ? bodyString(cached.leaf.leaf) : "",
    snapshotPathRoot: cached.pathRoot,
    snapshotRootMatches: cached.rootMatches ? 1 : 0,
    snapshotDiag: `rootMatches=${cached.rootMatches ? 1 : 0} leafOk=${cached.leafOk ? 1 : 0} leafActive=${cached.leafActive ? 1 : 0} leafVersion=${cached.leafVersion} minVersion=${minVersion} pathAvailable=${cached.pathAvailable ? 1 : 0}`,
  };
}

app.get("/health", async (_req, res) => {
  try {
    res.type("text/plain").send(semicolonKV({
      ok: 1,
      root: state.rootCache.root || treeCache.rootHex,
      chainRoot: state.chainCache.root || state.rootCache.root || treeCache.rootHex,
      epoch: Number(state.rootCache.epoch || 0),
      depth: TREE_DEPTH,
      active: treeCache.activeEntries.length,
      cache_stale: state.cacheStale ? 1 : 0,
      rebuild_in_flight: state.rebuildInFlight ? 1 : 0,
    }));
  } catch (e) {
    res.type("text/plain").send(semicolonKV({ ok: 0, err: "health_fail", detail: parseEthersError(e) }));
  }
});
app.get("/root", async (_req, res) => {
  try {
    if (!state.cacheBootstrapped) return res.status(409).type("text/plain").send(semicolonKV({ ok: 0, err: "bootstrap_not_ready" }));
    if (state.cacheStale) return res.status(409).type("text/plain").send(semicolonKV({ ok: 0, err: "cache_stale" }));
    res.type("text/plain").send(semicolonKV({ ok: 1, root: state.rootCache.root || treeCache.rootHex, epoch: Number(state.rootCache.epoch || 0), depth: TREE_DEPTH }));
  } catch (e) {
    res.type("text/plain").send(semicolonKV({ ok: 0, err: "root_fail", detail: parseEthersError(e) }));
  }
});
app.get("/record", async (req, res) => {
  try {
    const id = bodyString(req.query.id || "");
    if (!id) return res.type("text/plain").send("ok=0;err=missing_id");
    const rec = await getRecordOnchain(id);
    res.type("text/plain").send(semicolonKV({ ok: 1, cid: rec.cid, owner: rec.owner, recovery: rec.recovery, version: rec.version, active: rec.active ? 1 : 0, pkNormHash: rec.pkNormHash, pkRecHash: rec.pkRecHash }));
  } catch (e) {
    res.type("text/plain").send(semicolonKV({ ok: 0, err: "record_fail", detail: parseEthersError(e) }));
  }
});
app.get("/leaf", async (req, res) => {
  try {
    const id = bodyString(req.query.id || "");
    const minVersion = parseMinVersionInput(req.query.minVersion);
    if (!id) return res.status(400).json({ ok: 0, err: "missing_id" });
    if (!state.cacheBootstrapped) return res.status(409).json({ ok: 0, err: "cache_stale" });
    const ensured = await ensureSnapshotFreshForVersion(id, minVersion, "http:/leaf");
    if (state.cacheStale) return res.status(409).json({ ok: 0, err: "cache_stale" });
    const cached = ensured.cached;
    const leaf = cached.leaf;
    if (!leaf || !leaf.active) return res.status(404).json({ ok: 0, err: "not_ready" });
    return res.json({ ok: 1, id, ...leaf, root: cached.root, epoch: cached.epoch, healed: ensured.healed ? 1 : 0 });
  } catch (e) {
    return res.status(500).json({ ok: 0, err: "leaf_fail", detail: parseEthersError(e) });
  }
});
app.get("/path", async (req, res) => {
  try {
    const id = bodyString(req.query.id || "");
    const minVersion = parseMinVersionInput(req.query.minVersion);
    if (!id) return res.status(400).json({ ok: 0, err: "missing_id" });
    if (!state.cacheBootstrapped) return res.status(409).json({ ok: 0, err: "cache_stale" });
    const ensured = await ensureSnapshotFreshForVersion(id, minVersion, "http:/path");
    if (state.cacheStale) return res.status(409).json({ ok: 0, err: "cache_stale" });
    const cached = ensured.cached;
    const pathInfo = cached.pathInfo || state.pathCache[id] || null;
    if (!pathInfo) return res.status(404).json({ ok: 0, err: "path_not_ready", idHash: rememberId(id) });
    return res.json({ ok: 1, id, ...pathInfo, root: cached.root, epoch: cached.epoch, healed: ensured.healed ? 1 : 0 });
  } catch (e) {
    return res.status(500).json({ ok: 0, err: "path_fail", detail: parseEthersError(e) });
  }
});
app.get("/readySnapshot", async (req, res) => {
  try {
    const { httpStatus, payload } = await buildReadySnapshotPayload(req.query.id, req.query.minVersion);
    return res.status(httpStatus).json(payload);
  } catch (e) {
    return res.status(500).json({ ok: 0, err: "ready_snapshot_fail", detail: parseEthersError(e) });
  }
});
app.post("/recomputeRoot", async (_req, res) => {
  const t0 = nowMs();
  try {
    scheduleRebuild("http:/recomputeRoot", [], [], { fullSync: true });
    await waitForRebuildDrain();
    return res.json({ ok: 1, total_ms: nowMs() - t0, root: state.rootCache.root, epoch: state.rootCache.epoch, cache_stale: state.cacheStale ? 1 : 0 });
  } catch (e) {
    return res.status(500).json({ ok: 0, err: "recompute_fail", detail: parseEthersError(e) });
  }
});
app.get("/registerStatus", async (req, res) => {
  try {
    const opId = bodyString(req.query.opId || "");
    const requestKey = bodyString(req.query.requestKey || "");
    const op = getOpByLookup(opId, requestKey);
    if (!op) return res.status(404).json({ ok: 0, err: "op_not_found" });
    const fresh = await updateOperationReadinessFromCache(op.opId);
    logInfo("op-status-response", { opId: fresh.opId, id: fresh.id, status: fresh.status, ready: fresh.ready ? 1 : 0, currentRoot: fresh.currentRoot || "", currentEpoch: fresh.currentEpoch ?? null, lastError: fresh.lastError || "", cache_stale: state.cacheStale ? 1 : 0, rebuild_in_flight: state.rebuildInFlight ? 1 : 0 });
    return res.json({
      ok: 1,
      accepted: 1,
      opId: fresh.opId,
      requestKey: fresh.requestKey,
      kind: fresh.kind,
      id: fresh.id,
      status: fresh.status,
      ready: fresh.ready ? 1 : 0,
      txHash: fresh.txHash || "",
      owner: fresh.owner || "",
      recovery: fresh.recovery || "",
      version: fresh.currentVersion ?? fresh.minVersion ?? 0,
      root: fresh.currentRoot || state.rootCache.root || treeCache.rootHex,
      epoch: fresh.currentEpoch !== undefined ? fresh.currentEpoch : Number(state.rootCache.epoch || 0),
      cache_stale: state.cacheStale ? 1 : 0,
      lastError: fresh.lastError || "",
      acceptedAt: fresh.acceptedAt,
      updatedAt: fresh.updatedAt,
    });
  } catch (e) {
    return res.status(500).json({ ok: 0, err: "register_status_fail", detail: parseEthersError(e) });
  }
});

app.post("/registerZk", async (req, res) => {
  const t0 = nowMs();
  try {
    const body = req.body || {};
    const id = bodyString(body.id);
    const cidHex = ensure0x64(body.cidHex);
    const pkNormHash = ensure0x64(body.pkNormHash);
    const pkRecHash = ensure0x64(body.pkRecHash);
    const ownerSeedHex = ensure0x64(body.ownerSeedHex);
    const recoverySeedHex = ensure0x64(body.recoverySeedHex);
    const waitRaw = (body.wait ?? "0");
    const waitConfirm = String(waitRaw) === "1";
    const confirmations = Math.max(1, parseInt(String(body.confirmations ?? "1"), 10));
    const includeSnapshot = String(body.includeSnapshot ?? "0") === "1";
    const ttssSetupModeRaw = bodyString(body.ttssSetupMode || "deferred");
    const ttssSetupMode = ttssSetupModeRaw === "inline" || ttssSetupModeRaw === "off" ? ttssSetupModeRaw : "deferred";
    const requestKey = defaultRequestKey("register", id, body.requestId);

    if (!id) return res.type("text/plain").send("ok=0;err=missing_id");
    if (!cidHex || !pkNormHash || !pkRecHash) return res.type("text/plain").send("ok=0;err=bad_hex");
    if (!ownerSeedHex || !recoverySeedHex) return res.type("text/plain").send("ok=0;err=bad_seedHex");

    const existing = getOpByRequestKey(requestKey);
    if (existing && existing.status !== "FAILED") {
      const fresh = await updateOperationReadinessFromCache(existing.opId);
      const extra = { idempotent: 1, total_ms: nowMs() - t0, ttssDeferredScheduled: 0, ...(includeSnapshot ? buildInlineReadySnapshotFromCache(id, 0) : {}) };
      return res.type("text/plain").send(semicolonKV(opResponseKV(fresh, extra)));
    }

    const ownerW = walletFromSeedHex(ownerSeedHex);
    const recoveryW = walletFromSeedHex(recoverySeedHex);
    await hardhatSetBalance(ownerW.address, "1000");
    await hardhatSetBalance(recoveryW.address, "1000");

    const keySlot = getOrCreateKeySlot(id);
    keySlot.ownerSeedHex = ownerSeedHex;
    keySlot.recoverySeedHex = recoverySeedHex;
    saveState();

    const idHash = rememberId(id);
    const op = createOperation("register", id, requestKey, { status: "ACCEPTED", owner: ownerW.address, recovery: recoveryW.address, minVersion: 0, currentVersion: 0, currentRoot: state.rootCache.root, currentEpoch: state.rootCache.epoch });

    try {
      const tx = await sendContractTxLocked(
        rootUpdaterWallet,
        (ov) => rootUpdaterContract.registerZK(idHash, cidHex, pkNormHash, pkRecHash, ownerW.address, recoveryW.address, ov),
        { label: "/registerZk:registerZK" }
      );
      const mirrorPatch = async () => {
        state.mirror[idHash] = { idHash, cid: cidHex, pkNormHash, pkRecHash, owner: getAddress(ownerW.address), recovery: getAddress(recoveryW.address), version: 0, active: true };
        saveState();
      };
      updateOperation(op.opId, { txHash: tx && tx.hash ? String(tx.hash) : "", submitMs: nowMs() - t0 });

      let gas = "";
      let settled = null;
      if (waitConfirm) {
        settled = await waitForTxAndRefresh(tx, { confirmations, id, reason: "http:/registerZk", opId: op.opId, onMirrorPatch: mirrorPatch, awaitReady: true });
        gas = settled.receipt && settled.receipt.gasUsed ? settled.receipt.gasUsed.toString() : "";
      } else {
        scheduleTxSettlement(tx, { confirmations, id, reason: "txwait:/registerZk", opId: op.opId, onMirrorPatch: mirrorPatch, awaitReady: true });
      }

      const fresh = waitConfirm ? (await updateOperationReadinessFromCache(op.opId)) : (state.ops[op.opId] || op);
      const actualEpoch = settled && settled.recompute && settled.recompute.epoch !== undefined ? Number(settled.recompute.epoch) : Number(fresh.currentEpoch !== undefined ? fresh.currentEpoch : state.rootCache.epoch || 0);
      const ttssAttachBase = { ttssMerged: false, ttssVkSetHash: "", ttssMetaHash: "", ttssMetaTxHash: "", ttssEpoch: 0, ttssMergeScheduled: 0 };
      let ttssAttach = ttssAttachBase;
      if (waitConfirm) {
        if (ttssSetupMode === "inline") {
          ttssAttach = { ...ttssAttachBase, ...(await maybeInlineTTSSMeta({ body, id, idHash, ver: 0, actualEpoch, mergeRequestKind: "ttss_meta" })) };
        } else if (ttssSetupMode === "deferred") {
          ttssAttach = { ...ttssAttachBase, ...scheduleDeferredTTSSMetaAttach({ body, id, idHash, ver: 0, actualEpoch, mergeRequestKind: "ttss_meta" }) };
        }
      }
      const extra = {
        total_ms: nowMs() - t0,
        gas,
        ttssMergeMode: waitConfirm ? ttssSetupMode : "none",
        ttssMergeScheduled: ttssAttach.ttssMergeScheduled ? 1 : 0,
        ttssDeferredScheduled: ttssAttach.ttssMergeScheduled ? 1 : 0,
        ttssMerged: ttssAttach.ttssMerged ? 1 : 0,
        ttssVkSetHash: ttssAttach.ttssVkSetHash,
        ttssMetaHash: ttssAttach.ttssMetaHash,
        ttssMetaTxHash: ttssAttach.ttssMetaTxHash,
        ttssEpoch: ttssAttach.ttssEpoch,
        ...(includeSnapshot ? buildInlineReadySnapshotFromCache(id, 0) : {}),
      };
      return res.type("text/plain").send(semicolonKV(opResponseKV(fresh, extra)));
    } catch (e) {
      try {
        const rec = await getRecordOnchain(id);
        if (rec && rec.active) {
          await refreshMirrorEntryFromChain(id);
          await runInlineReadyRefresh("idempotent:/registerZk", [id], [op.opId], { fullSync: false });
          updateOperation(op.opId, { status: "ROOT_REBUILT", owner: rec.owner, recovery: rec.recovery, currentVersion: Number(rec.version), lastError: parseEthersError(e) });
          const fresh = await updateOperationReadinessFromCache(op.opId);
          const extra = { idempotent: 1, total_ms: nowMs() - t0, ...(includeSnapshot ? buildInlineReadySnapshotFromCache(id, 0) : {}) };
      return res.type("text/plain").send(semicolonKV(opResponseKV(fresh, extra)));
        }
      } catch {}
      failOperation(op.opId, parseEthersError(e));
      return res.type("text/plain").send(semicolonKV({ ok: 0, err: "registerZk_fail", detail: parseEthersError(e), opId: op.opId, requestKey }));
    }
  } catch (e) {
    return res.type("text/plain").send(semicolonKV({ ok: 0, err: "registerZk_fail", detail: parseEthersError(e) }));
  }
});

app.post("/applyUpdateZk", async (req, res) => {
  const t0 = nowMs();
  try {
    const body = req.body || {};
    const id = bodyString(body.id);
    const newCidHex = ensure0x64(body.newCidHex);
    const pkNormHash = ensure0x64(body.pkNormHash);
    const pkRecHash = ensure0x64(body.pkRecHash);
    const providedOwnerSeed = ensure0x64(body.ownerSeedHex);
    const waitRaw = (body.wait ?? "0");
    const waitConfirm = String(waitRaw) === "1";
    const confirmations = Math.max(1, parseInt(String(body.confirmations ?? "1"), 10));
    const includeSnapshot = String(body.includeSnapshot ?? "0") === "1";

    if (!id) return res.type("text/plain").send("ok=0;err=missing_id");
    if (!newCidHex || !pkNormHash || !pkRecHash) return res.type("text/plain").send("ok=0;err=bad_hex");

    const keySlot = getOrCreateKeySlot(id);
    const ownerSeedHex = providedOwnerSeed || keySlot.ownerSeedHex;
    if (!ownerSeedHex) return res.type("text/plain").send("ok=0;err=missing_ownerSeedHex");

    const ownerW = walletFromSeedHex(ownerSeedHex);
    const rec = await getRecordOnchain(id);
    if (getAddress(ownerW.address) !== getAddress(rec.owner)) {
      return res.type("text/plain").send(semicolonKV({ ok: 0, err: "owner_seed_mismatch", onchain: rec.owner, derived: ownerW.address }));
    }

    await hardhatSetBalance(ownerW.address, "1000");
    const ownerContract = contract.connect(ownerW);
    const idHash = rememberId(id);
    const newVersion = BigInt(rec.version) + 1n;
    const tx = await sendContractTxLocked(ownerW, (ov) => ownerContract.applyUpdateZK(idHash, newCidHex, pkNormHash, pkRecHash, newVersion, ov));
    const mirrorPatch = async () => {
      state.mirror[idHash] = { idHash, cid: newCidHex, pkNormHash, pkRecHash, owner: getAddress(ownerW.address), recovery: getAddress(rec.recovery), version: Number(newVersion), active: true };
      saveState();
    };

    let confirm_ms = 0; let gas = "";
    if (waitConfirm) {
      const t1 = nowMs();
      const settled = await waitForTxAndRefresh(tx, { confirmations, id, reason: "http:/applyUpdateZk", onMirrorPatch: mirrorPatch, awaitReady: true });
      confirm_ms = nowMs() - t1;
      gas = settled.receipt && settled.receipt.gasUsed ? settled.receipt.gasUsed.toString() : "";
    } else {
      scheduleTxSettlement(tx, { confirmations, id, reason: "txwait:/applyUpdateZk", onMirrorPatch: mirrorPatch, awaitReady: true });
    }
    return res.type("text/plain").send(semicolonKV({ ok: 1, total_ms: nowMs() - t0, confirm_ms, gas, version: newVersion.toString() }));
  } catch (e) {
    return res.type("text/plain").send(semicolonKV({ ok: 0, err: "applyUpdateZk_fail", detail: parseEthersError(e) }));
  }
});

app.post("/applyRecoveryRotateZk", async (req, res) => {
  const t0 = nowMs();
  try {
    const body = req.body || {};
    const id = bodyString(body.id);
    const newCidHex = ensure0x64(body.newCidHex);
    const pkNormHash = ensure0x64(body.pkNormHash);
    const pkRecHash = ensure0x64(body.pkRecHash);
    const providedRecoverySeed = ensure0x64(body.recoverySeedHex);
    const newOwnerSeedHex = ensure0x64(body.newOwnerSeedHex || body.ownerSeedHex);
    const newRecoverySeedHex = ensure0x64(body.newRecoverySeedHex);
    const waitRaw = (body.wait ?? "0");
    const waitConfirm = String(waitRaw) === "1";
    const confirmations = Math.max(1, parseInt(String(body.confirmations ?? "1"), 10));
    const includeSnapshot = String(body.includeSnapshot ?? "0") === "1";

    if (!id) return res.type("text/plain").send("ok=0;err=missing_id");
    if (!newCidHex || !pkNormHash || !pkRecHash) return res.type("text/plain").send("ok=0;err=bad_hex");
    if (!newOwnerSeedHex || !newRecoverySeedHex) return res.type("text/plain").send("ok=0;err=missing_new_seeds");

    const keySlot = getOrCreateKeySlot(id);
    const recoverySeedHex = providedRecoverySeed || keySlot.recoverySeedHex;
    if (!recoverySeedHex) return res.type("text/plain").send("ok=0;err=missing_recoverySeedHex");

    const recoveryW = walletFromSeedHex(recoverySeedHex);
    const rec = await getRecordOnchain(id);
    if (getAddress(recoveryW.address) !== getAddress(rec.recovery)) {
      return res.type("text/plain").send(semicolonKV({ ok: 0, err: "recovery_seed_mismatch", onchain: rec.recovery, derived: recoveryW.address }));
    }

    const newOwnerW = walletFromSeedHex(newOwnerSeedHex);
    const newRecoveryW = walletFromSeedHex(newRecoverySeedHex);
    await hardhatSetBalance(recoveryW.address, "1000");
    await hardhatSetBalance(newOwnerW.address, "1000");
    await hardhatSetBalance(newRecoveryW.address, "1000");

    const recoveryContract = contract.connect(recoveryW);
    const idHash = rememberId(id);

    const chainVersion = BigInt(rec.version);
    const hasBodyNewVersion = body.newVersion !== undefined || body.version !== undefined;
    const hasBodyOldVersion = body.oldVersion !== undefined || body.currentVersion !== undefined;

    let requestedNewVersion = chainVersion + 1n;
    if (hasBodyNewVersion) {
      try {
        requestedNewVersion = BigInt(String(body.newVersion ?? body.version));
      } catch {
        return res.type("text/plain").send(semicolonKV({ ok: 0, err: "bad_new_version" }));
      }
    }

    let requestedOldVersion = chainVersion;
    if (hasBodyOldVersion) {
      try {
        requestedOldVersion = BigInt(String(body.oldVersion ?? body.currentVersion));
      } catch {
        return res.type("text/plain").send(semicolonKV({ ok: 0, err: "bad_old_version" }));
      }
    }

    if (requestedOldVersion !== chainVersion) {
      return res.type("text/plain").send(semicolonKV({
        ok: 0,
        err: "old_version_mismatch",
        onchainVersion: chainVersion.toString(),
        requestedOldVersion: requestedOldVersion.toString(),
      }));
    }
    if (requestedNewVersion !== chainVersion + 1n) {
      return res.type("text/plain").send(semicolonKV({
        ok: 0,
        err: "bad_new_version",
        onchainVersion: chainVersion.toString(),
        requestedNewVersion: requestedNewVersion.toString(),
      }));
    }

    const newVersion = requestedNewVersion;
    const requestKey = defaultRequestKey("recovery", id, body.requestId, newVersion.toString());

    const existing = getOpByRequestKey(requestKey);
    if (existing && existing.status !== "FAILED") {
      const fresh = await updateOperationReadinessFromCache(existing.opId);
      const extra = { idempotent: 1, total_ms: nowMs() - t0, ...(includeSnapshot ? buildInlineReadySnapshotFromCache(id, 0) : {}) };
      return res.type("text/plain").send(semicolonKV(opResponseKV(fresh, extra)));
    }

    const op = createOperation("recovery", id, requestKey, {
      status: "ACCEPTED",
      minVersion: Number(newVersion),
      targetVersion: Number(newVersion),
      currentVersion: Number(rec ? rec.version : ver),
      owner: rec.owner,
      recovery: rec.recovery,
      currentRoot: state.rootCache.root,
      currentEpoch: state.rootCache.epoch,
    });

    const tx = await sendContractTxLocked(recoveryW, (ov) =>
      recoveryContract.applyRecoveryRotateZK(idHash, newCidHex, pkNormHash, pkRecHash, newVersion, newOwnerW.address, newRecoveryW.address, ov)
    );

    const updateKeySeeds = async () => {
      keySlot.ownerSeedHex = newOwnerSeedHex;
      keySlot.recoverySeedHex = newRecoverySeedHex;
      saveState();
      updateOperation(op.opId, { owner: newOwnerW.address, recovery: newRecoveryW.address });
    };
    const mirrorPatch = async () => {
      state.mirror[idHash] = { idHash, cid: newCidHex, pkNormHash, pkRecHash, owner: getAddress(newOwnerW.address), recovery: getAddress(newRecoveryW.address), version: Number(newVersion), active: true };
      saveState();
    };

    updateOperation(op.opId, { txHash: tx && tx.hash ? String(tx.hash) : "", submitMs: nowMs() - t0 });

    let gas = "";
    let settled = null;
    if (waitConfirm) {
      settled = await waitForTxAndRefresh(tx, { confirmations, id, reason: "http:/applyRecoveryRotateZk", onConfirmed: updateKeySeeds, onMirrorPatch: mirrorPatch, opId: op.opId, awaitReady: true });
      gas = settled.receipt && settled.receipt.gasUsed ? settled.receipt.gasUsed.toString() : "";
    } else {
      scheduleTxSettlement(tx, { confirmations, id, reason: "txwait:/applyRecoveryRotateZk", onConfirmed: updateKeySeeds, onMirrorPatch: mirrorPatch, opId: op.opId, awaitReady: true });
    }

    let fresh = waitConfirm ? (await updateOperationReadinessFromCache(op.opId)) : (state.ops[op.opId] || op);
    const actualEpoch = settled && settled.recompute && settled.recompute.epoch !== undefined ? Number(settled.recompute.epoch) : Number(fresh.currentEpoch !== undefined ? fresh.currentEpoch : state.rootCache.epoch || 0);
    const ttssInline = { ttssMerged: false, ttssVkSetHash: "", ttssMetaHash: "", ttssMetaTxHash: "", ttssEpoch: 0 };
    if (waitConfirm) {
      await ensureSnapshotFreshForVersion(id, Number(newVersion), "http:/applyRecoveryRotateZk:postcheck");
      fresh = (await updateOperationReadinessFromCache(op.opId)) || fresh;
    }
    const ttssMerged = ttssInline.ttssMerged;
    const ttssVkSetHash = ttssInline.ttssVkSetHash;
    const ttssMetaHash = ttssInline.ttssMetaHash;
    const ttssMetaTxHash = ttssInline.ttssMetaTxHash;
    const ttssEpoch = ttssInline.ttssEpoch;

    return res.type("text/plain").send(semicolonKV(opResponseKV(fresh, {
      total_ms: nowMs() - t0,
      gas,
      newOwner: newOwnerW.address,
      newRecovery: newRecoveryW.address,
      currentVersion: chainVersion.toString(),
      version: newVersion.toString(),
      targetVersion: newVersion.toString(),
      oldVersion: chainVersion.toString(),
      newVersion: newVersion.toString(),
      ttssMerged: ttssMerged ? 1 : 0,
      ttssVkSetHash,
      ttssMetaHash,
      ttssMetaTxHash,
      ttssEpoch,
      ...(includeSnapshot ? buildInlineReadySnapshotFromCache(id, Number(newVersion)) : {}),
    })));
  } catch (e) {
    return res.type("text/plain").send(semicolonKV({ ok: 0, err: "applyRecoveryRotateZk_fail", detail: parseEthersError(e) }));
  }
});

bootstrap()
  .then(() => {
    
app.post("/registerTTSSMeta", async (req, res) => {
  const t0 = nowMs();
  try {
    const body = req.body || {};
    const { id, idHash } = resolveIdAndHash(body, {});
    const vkSetHash = ensure0x64(body.vkSetHash);
    const metaHash = ensure0x64(body.metaHash);
    const waitConfirm = String(body.wait ?? "1") === "1";
    const confirmations = Math.max(1, parseInt(String(body.confirmations ?? "1"), 10));
    if (!idHash) return res.status(400).json({ ok: 0, err: "missing_id_or_idHash" });
    if (!vkSetHash || !metaHash) return res.status(400).json({ ok: 0, err: "bad_hash" });

    const haveExplicitVer = body.ver !== undefined;
    const haveExplicitEpoch = body.epoch !== undefined;
    const rec = haveExplicitVer ? null : (id ? await getRecordOnchain(id) : await getRecordByIdHashOnchain(idHash));
    const ver = haveExplicitVer ? Number(body.ver) : Number(rec.version);
    const epoch = body.epoch !== undefined ? Number(body.epoch) : Number(state.rootCache.epoch);
    if (!Number.isFinite(ver) || ver < 0) return res.status(400).json({ ok: 0, err: "bad_ver" });
    if (!Number.isFinite(epoch) || epoch < 0) return res.status(400).json({ ok: 0, err: "bad_epoch" });

    const requestKey = defaultRequestKey("ttss_meta", id || idHash, body.requestId, `${ver}`);
    const existingLocal = state.ttssMeta[idHash] || null;
    if (ttssMetaMatchesLocalEntry(existingLocal, ver, epoch, vkSetHash, metaHash)) {
      const effectiveMeta = buildTTSSMetaEffective(existingLocal, { id: id || state.idByHash[idHash] || "", idHash, ver, epoch, vkSetHash, metaHash, requestKey });
      return res.json({ ok: 1, accepted: 1, ready: 1, status: "READY", idempotent: 1, total_ms: nowMs() - t0, idHash, ver, epoch, vkSetHash, metaHash, txHash: bodyString(effectiveMeta.txHash || ""), effectiveMeta });
    }
    const existing = getOpByRequestKey(requestKey);
    if (existing && existing.status !== "FAILED") {
      const fresh = await updateOperationReadinessFromCache(existing.opId);
      const effectiveMeta = ttssMetaMatchesLocalEntry(state.ttssMeta[idHash] || null, ver, epoch, vkSetHash, metaHash)
        ? buildTTSSMetaEffective(state.ttssMeta[idHash], { id: id || state.idByHash[idHash] || "", idHash, ver, epoch, vkSetHash, metaHash, requestKey })
        : undefined;
      return res.json({ ...opResponseKV(fresh, { idempotent: 1, total_ms: nowMs() - t0 }), idHash, ver, epoch, vkSetHash, metaHash, ...(effectiveMeta ? { effectiveMeta, txHash: bodyString(effectiveMeta.txHash || "") } : {}) });
    }

    const op = createOperation("ttss_meta", id || idHash, requestKey, {
      status: "ACCEPTED",
      idHash,
      minVersion: ver,
      targetVersion: ver,
      currentVersion: Number(rec ? rec.version : ver),
      currentRoot: state.rootCache.root,
      currentEpoch: state.rootCache.epoch,
    });

    const tx = await sendContractTxLocked(
      rootUpdaterWallet,
      (ov) => rootUpdaterContract.setTTSSMeta(idHash, BigInt(ver), BigInt(epoch), vkSetHash, metaHash, ov),
      { label: "/registerTTSSMeta:setTTSSMeta" }
    );
    updateOperation(op.opId, { txHash: tx && tx.hash ? String(tx.hash) : "", submitMs: nowMs() - t0 });

    let gas = "";
    if (waitConfirm) {
      const receipt = await tx.wait(confirmations);
      gas = receipt && receipt.gasUsed ? receipt.gasUsed.toString() : "";
      updateOperation(op.opId, { status: "ONCHAIN_CONFIRMED", txHash: receipt && receipt.hash ? String(receipt.hash) : (tx && tx.hash ? String(tx.hash) : ""), confirmMs: nowMs() - t0 });
    }

    storeTTSSMetaLocal({
      id: id || state.idByHash[idHash] || "",
      idHash,
      ver,
      epoch,
      vkSetHash,
      metaHash,
      txHash: tx && tx.hash ? String(tx.hash) : "",
      requestKey,
    });

    const effectiveMeta = state.ttssMeta[idHash] || { idHash, ver, epoch, vkSetHash, metaHash };
    const fresh = waitConfirm ? (updateOperation(op.opId, { status: "READY", ready: true, currentRoot: state.rootCache.root, currentEpoch: state.rootCache.epoch }) || state.ops[op.opId] || op) : op;
    return res.json({
      ...opResponseKV(fresh, { total_ms: nowMs() - t0, gas, stable: waitConfirm ? 1 : 0 }),
      idHash,
      ver,
      epoch,
      vkSetHash,
      metaHash,
      txHash: tx && tx.hash ? String(tx.hash) : "",
      effectiveMeta,
    });
  } catch (e) {
    return res.status(500).json({ ok: 0, err: "register_ttss_meta_fail", detail: parseEthersError(e) });
  }
});

app.post("/applyRecoveryRotateTTSS", async (req, res) => {
  const t0 = nowMs();
  try {
    const body = req.body || {};
    const { id, idHash } = resolveIdAndHash(body, {});
    const vkSetHash = ensure0x64(body.vkSetHash);
    const metaHash = ensure0x64(body.metaHash);
    const waitConfirm = String(body.wait ?? "1") === "1";
    const confirmations = Math.max(1, parseInt(String(body.confirmations ?? "1"), 10));
    if (!idHash) return res.status(400).json({ ok: 0, err: "missing_id_or_idHash" });
    if (!vkSetHash || !metaHash) return res.status(400).json({ ok: 0, err: "bad_hash" });

    const haveExplicitVer = body.ver !== undefined;
    const rec = haveExplicitVer ? null : (id ? await getRecordOnchain(id) : await getRecordByIdHashOnchain(idHash));
    const ver = haveExplicitVer ? Number(body.ver) : Number(rec.version);
    const epoch = body.epoch !== undefined ? Number(body.epoch) : Number(state.rootCache.epoch);
    if (!Number.isFinite(ver) || ver < 0) return res.status(400).json({ ok: 0, err: "bad_ver" });
    if (!Number.isFinite(epoch) || epoch < 0) return res.status(400).json({ ok: 0, err: "bad_epoch" });

    const requestKey = defaultRequestKey("ttss_meta_rotate", id || idHash, body.requestId, `${ver}`);
    const existingLocal = state.ttssMeta[idHash] || null;
    if (ttssMetaMatchesLocalEntry(existingLocal, ver, epoch, vkSetHash, metaHash)) {
      const effectiveMeta = buildTTSSMetaEffective(existingLocal, { id: id || state.idByHash[idHash] || "", idHash, ver, epoch, vkSetHash, metaHash, requestKey });
      return res.json({ ok: 1, accepted: 1, ready: 1, status: "READY", idempotent: 1, total_ms: nowMs() - t0, idHash, ver, epoch, vkSetHash, metaHash, txHash: bodyString(effectiveMeta.txHash || ""), effectiveMeta });
    }
    const existing = getOpByRequestKey(requestKey);
    if (existing && existing.status !== "FAILED") {
      const fresh = await updateOperationReadinessFromCache(existing.opId);
      const effectiveMeta = ttssMetaMatchesLocalEntry(state.ttssMeta[idHash] || null, ver, epoch, vkSetHash, metaHash)
        ? buildTTSSMetaEffective(state.ttssMeta[idHash], { id: id || state.idByHash[idHash] || "", idHash, ver, epoch, vkSetHash, metaHash, requestKey })
        : undefined;
      return res.json({ ...opResponseKV(fresh, { idempotent: 1, total_ms: nowMs() - t0 }), idHash, ver, epoch, vkSetHash, metaHash, ...(effectiveMeta ? { effectiveMeta, txHash: bodyString(effectiveMeta.txHash || "") } : {}) });
    }

    const opCurrentVersion = Number(rec ? rec.version : ver);
    const op = createOperation("ttss_meta_rotate", id || idHash, requestKey, {
      status: "ACCEPTED",
      idHash,
      minVersion: ver,
      targetVersion: ver,
      currentVersion: opCurrentVersion,
      currentRoot: state.rootCache.root,
      currentEpoch: state.rootCache.epoch,
    });

    const runRotateMetaWrite = async () => {
      const jobStart = nowMs();
      try {
        const tx = await sendContractTxLocked(
          rootUpdaterWallet,
          (ov) => rootUpdaterContract.setTTSSMeta(idHash, BigInt(ver), BigInt(epoch), vkSetHash, metaHash, ov),
          { label: "/applyRecoveryRotateTTSS:setTTSSMeta" }
        );
        updateOperation(op.opId, { txHash: tx && tx.hash ? String(tx.hash) : "", submitMs: nowMs() - jobStart });
        const receipt = await tx.wait(confirmations);
        const gas = receipt && receipt.gasUsed ? receipt.gasUsed.toString() : "";
        const txHash = receipt && receipt.hash ? String(receipt.hash) : (tx && tx.hash ? String(tx.hash) : "");
        updateOperation(op.opId, { status: "ONCHAIN_CONFIRMED", txHash, confirmMs: nowMs() - jobStart, gas });
        storeTTSSMetaLocal({
          id: id || state.idByHash[idHash] || "",
          idHash,
          ver,
          epoch,
          vkSetHash,
          metaHash,
          txHash,
          requestKey,
        });
        updateOperation(op.opId, { status: "READY", ready: true, currentRoot: state.rootCache.root, currentEpoch: state.rootCache.epoch, txHash });
      } catch (e) {
        failOperation(op.opId, parseEthersError(e));
        console.error(`[bb_service_zk] applyRecoveryRotateTTSS async failed (${id || idHash}): ${parseEthersError(e)}`);
      }
    };

    if (waitConfirm) {
      await runRotateMetaWrite();
      const effectiveMeta = state.ttssMeta[idHash] || { idHash, ver, epoch, vkSetHash, metaHash };
      const fresh = state.ops[op.opId] || op;
      return res.json({
        ...opResponseKV(fresh, { total_ms: nowMs() - t0, stable: 1 }),
        idHash,
        ver,
        epoch,
        vkSetHash,
        metaHash,
        txHash: bodyString(effectiveMeta.txHash || ""),
        effectiveMeta,
      });
    }

    void runRotateMetaWrite();
    return res.json({
      ...opResponseKV(op, { total_ms: nowMs() - t0, stable: 0, asyncAccepted: 1 }),
      idHash,
      ver,
      epoch,
      vkSetHash,
      metaHash,
      txHash: "",
    });
  } catch (e) {
    return res.status(500).json({ ok: 0, err: "apply_recovery_rotate_ttss_fail", detail: parseEthersError(e) });
  }
});

app.get("/ttssMeta", async (req, res) => {
  try {
    const { id, idHash } = resolveIdAndHash({}, req.query || {});
    if (!idHash) return res.status(400).json({ ok: 0, err: "missing_id_or_idHash" });
    const local = state.ttssMeta[idHash] || null;
    const onchain = await getTTSSMetaOnchain(idHash);
    const effective = {
      id: id || (local && local.id) || state.idByHash[idHash] || "",
      idHash,
      ver: local && local.ver !== undefined ? Number(local.ver) : Number(onchain.ver),
      epoch: local && local.epoch !== undefined ? Number(local.epoch) : Number(onchain.epoch),
      vkSetHash: (local && local.vkSetHash) || zeroHex32(onchain.vkSetHash),
      metaHash: (local && local.metaHash) || zeroHex32(onchain.metaHash),
      onchain,
      local,
    };
    return res.json({ ok: 1, ...effective });
  } catch (e) {
    return res.status(500).json({ ok: 0, err: "ttss_meta_fail", detail: parseEthersError(e) });
  }
});

app.post("/publishTrace", async (req, res) => {
  const t0 = nowMs();
  try {
    const body = req.body || {};
    const { id, idHash } = resolveIdAndHash(body, {});
    const accusedSetHash = ensure0x64(body.accusedSetHash);
    const proofHash = ensure0x64(body.proofHash);
    const traceDigest = ensure0x64(body.traceDigest);
    const waitConfirm = String(body.wait ?? "1") === "1";
    const confirmations = Math.max(1, parseInt(String(body.confirmations ?? "1"), 10));
    if (!idHash) return res.status(400).json({ ok: 0, err: "missing_id_or_idHash" });
    if (!accusedSetHash || !proofHash || !traceDigest) return res.status(400).json({ ok: 0, err: "bad_hash" });

    const rec = id ? await getRecordOnchain(id) : await getRecordByIdHashOnchain(idHash);
    const ver = body.ver !== undefined ? Number(body.ver) : Number(rec.version);
    const epoch = body.epoch !== undefined ? Number(body.epoch) : Number(state.rootCache.epoch);
    if (!Number.isFinite(ver) || ver < 0) return res.status(400).json({ ok: 0, err: "bad_ver" });
    if (!Number.isFinite(epoch) || epoch < 0) return res.status(400).json({ ok: 0, err: "bad_epoch" });

    const requestKey = defaultRequestKey("trace_publish", id || idHash, body.requestId, `${ver}`);
    const existing = getOpByRequestKey(requestKey);
    if (existing && existing.status !== "FAILED") {
      const fresh = await updateOperationReadinessFromCache(existing.opId);
      return res.json({ ...opResponseKV(fresh, { idempotent: 1, total_ms: nowMs() - t0 }), idHash, ver, epoch, accusedSetHash, proofHash, traceDigest });
    }

    const op = createOperation("trace_publish", id || idHash, requestKey, {
      status: "ACCEPTED",
      idHash,
      minVersion: ver,
      targetVersion: ver,
      currentVersion: Number(rec.version),
      currentRoot: state.rootCache.root,
      currentEpoch: state.rootCache.epoch,
    });

    const tx = await sendContractTxLocked(
      rootUpdaterWallet,
      (ov) => rootUpdaterContract.publishTraceResult(idHash, BigInt(ver), BigInt(epoch), accusedSetHash, proofHash, traceDigest, ov),
      { label: "/publishTraceResult:publishTraceResult" }
    );
    updateOperation(op.opId, { txHash: tx && tx.hash ? String(tx.hash) : "", submitMs: nowMs() - t0 });

    let gas = "";
    if (waitConfirm) {
      const settled = await waitForTxAndRefresh(tx, {
        confirmations,
        id: id || (state.idByHash[idHash] || ""),
        reason: "http:/publishTrace",
        opId: op.opId,
        awaitReady: false,
      });
      gas = settled.receipt && settled.receipt.gasUsed ? settled.receipt.gasUsed.toString() : "";
    }

    storeTraceAnchorLocal({
      id: id || state.idByHash[idHash] || "",
      idHash,
      ver,
      epoch,
      accusedSetHash,
      proofHash,
      traceDigest,
      txHash: tx && tx.hash ? String(tx.hash) : "",
      requestKey,
      traceResultPath: bodyString(body.traceResultPath || ""),
    });

    const fresh = waitConfirm ? (state.ops[op.opId] || op) : op;
    return res.json({
      ...opResponseKV(fresh, { total_ms: nowMs() - t0, gas }),
      idHash,
      ver,
      epoch,
      accusedSetHash,
      proofHash,
      traceDigest,
      txHash: tx && tx.hash ? String(tx.hash) : "",
    });
  } catch (e) {
    return res.status(500).json({ ok: 0, err: "publish_trace_fail", detail: parseEthersError(e) });
  }
});

app.listen(PORT, () => {
      console.log(`[bb_service_zk] listening on :${PORT}`);
      console.log(`[bb_service_zk] CONTRACT=${CONTRACT_ADDR}`);
      console.log(`[bb_service_zk] TREE_DEPTH=${TREE_DEPTH}`);
      console.log(`[bb_service_zk] STATE_FILE=${STATE_FILE}`);
      console.log(`[bb_service_zk] ROOT_UPDATER=${rootUpdaterWallet.address}`);
    });
  })
  .catch((e) => {
    console.error("[bb_service_zk] bootstrap failed:", e);
    process.exit(1);
  });
