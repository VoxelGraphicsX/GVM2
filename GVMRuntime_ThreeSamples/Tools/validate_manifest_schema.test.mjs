import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  SchemaDefinitionError,
  parseManifestSchemaArguments,
  validateJsonSchema,
  validateManifestSchemaFiles
} from './validate_manifest_schema.mjs';

const toolsDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(toolsDirectory, '..', '..');
const manifestPath = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json');
const schemaPath = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.schema.json');
const toolPath = path.join(toolsDirectory, 'validate_manifest_schema.mjs');

/** Builds a compact schema that exercises every keyword used by the r185 Manifest schema. */
function createKeywordFixtureSchema() {
  return {
    $defs: {
      tag: {
        type: 'string',
        minLength: 2,
        pattern: '^[a-z]+$'
      },
      entry: {
        type: 'object',
        additionalProperties: false,
        required: ['kind', 'value', 'tags', 'uri', 'nullable'],
        properties: {
          kind: { const: 'entry' },
          value: { type: 'integer', minimum: 1, maximum: 3 },
          tags: {
            type: 'array',
            minItems: 1,
            maxItems: 2,
            uniqueItems: true,
            items: { $ref: '#/$defs/tag' }
          },
          uri: { type: 'string', format: 'uri' },
          nullable: { type: ['string', 'null'], enum: [null, 'yes'] }
        }
      }
    },
    type: 'object',
    additionalProperties: false,
    required: ['entry', 'choice'],
    properties: {
      entry: { $ref: '#/$defs/entry' },
      choice: { oneOf: [{ const: 'a' }, { enum: ['b', 'c'] }] }
    }
  };
}

/** Builds one valid instance for the complete keyword fixture schema. */
function createValidKeywordFixture() {
  return {
    entry: {
      kind: 'entry',
      value: 2,
      tags: ['ok'],
      uri: 'https://example.test/input',
      nullable: null
    },
    choice: 'b'
  };
}

test('checked-in r185 Manifest satisfies the checked-in JSON Schema', async () => {
  const report = await validateManifestSchemaFiles(manifestPath, schemaPath);
  assert.equal(report.status, 'pass', JSON.stringify(report.errors, null, 2));
  assert.equal(report.errorCount, 0);
  assert.deepEqual(report.errors, []);
});

test('supports refs, null type unions, const, enum, objects, arrays, numeric, string, and format constraints', () => {
  const schema = createKeywordFixtureSchema();
  assert.deepEqual(validateJsonSchema(createValidKeywordFixture(), schema), []);

  const invalid = createValidKeywordFixture();
  invalid.unexpected = true;
  delete invalid.entry.uri;
  invalid.entry.kind = 'wrong';
  invalid.entry.value = 0;
  invalid.entry.tags = ['A', 'A', 'third'];
  invalid.entry.nullable = 'no';
  invalid.choice = 'z';
  const errors = validateJsonSchema(invalid, schema);
  const keywords = new Set(errors.map((error) => error.keyword));
  for (const keyword of [
    'additionalProperties', 'required', 'const', 'minimum', 'maxItems',
    'uniqueItems', 'minLength', 'pattern', 'enum', 'oneOf'
  ]) {
    assert.ok(keywords.has(keyword), `missing keyword failure ${keyword}`);
  }
  assert.ok(errors.some((error) => error.instancePath === '/entry/tags/0' && error.keyword === 'pattern'));
  assert.ok(errors.some((error) => error.schemaPath === '#/$defs/tag/pattern'));

  const aboveMaximum = createValidKeywordFixture();
  aboveMaximum.entry.value = 4;
  assert.ok(validateJsonSchema(aboveMaximum, schema).some((error) => error.keyword === 'maximum'));

  const belowMinItems = createValidKeywordFixture();
  belowMinItems.entry.tags = [];
  assert.ok(validateJsonSchema(belowMinItems, schema).some((error) => error.keyword === 'minItems'));

  const invalidType = createValidKeywordFixture();
  invalidType.entry.nullable = 7;
  assert.ok(validateJsonSchema(invalidType, schema).some((error) => (
    error.keyword === 'type' && error.instancePath === '/entry/nullable'
  )));

  const invalidUri = createValidKeywordFixture();
  invalidUri.entry.uri = 'relative/path';
  assert.ok(validateJsonSchema(invalidUri, schema).some((error) => error.keyword === 'format'));
});

