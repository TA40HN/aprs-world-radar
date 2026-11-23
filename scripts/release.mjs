#!/usr/bin/env node
// Release build of APRS World Radar for the browser installer and for manual flashing:
//
//   node scripts/release.mjs [--out DIR] [--no-build]
//
// 1. `platformio run -e release` (RELEASE_BUILD: include/secrets.h is never compiled in);
// 2. takes the flash images and their offsets from the build's own idedata.json (`-t idedata`; what `-t upload` writes);
// 3. checks every image (ESP magic, appended SHA-256, matching flash parameters, the version from src/config.h inside
//    the application, and none of the values of a local include/secrets.h anywhere in it);
// 4. writes DIR/{manifest.json, bootloader.bin, partitions.bin, boot_app0.bin, firmware.bin} with a SHA-256 per file
//    (the format the aprs.world installer reads), plus DIR/aprs-world-radar-<version>.bin: one image to write at 0x0
//    with any tool (`esptool write-flash 0x0 aprs-world-radar-<version>.bin`);
// 5. reads everything back and re-checks the hashes.
// Default DIR is dist/. Nothing is uploaded and no serial port is opened.

import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ENV = 'release';
const NAME = 'aprs-world-radar';
const BAUD = 460800;
const here = dirname(fileURLToPath(import.meta.url));
const project = resolve(here, '..');

const args = process.argv.slice(2);
const outIdx = args.indexOf('--out');
if (outIdx >= 0 && !args[outIdx + 1]) fail('--out needs a directory');
const outDir = resolve(outIdx >= 0 ? args[outIdx + 1] : join(project, 'dist'));

function fail(msg) {
  console.error(`release: ${msg}`);
  process.exit(1);
}
const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');

function firstWorking(candidates, versionArgs) {
  for (const c of candidates.filter(Boolean)) {
    const r = spawnSync(c[0], [...c.slice(1), ...versionArgs], { encoding: 'utf8' });
    if (r.status === 0) return c;
  }
  return null;
}

if (!args.includes('--no-build')) {
  const pio = firstWorking(
    [process.env.PLATFORMIO && [process.env.PLATFORMIO], ['/tmp/pio-venv/bin/platformio'], ['platformio'], ['pio']],
    ['--version'],
  );
  if (!pio) fail('PlatformIO not found (set $PLATFORMIO, or pip install platformio)');
  for (const extraArgs of [[], ['-t', 'idedata']]) {
    const r = spawnSync(pio[0], [...pio.slice(1), 'run', '-e', ENV, ...extraArgs], { cwd: project, stdio: 'inherit' });
    if (r.status !== 0) fail(`platformio run ${extraArgs.join(' ')} failed`);
  }
}

// ───────── images and offsets from the build ─────────
const buildDir = join(project, '.pio', 'build', ENV);
const idedataPath = join(buildDir, 'idedata.json');
if (!existsSync(idedataPath)) fail(`${idedataPath} is missing: build first (drop --no-build)`);
const extra = JSON.parse(readFileSync(idedataPath, 'utf8')).extra ?? {};
if (!Array.isArray(extra.flash_images) || typeof extra.application_offset !== 'string') fail('idedata.json has no flash image list');
const offset = (s) => {
  const n = Number(s);
  if (!Number.isInteger(n) || n < 0 || n % 4096 !== 0) fail(`bad flash offset ${s}`);
  return n;
};
const appPath = join(buildDir, 'firmware.bin');
const slots = [...extra.flash_images.map((i) => ({ path: i.path, offset: offset(i.offset) })), { path: appPath, offset: offset(extra.application_offset) }];
const NAMES = ['bootloader.bin', 'partitions.bin', 'boot_app0.bin', 'firmware.bin'];
const files = slots.map((s) => {
  const name = s.path.split('/').pop();
  if (!NAMES.includes(name)) fail(`unexpected flash image ${s.path}`);
  if (!existsSync(s.path)) fail(`${s.path} is missing`);
  return { name, offset: s.offset, data: readFileSync(s.path) };
});
for (const n of NAMES) if (!files.some((f) => f.name === n)) fail(`the build lists no ${n}`);
files.sort((a, b) => a.offset - b.offset);
files.forEach((f, i) => {
  const next = files[i + 1];
  if (next && f.offset + f.data.length > next.offset) fail(`${f.name} overlaps ${next.name}`);
});

