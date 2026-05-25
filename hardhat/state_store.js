"use strict";

const fs = require("fs");

function createDefaultState() {
  return {
    keys: {},
    mirror: {},
    idByHash: {},
    ops: {},
    requestToOp: {},
    meta: {},
    ttssMeta: {},
    traceAnchors: {},
  };
}

function loadState(stateFile) {
  try {
    const raw = fs.readFileSync(stateFile, "utf8");
    const st = JSON.parse(raw);
    if (!st || typeof st !== "object") throw new Error("bad_state");
    return {
      ...createDefaultState(),
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
    return createDefaultState();
  }
}

function initializeRuntimeState(state, opts = {}) {
  const treeDepth = Number(opts.treeDepth || 20);
  state.meta = state.meta || {};
  state.meta.lastSyncedBlock = Number(state.meta.lastSyncedBlock || 0);
  state.meta.rebuildSeq = Number(state.meta.rebuildSeq || 0);
  state.rootCache = state.rootCache || { root: "0x" + "00".repeat(32), epoch: 0, depth: treeDepth, builtAt: 0 };
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
  return state;
}

function snapshotPersistentState(state) {
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

function createStatePersistence({ stateFile, state, formatError, flushDelayMs = 150 }) {
  let flushTimer = null;
  let flushInFlight = false;
  let flushPending = false;
  const format = typeof formatError === "function" ? formatError : (e) => (e && e.message ? e.message : String(e));

  function scheduleStateFlush() {
    flushPending = true;
    if (flushTimer) return;
    flushTimer = setTimeout(async () => {
      flushTimer = null;
      if (flushInFlight || !flushPending) return;
      flushPending = false;
      flushInFlight = true;
      try {
        await fs.promises.writeFile(stateFile, snapshotPersistentState(state));
      } catch (e) {
        console.warn(`[bb_service_zk] state flush failed: ${format(e)}`);
        flushPending = true;
      } finally {
        flushInFlight = false;
        if (flushPending) scheduleStateFlush();
      }
    }, flushDelayMs);
  }

  return {
    saveState: scheduleStateFlush,
    scheduleStateFlush,
    snapshotPersistentState: () => snapshotPersistentState(state),
  };
}

module.exports = {
  createDefaultState,
  loadState,
  initializeRuntimeState,
  snapshotPersistentState,
  createStatePersistence,
};