test('oneOf requires exactly one matching branch and preserves branch diagnostics', () => {
  const schema = {
    oneOf: [
      { type: 'number' },
      { minimum: 0 }
    ]
  };
  const errors = validateJsonSchema(1, schema);
  assert.equal(errors.length, 1);
  assert.equal(errors[0].keyword, 'oneOf');
  assert.deepEqual(errors[0].details.matchingBranches, [0, 1]);

  const noMatch = validateJsonSchema('text', {
    oneOf: [{ const: 'left' }, { const: 'right' }]
  });
  assert.equal(noMatch[0].keyword, 'oneOf');
  assert.equal(noMatch[0].details.matchingBranches.length, 0);
  assert.equal(noMatch[0].details.branchErrors.length, 2);
});

test('local refs decode escaped RFC 6901 property names', () => {
  const schema = {
    $defs: {
      'path/name~kind': { const: 7 }
    },
    $ref: '#/$defs/path~1name~0kind'
  };
  assert.deepEqual(validateJsonSchema(7, schema), []);
  const errors = validateJsonSchema(8, schema);
  assert.equal(errors[0].keyword, 'const');
  assert.equal(errors[0].schemaPath, '#/$defs/path~1name~0kind/const');
});

test('invalid schema definitions fail separately from instance validation', () => {
  assert.throws(
    () => validateJsonSchema({}, { $ref: '#/$defs/missing' }),
    (error) => error instanceof SchemaDefinitionError && /Unresolved local \$ref/u.test(error.message)
  );
  assert.throws(
    () => validateJsonSchema('text', { type: 'string', pattern: '[' }),
    (error) => error instanceof SchemaDefinitionError && /Invalid regular expression/u.test(error.message)
  );
  assert.throws(
    () => validateJsonSchema({}, { type: 'unsupported' }),
    (error) => error instanceof SchemaDefinitionError && /Unsupported JSON Schema type/u.test(error.message)
  );
});

test('actual schema rejects added fields and invalid status values with precise paths', async () => {
  const [manifest, schema] = await Promise.all([
    fs.readFile(manifestPath, 'utf8').then(JSON.parse),
    fs.readFile(schemaPath, 'utf8').then(JSON.parse)
  ]);
  const modified = structuredClone(manifest);
  modified.examples[0].unexpectedField = true;
  modified.examples[1].status = 'not-a-status';
  const errors = validateJsonSchema(modified, schema);
  assert.ok(errors.some((error) => (
    error.keyword === 'additionalProperties'
      && error.instancePath === '/examples/0/unexpectedField'
  )));
  assert.ok(errors.some((error) => (
    error.keyword === 'enum'
      && error.instancePath === '/examples/1/status'
  )));
});

test('CLI parser requires explicit Manifest and schema paths', () => {
  assert.deepEqual(parseManifestSchemaArguments([
    '--manifest', '/input/manifest.json',
    '--schema', '/input/schema.json',
    '--format', 'json'
  ]), {
    manifestPath: '/input/manifest.json',
    schemaPath: '/input/schema.json',
    format: 'json',
    help: false
  });
  assert.throws(() => parseManifestSchemaArguments([
    '--schema', '/input/schema.json'
  ]), /--manifest is required/u);
  assert.throws(() => parseManifestSchemaArguments([
    '--manifest', '/input/manifest.json'
  ]), /--schema is required/u);
  assert.throws(() => parseManifestSchemaArguments([
    '--manifest', '/input/manifest.json', '--schema', '/input/schema.json', '--format', 'xml'
  ]), /text or json/u);
});

test('standalone CLI emits a structured failing report and exit code one', async (t) => {
  const temporaryRoot = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-manifest-schema-'));
  t.after(() => fs.rm(temporaryRoot, { recursive: true, force: true }));
  const invalidManifestPath = path.join(temporaryRoot, 'manifest.json');
  await fs.writeFile(invalidManifestPath, '{}\n', 'utf8');
  const execution = spawnSync(process.execPath, [
    toolPath,
    '--manifest', invalidManifestPath,
    '--schema', schemaPath,
    '--format', 'json'
  ], { encoding: 'utf8' });
  assert.equal(execution.status, 1, execution.stderr);
  assert.equal(execution.stderr, '');
  const report = JSON.parse(execution.stdout);
  assert.equal(report.status, 'fail');
  assert.ok(report.errorCount > 0);
  assert.ok(report.errors.every((error) => (
    typeof error.keyword === 'string'
      && typeof error.instancePath === 'string'
      && typeof error.schemaPath === 'string'
  )));
});
