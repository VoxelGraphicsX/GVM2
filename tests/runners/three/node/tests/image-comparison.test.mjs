import assert from 'node:assert/strict';
import test from 'node:test';

import {
  boxDownsampleRgba,
  compareRgbaImages,
  compareThreeCaptures,
  validateComparisonMetrics
} from '../image-comparison.mjs';

/** Creates one opaque constant-color RGBA8 image for metric tests. */
function makeImage(width, height, red = 0, green = 0, blue = 0) {
  const pixels = new Uint8Array(width * height * 4);
  for (let index = 0; index < width * height; index += 1) {
    pixels[index * 4] = red;
    pixels[index * 4 + 1] = green;
    pixels[index * 4 + 2] = blue;
    pixels[index * 4 + 3] = 255;
  }
  return { width, height, pixels };
}

test('boxDownsampleRgba averages each 2x2 source footprint', () => {
  const image = makeImage(2, 2);
  image.pixels.set([
    0, 0, 0, 255,
    100, 20, 40, 255,
    200, 40, 80, 255,
    100, 20, 40, 255
  ]);
  const result = boxDownsampleRgba(image, 1, 1);
  assert.deepEqual([...result.pixels], [100, 20, 40, 255]);
});

test('identical captures pass every immutable threshold', () => {
  const reference = makeImage(800, 500, 40, 90, 160);
  const result = compareThreeCaptures(reference, reference);
  assert.deepEqual(result.failures, []);
  assert.equal(result.metrics.meanAbsoluteRgb, 0);
  assert.equal(result.metrics.p99AbsoluteRgb, 0);
  assert.equal(result.metrics.luminanceSsim, 1);
});

test('normalized distance ratio uses a strict 0.1 percent upper bound', () => {
  const reference = makeImage(400, 250);
  const actual = makeImage(400, 250);
  for (let pixelIndex = 0; pixelIndex < 100; pixelIndex += 1) {
    actual.pixels[pixelIndex * 4] = 255;
    actual.pixels[pixelIndex * 4 + 1] = 255;
    actual.pixels[pixelIndex * 4 + 2] = 255;
  }
  const metrics = compareRgbaImages(reference, actual);
  assert.equal(metrics.normalizedDistancePixelRatio, 0.001);
  assert.match(validateComparisonMetrics(metrics).join('\n'), /must be < 0\.001/u);
});

test('P99 and MAE failures are reported independently', () => {
  const reference = makeImage(10, 10);
  const actual = makeImage(10, 10, 20, 20, 20);
  const metrics = compareRgbaImages(reference, actual);
  const failures = validateComparisonMetrics(metrics);
  assert.equal(metrics.meanAbsoluteRgb, 20);
  assert.equal(metrics.p99AbsoluteRgb, 20);
  assert.ok(failures.some((failure) => failure.startsWith('RGB MAE')));
  assert.ok(failures.some((failure) => failure.startsWith('RGB P99')));
});
