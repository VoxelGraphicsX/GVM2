import test from 'node:test';
import assert from 'node:assert/strict';
import { selectNextThreeBatch } from './plan_three_batch.mjs';

function manifest() {
  const examples = [
      { id: 'strict', status: 'phase1_required', dslShard: 'Strict' },
      { id: 'semantic', status: 'phase1_required', dslShard: 'Semantic' },
      { id: 'scaffold', status: 'phase1_required', dslShard: 'Scaffold' },
      { id: 'new', status: 'phase1_required', dslShard: 'New' },
      { id: 'deferred', status: 'deferred_missing_capability', dslShard: 'Deferred' }
    ];
  return { examples: examples.concat(Array.from({ length: 583 }, (_, index) => ({
    id: `excluded-${index}`,
    status: 'excluded_upstream',
    dslShard: 'Excluded'
  }))) };
}

test('prioritizes semantic and scaffold work while excluding strict and deferred cases', () => {
  const entries = [
    { id: 'semantic', implementationLevel: 'semantic-complete', dslEntry: 'SemanticRenderer', hostTarget: 'SemanticHost', shard: 'Semantic' },
    { id: 'scaffold', implementationLevel: 'scaffolded', dslEntry: 'ScaffoldRenderer', hostTarget: 'ScaffoldHost', shard: 'Scaffold' }
  ];
  const result = selectNextThreeBatch({
    manifest: {
      examples: [
        ...manifest().examples.slice(0, 570),
        ...Array.from({ length: 18 }, (_, index) => ({
          id: `new-${index}`,
          status: 'phase1_required',
          dslShard: `New${index}`
        }))
      ]
    },
    inventories: new Map(entries.map((entry) => [entry.id, entry])),
    strictCaseIds: ['strict'],
    minimumCaseCount: 20
  });
  assert.equal(result.activeCases.length, 20);
  assert.equal(result.activeCases[0].caseId, 'semantic');
  assert.equal(result.activeCases[1].caseId, 'scaffold');
  assert.ok(!result.activeCases.some((entry) => entry.caseId === 'strict' || entry.caseId === 'deferred'));
  assert.equal(result.notYetRunnableCount, 18);
});

test('requires a twenty-case batch', () => {
  assert.throws(() => selectNextThreeBatch({
    manifest: { examples: Array.from({ length: 19 }, (_, index) => ({ id: `case-${index}`, status: 'phase1_required', dslShard: 'Shard' })).concat(Array.from({ length: 569 }, (_, index) => ({ id: `excluded-${index}`, status: 'excluded_upstream', dslShard: 'Excluded' }))) },
    inventories: new Map()
  }), /cannot form/);
});

test('does not requeue a strict inventory case when the baseline ledger is stale', () => {
  const source = {
    examples: [
      { id: 'strict-inventory', status: 'phase1_required', dslShard: 'Strict' },
      ...Array.from({ length: 20 }, (_, index) => ({
        id: `required-${index}`,
        status: 'phase1_required',
        dslShard: `Required${index}`
      })),
      ...Array.from({ length: 567 }, (_, index) => ({
        id: `excluded-${index}`,
        status: 'excluded_upstream',
        dslShard: 'Excluded'
      }))
    ]
  };
  const result = selectNextThreeBatch({
    manifest: source,
    inventories: new Map([
      ['strict-inventory', { caseId: 'strict-inventory', implementationLevel: 'strict-pass', dslEntry: 'Strict', hostTarget: 'Strict' }]
    ]),
    strictCaseIds: [],
    minimumCaseCount: 20
  });
  assert.ok(!result.activeCases.some((entry) => entry.caseId === 'strict-inventory'));
});
