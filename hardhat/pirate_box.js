#!/usr/bin/env node
'use strict';

const http = require('http');
const { URL } = require('url');
const crypto = require('crypto');

const DEFAULT_PORT = parseInt(process.env.PORT || '4000', 10);
const TOKEN = process.env.TTSS_BOX_TOKEN || 'demo-token';
const FIELD_MOD = 65537n;
const SECRET_CHUNK_COUNT = 16;
const PACKED_Y_BYTES = 34;
const SCHEME = 'NITS-Shamir-v1';

function nowMs() { return Date.now(); }
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
function hexToBytes(hex) {
  const v = ensure0xHex(hex).slice(2);
  if (v.length % 2 !== 0) throw new Error('odd_hex_length');
  return Buffer.from(v, 'hex');
}
function bytesToHex(buf) { return '0x' + Buffer.from(buf).toString('hex'); }
function json(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body)
  });
  res.end(body);
}
function parseJsonBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', d => chunks.push(d));
    req.on('end', () => {
      try {
        const raw = Buffer.concat(chunks).toString('utf8');
        resolve(raw ? JSON.parse(raw) : {});
      } catch (e) { reject(e); }
    });
    req.on('error', reject);
  });
}
function checkToken(bodyOrQuery) {
  const tok = bodyOrQuery.token || bodyOrQuery.get?.('token');
  return !TOKEN || tok === TOKEN;
}

function fieldNormalize(x) {
  x %= FIELD_MOD;
  if (x < 0n) x += FIELD_MOD;
  return x;
}
function fieldFromHex(hex) {
  const body = ensure0xHex(hex).slice(2);
  let acc = 0n;
  for (const ch of body) {
    const digit = BigInt(parseInt(ch, 16));
    acc = (acc * 16n + digit) % FIELD_MOD;
  }
  return acc;
}
function fieldToHex32(x) {
  x = fieldNormalize(x);
  const out = Buffer.alloc(32, 0);
  let v = x;
  out[28] = Number((v >> 24n) & 0xffn);
  out[29] = Number((v >> 16n) & 0xffn);
  out[30] = Number((v >> 8n) & 0xffn);
  out[31] = Number(v & 0xffn);
  return bytesToHex(out);
}
function computeUiHexFromX(xiHex) {
  return sha256Hex(['NITS-UI', fieldToHex32(fieldFromHex(xiHex))].join('|'));
}
function addMod(a, b) { return (a + b) % FIELD_MOD; }
function subMod(a, b) { return (a - b + FIELD_MOD) % FIELD_MOD; }
function mulMod(a, b) { return (a * b) % FIELD_MOD; }
function powMod(base, exp) {
  let out = 1n;
  let b = fieldNormalize(base);
  let e = BigInt(exp);
  while (e > 0n) {
    if (e & 1n) out = mulMod(out, b);
    b = mulMod(b, b);
    e >>= 1n;
  }
  return out;
}
function invMod(x) {
  x = fieldNormalize(x);
  if (x === 0n) throw new Error('inverse_of_zero');
  return powMod(x, FIELD_MOD - 2n);
}
function unpackYChunksHex(packedHex) {
  const inBuf = hexToBytes(packedHex);
  if (inBuf.length !== PACKED_Y_BYTES) throw new Error('bad_packed_y_size');
  const out = [];
  let bitPos = 0;
  for (let i = 0; i < SECRET_CHUNK_COUNT; ++i) {
    let v = 0n;
    for (let bit = 16; bit >= 0; --bit) {
      const byteIdx = Math.floor(bitPos / 8);
      const bitInByte = 7 - (bitPos % 8);
      const b = BigInt((inBuf[byteIdx] >> bitInByte) & 1);
      v = (v << 1n) | b;
      bitPos += 1;
    }
    out.push(v);
  }
  return out;
}
function chunksToSeedHex(chunks) {
  if (chunks.length !== SECRET_CHUNK_COUNT) throw new Error('bad_chunk_count');
  const bytes = Buffer.alloc(32, 0);
  for (let i = 0; i < SECRET_CHUNK_COUNT; ++i) {
    const v = Number(chunks[i]);
    if (v < 0 || v > 0xffff) throw new Error('recovered_chunk_out_of_16bit_range');
    bytes[2 * i] = (v >> 8) & 0xff;
    bytes[2 * i + 1] = v & 0xff;
  }
  return bytesToHex(bytes);
}
function lagrangeAtZero(points) {
  let secret = 0n;
  for (let i = 0; i < points.length; ++i) {
    const [xi, yi] = points[i];
    let num = 1n;
    let den = 1n;
    for (let j = 0; j < points.length; ++j) {
      if (j === i) continue;
      const [xj] = points[j];
      num = mulMod(num, subMod(0n, xj));
      den = mulMod(den, subMod(xi, xj));
    }
    const li0 = mulMod(num, invMod(den));
    secret = addMod(secret, mulMod(yi, li0));
  }
  return secret;
}
function reconstructSeedFromShares(envelopes) {
  if (!Array.isArray(envelopes) || envelopes.length === 0) throw new Error('no_shares');
  const first = envelopes[0];
  const t = Number(first.t);
  const seen = new Set();
  const points = [];
  for (const env of envelopes) {
    if (!env || env.scheme !== SCHEME) throw new Error('scheme_mismatch');
    if (String(env.idHash).toLowerCase() !== String(first.idHash).toLowerCase()) throw new Error('share_header_mismatch');
    if (Number(env.ver) !== Number(first.ver) || Number(env.epoch) !== Number(first.epoch)) throw new Error('share_header_mismatch');
    if (Number(env.n) !== Number(first.n) || Number(env.t) !== Number(first.t)) throw new Error('share_header_mismatch');
    if (env.secretType !== first.secretType) throw new Error('share_header_mismatch');
    if (env.active === false) throw new Error('inactive_share_present');
    const xiHex = fieldToHex32(fieldFromHex(env.share.xiHex));
    if (seen.has(xiHex)) continue;
    seen.add(xiHex);
    points.push([fieldFromHex(env.share.xiHex), unpackYChunksHex(env.share.yiHex)]);
    if (points.length === t) break;
  }
  if (points.length < t) throw new Error('not_enough_distinct_points');
  const recovered = [];
  for (let c = 0; c < SECRET_CHUNK_COUNT; ++c) {
    const chunkPoints = points.map(p => [p[0], p[1][c]]);
    const rc = lagrangeAtZero(chunkPoints);
    if (rc > 0xffffn) throw new Error('recovered_chunk_out_of_range');
    recovered.push(rc);
  }
  return chunksToSeedHex(recovered);
}
function validateShareEnvelopeShape(env) {
  if (!env || typeof env !== 'object') throw new Error('bad_envelope');
  if (env.scheme !== SCHEME) throw new Error('scheme_mismatch');
  if (!env.idHash) throw new Error('missing_idHash');
  if (!env.share || !env.share.xiHex || !env.share.yiHex) throw new Error('missing_share');
  if (!env.traceBinding || !env.traceBinding.uiHex) throw new Error('missing_traceBinding');
  // weak consistency check
  const ui = computeUiHexFromX(env.share.xiHex);
  if (lower(ui) !== lower(normalizeDigestHex32(env.traceBinding.uiHex))) throw new Error('ui_mismatch');
}

