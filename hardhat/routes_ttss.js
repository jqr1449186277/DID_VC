"use strict";

function registerTTSSRoutes(app, ctx) {
  const {
    state, rootUpdaterWallet, rootUpdaterContract, nowMs, bodyString, ensure0x64, resolveIdAndHash,
    defaultRequestKey, getOpByRequestKey, opResponseKV, createOperation, updateOperation, failOperation,
    updateOperationReadinessFromCache, getRecordOnchain, getRecordByIdHashOnchain, sendContractTxLocked,
    ttssMetaMatchesLocalEntry, buildTTSSMetaEffective, storeTTSSMetaLocal, getTTSSMetaOnchain, zeroHex32, parseEthersError,
  } = ctx;

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
}

module.exports = { registerTTSSRoutes };
