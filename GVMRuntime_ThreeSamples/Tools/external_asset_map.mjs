import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';

const NETWORK_PROTOCOLS = new Set(['http:', 'https:']);
const REQUIRED_REFERENCE_KINDS = new Set(['asset', 'asset-directory', 'module', 'stylesheet']);
const MIME_TYPES = new Map([
  ['.avif', 'image/avif'],
  ['.bin', 'application/octet-stream'],
  ['.css', 'text/css; charset=utf-8'],
  ['.drc', 'application/octet-stream'],
  ['.exr', 'image/x-exr'],
  ['.fbx', 'application/octet-stream'],
  ['.gif', 'image/gif'],
  ['.glb', 'model/gltf-binary'],
  ['.gltf', 'model/gltf+json'],
  ['.hdr', 'image/vnd.radiance'],
  ['.html', 'text/html; charset=utf-8'],
  ['.jpeg', 'image/jpeg'],
  ['.jpg', 'image/jpeg'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.ktx', 'image/ktx'],
  ['.ktx2', 'image/ktx2'],
  ['.mjs', 'text/javascript; charset=utf-8'],
  ['.mp3', 'audio/mpeg'],
  ['.obj', 'text/plain; charset=utf-8'],
  ['.ogg', 'audio/ogg'],
  ['.png', 'image/png'],
  ['.svg', 'image/svg+xml'],
  ['.wasm', 'application/wasm'],
  ['.wav', 'audio/wav'],
  ['.webm', 'video/webm'],
  ['.webp', 'image/webp'],
  ['.woff', 'font/woff'],
  ['.woff2', 'font/woff2']
]);

/** Rejects misspelled or unsupported fields in one external-map schema object. */
function validateKnownKeys(value, allowedKeys, label) {
  const unknownKeys = Object.keys(value).filter((key) => !allowedKeys.has(key));
  if (unknownKeys.length > 0) {
    throw new Error(`${label} contains unsupported field(s): ${unknownKeys.sort(compareStrings).join(', ')}.`);
  }
}

/** Sorts strings with stable bytewise ordering independent of the active locale. */
function compareStrings(left, right) {
  return left < right ? -1 : left > right ? 1 : 0;
}

/** Calculates the lowercase SHA-256 digest for one byte sequence. */
function sha256Bytes(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

/** Validates an English-only relative path used by the external asset map. */
export function normalizeExternalAssetPath(value, label = 'path') {
  if (typeof value !== 'string' || value.length === 0 || value !== value.trim()) {
    throw new Error(`${label} must be a non-empty relative path without surrounding whitespace.`);
  }
  if (!/^[A-Za-z0-9@+_.~%/-]+$/u.test(value)) {
    throw new Error(`${label} must use English ASCII path characters only: '${value}'.`);
  }
  if (value.includes('\\') || value.startsWith('/') || /^[A-Za-z]:/u.test(value)) {
    throw new Error(`${label} must be relative and use forward slashes: '${value}'.`);
  }
  let decoded;
  try {
    decoded = decodeURIComponent(value);
  } catch {
    throw new Error(`${label} contains malformed URL escaping: '${value}'.`);
  }
  if (decoded.includes('\\') || decoded.startsWith('/')) {
    throw new Error(`${label} decodes to an absolute or backslash path: '${value}'.`);
  }
  for (const candidate of [value, decoded]) {
    const segments = candidate.split('/');
    if (segments.some((segment) => segment === '' || segment === '.' || segment === '..')) {
      throw new Error(`${label} must not contain empty, current-directory, or parent-directory segments: '${value}'.`);
    }
  }
  const normalized = path.posix.normalize(value);
  if (normalized !== value || normalized.startsWith('../') || path.posix.isAbsolute(normalized)) {
    throw new Error(`${label} escapes its declared root: '${value}'.`);
  }
  return normalized;
}

/** Parses one canonical HTTP(S) URL used by an exact or prefix mapping. */
function normalizeHttpUrl(value, label, requireTrailingSlash = false) {
  if (typeof value !== 'string' || value.trim() === '' || value !== value.trim()) {
    throw new Error(`${label} must be a non-empty absolute HTTP(S) URL.`);
  }
  let parsed;
  try {
    parsed = new URL(value);
  } catch {
    throw new Error(`${label} is not a valid absolute URL: '${value}'.`);
  }
  if (!NETWORK_PROTOCOLS.has(parsed.protocol) || parsed.username || parsed.password) {
    throw new Error(`${label} must be an HTTP(S) URL without credentials: '${value}'.`);
  }
  if (parsed.hash) {
    throw new Error(`${label} must not contain a fragment because URL fragments are not sent in HTTP requests: '${value}'.`);
  }
  if (requireTrailingSlash && (!parsed.pathname.endsWith('/') || parsed.search || parsed.hash)) {
    throw new Error(`${label} must end in '/' and must not contain a query or fragment: '${value}'.`);
  }
  return parsed.href;
}

/** Validates one immutable mapped-file descriptor and resolves its response MIME type. */
function normalizeFileDescriptor(descriptor, label, requestUrl, relativePath = null) {
  if (descriptor == null || typeof descriptor !== 'object' || Array.isArray(descriptor)) {
    throw new Error(`${label} must be an object.`);
  }
  const assetPackPath = normalizeExternalAssetPath(descriptor.assetPackPath, `${label}.assetPackPath`);
  if (typeof descriptor.sha256 !== 'string' || !/^[a-f0-9]{64}$/u.test(descriptor.sha256)) {
    throw new Error(`${label}.sha256 must be a lowercase 64-character SHA-256 digest.`);
  }
  if (!Number.isSafeInteger(descriptor.byteSize) || descriptor.byteSize < 0) {
    throw new Error(`${label}.byteSize must be a non-negative safe integer.`);
  }
  let mimeType = descriptor.mimeType;
  if (mimeType != null) {
    if (typeof mimeType !== 'string' || mimeType.trim() === '' || /[\r\n]/u.test(mimeType)) {
      throw new Error(`${label}.mimeType must be a non-empty HTTP header value when declared.`);
    }
    mimeType = mimeType.trim();
  } else {
    const requestPath = new URL(requestUrl).pathname;
    const extension = path.posix.extname(requestPath || assetPackPath).toLowerCase()
      || path.posix.extname(assetPackPath).toLowerCase();
    mimeType = MIME_TYPES.get(extension);
    if (!mimeType) {
      throw new Error(`${label}.mimeType is required because '${requestUrl}' has no recognized file extension.`);
    }
  }
  return {
    assetPackPath,
    sha256: descriptor.sha256,
    byteSize: descriptor.byteSize,
    mimeType,
    relativePath
  };
}

/** Resolves a mapped asset path and rejects both lexical and symbolic-link root escape. */
async function resolveMappedFile(assetPackRoot, assetPackPath) {
  const absoluteRoot = path.resolve(assetPackRoot);
  const absolutePath = path.resolve(absoluteRoot, ...assetPackPath.split('/'));
  if (absolutePath === absoluteRoot || !absolutePath.startsWith(`${absoluteRoot}${path.sep}`)) {
    throw new Error(`Mapped asset path escapes the asset-pack root: '${assetPackPath}'.`);
  }
  const [realRoot, realPath] = await Promise.all([
    fs.realpath(absoluteRoot),
    fs.realpath(absolutePath)
  ]);
  if (realPath === realRoot || !realPath.startsWith(`${realRoot}${path.sep}`)) {
    throw new Error(`Mapped asset path escapes the asset-pack root through a symbolic link: '${assetPackPath}'.`);
  }
  return absolutePath;
}

/** Reads one mapped file and enforces its declared byte size and SHA-256 digest. */
async function readVerifiedRouteBytes(assetMap, route) {
  let absolutePath;
  try {
    absolutePath = await resolveMappedFile(assetMap.assetPackRoot, route.assetPackPath);
  } catch (error) {
    throw new Error(`External asset '${route.assetPackPath}' cannot be resolved: ${error.message}`);
  }
  let bytes;
  let statistics;
  try {
    [bytes, statistics] = await Promise.all([fs.readFile(absolutePath), fs.stat(absolutePath)]);
  } catch (error) {
    throw new Error(`External asset '${route.assetPackPath}' cannot be read: ${error.message}`);
  }
  if (!statistics.isFile()) {
    throw new Error(`External asset '${route.assetPackPath}' is not a regular file.`);
  }
  const digest = sha256Bytes(bytes);
  if (bytes.byteLength !== route.byteSize || digest !== route.sha256) {
    throw new Error(`External asset hash drift for '${route.assetPackPath}': declared=${route.sha256}/${route.byteSize}, current=${digest}/${bytes.byteLength}.`);
  }
  return bytes;
}

/** Loads, validates, and byte-verifies one explicit external URL asset map. */
export async function loadExternalAssetMap(mapPath, assetPackRoot) {
  if (!mapPath) return null;
  const absoluteMapPath = path.resolve(mapPath);
  const absoluteAssetPackRoot = path.resolve(assetPackRoot);
  let mapBytes;
  let document;
  try {
    mapBytes = await fs.readFile(absoluteMapPath);
    document = JSON.parse(mapBytes.toString('utf8'));
  } catch (error) {
    throw new Error(`External asset map '${absoluteMapPath}' is missing or invalid JSON: ${error.message}`);
  }
  if (document == null || typeof document !== 'object' || Array.isArray(document)) {
    throw new Error('External asset map must be a JSON object.');
  }
  if (document.schemaVersion !== 1) {
    throw new Error(`External asset map schemaVersion must be 1; received '${document.schemaVersion}'.`);
  }
  validateKnownKeys(document, new Set(['schemaVersion', 'mappings']), 'External asset map');
  if (!Array.isArray(document.mappings) || document.mappings.length === 0) {
    throw new Error('External asset map mappings must be a non-empty array.');
  }

  const routes = [];
  const prefixes = [];
  for (let mappingIndex = 0; mappingIndex < document.mappings.length; mappingIndex += 1) {
    const mapping = document.mappings[mappingIndex];
    const label = `External asset mapping ${mappingIndex}`;
    if (mapping == null || typeof mapping !== 'object' || Array.isArray(mapping)) {
      throw new Error(`${label} must be an object.`);
    }
    if (mapping.type === 'exact') {
      validateKnownKeys(
        mapping,
        new Set(['type', 'url', 'assetPackPath', 'sha256', 'byteSize', 'mimeType']),
        label
      );
      const requestUrl = normalizeHttpUrl(mapping.url, `${label}.url`);
      routes.push({
        mappingType: 'exact',
        requestUrl,
        ...normalizeFileDescriptor(mapping, label, requestUrl)
      });
      continue;
    }
    if (mapping.type === 'url-prefix') {
      validateKnownKeys(mapping, new Set(['type', 'urlPrefix', 'files']), label);
      const urlPrefix = normalizeHttpUrl(mapping.urlPrefix, `${label}.urlPrefix`, true);
      if (!Array.isArray(mapping.files) || mapping.files.length === 0) {
        throw new Error(`${label}.files must be a non-empty array.`);
      }
      const prefix = { urlPrefix, routes: [] };
      prefixes.push(prefix);
      for (let fileIndex = 0; fileIndex < mapping.files.length; fileIndex += 1) {
        const file = mapping.files[fileIndex];
        const fileLabel = `${label}.files[${fileIndex}]`;
        if (file == null || typeof file !== 'object' || Array.isArray(file)) {
          throw new Error(`${fileLabel} must be an object.`);
        }
        validateKnownKeys(
          file,
          new Set(['relativePath', 'assetPackPath', 'sha256', 'byteSize', 'mimeType']),
          fileLabel
        );
        const relativePath = normalizeExternalAssetPath(file?.relativePath, `${fileLabel}.relativePath`);
        const requestUrl = new URL(relativePath, urlPrefix).href;
        if (!requestUrl.startsWith(urlPrefix)) {
          throw new Error(`${fileLabel}.relativePath escapes URL prefix '${urlPrefix}'.`);
        }
        const route = {
          mappingType: 'url-prefix',
          urlPrefix,
          requestUrl,
          ...normalizeFileDescriptor(file, fileLabel, requestUrl, relativePath)
        };
        prefix.routes.push(route);
        routes.push(route);
      }
      continue;
    }
    throw new Error(`${label}.type must be 'exact' or 'url-prefix'; received '${mapping.type}'.`);
  }

  for (let leftIndex = 0; leftIndex < prefixes.length; leftIndex += 1) {
    for (let rightIndex = leftIndex + 1; rightIndex < prefixes.length; rightIndex += 1) {
      const left = prefixes[leftIndex].urlPrefix;
      const right = prefixes[rightIndex].urlPrefix;
      if (left.startsWith(right) || right.startsWith(left)) {
        throw new Error(`External asset URL prefixes overlap ambiguously: '${left}' and '${right}'.`);
      }
    }
  }

  const routeByUrl = new Map();
  for (const route of routes) {
    const prior = routeByUrl.get(route.requestUrl);
    if (prior) {
      throw new Error(`External asset URL is covered more than once: '${route.requestUrl}'.`);
    }
    routeByUrl.set(route.requestUrl, route);
  }
  const prefixByUrl = new Map();
  for (const prefix of prefixes) {
    if (prefixByUrl.has(prefix.urlPrefix)) {
      throw new Error(`External asset URL prefix is declared more than once: '${prefix.urlPrefix}'.`);
    }
    prefixByUrl.set(prefix.urlPrefix, prefix);
  }

  const assetMap = {
    schemaVersion: 1,
    assetPackRoot: absoluteAssetPackRoot,
    mapPath: absoluteMapPath,
    mapByteSize: mapBytes.byteLength,
    mapSha256: sha256Bytes(mapBytes),
    mappingCount: document.mappings.length,
    routes: routes.sort((left, right) => compareStrings(left.requestUrl, right.requestUrl)),
    prefixes: prefixes.sort((left, right) => compareStrings(left.urlPrefix, right.urlPrefix)),
    routeByUrl,
    prefixByUrl
  };
  for (const route of assetMap.routes) await readVerifiedRouteBytes(assetMap, route);
  return assetMap;
}

/** Resolves one browser request to exactly one enumerated external asset route. */
export function resolveExternalAssetRoute(assetMap, requestUrl) {
  if (!assetMap) return null;
  let canonicalUrl;
  try {
    canonicalUrl = normalizeHttpUrl(requestUrl, 'External request URL');
  } catch {
    return null;
  }
  return assetMap.routeByUrl.get(canonicalUrl) ?? null;
}

/** Builds a CDP Fetch.fulfillRequest response from one verified local asset. */
export async function createExternalAssetCaptureResponse(assetMap, requestUrl) {
  const route = resolveExternalAssetRoute(assetMap, requestUrl);
  if (!route) return null;
  const bytes = await readVerifiedRouteBytes(assetMap, route);
  return {
    responseCode: 200,
    responseHeaders: [
      { name: 'Access-Control-Allow-Origin', value: '*' },
      { name: 'Cache-Control', value: 'no-store' },
      { name: 'Content-Length', value: String(bytes.byteLength) },
      { name: 'Content-Type', value: route.mimeType },
      { name: 'Cross-Origin-Resource-Policy', value: 'cross-origin' }
    ],
    body: bytes.toString('base64'),
    route
  };
}

/** Converts one scanned external reference into the URL requested from the local HTTP capture page. */
function normalizeScannedReference(reference) {
  const value = String(reference ?? '').trim();
  try {
    const parsed = new URL(value.startsWith('//') ? `http:${value}` : value);
    parsed.hash = '';
    return normalizeHttpUrl(
      parsed.href,
      value.startsWith('//') ? 'Protocol-relative external reference' : 'External reference'
    );
  } catch {
    return null;
  }
}

/** Enforces complete unique map coverage for required-case external references. */
export function validateRequiredExternalAssetCoverage(externalReferences, requiredCaseIds, assetMap) {
  const requiredIds = requiredCaseIds instanceof Set ? requiredCaseIds : new Set(requiredCaseIds ?? []);
  const requiredReferences = (externalReferences ?? []).filter((entry) => (
    REQUIRED_REFERENCE_KINDS.has(entry.kind)
      && (entry.caseIds ?? []).some((caseId) => requiredIds.has(caseId))
  ));
  const actionable = [];
  const failures = [];
  for (const reference of requiredReferences) {
    const requestUrl = normalizeScannedReference(reference.reference);
    if (!requestUrl) {
      if (!String(reference.reference).startsWith('data:') && !String(reference.reference).startsWith('blob:')) {
        failures.push(`${reference.kind} '${reference.reference}' is not a mappable HTTP(S) URL.`);
      }
      continue;
    }
    actionable.push({ ...reference, requestUrl });
  }
  if (actionable.length > 0 && !assetMap) {
    failures.push(`${actionable.length} required external reference(s) need --external-asset-map.`);
  }

  const routeCaseIds = new Map();
  if (assetMap) {
    for (const reference of actionable) {
      const caseIds = reference.caseIds;
      if (reference.kind === 'asset-directory') {
        const prefix = assetMap.prefixByUrl.get(reference.requestUrl);
        if (!prefix) {
          failures.push(`Required asset-directory '${reference.reference}' has no exact url-prefix mapping.`);
          continue;
        }
        for (const route of prefix.routes) {
          const owners = routeCaseIds.get(route.requestUrl) ?? new Set();
          for (const caseId of caseIds) owners.add(caseId);
          routeCaseIds.set(route.requestUrl, owners);
        }
        continue;
      }
      const route = assetMap.routeByUrl.get(reference.requestUrl);
      if (!route) {
        failures.push(`Required ${reference.kind} '${reference.reference}' is not enumerated by the external asset map.`);
        continue;
      }
      const owners = routeCaseIds.get(route.requestUrl) ?? new Set();
      for (const caseId of caseIds) owners.add(caseId);
      routeCaseIds.set(route.requestUrl, owners);
    }
  }
  if (failures.length > 0) {
    throw new Error(`External asset map coverage failed:\n${failures.map((failure) => `- ${failure}`).join('\n')}`);
  }
  return { requiredReferenceCount: actionable.length, routeCaseIds };
}

/** Returns the deterministic public lock record for one loaded external asset map. */
export function makeExternalAssetMapLockRecord(assetMap) {
  if (!assetMap) return null;
  return {
    schemaVersion: assetMap.schemaVersion,
    byteSize: assetMap.mapByteSize,
    sha256: assetMap.mapSha256,
    mappingCount: assetMap.mappingCount,
    mappedFileCount: assetMap.routes.length
  };
}