const state = {
  boxId: null,
  loadedAtMs: 0,
  mode: 'stateless',
  outputMode: 'seed',
  config: null,
  leakedShares: [],
  queryCount: 0,
};

function clearBox() {
  state.boxId = null;
  state.loadedAtMs = 0;
  state.mode = 'stateless';
  state.outputMode = 'seed';
  state.config = null;
  state.leakedShares = [];
  state.queryCount = 0;
}

async function handle(req, res) {
  const url = new URL(req.url, `http://${req.headers.host}`);
  try {
    if (req.method === 'GET' && url.pathname === '/health') {
      return json(res, 200, {
        ok: 1,
        service: 'pirate_box',
        scheme: SCHEME,
        mode: state.mode,
        hasBox: !!state.boxId,
        boxId: state.boxId,
        queryCount: state.queryCount,
      });
    }

    if (req.method === 'POST' && url.pathname === '/clear') {
      const body = await parseJsonBody(req);
      if (!checkToken(body)) return json(res, 403, { ok: 0, err: 'bad_token' });
      clearBox();
      return json(res, 200, { ok: 1, cleared: 1 });
    }

    if (req.method === 'GET' && url.pathname === '/describe') {
      if (!state.boxId) return json(res, 404, { ok: 0, err: 'no_box_loaded' });
      const loadedGuardianIndexes = state.leakedShares.map(s => Number(s.guardianIndex)).sort((a,b)=>a-b);
      return json(res, 200, {
        ok: 1,
        boxId: state.boxId,
        scheme: SCHEME,
        mode: state.mode,
        idHash: state.config.idHash,
        ver: state.config.ver,
        epoch: state.config.epoch,
        n: state.config.n,
        t: state.config.t,
        f: state.config.f,
        loadedGuardianIndexes,
        outputMode: state.outputMode,
        queryCount: state.queryCount,
      });
    }

    if (req.method === 'POST' && url.pathname === '/loadStaticBox') {
      const body = await parseJsonBody(req);
      if (!checkToken(body)) return json(res, 403, { ok: 0, err: 'bad_token' });
      const cfg = body.boxConfig || {};
      if ((cfg.scheme || SCHEME) !== SCHEME) return json(res, 400, { ok: 0, err: 'unsupported_scheme' });
      if (!Array.isArray(cfg.leakedShares) || cfg.leakedShares.length === 0) {
        return json(res, 400, { ok: 0, err: 'missing_leakedShares' });
      }
      const first = cfg.leakedShares[0];
      const leakedShares = [];
      for (const env of cfg.leakedShares) {
        validateShareEnvelopeShape(env);
        if (lower(env.idHash) !== lower(first.idHash)) throw new Error('share_header_mismatch');
        if (Number(env.ver) !== Number(first.ver) || Number(env.epoch) !== Number(first.epoch)) throw new Error('share_header_mismatch');
        leakedShares.push(env);
      }
      const boxId = cfg.boxId || ('box_' + nowMs());
      state.boxId = boxId;
      state.loadedAtMs = nowMs();
      state.mode = cfg.mode || 'stateless';
      state.outputMode = cfg.outputMode || 'seed';
      state.config = {
        scheme: SCHEME,
        idHash: normalizeDigestHex32(first.idHash),
        ver: Number(first.ver),
        epoch: Number(first.epoch),
        n: Number(first.n),
        t: Number(first.t),
        f: leakedShares.length,
        secretType: first.secretType || 'srec_seed',
      };
      state.leakedShares = leakedShares;
      state.queryCount = 0;
      return json(res, 200, { ok: 1, boxId, loaded: leakedShares.length, mode: state.mode });
    }

    if (req.method === 'POST' && url.pathname === '/oracle') {
      const body = await parseJsonBody(req);
      if (!state.boxId) return json(res, 404, { ok: 0, err: 'no_box_loaded' });
      const boxId = body.boxId || state.boxId;
      if (boxId !== state.boxId) return json(res, 404, { ok: 0, err: 'unknown_box' });
      const honestShares = Array.isArray(body.honestShares) ? body.honestShares : [];
      const queryId = body.queryId || ('q_' + (state.queryCount + 1));
      const allShares = [...state.leakedShares];
      for (const env of honestShares) {
        validateShareEnvelopeShape(env);
        allShares.push(env);
      }
      state.queryCount += 1;
      try {
        const seed = reconstructSeedFromShares(allShares);
        const roots = state.leakedShares.map(s => fieldToHex32(fieldFromHex(s.share.xiHex)));
        if (state.outputMode === 'seed') {
          return json(res, 200, {
            ok: 1,
            boxId: state.boxId,
            queryId,
            resultType: 'seed',
            srecSeedHex: normalizeDigestHex32(seed),
            epsilonHint: 1.0,
            debugCandidateRootsHex: roots,
          });
        }
        return json(res, 200, {
          ok: 1,
          boxId: state.boxId,
          queryId,
          resultType: 'reset_auth',
          resetAuth: {
            idHash: state.config.idHash,
            derivedFromSeedHex: normalizeDigestHex32(seed),
            newVersion: state.config.ver + 1,
            nonce: sha256Hex(`reset-auth|${state.config.idHash}|${queryId}`),
            expiry: nowMs() + 300000,
            sigHex: sha256Hex(`reset-auth-sig|${state.config.idHash}|${queryId}`),
          },
          epsilonHint: 1.0,
          debugCandidateRootsHex: roots,
        });
      } catch (e) {
        return json(res, 200, {
          ok: 0,
          boxId: state.boxId,
          queryId,
          err: String(e && e.message || e),
          debugCandidateRootsHex: state.leakedShares.map(s => fieldToHex32(fieldFromHex(s.share.xiHex))),
        });
      }
    }

    return json(res, 404, { ok: 0, err: 'not_found' });
  } catch (e) {
    return json(res, 500, { ok: 0, err: String(e && e.message || e) });
  }
}

const server = http.createServer((req, res) => {
  handle(req, res);
});

server.listen(DEFAULT_PORT, '127.0.0.1', () => {
  console.log(`[pirate_box] listening on http://127.0.0.1:${DEFAULT_PORT}`);
});
