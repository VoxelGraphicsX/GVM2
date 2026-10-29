import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const fixtureDirectory = path.dirname(fileURLToPath(import.meta.url));
const generatedHeaderPath = path.join(fixtureDirectory, 'MiscUvGeometryData.hpp');

/** Counts aggregate initializers inside one named generated face array. */
function countGeneratedFaces(header, identifier) {
  const startMarker = `inline constexpr MiscUvFace ${identifier}[] = {`;
  const start = header.indexOf(startMarker);
  assert.notEqual(start, -1, `${identifier} must exist.`);
  const end = header.indexOf('\n    };', start);
  assert.notEqual(end, -1, `${identifier} must terminate.`);
  return (header.slice(start + startMarker.length, end).match(/^        \{\{/gm) ?? []).length;
}

test('generated UV face streams preserve all r185 geometry triangle counts', async () => {
  const header = await fs.readFile(generatedHeaderPath, 'utf8');
  const expectedCounts = new Map([
    ['MiscUvPlaneFaces', 32],
    ['MiscUvSphereFaces', 120],
    ['MiscUvIcosahedronFaces', 80],
    ['MiscUvOctahedronFaces', 72],
    ['MiscUvCylinderFaces', 120],
    ['MiscUvBoxFaces', 192],
    ['MiscUvLatheFaces', 144],
    ['MiscUvTorusFaces', 128],
    ['MiscUvTorusKnotFaces', 144]
  ]);
  for (const [identifier, expectedCount] of expectedCounts) {
    assert.equal(countGeneratedFaces(header, identifier), expectedCount);
  }
});

test('generated UV data contains no non-finite C++ literals', async () => {
  const header = await fs.readFile(generatedHeaderPath, 'utf8');
  assert.doesNotMatch(header, /\b(?:NaN|Infinity|-Infinity)\b/);
  assert.match(header, /struct MiscUvFace/);
});
