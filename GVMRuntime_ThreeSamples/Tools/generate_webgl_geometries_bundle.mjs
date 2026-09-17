#!/usr/bin/env node

import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

/** Writes one unsigned 32-bit integer to the deterministic little-endian bundle. */
function pushUint32(chunks, value) {
  const bytes = Buffer.allocUnsafe(4);
  bytes.writeUInt32LE(value, 0);
  chunks.push(bytes);
}

/** Writes one UTF-8 identifier prefixed by its unsigned 32-bit byte count. */
function pushString(chunks, value) {
  const bytes = Buffer.from(value, 'utf8');
  pushUint32(chunks, bytes.length);
  chunks.push(bytes);
}

/** Copies a typed array without exposing its backing buffer's unrelated bytes. */
function typedArrayBytes(array) {
  return Buffer.from(array.buffer, array.byteOffset, array.byteLength);
}

/** Generates the exact r185 geometry attributes consumed by webgl_geometries. */
async function generateBundle(upstreamRoot, outputPath) {
  const THREE = await import(pathToFileURL(path.join(upstreamRoot, 'build/three.module.js')).href);
  const { ParametricGeometry } = await import(
    pathToFileURL(path.join(upstreamRoot, 'examples/jsm/geometries/ParametricGeometry.js')).href
  );
  const { plane, klein, mobius } = await import(
    pathToFileURL(path.join(upstreamRoot, 'examples/jsm/geometries/ParametricFunctions.js')).href
  );

  const lathePoints = [];
  for (let index = 0; index < 50; index += 1) {
    lathePoints.push(new THREE.Vector2(
      Math.sin(index * 0.2) * Math.sin(index * 0.1) * 15 + 50,
      (index - 5) * 2
    ));
  }
  const planeSurface = new ParametricGeometry(plane, 10, 10);
  planeSurface.scale(100, 100, 100);
  planeSurface.center();
  const kleinSurface = new ParametricGeometry(klein, 20, 20);
  const mobiusSurface = new ParametricGeometry(mobius, 20, 20);
  const geometries = [
    ['sphere', new THREE.SphereGeometry(75, 20, 10)],
    ['icosahedron', new THREE.IcosahedronGeometry(75)],
    ['octahedron', new THREE.OctahedronGeometry(75)],
    ['tetrahedron', new THREE.TetrahedronGeometry(75)],
    ['plane', new THREE.PlaneGeometry(100, 100, 4, 4)],
    ['box', new THREE.BoxGeometry(100, 100, 100, 4, 4, 4)],
    ['circle', new THREE.CircleGeometry(50, 20, 0, Math.PI * 2)],
    ['ring', new THREE.RingGeometry(10, 50, 20, 5, 0, Math.PI * 2)],
    ['cylinder', new THREE.CylinderGeometry(25, 75, 100, 40, 5)],
    ['lathe', new THREE.LatheGeometry(lathePoints, 20)],
    ['torus', new THREE.TorusGeometry(50, 20, 20, 20)],
    ['torus-knot', new THREE.TorusKnotGeometry(50, 10, 50, 20)],
    ['capsule', new THREE.CapsuleGeometry(20, 50)],
    ['parametric-plane', planeSurface],
    ['klein', kleinSurface],
    ['mobius', mobiusSurface]
  ];

  const chunks = [Buffer.from('GVMGEO01', 'ascii')];
  pushUint32(chunks, geometries.length);
  for (const [name, geometry] of geometries) {
    const position = geometry.getAttribute('position');
    const normal = geometry.getAttribute('normal');
    const uv = geometry.getAttribute('uv');
    if (!position || !normal || !uv) {
      throw new Error(`${name} is missing a required position, normal, or uv attribute.`);
    }
    const indices = geometry.index
      ? new Uint32Array(geometry.index.array)
      : Uint32Array.from({ length: position.count }, (_, index) => index);
    pushString(chunks, name);
    pushUint32(chunks, position.count);
    pushUint32(chunks, indices.length);
    chunks.push(typedArrayBytes(position.array));
    chunks.push(typedArrayBytes(normal.array));
    chunks.push(typedArrayBytes(uv.array));
    chunks.push(typedArrayBytes(indices));
  }
  await mkdir(path.dirname(outputPath), { recursive: true });
  await writeFile(outputPath, Buffer.concat(chunks));
}

const [upstreamRoot, outputPath] = process.argv.slice(2);
if (!upstreamRoot || !outputPath) {
  throw new Error('Usage: generate_webgl_geometries_bundle.mjs <three-r185-root> <output-path>');
}
await generateBundle(path.resolve(upstreamRoot), path.resolve(outputPath));
