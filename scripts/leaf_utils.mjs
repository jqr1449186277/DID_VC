#!/usr/bin/env node
import path from 'path';
import { fileURLToPath } from 'url';
import { createRequire } from 'module';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

function makeRequire(baseDir) {
  try {
    return createRequire(path.join(baseDir, 'package.json'));
  } catch {
    return null;
  }
}

function resolveRequire() {
  const candidates = [
    __dirname,
    path.join(__dirname, '..'),
    path.join(__dirname, '..', 'hardhat'),
    process.cwd(),
    path.join(process.cwd(), 'hardhat')
  ];
  for (const base of candidates) {
    const req = makeRequire(base);
    if (!req) continue;
    try {
      req.resolve('circomlibjs');
      return req;
    } catch {
      // try next base
    }
  }
  return createRequire(import.meta.url);
}

const require = resolveRequire();
const { buildPoseidon } = require('circomlibjs');

let cachedPoseidon = null;
let cachedField = null;

function asBigInt(v) {
  return BigInt(v);
}

export function toDecString(v) {
  return asBigInt(v).toString(10);
}

export function toHex32(v) {
  let h = asBigInt(v).toString(16);
  while (h.length < 64) h = '0' + h;
  return '0x' + h;
}

export async function getPoseidonContext() {
  if (!cachedPoseidon) {
    cachedPoseidon = await buildPoseidon();
    cachedField = cachedPoseidon.F;
  }
  return { poseidon: cachedPoseidon, F: cachedField };
}

export async function poseidonHash(inputs) {
  const { poseidon, F } = await getPoseidonContext();
  const out = poseidon(inputs.map((x) => asBigInt(x)));
  return asBigInt(F.toString(out));
}

export async function computeCid(sid, rho) {
  return poseidonHash([sid, rho]);
}

export async function computeLeaf(cid, pkNormHash, pkRecHash, ver, active = 1n) {
  return poseidonHash([cid, pkNormHash, pkRecHash, ver, active]);
}

export async function computeCidAndLeaf({ sid, rho, pkNormHash, pkRecHash, ver, active = 1n }) {
  const cid = await computeCid(sid, rho);
  const leaf = await computeLeaf(cid, pkNormHash, pkRecHash, ver, active);
  return { cid, leaf };
}
