"use strict";

function registerTraceRoutes(app, ctx) {
  const {
    state, rootUpdaterWallet, rootUpdaterContract, nowMs, bodyString, ensure0x64, resolveIdAndHash,
    defaultRequestKey, getOpByRequestKey, opResponseKV, createOperation, updateOperation,
    updateOperationReadinessFromCache, getRecordOnchain, getRecordByIdHashOnchain, sendContractTxLocked,
    waitForTxAndRefresh, storeTraceAnchorLocal, parseEthersError,
  } = ctx;

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
}

module.exports = { registerTraceRoutes };
