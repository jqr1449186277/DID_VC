#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");

const RPC_URL = process.env.RPC_URL || "http://127.0.0.1:8545";
const PROJECT_ROOT = process.env.PROJECT_ROOT || path.resolve(__dirname, "..");
const STATE_FILE = process.env.STATE_FILE || path.join(PROJECT_ROOT, "hardhat", "bb_state_zk.json");
const BUILD_INFO_DIR = process.env.BUILD_INFO_DIR || path.join(PROJECT_ROOT, "hardhat", "artifacts", "build-info");
const OUT_FILE = process.env.OUT_FILE || path.join(PROJECT_ROOT, "results", "_logs", "gas_events.jsonl");
const POLL_MS = Number(process.env.POLL_MS || "1000");
const START_BLOCK = process.env.START_BLOCK ? Number(process.env.START_BLOCK) : null;
const AUTO_DETECT_WINDOW_BLOCKS = Number(process.env.AUTO_DETECT_WINDOW_BLOCKS || "20");
const AUTO_RETARGET = process.env.AUTO_RETARGET === "1";
const STRICT_TARGET_MATCH = process.env.STRICT_TARGET_MATCH === "1";

const WATCH_METHODS = new Set([
  "registerZK",
  "setActiveRoot",
  "setTTSSMeta",
  "applyRecoveryRotateZK",
  "publishTraceResult",
]);

function mkdirpForFile(file) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
}

function appendJsonl(file, obj) {
  mkdirpForFile(file);
  fs.appendFileSync(file, JSON.stringify(obj) + "\n", "utf8");
}

