import assert from 'node:assert/strict';
import test from 'node:test';
import { getTestRegistry } from './test-registry.mjs';
import { validateUglirSelection, requiredUglirCaseIds } from './uglir-contract.mjs';

test('enabled registry includes every required case, including RGBA32Float', () => {
  const profile = { legacyEnabled: true };
  const registry = getTestRegistry(process.cwd(), profile);
  assert.ok(requiredUglirCaseIds.includes('experimental-uglir-rgba32float-framebuffer'));
  validateUglirSelection(profile, registry, registry, true);
});

test('Legacy OFF keeps all required UGLIR cases and explicitly excludes Legacy execution', () => {
  const profile = { legacyEnabled: false };
  const registry = getTestRegistry(process.cwd(), profile);
  assert.ok(registry.some(entry => entry.group === 'uglir' && !entry.skipReason));
  assert.ok(registry.some(entry => entry.requiresLegacy && entry.skipReason));
  validateUglirSelection(profile, registry, registry, true);
});

test('an omitted registration cannot turn a full experimental run green', () => {
  const profile = { legacyEnabled: true };
  const registry = getTestRegistry(process.cwd(), profile).filter(entry => entry.id !== 'experimental-uglir-rgba32float-framebuffer');
  assert.throws(() => validateUglirSelection(profile, registry, registry), /not registered.*rgba32float/);
});

test('empty or partial selections cannot satisfy experimental acceptance', () => {
  const profile = { legacyEnabled: true };
  const registry = getTestRegistry(process.cwd(), profile);
  assert.throws(() => validateUglirSelection(profile, registry, [], true), /omits required tests|no executable tests/);
  assert.throws(() => validateUglirSelection(profile, registry, registry.slice(0, 1), true), /omits required tests|no executable tests/);
});

test('duplicate IDs cannot hide missing test results', () => {
  const profile = { legacyEnabled: true };
  const registry = getTestRegistry(process.cwd(), profile);
  assert.throws(() => validateUglirSelection(profile, [...registry, registry[0]], registry), /duplicate/);
});
