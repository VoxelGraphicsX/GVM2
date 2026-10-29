#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

import {
  advanceThreeRandomState,
  defaultThreeRandomSeed,
  threeRandomFloatFromState
} from '../../../tests/runners/three/node/determinism.mjs';

const expectedVertexCount = 32_442;
const expectedFinalRandomState = 3_268_735_856;
const recordFloatCount = 10;

/** Parses unique value-bearing options required by the deterministic asset generator. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) throw new Error(`Duplicate --${name}.`);
    options[name] = value;
  }
  return options;
}

/** Returns one required absolute path option. */
function requirePath(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name}.`);
  return path.resolve(options[name]);
}

/** Converts Three's deterministic HSL face color into linear working-space RGB. */
function createFaceColor(Color, hue, saturation, lightness) {
  const color = new Color();
  color.setHSL(hue, saturation, lightness);
  return color;
}

/** Builds the exact centered, tessellated, seeded r185 vertex stream. */
export async function generateAsset(upstreamRoot, assetPackRoot, outputPath, seed = defaultThreeRandomSeed) {
  let randomState = Number(seed) >>> 0;
  const originalRandom = Math.random;
  Math.random = () => {
    randomState = advanceThreeRandomState(randomState);
    return threeRandomFloatFromState(randomState);
  };
  try {
    const [{ Color, Mesh, PerspectiveCamera, Scene, ShaderMaterial }, { Font }, { TextGeometry }, { TessellateModifier }] = await Promise.all([
      import(pathToFileURL(path.join(upstreamRoot, 'build', 'three.module.js')).href),
      import(pathToFileURL(path.join(upstreamRoot, 'examples', 'jsm', 'loaders', 'FontLoader.js')).href),
      import(pathToFileURL(path.join(upstreamRoot, 'examples', 'jsm', 'geometries', 'TextGeometry.js')).href),
      import(pathToFileURL(path.join(upstreamRoot, 'examples', 'jsm', 'modifiers', 'TessellateModifier.js')).href)
    ]);
    const fontPath = path.join(assetPackRoot, 'fonts', 'helvetiker_bold.typeface.json');
    const font = new Font(JSON.parse(await fs.readFile(fontPath, 'utf8')));
    new PerspectiveCamera(40, 800 / 500, 1, 10_000);
    new Scene();
    let geometry = new TextGeometry('THREE.JS', {
      font,
      size: 40,
      depth: 5,
      curveSegments: 3,
      bevelThickness: 2,
      bevelSize: 1,
      bevelEnabled: true
    });
    geometry.center();
    geometry = new TessellateModifier(8, 6).modify(geometry);
    const positions = geometry.getAttribute('position');
    const normals = geometry.getAttribute('normal');
    if (positions.count !== expectedVertexCount || normals.count !== expectedVertexCount) {
      throw new Error(`Unexpected tessellated vertex count ${positions.count}; expected ${expectedVertexCount}.`);
    }

    const headerByteCount = 20;
    const output = Buffer.allocUnsafe(headerByteCount + expectedVertexCount * recordFloatCount * 4);
    output.write('TESSR185', 0, 8, 'ascii');
    output.writeUInt32LE(1, 8);
    output.writeUInt32LE(expectedVertexCount, 12);
    let outputOffset = headerByteCount;
    for (let face = 0; face < expectedVertexCount / 3; face += 1) {
      const hue = 0.2 * Math.random();
      const saturation = 0.5 + 0.5 * Math.random();
      const lightness = 0.5 + 0.5 * Math.random();
      const color = createFaceColor(Color, hue, saturation, lightness);
      const displacement = 10 * (0.5 - Math.random());
      for (let corner = 0; corner < 3; corner += 1) {
        const vertexIndex = face * 3 + corner;
        const values = [
          positions.getX(vertexIndex), positions.getY(vertexIndex), positions.getZ(vertexIndex),
          normals.getX(vertexIndex), normals.getY(vertexIndex), normals.getZ(vertexIndex),
          color.r, color.g, color.b, displacement
        ];
        for (const value of values) {
          output.writeFloatLE(value, outputOffset);
          outputOffset += 4;
        }
      }
    }
    const material = new ShaderMaterial({ uniforms: { amplitude: { value: 0 } } });
    new Mesh(geometry, material);
    for (let rendererRandomDraw = 0; rendererRandomDraw < 36; rendererRandomDraw += 1) {
      Math.random();
    }
    output.writeUInt32LE(randomState, 16);
    if (randomState !== expectedFinalRandomState) {
      throw new Error(`Random stream ended at ${randomState}; expected ${expectedFinalRandomState}.`);
    }
    await fs.mkdir(path.dirname(outputPath), { recursive: true });
    await fs.writeFile(outputPath, output);
    return { outputPath, vertexCount: expectedVertexCount, finalRandomState: randomState };
  } finally {
    Math.random = originalRandom;
  }
}

/** Runs the standalone deterministic asset generator. */
async function main() {
  const options = parseArguments(process.argv);
  const result = await generateAsset(
    requirePath(options, 'upstream-root'),
    requirePath(options, 'asset-pack-root'),
    requirePath(options, 'output')
  );
  console.log(JSON.stringify(result, null, 2));
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