function nowIso() {
  return new Date().toISOString();
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function rpc(method, params) {
  const body = { jsonrpc: "2.0", id: Date.now(), method, params };
  const resp = await fetch(RPC_URL, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  if (!resp.ok) {
    throw new Error(`rpc http ${resp.status} for method=${method}`);
  }
  const data = await resp.json();
  if (data.error) {
    throw new Error(`rpc error method=${method}: ${JSON.stringify(data.error)}`);
  }
  return data.result;
}

function hexToNum(hex) {
  if (!hex) return 0;
  return Number.parseInt(hex, 16);
}

function hexToBigIntString(hex) {
  if (!hex) return "0";
  try {
    return BigInt(hex).toString();
  } catch {
    return "0";
  }
}

function normalizeAddr(addr) {
  return String(addr || "").toLowerCase();
}

function methodToStage(method) {
  switch (method) {
    case "registerZK":
      return "register";
    case "setActiveRoot":
      return "root_update";
    case "setTTSSMeta":
      return "ttss_meta";
    case "applyRecoveryRotateZK":
      return "recover_rotate";
    case "publishTraceResult":
      return "trace_publish";
    default:
      return "other";
  }
}

function loadTargetContractAddressFromState() {
  try {
    const raw = JSON.parse(fs.readFileSync(STATE_FILE, "utf8"));
    const candidates = [raw.contract, raw.address, raw.contractAddress, raw.CONTRACT].filter(Boolean);
    if (candidates.length === 0) return "";
    return normalizeAddr(candidates[0]);
  } catch (err) {
    console.error(`[gas_monitor] WARN failed to read STATE_FILE=${STATE_FILE}: ${err.message}`);
    return "";
  }
}

function loadSelectorMap() {
  const out = {};
  if (!fs.existsSync(BUILD_INFO_DIR)) {
    console.error(`[gas_monitor] WARN build-info dir not found: ${BUILD_INFO_DIR}`);
    return out;
  }

  const files = fs.readdirSync(BUILD_INFO_DIR).filter((x) => x.endsWith(".json"));
  for (const file of files) {
    const full = path.join(BUILD_INFO_DIR, file);
    let raw;
    try {
      raw = JSON.parse(fs.readFileSync(full, "utf8"));
    } catch (err) {
      console.error(`[gas_monitor] WARN cannot parse build-info file=${full}: ${err.message}`);
      continue;
    }

    const contracts = raw?.output?.contracts || {};
    for (const [sourceName, sourceContracts] of Object.entries(contracts)) {
      for (const [contractName, artifact] of Object.entries(sourceContracts || {})) {
        const methodIdentifiers = artifact?.evm?.methodIdentifiers || {};
        for (const [signature, selectorNo0x] of Object.entries(methodIdentifiers)) {
          const method = String(signature).split("(")[0];
          if (!WATCH_METHODS.has(method)) continue;
          const selector = "0x" + String(selectorNo0x).toLowerCase();
          out[selector] = {
            method,
            signature,
            source: sourceName,
            contract: contractName,
          };
        }
      }
    }
  }

  return out;
}

async function getBlockNumber() {
  return hexToNum(await rpc("eth_blockNumber", []));
}

async function getBlockByNumber(blockNumber) {
  const hexBn = "0x" + blockNumber.toString(16);
  return await rpc("eth_getBlockByNumber", [hexBn, true]);
}

async function getReceipt(txHash) {
  return await rpc("eth_getTransactionReceipt", [txHash]);
}

async function detectTargetFromRecentBlocks(latestBlock, watchedSelectors) {
  const start = Math.max(0, latestBlock - AUTO_DETECT_WINDOW_BLOCKS + 1);
  const counts = {};

  for (let bn = start; bn <= latestBlock; bn++) {
    const block = await getBlockByNumber(bn);
    const txs = block?.transactions || [];
    for (const tx of txs) {
      const input = String(tx.input || tx.data || "0x").toLowerCase();
      if (!input || input === "0x" || input.length < 10) continue;
      const selector = input.slice(0, 10);
      if (!watchedSelectors.has(selector)) continue;
      const to = normalizeAddr(tx.to);
      if (!to) continue;
      counts[to] = (counts[to] || 0) + 1;
    }
  }

  const sorted = Object.entries(counts).sort((a, b) => b[1] - a[1]);
  if (sorted.length === 0) {
    return { ok: false, reason: "no_selector_hits", counts };
  }

  for (const [addr, hitCount] of sorted) {
    try {
      const code = await rpc("eth_getCode", [addr, "latest"]);
      if (code && code !== "0x" && code !== "0x0") {
        return {
          ok: true,
          reason: "recent_blocks",
          address: addr,
          hitCount,
          counts,
          codeBytes: Math.max(0, (code.length - 2) / 2),
        };
      }
    } catch {
      // continue
    }
  }

  return { ok: false, reason: "no_contract_code_for_candidates", counts };
}

async function resolveTargetContract(selectorMap) {
  if (process.env.CONTRACT) {
    return {
      ok: true,
      reason: "env_contract",
      address: normalizeAddr(process.env.CONTRACT),
    };
  }

  const fromState = loadTargetContractAddressFromState();
  if (fromState) {
    try {
      const code = await rpc("eth_getCode", [fromState, "latest"]);
      if (code && code !== "0x" && code !== "0x0") {
        return {
          ok: true,
          reason: "state_file",
          address: fromState,
          codeBytes: Math.max(0, (code.length - 2) / 2),
        };
      }
    } catch (err) {
      console.error(`[gas_monitor] WARN state file address eth_getCode failed: ${err.message}`);
    }
  }

  const latest = await getBlockNumber();
  const watchedSelectors = new Set(Object.keys(selectorMap));
  return await detectTargetFromRecentBlocks(latest, watchedSelectors);
}

async function preflightCheckTargetContract(targetContract) {
  if (!targetContract) {
    console.error("[gas_monitor] WARN targetContract is empty");
    return { ok: false, reason: "empty_target_contract" };
  }

  const isHexAddr = /^0x[a-fA-F0-9]{40}$/.test(targetContract);
  if (!isHexAddr) {
    console.error(`[gas_monitor] FATAL invalid targetContract format: ${targetContract}`);
    appendJsonl(OUT_FILE, {
      ts: Date.now(),
      isoTime: nowIso(),
      type: "preflight_error",
      targetContract,
      reason: "invalid_address_format",
    });
    return { ok: false, reason: "invalid_address_format" };
  }

  let code = "0x";
  try {
    code = await rpc("eth_getCode", [targetContract, "latest"]);
  } catch (err) {
    console.error(`[gas_monitor] FATAL eth_getCode failed for ${targetContract}: ${err.message || err}`);
    appendJsonl(OUT_FILE, {
      ts: Date.now(),
      isoTime: nowIso(),
      type: "preflight_error",
      targetContract,
      reason: "eth_getCode_failed",
      error: String(err.message || err),
    });
    return { ok: false, reason: "eth_getCode_failed" };
  }

  if (!code || code === "0x" || code === "0x0") {
    console.error(`[gas_monitor] FATAL no contract code at targetContract=${targetContract}`);
    appendJsonl(OUT_FILE, {
      ts: Date.now(),
      isoTime: nowIso(),
      type: "preflight_error",
      targetContract,
      reason: "no_contract_code",
    });
    return { ok: false, reason: "no_contract_code" };
  }

  console.error(`[gas_monitor] preflight ok: targetContract=${targetContract} codeBytes=${Math.max(0, (code.length - 2) / 2)}`);
  appendJsonl(OUT_FILE, {
    ts: Date.now(),
    isoTime: nowIso(),
    type: "preflight_ok",
    targetContract,
    codeBytes: Math.max(0, (code.length - 2) / 2),
  });

  return { ok: true, reason: "ok" };
}

async function main() {
  const selectorMap = loadSelectorMap();
  const watchedSelectors = new Set(Object.keys(selectorMap));

  mkdirpForFile(OUT_FILE);

  const resolved = await resolveTargetContract(selectorMap);
  let targetContract = resolved.ok ? normalizeAddr(resolved.address) : "";

  appendJsonl(OUT_FILE, {
    ts: Date.now(),
    isoTime: nowIso(),
    type: "monitor_started",
    rpcUrl: RPC_URL,
    outFile: OUT_FILE,
    targetContract,
    resolveReason: resolved.reason || "unknown",
    resolveDetail: resolved,
    watchedSelectors: [...watchedSelectors].sort(),
    watchedMethods: [...WATCH_METHODS].sort(),
  });

  console.error(`[gas_monitor] rpc=${RPC_URL}`);
  console.error(`[gas_monitor] out=${OUT_FILE}`);
  console.error(`[gas_monitor] targetContract=${targetContract || "(empty)"} resolveReason=${resolved.reason || "unknown"}`);
  console.error(`[gas_monitor] watchedSelectors=${[...watchedSelectors].sort().join(",") || "(none)"}`);

  if (watchedSelectors.size === 0) {
    console.error("[gas_monitor] WARN no watched selectors loaded from build-info");
  }

  const preflight = await preflightCheckTargetContract(targetContract);
  if (!preflight.ok) {
    appendJsonl(OUT_FILE, {
      ts: Date.now(),
      isoTime: nowIso(),
      type: "fatal",
      reason: `preflight_failed:${preflight.reason}`,
    });
    process.exit(2);
  }

  let lastBlock = START_BLOCK;
  if (lastBlock == null) {
    lastBlock = await getBlockNumber();
  }

  console.error(`[gas_monitor] startBlock=${lastBlock}`);

  let mismatchCount = 0;
  const observedToCounts = {};

  while (true) {
    try {
      const latest = await getBlockNumber();
      if (latest <= lastBlock) {
        await sleep(POLL_MS);
        continue;
      }

      for (let bn = lastBlock + 1; bn <= latest; bn++) {
        const block = await getBlockByNumber(bn);
        const txs = block?.transactions || [];
        console.error(`[gas_monitor] scan block=${bn} txCount=${txs.length}`);

        for (const tx of txs) {
          const input = String(tx.input || tx.data || "0x").toLowerCase();
          if (!input || input === "0x" || input.length < 10) continue;

          const selector = input.slice(0, 10);
          if (!watchedSelectors.has(selector)) continue;

          const meta = selectorMap[selector] || {
            method: "unknown",
            signature: "unknown",
            source: "",
            contract: "",
          };

          let receipt = null;
          try {
            receipt = await getReceipt(tx.hash);
          } catch (err) {
            appendJsonl(OUT_FILE, {
              ts: Date.now(),
              isoTime: nowIso(),
              type: "receipt_error",
              blockNumber: bn,
              txHash: tx.hash,
              selector,
              method: meta.method,
              signature: meta.signature,
              to: tx.to || "",
              targetContract,
              targetMatch: normalizeAddr(tx.to) === targetContract,
              error: String(err.message || err),
            });
            console.error(`[gas_monitor] WARN receipt_error tx=${tx.hash} method=${meta.method} to=${tx.to || ""} err=${err.message || err}`);
            continue;
          }

          const observedTo = normalizeAddr(tx.to);
          const targetMatch = !!targetContract && observedTo === targetContract;
          observedToCounts[observedTo] = (observedToCounts[observedTo] || 0) + 1;

          if (!targetMatch) {
            mismatchCount += 1;
            const warnObj = {
              ts: Date.now(),
              isoTime: nowIso(),
              type: "target_contract_mismatch",
              targetContract,
              observedTo,
              method: meta.method,
              selector,
              txHash: tx.hash,
              blockNumber: bn,
              mismatchCount,
              observedToCounts,
            };
            appendJsonl(OUT_FILE, warnObj);
            console.error(`[gas_monitor] WARN target mismatch #${mismatchCount}: target=${targetContract} observed=${observedTo} method=${meta.method} tx=${tx.hash}`);

            if (AUTO_RETARGET && observedTo && observedToCounts[observedTo] >= 2) {
              const oldTargetContract = targetContract;
              targetContract = observedTo;
              console.error(`[gas_monitor] WARN auto-retarget from ${oldTargetContract} to ${targetContract}`);
              appendJsonl(OUT_FILE, {
                ts: Date.now(),
                isoTime: nowIso(),
                type: "target_contract_retargeted",
                oldTargetContract,
                newTargetContract: targetContract,
                mismatchCount,
                observedToCounts,
              });
              mismatchCount = 0;
            }

            if (STRICT_TARGET_MATCH && mismatchCount >= 2) {
              console.error(`[gas_monitor] FATAL strict mode enabled and target mismatch persisted; target=${targetContract} observed=${observedTo}`);
              appendJsonl(OUT_FILE, {
                ts: Date.now(),
                isoTime: nowIso(),
                type: "fatal",
                reason: "strict_target_mismatch",
                targetContract,
                observedTo,
                mismatchCount,
              });
              process.exit(3);
            }
          }

          const row = {
            ts: Date.now(),
            isoTime: nowIso(),
            type: "tx_hit",
            blockNumber: bn,
            blockHash: block.hash || "",
            txHash: tx.hash,
            txIndex: tx.transactionIndex != null ? hexToNum(tx.transactionIndex) : -1,
            selector,
            method: meta.method,
            signature: meta.signature,
            source: meta.source,
            contractFromBuildInfo: meta.contract,
            stage: methodToStage(meta.method),
            from: tx.from || "",
            to: tx.to || "",
            targetContract,
            targetMatch,
            nonce: tx.nonce != null ? hexToNum(tx.nonce) : -1,
            valueWei: hexToBigIntString(tx.value),
            inputBytes: Math.max(0, (input.length - 2) / 2),
            gasLimit: hexToBigIntString(tx.gas),
            gasPriceWei: hexToBigIntString(tx.gasPrice),
            maxFeePerGasWei: hexToBigIntString(tx.maxFeePerGas),
            maxPriorityFeePerGasWei: hexToBigIntString(tx.maxPriorityFeePerGas),
            status: receipt?.status != null ? hexToNum(receipt.status) : -1,
            gasUsed: receipt?.gasUsed ? hexToBigIntString(receipt.gasUsed) : "0",
            cumulativeGasUsed: receipt?.cumulativeGasUsed ? hexToBigIntString(receipt.cumulativeGasUsed) : "0",
            effectiveGasPriceWei: receipt?.effectiveGasPrice ? hexToBigIntString(receipt.effectiveGasPrice) : "0",
            note: targetMatch ? "selector_hit_target_match" : "selector_hit_target_mismatch",
          };

          appendJsonl(OUT_FILE, row);
          console.error(`[gas_monitor] hit block=${bn} tx=${tx.hash} to=${tx.to || ""} method=${meta.method} selector=${selector} targetMatch=${targetMatch} gas=${row.gasUsed}`);
        }
      }

      lastBlock = latest;
    } catch (err) {
      appendJsonl(OUT_FILE, {
        ts: Date.now(),
        isoTime: nowIso(),
        type: "monitor_error",
        error: String(err && err.message ? err.message : err),
      });
      console.error(`[gas_monitor] ERROR ${err.message || err}`);
      await sleep(POLL_MS);
    }
  }
}

main().catch((err) => {
  appendJsonl(OUT_FILE, {
    ts: Date.now(),
    isoTime: nowIso(),
    type: "fatal",
    error: String(err && err.message ? err.message : err),
  });
  console.error(`[gas_monitor] FATAL ${err.message || err}`);
  process.exit(1);
});
