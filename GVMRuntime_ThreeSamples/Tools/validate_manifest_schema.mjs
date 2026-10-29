#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import { isDeepStrictEqual } from 'node:util';
import { fileURLToPath } from 'node:url';

/** Reports a malformed or unsupported schema definition separately from instance failures. */
export class SchemaDefinitionError extends Error {
  /** Creates one schema-definition failure with its exact JSON Pointer. */
  constructor(schemaPath, message) {
    super(`${schemaPath || '#'}: ${message}`);
    this.name = 'SchemaDefinitionError';
    this.schemaPath = schemaPath;
  }
}

/** Escapes one JSON property name for use as an RFC 6901 pointer segment. */
function escapeJsonPointerSegment(value) {
  return String(value).replaceAll('~', '~0').replaceAll('/', '~1');
}

/** Appends one property or array index to an RFC 6901 JSON Pointer. */
function appendJsonPointer(pointer, segment) {
  return `${pointer}/${escapeJsonPointerSegment(segment)}`;
}

/** Returns the JSON Schema type name for one parsed JSON value. */
function jsonTypeOf(value) {
  if (value === null) return 'null';
  if (Array.isArray(value)) return 'array';
  if (typeof value === 'number' && Number.isInteger(value)) return 'integer';
  return typeof value;
}

/** Returns whether one value satisfies a single JSON Schema primitive type. */
function matchesJsonType(value, expectedType) {
  if (expectedType === 'null') return value === null;
  if (expectedType === 'array') return Array.isArray(value);
  if (expectedType === 'object') return value !== null && typeof value === 'object' && !Array.isArray(value);
  if (expectedType === 'integer') return typeof value === 'number' && Number.isInteger(value);
  if (expectedType === 'number') return typeof value === 'number' && Number.isFinite(value);
  if (expectedType === 'string' || expectedType === 'boolean') return typeof value === expectedType;
  return false;
}

/** Returns a concise JSON representation that remains bounded in diagnostics. */
function formatJsonValue(value) {
  const serialized = JSON.stringify(value);
  if (serialized === undefined) return String(value);
  return serialized.length <= 160 ? serialized : `${serialized.slice(0, 157)}...`;
}

/** Appends one normalized validation error to the active result list. */
function addValidationError(errors, keyword, instancePath, schemaPath, message, details = undefined) {
  const error = { keyword, instancePath, schemaPath, message };
  if (details !== undefined) error.details = details;
  errors.push(error);
}

/** Decodes one internal URI-fragment JSON Pointer segment. */
function decodeReferenceSegment(segment, reference) {
  let decoded;
  try {
    decoded = decodeURIComponent(segment);
  } catch (error) {
    throw new SchemaDefinitionError(reference, `Invalid percent-encoding in $ref: ${error.message}`);
  }
  return decoded.replaceAll('~1', '/').replaceAll('~0', '~');
}

/** Resolves one local $ref against the root schema and returns its schema pointer. */
function resolveLocalReference(rootSchema, reference) {
  if (typeof reference !== 'string' || !reference.startsWith('#')) {
    throw new SchemaDefinitionError('#/$ref', `Only local fragment references are supported; received ${formatJsonValue(reference)}.`);
  }
  if (reference === '#') return { schema: rootSchema, schemaPath: '#' };
  if (!reference.startsWith('#/')) {
    throw new SchemaDefinitionError(reference, 'Local $ref must use an RFC 6901 JSON Pointer fragment.');
  }
  const segments = reference.slice(2).split('/').map((segment) => decodeReferenceSegment(segment, reference));
  let current = rootSchema;
  for (const segment of segments) {
    if (current === null || typeof current !== 'object' || !Object.hasOwn(current, segment)) {
      throw new SchemaDefinitionError(reference, `Unresolved local $ref segment ${formatJsonValue(segment)}.`);
    }
    current = current[segment];
  }
  return { schema: current, schemaPath: reference };
}