// ───────── checks ─────────
const boot = files.find((f) => f.name === 'bootloader.bin').data;
const app = files.find((f) => f.name === 'firmware.bin').data;
for (const [n, d] of [['bootloader.bin', boot], ['firmware.bin', app]]) if (d.length < 32 || d[0] !== 0xe9) fail(`${n} is not an ESP image`);
if (app[23] === 1 && sha256(app.subarray(0, app.length - 32)) !== app.subarray(app.length - 32).toString('hex')) {
  fail('firmware.bin: its appended SHA-256 does not match (corrupt build)');
}
if (boot[2] !== app[2] || boot[3] !== app[3]) fail('flash parameters differ between bootloader and application headers');
const flashMode = { 0: 'qio', 1: 'qout', 2: 'dio', 3: 'dout' }[boot[2]];
const flashFreq = { 0x0: '40m', 0x1: '26m', 0x2: '20m', 0xf: '80m' }[boot[3] & 0x0f];
const flashSize = { 0x0: '1MB', 0x1: '2MB', 0x2: '4MB', 0x3: '8MB', 0x4: '16MB' }[boot[3] >> 4];
if (!flashMode || !flashFreq || !flashSize) fail('unknown flash parameters in the image header');

const version = /#define FW_VERSION "(\d+\.\d+\.\d+)"/.exec(readFileSync(join(project, 'src', 'config.h'), 'utf8'))?.[1];
if (!version) fail('src/config.h has no #define FW_VERSION "x.y.z"');
if (!app.includes(Buffer.from(`${version}\0`)) && !app.includes(Buffer.from(`v${version}`))) fail(`firmware.bin does not contain ${version}: stale build`);

const secretsPath = join(project, 'include', 'secrets.h');
if (existsSync(secretsPath)) {
  for (const [, key, value] of readFileSync(secretsPath, 'utf8').matchAll(/#define\s+(\w+)\s+"([^"]+)"/g)) {
    if (value.length >= 4 && app.includes(Buffer.from(value))) fail(`firmware.bin contains the value of ${key} from include/secrets.h`);
  }
}

// ───────── one merged image ─────────
const flashBytes = { '1MB': 1, '2MB': 2, '4MB': 4, '8MB': 8, '16MB': 16 }[flashSize] * 1024 * 1024;
const end = files.at(-1).offset + files.at(-1).data.length;
if (end > flashBytes) fail(`the images end at 0x${end.toString(16)}, beyond ${flashSize}`);
const merged = Buffer.alloc(end, 0xff);
for (const f of files) f.data.copy(merged, f.offset);
const mergedName = `${NAME}-${version}.bin`;

// ───────── write ─────────
mkdirSync(outDir, { recursive: true });
const manifest = {
  name: NAME,
  version,
  builtAt: statSync(appPath).mtime.toISOString(),
  chip: 'esp32',
  flashMode,
  flashFreq,
  flashSize,
  baud: BAUD,
  files: files.map((f) => ({ name: f.name, offset: f.offset, bytes: f.data.length, sha256: sha256(f.data) })),
  merged: { name: mergedName, offset: 0, bytes: merged.length, sha256: sha256(merged) },
};
for (const f of files) writeFileSync(join(outDir, f.name), f.data);
writeFileSync(join(outDir, mergedName), merged);
writeFileSync(join(outDir, 'manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`);

// ───────── read back ─────────
const written = JSON.parse(readFileSync(join(outDir, 'manifest.json'), 'utf8'));
for (const e of [...written.files, written.merged]) {
  const d = readFileSync(join(outDir, e.name));
  if (d.length !== e.bytes || sha256(d) !== e.sha256) fail(`read-back check failed for ${e.name}`);
}
console.log(`${NAME} ${version} → ${outDir}`);
for (const e of [...written.files, written.merged]) {
  console.log(`  0x${e.offset.toString(16).padStart(5, '0')}  ${e.name.padEnd(28)} ${String(e.bytes).padStart(8)} B  ${e.sha256}`);
}
console.log(`  esp32, flash ${flashMode}/${flashFreq}/${flashSize}`);
