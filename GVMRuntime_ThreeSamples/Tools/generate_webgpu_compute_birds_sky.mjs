#!/usr/bin/env node

import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

/** Copies one typed-array view without unrelated backing-buffer bytes. */
function typedArrayBytes(array) {
  return Buffer.from(array.buffer, array.byteOffset, array.byteLength);
}

/** Generates the exact r185 detail-six IcosahedronGeometry used by the sky. */
async function generateSky(upstreamRoot, outputPath) {
  const THREE = await import(
    pathToFileURL(path.join(upstreamRoot, 'build/three.module.js')).href
  );
  const geometry = new THREE.IcosahedronGeometry(1, 6);
  const positions = geometry.getAttribute('position');
  if (!positions) {
    throw new Error('webgpu_compute_birds sky has no position attribute.');
  }
  const indices = geometry.index
    ? new Uint32Array(geometry.index.array)
    : Uint32Array.from({ length: positions.count }, (_, index) => index);
  const header = Buffer.allocUnsafe(16);
  header.write('GVMBIRD1', 0, 'ascii');
  header.writeUInt32LE(positions.count, 8);
  header.writeUInt32LE(indices.length, 12);
  await mkdir(path.dirname(outputPath), { recursive: true });
  await writeFile(
    outputPath,
    Buffer.concat([
      header,
      typedArrayBytes(positions.array),
      typedArrayBytes(indices)
    ])
  );
}

const [upstreamRoot, outputPath] = process.argv.slice(2);
if (!upstreamRoot || !outputPath) {
  throw new Error(
    'Usage: generate_webgpu_compute_birds_sky.mjs <three-r185-root> <output-path>'
  );
}
await generateSky(path.resolve(upstreamRoot), path.resolve(outputPath));
