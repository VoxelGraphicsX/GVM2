import { promises as fs } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const threeModulePath = path.join(
  repositoryRoot,
  'build/three-r185-lock-upstream/build/three.module.js'
);
const outputPath = path.join(scriptDirectory, 'MiscUvGeometryData.hpp');

/** Formats one finite JavaScript number as a deterministic C++ float literal. */
function formatFloat(value) {
  if (!Number.isFinite(value)) throw new Error(`Non-finite UV value ${value}.`);
  const normalized = Object.is(value, -0) ? 0 : value;
  return `${normalized.toFixed(9).replace(/0+$/, '').replace(/\.$/, '.0')}f`;
}

/** Extracts the exact UV face stream consumed by Three r185 UVsDebug. */
function extractFaces(geometry) {
  const uv = geometry.getAttribute('uv');
  if (!uv) throw new Error(`${geometry.type} has no UV attribute.`);
  const faces = [];
  const index = geometry.getIndex();
  const count = index ? index.count : uv.count;
  for (let offset = 0; offset < count; offset += 3) {
    const vertexIndices = index
      ? [index.getX(offset), index.getX(offset + 1), index.getX(offset + 2)]
      : [offset, offset + 1, offset + 2];
    faces.push({
      uv: vertexIndices.map((vertexIndex) => [uv.getX(vertexIndex), uv.getY(vertexIndex)]),
      vertexIndices
    });
  }
  geometry.dispose();
  return faces;
}

/** Emits one self-contained C++ array for a frozen geometry face stream. */
function emitFaceArray(identifier, faces) {
  const records = faces.map((face) => `        {{{${formatFloat(face.uv[0][0])}, ${formatFloat(face.uv[0][1])}}, {${formatFloat(face.uv[1][0])}, ${formatFloat(face.uv[1][1])}}, {${formatFloat(face.uv[2][0])}, ${formatFloat(face.uv[2][1])}}}, {${face.vertexIndices[0]}u, ${face.vertexIndices[1]}u, ${face.vertexIndices[2]}u}}`);
  return `    /** Stores the exact r185 UV face stream for ${identifier}. */\n    inline constexpr MiscUvFace ${identifier}[] = {\n${records.join(',\n')}\n    };\n`;
}

/** Generates all nine immutable UV face streams from the locked Three module. */
async function generate() {
  const THREE = await import(pathToFileURL(threeModulePath).href);
  if (THREE.REVISION !== '185') throw new Error(`Expected Three r185, found ${THREE.REVISION}.`);
  const points = [];
  for (let index = 0; index < 10; index += 1) {
    points.push(new THREE.Vector2(Math.sin(index * 0.2) * 15 + 50, (index - 5) * 2));
  }
  const cases = [
    ['MiscUvPlaneFaces', new THREE.PlaneGeometry(100, 100, 4, 4)],
    ['MiscUvSphereFaces', new THREE.SphereGeometry(75, 12, 6)],
    ['MiscUvIcosahedronFaces', new THREE.IcosahedronGeometry(30, 1)],
    ['MiscUvOctahedronFaces', new THREE.OctahedronGeometry(30, 2)],
    ['MiscUvCylinderFaces', new THREE.CylinderGeometry(25, 75, 100, 10, 5)],
    ['MiscUvBoxFaces', new THREE.BoxGeometry(100, 100, 100, 4, 4, 4)],
    ['MiscUvLatheFaces', new THREE.LatheGeometry(points, 8)],
    ['MiscUvTorusFaces', new THREE.TorusGeometry(50, 20, 8, 8)],
    ['MiscUvTorusKnotFaces', new THREE.TorusKnotGeometry(50, 10, 12, 6)]
  ];
  const arrays = cases.map(([identifier, geometry]) =>
    emitFaceArray(identifier, extractFaces(geometry))
  ).join('\n');
  const header = `#pragma once

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Stores one exact UVsDebug triangle and its source vertex indices. */
    struct MiscUvFace
    {
        float uv[3u][2u];
        uint32_t vertexIndices[3u];
    };

${arrays}
} // namespace GVM::ThreeSamples
`;
  await fs.writeFile(outputPath, header, 'utf8');
  process.stdout.write(`${outputPath}\n`);
}

await generate();
