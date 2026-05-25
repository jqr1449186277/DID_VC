"use strict";

function registerRegisterRoutes(app, ctx) {
  const {
    state, contract, rootUpdaterWallet, rootUpdaterContract, nowMs, bodyString, semicolonKV, ensure0x64,
    walletFromSeedHex, getAddress, defaultRequestKey, getOpByRequestKey, opResponseKV, createOperation,
    updateOperation, failOperation, updateOperationReadinessFromCache, buildInlineReadySnapshotFromCache,
    getOrCreateKeySlot, saveState, rememberId, getRecordOnchain, hardhatSetBalance, sendContractTxLocked,
    waitForTxAndRefresh, scheduleTxSettlement, refreshMirrorEntryFromChain, runInlineReadyRefresh,
    ensureSnapshotFreshForVersion, maybeInlineTTSSMeta, scheduleDeferredTTSSMetaAttach, parseEthersError,
  } = ctx;

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
}

module.exports = { registerRegisterRoutes };