/** Validates a schema type declaration and reports whether value-specific checks may continue. */
function validateTypeKeyword(value, schema, instancePath, schemaPath, errors) {
  if (schema.type === undefined) return true;
  const allowedTypes = Array.isArray(schema.type) ? schema.type : [schema.type];
  if (allowedTypes.length === 0 || allowedTypes.some((type) => typeof type !== 'string')) {
    throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'type'), 'type must be a string or non-empty string array.');
  }
  const supportedTypes = new Set(['array', 'boolean', 'integer', 'null', 'number', 'object', 'string']);
  for (const expectedType of allowedTypes) {
    if (!supportedTypes.has(expectedType)) {
      throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'type'), `Unsupported JSON Schema type '${expectedType}'.`);
    }
  }
  if (allowedTypes.some((expectedType) => matchesJsonType(value, expectedType))) return true;
  addValidationError(
    errors,
    'type',
    instancePath,
    appendJsonPointer(schemaPath, 'type'),
    `Expected ${allowedTypes.join(' or ')}, received ${jsonTypeOf(value)}.`,
    { expected: allowedTypes, actual: jsonTypeOf(value) }
  );
  return false;
}

/** Validates const and enum equality using structural JSON value semantics. */
function validateEqualityKeywords(value, schema, instancePath, schemaPath, errors) {
  if (Object.hasOwn(schema, 'const') && !isDeepStrictEqual(value, schema.const)) {
    addValidationError(
      errors,
      'const',
      instancePath,
      appendJsonPointer(schemaPath, 'const'),
      `Value must equal ${formatJsonValue(schema.const)}.`,
      { expected: schema.const, actual: value }
    );
  }
  if (schema.enum !== undefined) {
    if (!Array.isArray(schema.enum) || schema.enum.length === 0) {
      throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'enum'), 'enum must be a non-empty array.');
    }
    if (!schema.enum.some((candidate) => isDeepStrictEqual(candidate, value))) {
      addValidationError(
        errors,
        'enum',
        instancePath,
        appendJsonPointer(schemaPath, 'enum'),
        `Value ${formatJsonValue(value)} is not in the allowed enum.`,
        { allowed: schema.enum, actual: value }
      );
    }
  }
}

/** Validates numeric minimum and maximum constraints. */
function validateNumberKeywords(value, schema, instancePath, schemaPath, errors) {
  if (typeof value !== 'number') return;
  for (const [keyword, comparison, phrase] of [
    ['minimum', (actual, limit) => actual >= limit, 'greater than or equal to'],
    ['maximum', (actual, limit) => actual <= limit, 'less than or equal to']
  ]) {
    if (schema[keyword] === undefined) continue;
    if (typeof schema[keyword] !== 'number' || !Number.isFinite(schema[keyword])) {
      throw new SchemaDefinitionError(appendJsonPointer(schemaPath, keyword), `${keyword} must be a finite number.`);
    }
    if (!comparison(value, schema[keyword])) {
      addValidationError(
        errors,
        keyword,
        instancePath,
        appendJsonPointer(schemaPath, keyword),
        `Number must be ${phrase} ${schema[keyword]}.`,
        { limit: schema[keyword], actual: value }
      );
    }
  }
}

/** Validates string length, regular expression, and URI format constraints. */
function validateStringKeywords(value, schema, instancePath, schemaPath, errors) {
  if (typeof value !== 'string') return;
  if (schema.minLength !== undefined) {
    if (!Number.isInteger(schema.minLength) || schema.minLength < 0) {
      throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'minLength'), 'minLength must be a non-negative integer.');
    }
    const length = [...value].length;
    if (length < schema.minLength) {
      addValidationError(
        errors,
        'minLength',
        instancePath,
        appendJsonPointer(schemaPath, 'minLength'),
        `String length ${length} is below ${schema.minLength}.`,
        { minimum: schema.minLength, actual: length }
      );
    }
  }
  if (schema.pattern !== undefined) {
    if (typeof schema.pattern !== 'string') {
      throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'pattern'), 'pattern must be a string.');
    }
    let expression;
    try {
      expression = new RegExp(schema.pattern, 'u');
    } catch (error) {
      throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'pattern'), `Invalid regular expression: ${error.message}`);
    }
    if (!expression.test(value)) {
      addValidationError(
        errors,
        'pattern',
        instancePath,
        appendJsonPointer(schemaPath, 'pattern'),
        `String does not match /${schema.pattern}/u.`,
        { pattern: schema.pattern, actual: value }
      );
    }
  }
  if (schema.format !== undefined) {
    if (schema.format !== 'uri') {
      throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'format'), `Unsupported format '${schema.format}'.`);
    }
    let validUri = false;
    try {
      const url = new URL(value);
      validUri = url.protocol.length > 1 && !/\s/u.test(value);
    } catch {
      validUri = false;
    }
    if (!validUri) {
      addValidationError(
        errors,
        'format',
        instancePath,
        appendJsonPointer(schemaPath, 'format'),
        'String must be an absolute URI.',
        { format: 'uri', actual: value }
      );
    }
  }
}

