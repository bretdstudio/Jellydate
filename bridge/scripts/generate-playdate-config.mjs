import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { config as loadDotenv } from 'dotenv';

const bridgeRoot = fileURLToPath(new URL('../', import.meta.url));
const projectRoot = fileURLToPath(new URL('../../', import.meta.url));
loadDotenv({ path: `${bridgeRoot}.env`, quiet: true });

const host = process.argv[2]?.trim();
const token = process.env.JELLYDATE_TOKEN?.trim();
if (!host || !/^[A-Za-z0-9.-]+$/.test(host)) {
  throw new Error('Usage: node scripts/generate-playdate-config.mjs <bridge-host>');
}
if (!token || token.length < 16 || /[^\x20-\x7e]/.test(token)) {
  throw new Error('JELLYDATE_TOKEN must be at least 16 printable ASCII characters');
}

function cString(value) {
  return `"${value.replaceAll('\\', '\\\\').replaceAll('"', '\\"')}"`;
}

const output = [
  '#pragma once',
  '',
  '/* Generated from bridge/.env. This file is intentionally git-ignored. */',
  `#define JELLYDATE_BRIDGE_HOST ${cString(host)}`,
  `#define JELLYDATE_TOKEN ${cString(token)}`,
  '',
].join('\n');

writeFileSync(`${projectRoot}playdate/src/config_private.h`, output, { mode: 0o600 });
console.log(`Generated private Playdate config for ${host} (token length ${token.length}).`);
