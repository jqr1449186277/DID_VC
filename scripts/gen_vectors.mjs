#!/usr/bin/env node
import fs from 'fs';
import path from 'path';
import { computeCidAndLeaf, toHex32 } from './leaf_utils.mjs';

function getArg(name, fallback) {
  const idx = process.argv.indexOf(`--${name}`);
  if (idx >= 0 && idx + 1 < process.argv.length) return process.argv[idx + 1];
  return process.env[name.toUpperCase()] ?? fallback;
}

const sid = BigInt(getArg('sid', '12345'));
const rho = BigInt(getArg('rho', '67890'));
const pkNormHash = BigInt(getArg('pkNormHash', '111111'));
const pkRecHash = BigInt(getArg('pkRecHash', '222222'));
const ver = BigInt(getArg('ver', '0'));
const active = BigInt(getArg('active', '1'));
const outPath = getArg('out', 'zk_inputs/vector_alice.json');

const { cid, leaf } = await computeCidAndLeaf({ sid, rho, pkNormHash, pkRecHash, ver, active });

const out = {
  sid: sid.toString(),
  rho: rho.toString(),
  pkNormHash: pkNormHash.toString(),
  pkNormHashHex: toHex32(pkNormHash),
  pkRecHash: pkRecHash.toString(),
  pkRecHashHex: toHex32(pkRecHash),
  ver: ver.toString(),
  active: active.toString(),
  cidDec: cid.toString(),
  cidHex: toHex32(cid),
  leafDec: leaf.toString(),
  leafHex: toHex32(leaf)
};

fs.mkdirSync(path.dirname(outPath), { recursive: true });
fs.writeFileSync(outPath, JSON.stringify(out, null, 2));
console.log(JSON.stringify(out, null, 2));
console.error(`wrote ${outPath}`);