/** Returns whether an array contains a prior structurally equal JSON value. */
function findDuplicateArrayItem(values) {
  for (let right = 1; right < values.length; right += 1) {
    for (let left = 0; left < right; left += 1) {
      if (isDeepStrictEqual(values[left], values[right])) return { firstIndex: left, duplicateIndex: right };
    }
  }
  return null;
}

/** Validates array length, uniqueness, and item schemas. */
function validateArrayKeywords(value, schema, instancePath, schemaPath, context, errors) {
  if (!Array.isArray(value)) return;
  for (const [keyword, comparison, phrase] of [
    ['minItems', (actual, limit) => actual >= limit, 'at least'],
    ['maxItems', (actual, limit) => actual <= limit, 'at most']
  ]) {
    if (schema[keyword] === undefined) continue;
    if (!Number.isInteger(schema[keyword]) || schema[keyword] < 0) {
      throw new SchemaDefinitionError(appendJsonPointer(schemaPath, keyword), `${keyword} must be a non-negative integer.`);
    }
    if (!comparison(value.length, schema[keyword])) {
      addValidationError(
        errors,
        keyword,
        instancePath,
        appendJsonPointer(schemaPath, keyword),
        `Array must contain ${phrase} ${schema[keyword]} items; received ${value.length}.`,
        { limit: schema[keyword], actual: value.length }
      );
    }
  }
  if (schema.uniqueItems === true) {
    const duplicate = findDuplicateArrayItem(value);
    if (duplicate) {
      addValidationError(
        errors,
        'uniqueItems',
        appendJsonPointer(instancePath, duplicate.duplicateIndex),
        appendJsonPointer(schemaPath, 'uniqueItems'),
        `Array item duplicates index ${duplicate.firstIndex}.`,
        duplicate
      );
    }
  } else if (schema.uniqueItems !== undefined && schema.uniqueItems !== false) {
    throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'uniqueItems'), 'uniqueItems must be boolean.');
  }
  if (schema.items === undefined) return;
  if (typeof schema.items !== 'boolean' && (schema.items === null || typeof schema.items !== 'object' || Array.isArray(schema.items))) {
    throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'items'), 'items must be a boolean or schema object.');
  }
  for (let index = 0; index < value.length; index += 1) {
    validateSchemaNode(
      value[index],
      schema.items,
      appendJsonPointer(instancePath, index),
      appendJsonPointer(schemaPath, 'items'),
      context,
      errors
    );
  }
}

/** Validates object required fields, declared properties, and additional properties. */
function validateObjectKeywords(value, schema, instancePath, schemaPath, context, errors) {
  if (value === null || typeof value !== 'object' || Array.isArray(value)) return;
  const properties = schema.properties ?? {};
  if (properties === null || typeof properties !== 'object' || Array.isArray(properties)) {
    throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'properties'), 'properties must be an object.');
  }
  if (schema.required !== undefined) {
    if (!Array.isArray(schema.required) || schema.required.some((property) => typeof property !== 'string')) {
      throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'required'), 'required must be an array of property names.');
    }
    for (const property of schema.required) {
      if (!Object.hasOwn(value, property)) {
        addValidationError(
          errors,
          'required',
          instancePath,
          appendJsonPointer(schemaPath, 'required'),
          `Required property '${property}' is missing.`,
          { missingProperty: property }
        );
      }
    }
  }
  for (const property of Object.keys(properties)) {
    if (!Object.hasOwn(value, property)) continue;
    validateSchemaNode(
      value[property],
      properties[property],
      appendJsonPointer(instancePath, property),
      appendJsonPointer(appendJsonPointer(schemaPath, 'properties'), property),
      context,
      errors
    );
  }
  const additionalProperties = schema.additionalProperties;
  if (additionalProperties === undefined || additionalProperties === true) return;
  if (additionalProperties !== false
    && (additionalProperties === null || typeof additionalProperties !== 'object' || Array.isArray(additionalProperties))) {
    throw new SchemaDefinitionError(
      appendJsonPointer(schemaPath, 'additionalProperties'),
      'additionalProperties must be boolean or a schema object.'
    );
  }
  const additionalNames = Object.keys(value)
    .filter((property) => !Object.hasOwn(properties, property))
    .sort();
  for (const property of additionalNames) {
    if (additionalProperties === false) {
      addValidationError(
        errors,
        'additionalProperties',
        appendJsonPointer(instancePath, property),
        appendJsonPointer(schemaPath, 'additionalProperties'),
        `Additional property '${property}' is not allowed.`,
        { additionalProperty: property }
      );
    } else {
      validateSchemaNode(
        value[property],
        additionalProperties,
        appendJsonPointer(instancePath, property),
        appendJsonPointer(schemaPath, 'additionalProperties'),
        context,
        errors
      );
    }
  }
}

