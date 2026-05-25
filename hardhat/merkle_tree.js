"use strict";

function createMerkleTree(ctx) {
  const {
    state,
    treeDepth,
    rebuildChunk,
    poseidonHash,
    hexToField,
    fieldToHex,
    bodyString,
    rememberId,
    traceStepEnter,
    traceStepExit,
    traceStepError,
  } = ctx;

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
    return 1 << treeDepth;
  }
  function preferredIndexForIdHash(idHash) {
    const mask = (1n << BigInt(treeDepth)) - 1n;
    return Number(BigInt(String(idHash)) & mask);
  }
  function initSparseTreeCache() {
    treeCache.activeEntries = [];
    treeCache.indexById = {};
    treeCache.idByIndex = {};
    treeCache.leafById = {};
    treeCache.leafFieldById = {};
    treeCache.levels = [];
    treeCache.nodeCache = Array.from({ length: treeDepth + 1 }, () => new Map());
    treeCache.activeCount = 0;
    treeCache.rootField = treeCache.zeroes[treeDepth];
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
    for (let level = 0; level < treeDepth; level++) {
      const base = idx & ~1;
      const left = getNodeValue(level, base);
      const right = getNodeValue(level, base + 1);
      const parent = poseidonHash([left, right]);
      idx >>= 1;
      setNodeValue(level + 1, idx, parent);
    }
    treeCache.rootField = getNodeValue(treeDepth, 0);
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
    for (let level = 0; level < treeDepth; level++) {
      const sib = idx ^ 1;
      pathElements.push(fieldToHex(getNodeValue(level, sib)));
      pathIndex.push(idx & 1);
      idx >>= 1;
    }
    state.pathCache[id] = {
      idHash,
      depth: treeDepth,
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
    const t = traceStepEnter("rebuildTreeCacheFromMirror", { mode: "full_sparse", activeMirror: Object.keys(state.mirror || {}).length, preheatIds: preheatIds.length, chunk: rebuildChunk });
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
        if (((i + 1) % rebuildChunk) === 0) await yieldImmediate();
      }
      refreshActiveEntriesView();
      const idsToPreheat = new Set();
      for (const id of preheatIds) if (bodyString(id)) idsToPreheat.add(bodyString(id));
      for (const rawId of idsToPreheat) {
        if (recomputePathCacheForId(rawId)) warmed += 1;
        if ((warmed % rebuildChunk) === 0) await yieldImmediate();
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

  return {
    treeCache,
    buildZeroes,
    computeLeafFromMirror,
    updateLeafCachesForId,
    recomputePathCacheForId,
    fullRebuildSparseTreeFromMirror,
    incrementalUpdateTreeCacheFromMirror,
  };
}

module.exports = { createMerkleTree };
