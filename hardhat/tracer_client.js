#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const http = require('http');
const https = require('https');
const crypto = require('crypto');

function usage() {
  console.error(`Usage:
  node tracer_client.js trace-run --idHash 0x.. --ver 1 --epoch 10 --n 5 --t 3 --delta 1e-6 \
    --vk-file run/ttss/vk.json --committee http://127.0.0.1:8811,http://127.0.0.1:8813,http://127.0.0.1:8815 \
    --pirate http://127.0.0.1:4000 --box-id box_... --challenge-count 1 --max-queries 3 --out results/trace_result.json

  node tracer_client.js trace-verify --vk-file run/ttss/vk.json --trace-result results/trace_result.json --out results/trace_verify.json

  node tracer_client.js publish-trace --bb http://127.0.0.1:3000 --vk-file run/ttss/vk.json \
    --trace-result results/trace_result.json --trace-verify results/trace_verify.json --out results/trace_publish.json`);
  process.exit(2);
}

function sha256Hex(input) {
  const h = crypto.createHash('sha256');
  if (Array.isArray(input)) {
    for (const part of input) h.update(String(part));
  } else {
    h.update(String(input));
  }
  return '0x' + h.digest('hex');
}
function lower(s) { return String(s || '').trim().toLowerCase(); }
function ensure0xHex(s) {
  const v = lower(s);
  if (!/^0x[0-9a-f]+$/.test(v) && !/^[0-9a-f]+$/.test(v)) throw new Error('invalid_hex_string');
  return v.startsWith('0x') ? v : ('0x' + v);
}
function normalizeDigestHex32(s) {
  const v = ensure0xHex(s).slice(2);
  if (v.length > 64) throw new Error('hex_too_long');
  return '0x' + v.padStart(64, '0');
}
function fieldToHex32FromHex(s) {
  const v = ensure0xHex(s).slice(2);
  let acc = 0n;
  const mod = 65537n;
  for (const ch of v) {
    const d = BigInt(parseInt(ch, 16));
    acc = (acc * 16n + d) % mod;
  }
  const out = Buffer.alloc(32, 0);
  out[28] = Number((acc >> 24n) & 0xffn);
  out[29] = Number((acc >> 16n) & 0xffn);
  out[30] = Number((acc >> 8n) & 0xffn);
  out[31] = Number(acc & 0xffn);
  return '0x' + out.toString('hex');
}
function computeUiHexFromX(xiHex) {
  return sha256Hex(['NITS-UI', fieldToHex32FromHex(xiHex)].join('|'));
}
function parseArgs(argv) {
  const cmd = argv[2];
  if (!cmd) usage();
  const out = { _: cmd };
  for (let i = 3; i < argv.length; ++i) {
    const a = argv[i];
    if (!a.startsWith('--')) usage();
    const key = a.slice(2);
    const val = (i + 1 < argv.length && !argv[i + 1].startsWith('--')) ? argv[++i] : '1';
    out[key] = val;
  }
  return out;
}
function splitCsv(s) {
  if (!s) return [];
  return String(s).split(',').map(x => x.trim()).filter(Boolean);
}
function mkdirpForFile(file) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
}
function writeJson(file, obj) {
  mkdirpForFile(file);
  fs.writeFileSync(file, JSON.stringify(obj, null, 2) + '\n', 'utf8');
}
function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}
function canonicalize(value) {
  if (Array.isArray(value)) return value.map(canonicalize);
  if (value && typeof value === 'object' && !(value instanceof Date) && !Buffer.isBuffer(value)) {
    const out = {};
    for (const key of Object.keys(value).sort()) out[key] = canonicalize(value[key]);
    return out;
  }
  return value;
}
function stableJson(value) {
  return JSON.stringify(canonicalize(value));
}
function hashAccusedSet(accusedSet) {
  const norm = [...new Set((Array.isArray(accusedSet) ? accusedSet : []).map(x => Number(x)).filter(Number.isFinite))].sort((a, b) => a - b);
  return sha256Hex(stableJson(norm));
}
function hashProof(proof) {
  return sha256Hex(stableJson(proof || {}));
}
function hashTraceResult(traceResult) {
  return sha256Hex(stableJson(traceResult || {}));
}
function loadVk(vkFile) {
  const raw = readJson(vkFile);
  let vkObj;
  let vkEntries;
  if (Array.isArray(raw)) {
    vkEntries = raw;
    vkObj = { vk: raw };
  } else if (raw && typeof raw === 'object') {
    if (Array.isArray(raw.vk)) {
      vkEntries = raw.vk;
      vkObj = raw;
    } else if (Array.isArray(raw.entries)) {
      vkEntries = raw.entries;
      vkObj = { ...raw, vk: raw.entries };
    } else {
      vkEntries = [];
      vkObj = raw;
    }
  } else {
    vkEntries = [];
    vkObj = { vk: [] };
  }
  if (vkEntries.length === 0) throw new Error(`empty_vk_entries:${vkFile}`);
  return { vkObj, vkEntries };
}
function httpJson(method, urlStr, body) {
  return new Promise((resolve, reject) => {
    const u = new URL(urlStr);
    const lib = u.protocol === 'https:' ? https : http;
    const payload = body ? Buffer.from(JSON.stringify(body)) : null;
    const req = lib.request({
      method,
      hostname: u.hostname,
      port: u.port,
      path: u.pathname + u.search,
      headers: payload ? {
        'Content-Type': 'application/json',
        'Content-Length': payload.length,
      } : {}
    }, (res) => {
      const chunks = [];
      res.on('data', d => chunks.push(d));
      res.on('end', () => {
        const raw = Buffer.concat(chunks).toString('utf8');
        try {
          const obj = raw ? JSON.parse(raw) : {};
          resolve({ statusCode: res.statusCode, body: obj, raw });
        } catch (e) {
          reject(new Error(`invalid_json_response status=${res.statusCode} raw=${raw}`));
        }
      });
    });
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

async function fetchHonestShares(urls, requestTemplate, count) {
  const shares = [];
  const used = new Set();
  for (const base of urls) {
    if (shares.length >= count) break;
    const resp = await httpJson('POST', `${base}/shareForTrace`, requestTemplate);
    const body = resp.body || {};
    if (resp.statusCode !== 200 || body.ok !== 1) continue;
    const env = body.shareEnvelope || {};
    const idx = Number(env.guardianIndex);
    if (!idx || used.has(idx)) continue;
    used.add(idx);
    shares.push(env);
  }
  return shares;
}

function buildTraceResult(vkEntries, desc, rootsHex, meta) {
  const accusedSet = [];
  const witnessRootsHex = [];
  const seen = new Set();
  for (const root of rootsHex) {
    const rootNorm = fieldToHex32FromHex(root);
    const ui = normalizeDigestHex32(computeUiHexFromX(rootNorm));
    for (const entry of vkEntries) {
      if (lower(normalizeDigestHex32(entry.uiHex)) === lower(ui)) {
        const idx = Number(entry.guardianIndex);
        if (!seen.has(idx)) {
          seen.add(idx);
          accusedSet.push(idx);
          witnessRootsHex.push(rootNorm);
        }
      }
    }
  }
  accusedSet.sort((a, b) => a - b);
  return {
    ok: accusedSet.length > 0,
    scheme: 'NITS-Shamir-v1',
    idHash: normalizeDigestHex32(desc.idHash),
    ver: Number(desc.ver),
    epoch: Number(desc.epoch),
    n: Number(desc.n),
    t: Number(desc.t),
    delta: Number(desc.delta),
    accusedSet,
    proof: {
      proofType: 'nits_roots',
      witnessRootsHex,
    },
    traceMeta: meta,
    err: accusedSet.length > 0 ? '' : 'no_extracted_roots',
  };
}

function verifyTrace(vkEntries, traceResult) {
  const accusedSet = Array.isArray(traceResult.accusedSet) ? traceResult.accusedSet : [];
  const roots = (((traceResult || {}).proof || {}).witnessRootsHex) || [];
  if (((traceResult || {}).proof || {}).proofType !== 'nits_roots') {
    return { ok: 1, accepted: false, reason: 'unsupported_proof_type' };
  }
  if (accusedSet.length !== roots.length) {
    return { ok: 1, accepted: false, reason: 'arity_mismatch' };
  }
  if (accusedSet.length === 0) {
    return { ok: 1, accepted: false, reason: 'empty_accused_set' };
  }
  const vkMap = new Map();
  for (const e of vkEntries) vkMap.set(Number(e.guardianIndex), normalizeDigestHex32(e.uiHex));
  const seen = new Set();
  for (let i = 0; i < accusedSet.length; ++i) {
    const idx = Number(accusedSet[i]);
    if (seen.has(idx)) return { ok: 1, accepted: false, reason: 'duplicate_accused_index' };
    seen.add(idx);
    if (!vkMap.has(idx)) return { ok: 1, accepted: false, reason: 'accused_index_missing_from_vk' };
    const ui = normalizeDigestHex32(computeUiHexFromX(roots[i]));
    if (lower(ui) !== lower(vkMap.get(idx))) {
      return { ok: 1, accepted: false, reason: 'root_ui_mismatch' };
    }
  }
  return {
    ok: 1,
    accepted: true,
    idHash: normalizeDigestHex32(traceResult.idHash),
    ver: Number(traceResult.ver),
    epoch: Number(traceResult.epoch),
    checkedGuardianIndexes: accusedSet,
    reason: 'forall j, F(w_j) matches vk[i_j]',
  };
}

function parseOptionalNumber(value, fallback) {
  if (value === undefined || value === null || value === '') return fallback;
  return Number(value);
}

function isNonNegativeInteger(value) {
  return Number.isInteger(value) && value >= 0;
}

function isPositiveInteger(value) {
  return Number.isInteger(value) && value > 0;
}

async function cmdTraceRun(args) {
  const { vkObj, vkEntries } = loadVk(args['vk-file']);
  const desc = {
    idHash: args.idHash || vkObj.idHash,
    ver: parseOptionalNumber(args.ver, parseOptionalNumber(vkObj.ver, NaN)),
    epoch: parseOptionalNumber(args.epoch, parseOptionalNumber(vkObj.epoch, NaN)),
    n: parseOptionalNumber(args.n, parseOptionalNumber(vkObj.n, NaN)),
    t: parseOptionalNumber(args.t, parseOptionalNumber(vkObj.t, NaN)),
    delta: parseOptionalNumber(args.delta, 1e-6),
  };

  const missing = [];
  if (!desc.idHash) missing.push('idHash');
  if (!isNonNegativeInteger(desc.ver)) missing.push('ver');
  if (!isNonNegativeInteger(desc.epoch)) missing.push('epoch');
  if (!isPositiveInteger(desc.n)) missing.push('n');
  if (!isPositiveInteger(desc.t)) missing.push('t');
  if (!(Number.isFinite(desc.delta) && desc.delta > 0)) missing.push('delta');
  if (vkEntries.length === 0) missing.push('vk');
  if (missing.length > 0) {
    throw new Error(`missing_trace_run_inputs:${missing.join(',')}`);
  }

  const pirate = args.pirate;
  const boxId = args['box-id'];
  const challengeCommittee = splitCsv(args.committee || args['challenge-committee']);
  const challengeCount = Number(args['challenge-count'] || Math.max(1, desc.t - 1));
  const maxQueries = Number(args['max-queries'] || 3);
  const outFile = args.out;
  if (!pirate || !outFile || challengeCommittee.length === 0) throw new Error('missing_required_args');

  const queries = [];
  const allRoots = [];
  let okCount = 0;
  const traceSessionId = args['trace-session-id'] || ('trace_' + Date.now());
  for (let q = 0; q < maxQueries; ++q) {
    const traceReq = {
      token: args.token || 'demo-token',
      traceSessionId,
      idHash: normalizeDigestHex32(desc.idHash),
      ver: desc.ver,
      epoch: desc.epoch,
      requestKind: 'honest_challenge',
    };
    const honestShares = await fetchHonestShares(challengeCommittee, traceReq, challengeCount);
    const queryId = `trace_q_${q}`;
    const oracleResp = await httpJson('POST', `${pirate}/oracle`, {
      boxId,
      queryId,
      idHash: normalizeDigestHex32(desc.idHash),
      ver: desc.ver,
      epoch: desc.epoch,
      honestShares,
    });
    const body = oracleResp.body || {};
    const roots = Array.isArray(body.debugCandidateRootsHex) ? body.debugCandidateRootsHex : [];
    if (body.ok === 1) okCount += 1;
    for (const r of roots) allRoots.push(r);
    queries.push({
      queryId,
      honestGuardianIndexes: honestShares.map(s => Number(s.guardianIndex)),
      ok: body.ok === 1,
      responseType: body.resultType || '',
      err: body.err || '',
      rootCount: roots.length,
    });
  }

  const uniqueRoots = [...new Set(allRoots.map(fieldToHex32FromHex))];
  const transcriptHash = sha256Hex(JSON.stringify(queries));
  const meta = {
    guessedF: uniqueRoots.length,
    queryCount: queries.length,
    candidatePolynomialCount: uniqueRoots.length > 0 ? 1 : 0,
    oracleSuccessRate: queries.length ? (okCount / queries.length) : 0,
    transcriptHash,
    note: 'phase2_black_box_http_trace_uses_oracle_debugCandidateRootsHex',
  };
  const result = buildTraceResult(vkEntries, desc, uniqueRoots, meta);
  writeJson(outFile, result);
  writeJson(path.join(path.dirname(outFile), 'trace_queries.json'), {
    traceSessionId,
    idHash: normalizeDigestHex32(desc.idHash),
    ver: desc.ver,
    epoch: desc.epoch,
    delta: desc.delta,
    queryCount: queries.length,
    queries,
  });
  console.log(JSON.stringify({ ok: 1, out: outFile, accusedSet: result.accusedSet, err: result.err }));
}

function cmdTraceVerify(args) {
  const { vkEntries } = loadVk(args['vk-file']);
  const traceResult = readJson(args['trace-result']);
  const verify = verifyTrace(vkEntries, traceResult);
  if (args.out) writeJson(args.out, verify);
  console.log(JSON.stringify(verify));
}

async function cmdPublishTrace(args) {
  const bb = args.bb;
  const traceResultPath = args['trace-result'];
  if (!bb || !traceResultPath) throw new Error('missing_publish_trace_args');

  const traceResult = readJson(traceResultPath);
  let verify = null;
  let verifyPath = '';

  if (args['trace-verify']) {
    verifyPath = args['trace-verify'];
    verify = readJson(verifyPath);
  } else if (args['vk-file']) {
    const { vkEntries } = loadVk(args['vk-file']);
    verify = verifyTrace(vkEntries, traceResult);
    verifyPath = args['verify-out'] || '';
    if (verifyPath) writeJson(verifyPath, verify);
  } else {
    throw new Error('publish_trace_requires_trace_verify_or_vk_file');
  }

  if (!verify || verify.ok !== 1 || verify.accepted !== true) {
    throw new Error(`trace_verify_rejected:${stableJson(verify || {})}`);
  }

  const accusedSetHash = hashAccusedSet(traceResult.accusedSet);
  const proofHash = hashProof(traceResult.proof || {});
  const traceDigest = hashTraceResult(traceResult);
  const publishBody = {
    id: args.id || '',
    idHash: normalizeDigestHex32(traceResult.idHash),
    ver: Number(traceResult.ver),
    epoch: Number(traceResult.epoch),
    accusedSetHash,
    proofHash,
    traceDigest,
    traceResultPath: path.resolve(traceResultPath),
    wait: args.wait !== undefined ? Number(args.wait) : 1,
    confirmations: args.confirmations !== undefined ? Number(args.confirmations) : 1,
  };

  const resp = await httpJson('POST', `${bb}/publishTrace`, publishBody);
  const body = resp.body || {};
  if (resp.statusCode !== 200 || body.ok !== 1) {
    throw new Error(`publish_trace_http_fail status=${resp.statusCode} body=${resp.raw}`);
  }

  const out = {
    ok: 1,
    published: true,
    idHash: publishBody.idHash,
    ver: publishBody.ver,
    epoch: publishBody.epoch,
    accusedSetHash,
    proofHash,
    traceDigest,
    verifyAccepted: true,
    traceResultPath: path.resolve(traceResultPath),
    traceVerifyPath: verifyPath ? path.resolve(verifyPath) : '',
    publishResponse: body,
  };
  if (args.out) writeJson(args.out, out);
  console.log(JSON.stringify(out));
}

(async function main() {
  try {
    const args = parseArgs(process.argv);
    if (args._ === 'trace-run') return await cmdTraceRun(args);
    if (args._ === 'trace-verify') return cmdTraceVerify(args);
    if (args._ === 'publish-trace') return await cmdPublishTrace(args);
    usage();
  } catch (e) {
    console.error(`[tracer_client] fatal: ${e.message || e}`);
    process.exit(1);
  }
})();