/** Validates that exactly one oneOf branch accepts the current instance. */
function validateOneOfKeyword(value, schema, instancePath, schemaPath, context, errors) {
  if (schema.oneOf === undefined) return;
  if (!Array.isArray(schema.oneOf) || schema.oneOf.length === 0) {
    throw new SchemaDefinitionError(appendJsonPointer(schemaPath, 'oneOf'), 'oneOf must be a non-empty schema array.');
  }
  const branchResults = schema.oneOf.map((branchSchema, branchIndex) => {
    const branchErrors = [];
    validateSchemaNode(
      value,
      branchSchema,
      instancePath,
      appendJsonPointer(appendJsonPointer(schemaPath, 'oneOf'), branchIndex),
      context,
      branchErrors
    );
    return { branchIndex, errors: branchErrors };
  });
  const matchingBranches = branchResults
    .filter((branch) => branch.errors.length === 0)
    .map((branch) => branch.branchIndex);
  if (matchingBranches.length === 1) return;
  addValidationError(
    errors,
    'oneOf',
    instancePath,
    appendJsonPointer(schemaPath, 'oneOf'),
    `Expected exactly one matching branch; matched ${matchingBranches.length}.`,
    {
      matchingBranches,
      branchErrors: branchResults.map((branch) => ({
        branchIndex: branch.branchIndex,
        errors: branch.errors
      }))
    }
  );
}

/** Recursively validates one instance value against one schema node. */
function validateSchemaNode(value, schema, instancePath, schemaPath, context, errors) {
  if (schema === true) return;
  if (schema === false) {
    addValidationError(errors, 'falseSchema', instancePath, schemaPath, 'Boolean false schema rejects every value.');
    return;
  }
  if (schema === null || typeof schema !== 'object' || Array.isArray(schema)) {
    throw new SchemaDefinitionError(schemaPath, 'Schema node must be a boolean or object.');
  }
  if (schema.$ref !== undefined) {
    const resolved = resolveLocalReference(context.rootSchema, schema.$ref);
    const cycleKey = `${resolved.schemaPath}|${instancePath}`;
    if (context.activeReferences.has(cycleKey)) {
      throw new SchemaDefinitionError(resolved.schemaPath, `Cyclic $ref revisits instance path '${instancePath}'.`);
    }
    context.activeReferences.add(cycleKey);
    validateSchemaNode(value, resolved.schema, instancePath, resolved.schemaPath, context, errors);
    context.activeReferences.delete(cycleKey);
  }
  validateOneOfKeyword(value, schema, instancePath, schemaPath, context, errors);
  const typeMatches = validateTypeKeyword(value, schema, instancePath, schemaPath, errors);
  validateEqualityKeywords(value, schema, instancePath, schemaPath, errors);
  if (!typeMatches) return;
  validateNumberKeywords(value, schema, instancePath, schemaPath, errors);
  validateStringKeywords(value, schema, instancePath, schemaPath, errors);
  validateArrayKeywords(value, schema, instancePath, schemaPath, context, errors);
  validateObjectKeywords(value, schema, instancePath, schemaPath, context, errors);
}

/** Validates a parsed JSON value against the supported zero-dependency schema subset. */
export function validateJsonSchema(instance, schema) {
  if (schema === null || typeof schema !== 'object' || Array.isArray(schema)) {
    throw new SchemaDefinitionError('#', 'Root schema must be an object.');
  }
  const errors = [];
  validateSchemaNode(instance, schema, '', '#', {
    rootSchema: schema,
    activeReferences: new Set()
  }, errors);
  return errors;
}

