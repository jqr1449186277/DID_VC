#!/usr/bin/env node
import fs from 'fs';
import path from 'path';

function getArg(name, fallback) {
  const idx = process.argv.indexOf(`--${name}`);
  if (idx >= 0 && idx + 1 < process.argv.length) return process.argv[idx + 1];
  return process.env[name.toUpperCase()] ?? fallback;
}

function hexToDecString(hex) {
  return BigInt(hex).toString(10);
}

const vectorPath = getArg('vector', 'zk_inputs/vector_alice.json');
const pathPath = getArg('pathJson', 'zk_inputs/alice_path.json');
const outPath = getArg('out', 'zk_inputs/auth_membership_alice.json');
const ctxHash = getArg('ctxHash', '123456');
const sessPkHash = getArg('sessPkHash', '654321');

const vec = JSON.parse(fs.readFileSync(vectorPath, 'utf8'));
const p = JSON.parse(fs.readFileSync(pathPath, 'utf8'));
if (!p.ok) {
  console.error('path json has ok=0');
  process.exit(1);
}

const out = {
  root: hexToDecString(p.root),
  ctxHash: String(ctxHash),
  sessPkHash: String(sessPkHash),
  epoch: String(p.epoch),
  sid: String(vec.sid),
  rho: String(vec.rho),
  pkNormHash: String(vec.pkNormHash),
  pkRecHash: String(vec.pkRecHash),
  ver: String(vec.ver),
  pathElements: p.pathElements.map(hexToDecString),
  pathIndex: p.pathIndex.map((x) => String(x))
};

fs.mkdirSync(path.dirname(outPath), { recursive: true });
fs.writeFileSync(outPath, JSON.stringify(out, null, 2));
console.log(JSON.stringify(out, null, 2));
console.error(`wrote ${outPath}`);
