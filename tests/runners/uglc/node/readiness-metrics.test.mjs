import test from 'node:test';
import assert from 'node:assert/strict';
import { comparePairedMeasurements, inspectMemoryTrend } from './readiness-metrics.mjs';

test('missing, zero and insufficient performance evidence cannot pass', () => {
  assert.throws(() => comparePairedMeasurements([]), /five paired/);
  assert.throws(() => comparePairedMeasurements(Array(5).fill({})), /Missing or invalid/);
  assert.throws(() => comparePairedMeasurements(Array(5).fill({ legacy: { compileMs: 0 }, experimental: { compileMs: 1 } })), /Zero/);
});

test('timing and memory ratios are observations, not automatic acceptance limits', () => {
  const legacy = { compileMs: 100, peakRssBytes: 1000, computeClassCreationUs: 100, gpuPassNs: 100 };
  const experimental = { compileMs: 125, peakRssBytes: 1201, computeClassCreationUs: 115, gpuPassNs: 114 };
  const pairs = Array.from({ length: 5 }, () => ({ legacy, experimental: { ...experimental } }));
  const comparison = comparePairedMeasurements(pairs);
  assert.equal(comparison.metrics.compileMs.ratio, 1.25);
  assert.equal(comparison.metrics.gpuPassNs.ratio, 1.14);
  assert.equal('passed' in comparison, false);
  assert.equal('maximumRatio' in comparison.metrics.compileMs, false);
});

test('RSS observations require enough samples but do not equate a positive slope with a leak', () => {
  assert.throws(() => inspectMemoryTrend([], 60), /1800 seconds/);
  assert.throws(() => inspectMemoryTrend([], 1800), /Insufficient/);
  const stable = Array.from({ length: 1801 }, (_, seconds) => ({ seconds, rssBytes: 1024 }));
  assert.equal(inspectMemoryTrend(stable, 1800).windowDeltaBytes, 0);
  const increasing = inspectMemoryTrend(stable.map(sample => ({ ...sample, rssBytes: 1024 + sample.seconds })), 1800);
  assert.ok(increasing.windowDeltaBytes > 0);
  assert.equal(increasing.bytesPerMinute, 60);
  assert.equal('sustainedGrowth' in increasing, false);
});