/** Reads and parses one JSON document with an actionable path-prefixed error. */
async function readJsonDocument(filePath, label) {
  let text;
  try {
    text = await fs.readFile(filePath, 'utf8');
  } catch (error) {
    throw new Error(`Cannot read ${label} '${filePath}': ${error.message}`);
  }
  try {
    return JSON.parse(text);
  } catch (error) {
    throw new Error(`Invalid JSON in ${label} '${filePath}': ${error.message}`);
  }
}

/** Reads explicit Manifest and schema files and returns one structured validation report. */
export async function validateManifestSchemaFiles(manifestPath, schemaPath) {
  const absoluteManifestPath = path.resolve(manifestPath);
  const absoluteSchemaPath = path.resolve(schemaPath);
  const [manifest, schema] = await Promise.all([
    readJsonDocument(absoluteManifestPath, 'manifest'),
    readJsonDocument(absoluteSchemaPath, 'schema')
  ]);
  const errors = validateJsonSchema(manifest, schema);
  return {
    schemaVersion: 1,
    status: errors.length === 0 ? 'pass' : 'fail',
    manifestPath: absoluteManifestPath,
    schemaPath: absoluteSchemaPath,
    errorCount: errors.length,
    errors
  };
}

/** Parses the validator's explicit command-line configuration. */
export function parseManifestSchemaArguments(argv) {
  const options = { manifestPath: '', schemaPath: '', format: 'text', help: false };
  const valueOptions = new Map([
    ['--manifest', 'manifestPath'],
    ['--schema', 'schemaPath'],
    ['--format', 'format']
  ]);
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === '--help' || argument === '-h') {
      options.help = true;
      continue;
    }
    const property = valueOptions.get(argument);
    if (property === undefined) throw new Error(`Unknown argument: ${argument}`);
    const value = argv[index + 1];
    if (value === undefined || value.startsWith('--')) throw new Error(`Missing value for ${argument}.`);
    options[property] = value;
    index += 1;
  }
  if (!options.help && options.manifestPath === '') throw new Error('--manifest is required.');
  if (!options.help && options.schemaPath === '') throw new Error('--schema is required.');
  if (!['json', 'text'].includes(options.format)) {
    throw new Error(`--format must be text or json; received '${options.format}'.`);
  }
  return options;
}

/** Prints the standalone validator command contract. */
function printUsage(stream = process.stdout) {
  stream.write(`Usage:
  node GVMRuntime_ThreeSamples/Tools/validate_manifest_schema.mjs \\
    --manifest <three-r185-manifest.json> \\
    --schema <three-r185-manifest.schema.json> \\
    [--format text|json]
`);
}

/** Formats one validation failure as a stable compiler-style diagnostic. */
function formatValidationError(error) {
  const instancePath = error.instancePath === '' ? '/' : error.instancePath;
  return `ERROR [${error.keyword}] ${instancePath}: ${error.message} (schema ${error.schemaPath})`;
}

/** Executes the standalone schema validator and returns its process exit code. */
async function main(argv) {
  let options;
  try {
    options = parseManifestSchemaArguments(argv);
  } catch (error) {
    process.stderr.write(`manifest schema validator: ${error.message}\n`);
    printUsage(process.stderr);
    return 2;
  }
  if (options.help) {
    printUsage();
    return 0;
  }
  let report;
  try {
    report = await validateManifestSchemaFiles(options.manifestPath, options.schemaPath);
  } catch (error) {
    process.stderr.write(`manifest schema validator: ${error.message}\n`);
    return 2;
  }
  if (options.format === 'json') {
    process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
  } else if (report.status === 'pass') {
    process.stdout.write(`Manifest schema validation passed: ${report.manifestPath}.\n`);
  } else {
    for (const error of report.errors) process.stderr.write(`${formatValidationError(error)}\n`);
    process.stderr.write(`Manifest schema validation failed: errors=${report.errorCount}.\n`);
  }
  return report.status === 'pass' ? 0 : 1;
}

const isCommandLineEntry = process.argv[1] !== undefined
  && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isCommandLineEntry) process.exitCode = await main(process.argv.slice(2));
