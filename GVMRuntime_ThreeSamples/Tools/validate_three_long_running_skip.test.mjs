import test from 'node:test';
import assert from 'node:assert/strict';
import { validateLongRunningSkipLedger } from './validate_three_long_running_skip.mjs';

const manifest = {
  examples: [
    { id: 'webgl_pending', status: 'phase1_required' },
    { id: 'webgl_deferred', status: 'deferred_missing_capability' },
    { id: 'webgl_done', status: 'phase1_required' }
  ]
};

function makeEntry(caseId = 'webgl_pending') {
  return {
    caseId,
    status: 'selected',
    selectedAt: '2026-08-25T12:00:00.000Z',
    reasonCode: 'long_running_no_convergence',
    attempts: [1, 2, 3].map((index) => ({
      result: 'fail',
      durationSeconds: 3600,
      reportPath: `/tmp/${caseId}-${index}.json`,
      failures: [`failure-${index}`]
    })),
    totalAttemptSeconds: 10800
  };
}

test('accepts a selected entry only after three hours and three reports', async () => {
  const result = await validateLongRunningSkipLedger({
    manifest,
    ledger: { schemaVersion: 1, thresholdSeconds: 10800, entries: [makeEntry()] },
    dslRoot: null,
    requireEvidenceFiles: false
  });
  assert.equal(result.status, 'pass');
});

test('rejects short or incomplete attempts', async () => {
  const entry = makeEntry();
  entry.attempts = entry.attempts.slice(0, 2);
  const result = await validateLongRunningSkipLedger({
    manifest,
    ledger: { schemaVersion: 1, entries: [entry] },
    dslRoot: null,
    requireEvidenceFiles: false
  });
  assert.equal(result.status, 'fail');
  assert.ok(result.failures.some((failure) => failure.includes('at least three')));
});

test('never permits deferred or strict cases to be skipped by time', async () => {
  const deferred = makeEntry('webgl_deferred');
  const strict = makeEntry('webgl_done');
  const result = await validateLongRunningSkipLedger({
    manifest,
    ledger: { schemaVersion: 1, entries: [deferred, strict] },
    dslRoot: null,
    requireEvidenceFiles: false
  });
  assert.equal(result.status, 'fail');
  assert.ok(result.failures.some((failure) => failure.includes('only phase1_required')));
});
