import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';

import {
  loadExternalAssetMap,
  makeExternalAssetMapLockRecord,
  validateRequiredExternalAssetCoverage
} from './external_asset_map.mjs';
import { THREE_R185_COMMIT } from './manifest_common.mjs';

const LOCK_KIND = 'gvm-three-r185-input-lock';
const EXPECTED_EXAMPLE_COUNT = 588;
const EXPECTED_EXCLUDED_COUNT = 77;
const EXPECTED_CAPTURE_WIDTH = 800;
const EXPECTED_CAPTURE_HEIGHT = 500;
const EXPECTED_CAPTURE_BYTES = EXPECTED_CAPTURE_WIDTH * EXPECTED_CAPTURE_HEIGHT * 4;
const EXPECTED_RANDOM_SEED = 0x12345678;
const SOURCE_EXTENSIONS = new Set(['.css', '.html', '.js', '.mjs']);
const ASSET_EXTENSIONS = new Set([
  '.3dm', '.3ds', '.3mf', '.amf', '.avif', '.basis', '.bin', '.bmp', '.bvh', '.dae',
  '.dat', '.dds', '.drc', '.exr', '.fbx', '.fnt', '.frag', '.gcode', '.gif', '.glb',
  '.glsl', '.gltf', '.hdr', '.ico', '.ies', '.ifc', '.jpeg', '.jpg', '.json', '.kmz',
  '.ktx', '.ktx2', '.ldr', '.lwo', '.m3d', '.md2', '.mdd', '.mp3', '.mp4', '.mpd',
  '.mtl', '.nrrd', '.obj', '.ogg', '.otf', '.pdb', '.pcd', '.ply', '.png', '.prwm',
  '.pvr', '.raw', '.rgbm', '.stl', '.svg', '.svgz', '.tga', '.tif', '.tiff', '.ttf',
  '.usdz', '.vdb', '.vert', '.vox', '.vtk', '.wasm', '.wav', '.webm', '.webp', '.wgsl',
  '.woff', '.woff2', '.wrl', '.xyz', '.zip', '.zlib'
]);
const DIRECTORY_SETTERS = new Set(['setDecoderPath', 'setTranscoderPath', 'setWorkerPath']);
const INPUT_REPLAY_POINTER_EVENTS = new Set(['pointerdown', 'pointerleave', 'pointermove', 'pointerup']);
const INPUT_REPLAY_MOUSE_EVENTS = new Set(['click', 'mousemove']);
const INPUT_REPLAY_KEYBOARD_EVENTS = new Set(['keydown', 'keyup']);
const ROOT_ORDER = new Map([['upstream', 0], ['assetPack', 1], ['oracle', 2]]);

/** Converts arbitrary case and scenario identifiers to stable filesystem segments. */
export function sanitizeLockSegment(value) {
  const result = String(value ?? '').trim()
    .replaceAll(/[^a-zA-Z0-9._-]+/gu, '-')
    .replaceAll(/-+/gu, '-')
    .replaceAll(/^-|-$/gu, '');
  return result || 'unnamed';
}

/** Calculates the lowercase SHA-256 digest for one byte sequence. */
export function sha256Bytes(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

/** Normalizes one repository-relative path and rejects absolute or escaping values. */
export function normalizeLockRelativePath(value, label = 'path') {
  const slashPath = String(value).replaceAll('\\', '/');
  const normalized = path.posix.normalize(slashPath).replace(/^\.\//u, '');
  if (normalized === '' || normalized === '.' || normalized.startsWith('../') || path.posix.isAbsolute(normalized)) {
    throw new Error(`${label} must remain relative to its declared lock root: '${value}'.`);
  }
  return normalized;
}

/** Resolves a validated lock-relative path without permitting lexical root escape. */
function resolveInsideRoot(rootPath, relativePath) {
  const normalized = normalizeLockRelativePath(relativePath);
  const absoluteRoot = path.resolve(rootPath);
  const absolutePath = path.resolve(absoluteRoot, ...normalized.split('/'));
  if (absolutePath !== absoluteRoot && !absolutePath.startsWith(`${absoluteRoot}${path.sep}`)) {
    throw new Error(`Resolved path escapes its lock root: '${relativePath}'.`);
  }
  return absolutePath;
}

/** Removes URL query and fragment suffixes while preserving the local pathname. */
function stripUrlSuffix(value) {
  return String(value).split('#', 1)[0].split('?', 1)[0];
}

/** Returns true when a static reference cannot be satisfied from local input roots. */
function isExternalReference(value) {
  const reference = String(value).trim();
  return reference.startsWith('//')
    || /^[a-z][a-z0-9+.-]*:/iu.test(reference)
    || reference.startsWith('data:')
    || reference.startsWith('blob:');
}

/** Decodes one URL pathname without allowing malformed escaping to abort the full scan. */
function decodeReferencePath(value) {
  try {
    return decodeURIComponent(value);
  } catch {
    return value;
  }
}

/** Resolves a local URL reference to the upstream repository namespace. */
function resolveRepositoryReference(reference, importerRelativePath) {
  const stripped = decodeReferencePath(stripUrlSuffix(reference.trim()));
  if (stripped.startsWith('/')) {
    return normalizeLockRelativePath(stripped.slice(1), 'root-relative reference');
  }
  return normalizeLockRelativePath(
    path.posix.join(path.posix.dirname(importerRelativePath), stripped),
    'local reference'
  );
}

/** Maps an upstream URL path to the asset-pack namespace rooted at Three examples. */
function repositoryPathToAssetPath(repositoryRelativePath) {
  const normalized = normalizeLockRelativePath(repositoryRelativePath);
  return normalized.startsWith('examples/') ? normalized.slice('examples/'.length) : normalized;
}

/** Resolves one asset-pack-relative dependency from another packed asset. */
function resolvePackedAssetReference(reference, importerAssetPath) {
  const stripped = decodeReferencePath(stripUrlSuffix(reference.trim()));
  if (stripped.startsWith('/')) {
    return normalizeLockRelativePath(stripped.slice(1), 'asset root reference');
  }
  return normalizeLockRelativePath(
    path.posix.join(path.posix.dirname(importerAssetPath), stripped),
    'packed asset reference'
  );
}

/** Returns true when a string has a recognized local asset filename extension. */
function isAssetPath(value) {
  const pathname = stripUrlSuffix(value).toLowerCase();
  return ASSET_EXTENSIONS.has(path.posix.extname(pathname));
}

/** Replaces JavaScript comments with spaces while preserving strings and byte offsets. */
function maskJavaScriptComments(source) {
  const characters = source.split('');
  let state = 'code';
  let escaped = false;
  for (let index = 0; index < characters.length; index += 1) {
    const current = characters[index];
    const next = characters[index + 1] ?? '';
    if (state === 'line-comment') {
      if (current === '\n' || current === '\r') {
        state = 'code';
      } else {
        characters[index] = ' ';
      }
      continue;
    }
    if (state === 'block-comment') {
      if (current === '*' && next === '/') {
        characters[index] = ' ';
        characters[index + 1] = ' ';
        index += 1;
        state = 'code';
      } else if (current !== '\n' && current !== '\r') {
        characters[index] = ' ';
      }
      continue;
    }
    if (state !== 'code') {
      if (escaped) {
        escaped = false;
      } else if (current === '\\') {
        escaped = true;
      } else if ((state === 'single-quote' && current === "'")
        || (state === 'double-quote' && current === '"')
        || (state === 'template' && current === '`')) {
        state = 'code';
      }
      continue;
    }
    if (current === '/' && next === '/') {
      characters[index] = ' ';
      characters[index + 1] = ' ';
      index += 1;
      state = 'line-comment';
    } else if (current === '/' && next === '*') {
      characters[index] = ' ';
      characters[index + 1] = ' ';
      index += 1;
      state = 'block-comment';
    } else if (current === "'") {
      state = 'single-quote';
    } else if (current === '"') {
      state = 'double-quote';
    } else if (current === '`') {
      state = 'template';
    }
  }
  return characters.join('');
}

/** Removes HTML comments before scanning static element attributes and module bodies. */
function removeHtmlComments(source) {
  return source.replace(/<!--[\s\S]*?-->/gu, '');
}

/** Removes CSS comments before scanning imports and URL references. */
function removeCssComments(source) {
  return source.replace(/\/\*[\s\S]*?\*\//gu, '');
}

/** Sorts strings with a stable bytewise ordering independent of the active locale. */
function compareStrings(left, right) {
  return left < right ? -1 : left > right ? 1 : 0;
}

/** Sorts lock file entries by root kind and then canonical relative path. */
function compareLockFiles(left, right) {
  const rootDifference = (ROOT_ORDER.get(left.root) ?? 99) - (ROOT_ORDER.get(right.root) ?? 99);
  return rootDifference || compareStrings(left.relativePath, right.relativePath);
}

/** Resolves a checkout's Git metadata directory, including linked-worktree files. */
async function resolveGitDirectory(upstreamRoot) {
  const dotGitPath = path.join(path.resolve(upstreamRoot), '.git');
  let statistics;
  try {
    statistics = await fs.stat(dotGitPath);
  } catch (error) {
    if (error.code === 'ENOENT') {
      return null;
    }
    throw error;
  }
  if (statistics.isDirectory()) {
    return dotGitPath;
  }
  const declaration = (await fs.readFile(dotGitPath, 'utf8')).trim();
  const match = /^gitdir:\s*(.+)$/iu.exec(declaration);
  if (!match) {
    throw new Error(`${dotGitPath}: unsupported Git metadata declaration.`);
  }
  return path.resolve(path.dirname(dotGitPath), match[1]);
}

/** Reads one checked-out Git commit without invoking Git or consulting network state. */
async function readCheckedOutCommit(upstreamRoot) {
  const gitDirectory = await resolveGitDirectory(upstreamRoot);
  if (!gitDirectory) {
    return null;
  }
  const head = (await fs.readFile(path.join(gitDirectory, 'HEAD'), 'utf8')).trim();
  if (/^[a-f0-9]{40}$/u.test(head)) {
    return head;
  }
  const referenceMatch = /^ref:\s*(.+)$/u.exec(head);
  if (!referenceMatch) {
    throw new Error(`${gitDirectory}/HEAD: unsupported Git HEAD value '${head}'.`);
  }
  let commonDirectory = gitDirectory;
  try {
    const commonDeclaration = (await fs.readFile(path.join(gitDirectory, 'commondir'), 'utf8')).trim();
    commonDirectory = path.resolve(gitDirectory, commonDeclaration);
  } catch (error) {
    if (error.code !== 'ENOENT') {
      throw error;
    }
  }
  for (const metadataDirectory of [gitDirectory, commonDirectory]) {
    try {
      const commit = (await fs.readFile(path.join(metadataDirectory, referenceMatch[1]), 'utf8')).trim();
      if (/^[a-f0-9]{40}$/u.test(commit)) {
        return commit;
      }
    } catch (error) {
      if (error.code !== 'ENOENT') {
        throw error;
      }
    }
  }
  try {
    const packedReferences = await fs.readFile(path.join(commonDirectory, 'packed-refs'), 'utf8');
    for (const line of packedReferences.split(/\r?\n/u)) {
      const [commit, reference] = line.trim().split(/\s+/u);
      if (reference === referenceMatch[1] && /^[a-f0-9]{40}$/u.test(commit)) {
        return commit;
      }
    }
  } catch (error) {
    if (error.code !== 'ENOENT') {
      throw error;
    }
  }
  throw new Error(`${gitDirectory}/HEAD: cannot resolve checked-out ref '${referenceMatch[1]}'.`);
}

/** Rejects a version-controlled upstream root that is not checked out at the manifest commit. */
async function validateUpstreamCommit(upstreamRoot, expectedCommit) {
  const observedCommit = await readCheckedOutCommit(upstreamRoot);
  if (observedCommit && observedCommit !== expectedCommit) {
    throw new Error(`Upstream checkout commit drift: manifest=${expectedCommit}, checkout=${observedCommit}.`);
  }
}

/** Parses every import-map imports entry embedded directly in one HTML document. */
function extractImportMap(htmlSource, htmlRelativePath) {
  const imports = new Map();
  const pattern = /<script\b[^>]*\btype\s*=\s*["']importmap["'][^>]*>([\s\S]*?)<\/script>/giu;
  for (const match of htmlSource.matchAll(pattern)) {
    let document;
    try {
      document = JSON.parse(match[1]);
    } catch (error) {
      throw new Error(`${htmlRelativePath}: invalid inline import map: ${error.message}`);
    }
    for (const [specifier, target] of Object.entries(document.imports ?? {})) {
      if (typeof target === 'string') {
        imports.set(specifier, target);
      }
    }
  }
  return imports;
}

/** Applies exact and prefix import-map entries to one JavaScript module specifier. */
function applyImportMap(specifier, importMap) {
  if (importMap.has(specifier)) {
    return importMap.get(specifier);
  }
  const matchingPrefixes = [...importMap.keys()]
    .filter((key) => key.endsWith('/') && specifier.startsWith(key))
    .sort((left, right) => right.length - left.length || compareStrings(left, right));
  if (matchingPrefixes.length === 0) {
    return null;
  }
  const prefix = matchingPrefixes[0];
  return `${importMap.get(prefix)}${specifier.slice(prefix.length)}`;
}

/** Extracts literal static, re-export, and dynamic JavaScript module specifiers. */
function extractModuleSpecifiers(source) {
  const specifiers = [];
  const patterns = [
    /\b(?:import|export)\s+(?:[^'";]*?\s+from\s*)?["']([^"']+)["']/gu,
    /\bimport\s*\(\s*["']([^"']+)["']\s*\)/gu
  ];
  for (const pattern of patterns) {
    for (const match of source.matchAll(pattern)) {
      specifiers.push(match[1]);
    }
  }
  return [...new Set(specifiers)].sort(compareStrings);
}

/** Extracts inline module bodies and local script module sources from one HTML document. */
function extractHtmlModules(htmlSource) {
  const modules = [];
  const pattern = /<script\b([^>]*)\btype\s*=\s*["']module["']([^>]*)>([\s\S]*?)<\/script>/giu;
  for (const match of htmlSource.matchAll(pattern)) {
    const attributes = `${match[1]} ${match[2]}`;
    const sourceMatch = /\bsrc\s*=\s*["']([^"']+)["']/iu.exec(attributes);
    modules.push(sourceMatch ? { sourcePath: sourceMatch[1], inlineSource: '' } : { sourcePath: '', inlineSource: match[3] });
  }
  return modules;
}

/** Extracts local stylesheet paths from one HTML document. */
function extractHtmlStylesheets(htmlSource) {
  const stylesheets = [];
  const pattern = /<link\b([^>]*?)>/giu;
  for (const match of htmlSource.matchAll(pattern)) {
    if (!/\brel\s*=\s*["']stylesheet["']/iu.test(match[1])) {
      continue;
    }
    const hrefMatch = /\bhref\s*=\s*["']([^"']+)["']/iu.exec(match[1]);
    if (hrefMatch) {
      stylesheets.push(hrefMatch[1]);
    }
  }
  return [...new Set(stylesheets)].sort(compareStrings);
}

/** Extracts file-like HTML attribute values that are not module or stylesheet declarations. */
function extractHtmlAssetReferences(htmlSource) {
  const references = [];
  const pattern = /\b(?:src|href|poster)\s*=\s*["']([^"']+)["']/giu;
  for (const match of htmlSource.matchAll(pattern)) {
    if (isAssetPath(match[1])) {
      references.push(match[1]);
    }
  }
  return [...new Set(references)].sort(compareStrings);
}

/** Extracts CSS imports and URL references as source and asset dependencies. */
function extractCssReferences(cssSource) {
  const sourceReferences = [];
  const assetReferences = [];
  const importPattern = /@import\s+(?:url\(\s*)?["']?([^"')\s;]+)["']?\s*\)?/giu;
  for (const match of cssSource.matchAll(importPattern)) {
    sourceReferences.push(match[1]);
  }
  const urlPattern = /url\(\s*["']?([^"')]+)["']?\s*\)/giu;
  for (const match of cssSource.matchAll(urlPattern)) {
    if (isAssetPath(match[1])) {
      assetReferences.push(match[1]);
    }
  }
  return {
    sourceReferences: [...new Set(sourceReferences)].sort(compareStrings),
    assetReferences: [...new Set(assetReferences)].sort(compareStrings)
  };
}

/** Finds loader base-path setters and directory-style runtime dependency declarations. */
function extractPathSetters(source) {
  const setters = [];
  const pattern = /\.\s*(setPath|setResourcePath|setDecoderPath|setTranscoderPath|setWorkerPath)\s*\(\s*(["'`])([^"'`$]+)\2\s*\)/gu;
  for (const match of source.matchAll(pattern)) {
    setters.push({
      method: match[1],
      value: match[3],
      start: match.index,
      end: match.index + match[0].length
    });
  }
  return setters;
}

/** Returns the nearest applicable loader base path preceding one string literal. */
function findNearestBasePath(setters, offset) {
  let nearest = null;
  for (const setter of setters) {
    const appliesToLoaderAsset = setter.method === 'setPath' || setter.method === 'setResourcePath';
    if (appliesToLoaderAsset && setter.end <= offset && offset - setter.end <= 1024
      && (!nearest || setter.end > nearest.end)) {
      nearest = setter;
    }
  }
  return nearest?.value ?? '';
}

/** Extracts literal local asset paths and explicit decoder/transcoder directory references. */
function extractJavaScriptAssetReferences(source) {
  const setters = extractPathSetters(source);
  const assets = [];
  const directories = [];
  for (const setter of setters) {
    if (DIRECTORY_SETTERS.has(setter.method)) {
      directories.push(setter.value);
    }
  }
  const stringPattern = /(["'`])([^\r\n"'`$]+)\1/gu;
  for (const match of source.matchAll(stringPattern)) {
    const offset = match.index;
    if (setters.some((setter) => offset >= setter.start && offset < setter.end)) {
      continue;
    }
    const value = match[2].trim();
    if (!isAssetPath(value)) {
      continue;
    }
    const basePath = findNearestBasePath(setters, offset);
    assets.push(basePath && !isExternalReference(basePath) && !isExternalReference(value)
      ? path.posix.join(basePath, value)
      : value);
  }
  return {
    assetReferences: [...new Set(assets)].sort(compareStrings),
    directoryReferences: [...new Set(directories)].sort(compareStrings)
  };
}

/** Extracts literal new-URL worker and resource references from JavaScript source. */
function extractNewUrlReferences(source) {
  const references = [];
  const pattern = /\bnew\s+URL\s*\(\s*(["'`])([^"'`$]+)\1\s*,\s*import\.meta\.url\s*\)/gu;
  for (const match of source.matchAll(pattern)) {
    references.push(match[2]);
  }
  return [...new Set(references)].sort(compareStrings);
}

/** Recursively extracts file-like string values from one parsed JSON asset. */
function extractJsonAssetReferences(value, references) {
  if (typeof value === 'string') {
    if (isAssetPath(value) || isExternalReference(value)) {
      references.push(value);
    }
    return;
  }
  if (Array.isArray(value)) {
    for (const entry of value) {
      extractJsonAssetReferences(entry, references);
    }
    return;
  }
  if (value && typeof value === 'object') {
    for (const entry of Object.values(value)) {
      extractJsonAssetReferences(entry, references);
    }
  }
}

/** Extracts transitive local dependencies from supported text-based asset containers. */
function extractPackedAssetReferences(relativePath, bytes) {
  const extension = path.posix.extname(relativePath).toLowerCase();
  const references = [];
  if (extension === '.gltf' || extension === '.json') {
    try {
      extractJsonAssetReferences(JSON.parse(bytes.toString('utf8')), references);
    } catch (error) {
      throw new Error(`${relativePath}: invalid JSON asset: ${error.message}`);
    }
  } else if (extension === '.obj') {
    for (const match of bytes.toString('utf8').matchAll(/^\s*mtllib\s+(.+?)\s*$/gimu)) {
      references.push(match[1].trim());
    }
  } else if (extension === '.mtl') {
    for (const match of bytes.toString('utf8').matchAll(/^\s*map_[a-z0-9_]+\s+(?:-[a-z]+\s+\S+\s+)*(.+?)\s*$/gimu)) {
      references.push(match[1].trim());
    }
  } else if (extension === '.dae') {
    for (const match of bytes.toString('utf8').matchAll(/<init_from>\s*([^<]+?)\s*<\/init_from>/giu)) {
      references.push(match[1].trim());
    }
  }
  return [...new Set(references)].sort(compareStrings);
}

/** Parses and caches source dependencies that are independent of the referring case. */
function getCachedSourceDependencies(registry, relativePath, bytes) {
  let parsed = registry.sourceDependencyCache.get(relativePath);
  if (parsed) {
    return parsed;
  }
  const extension = path.posix.extname(relativePath).toLowerCase();
  if (extension === '.css') {
    parsed = { kind: 'css', ...extractCssReferences(removeCssComments(bytes.toString('utf8'))) };
  } else {
    const source = maskJavaScriptComments(bytes.toString('utf8'));
    parsed = {
      kind: 'javascript',
      moduleSpecifiers: extractModuleSpecifiers(source),
      scriptAssets: extractJavaScriptAssetReferences(source),
      newUrlReferences: extractNewUrlReferences(source)
    };
  }
  registry.sourceDependencyCache.set(relativePath, parsed);
  return parsed;
}

/** Parses and caches transitive references embedded in one packed asset. */
function getCachedAssetDependencies(registry, relativePath, bytes) {
  let references = registry.assetDependencyCache.get(relativePath);
  if (!references) {
    references = extractPackedAssetReferences(relativePath, bytes);
    registry.assetDependencyCache.set(relativePath, references);
  }
  return references;
}

/** Creates one mutable deterministic file registry shared across all scanned cases. */
function createFileRegistry(roots) {
  return {
    roots,
    files: new Map(),
    byteCache: new Map(),
    sourceDependencyCache: new Map(),
    assetDependencyCache: new Map()
  };
}

/** Reads and registers one locked file while merging all referring case identifiers. */
async function registerFile(registry, rootKind, relativePath, caseId) {
  const normalized = normalizeLockRelativePath(relativePath);
  const key = `${rootKind}:${normalized}`;
  let entry = registry.files.get(key);
  if (!entry) {
    const absolutePath = resolveInsideRoot(registry.roots[rootKind], normalized);
    let bytes;
    try {
      bytes = await fs.readFile(absolutePath);
    } catch (error) {
      throw new Error(`${caseId ?? 'external-asset-map'}: missing ${rootKind} input '${normalized}' at ${absolutePath}: ${error.message}`);
    }
    const statistics = await fs.stat(absolutePath);
    if (!statistics.isFile()) {
      throw new Error(`${caseId ?? 'external-asset-map'}: ${rootKind} input '${normalized}' is not a regular file.`);
    }
    entry = {
      root: rootKind,
      relativePath: normalized,
      byteSize: bytes.byteLength,
      sha256: sha256Bytes(bytes),
      caseIds: new Set()
    };
    registry.files.set(key, entry);
    registry.byteCache.set(key, bytes);
  }
  if (caseId) entry.caseIds.add(caseId);
  return registry.byteCache.get(key);
}

/** Records one external static dependency for review without attempting network access. */
function registerExternalReference(externalReferences, reference, kind, caseId) {
  const key = `${kind}:${reference}`;
  let entry = externalReferences.get(key);
  if (!entry) {
    entry = { kind, reference, caseIds: new Set() };
    externalReferences.set(key, entry);
  }
  entry.caseIds.add(caseId);
}

/** Enumerates and registers every regular file below an explicit asset-pack directory. */
async function registerAssetDirectory(registry, directoryRelativePath, caseId, assetQueue) {
  const normalized = normalizeLockRelativePath(directoryRelativePath, 'asset directory');
  const absoluteDirectory = resolveInsideRoot(registry.roots.assetPack, normalized);
  let directoryEntries;
  try {
    directoryEntries = await fs.readdir(absoluteDirectory, { withFileTypes: true });
  } catch (error) {
    throw new Error(`${caseId}: missing assetPack directory '${normalized}' at ${absoluteDirectory}: ${error.message}`);
  }
  directoryEntries.sort((left, right) => compareStrings(left.name, right.name));
  let fileCount = 0;
  for (const directoryEntry of directoryEntries) {
    const childPath = path.posix.join(normalized, directoryEntry.name);
    if (directoryEntry.isDirectory()) {
      fileCount += await registerAssetDirectory(registry, childPath, caseId, assetQueue);
    } else if (directoryEntry.isFile()) {
      assetQueue.push(childPath);
      fileCount += 1;
    }
  }
  if (fileCount === 0) {
    throw new Error(`${caseId}: assetPack directory '${normalized}' contains no regular files.`);
  }
  return fileCount;
}

/** Resolves a module specifier using an example's import map and importer location. */
function resolveModuleSpecifier(specifier, importerRelativePath, htmlRelativePath, importMap) {
  if (isExternalReference(specifier)) {
    return { external: specifier, relativePath: '' };
  }
  const mapped = specifier.startsWith('.') || specifier.startsWith('/')
    ? specifier
    : applyImportMap(specifier, importMap);
  if (!mapped) {
    return { external: `unmapped:${specifier}`, relativePath: '' };
  }
  const basePath = mapped === specifier ? importerRelativePath : htmlRelativePath;
  if (isExternalReference(mapped)) {
    return { external: mapped, relativePath: '' };
  }
  return { external: '', relativePath: resolveRepositoryReference(mapped, basePath) };
}

/** Scans one non-excluded manifest case and registers its full local static input closure. */
async function scanExampleInputs(example, registry, externalReferences) {
  const htmlRelativePath = normalizeLockRelativePath(example.upstreamPath, `${example.id} upstreamPath`);
  const htmlBytes = await registerFile(registry, 'upstream', htmlRelativePath, example.id);
  const htmlSource = removeHtmlComments(htmlBytes.toString('utf8'));
  const importMap = extractImportMap(htmlSource, htmlRelativePath);
  const sourceQueue = [];
  const assetQueue = [];
  const visitedSources = new Set([htmlRelativePath]);
  const visitedAssets = new Set();

  for (const stylesheet of extractHtmlStylesheets(htmlSource)) {
    if (isExternalReference(stylesheet)) {
      registerExternalReference(externalReferences, stylesheet, 'stylesheet', example.id);
    } else {
      sourceQueue.push(resolveRepositoryReference(stylesheet, htmlRelativePath));
    }
  }
  for (const module of extractHtmlModules(htmlSource)) {
    if (module.sourcePath) {
      const resolved = resolveModuleSpecifier(module.sourcePath, htmlRelativePath, htmlRelativePath, importMap);
      if (resolved.external) {
        registerExternalReference(externalReferences, resolved.external, 'module', example.id);
      } else {
        sourceQueue.push(resolved.relativePath);
      }
    } else {
      const inlineSource = maskJavaScriptComments(module.inlineSource);
      for (const specifier of extractModuleSpecifiers(inlineSource)) {
        const resolved = resolveModuleSpecifier(specifier, htmlRelativePath, htmlRelativePath, importMap);
        if (resolved.external) {
          registerExternalReference(externalReferences, resolved.external, 'module', example.id);
        } else {
          sourceQueue.push(resolved.relativePath);
        }
      }
      const inlineAssets = extractJavaScriptAssetReferences(inlineSource);
      for (const reference of [...inlineAssets.assetReferences, ...extractNewUrlReferences(inlineSource)]) {
        if (isExternalReference(reference)) {
          registerExternalReference(externalReferences, reference, 'asset', example.id);
        } else if (isAssetPath(reference)) {
          assetQueue.push(repositoryPathToAssetPath(resolveRepositoryReference(reference, htmlRelativePath)));
        } else {
          sourceQueue.push(resolveRepositoryReference(reference, htmlRelativePath));
        }
      }
      for (const reference of inlineAssets.directoryReferences) {
        if (isExternalReference(reference)) {
          registerExternalReference(externalReferences, reference, 'asset-directory', example.id);
        } else {
          const packedPath = repositoryPathToAssetPath(resolveRepositoryReference(reference, htmlRelativePath));
          await registerAssetDirectory(registry, packedPath, example.id, assetQueue);
        }
      }
    }
  }
  for (const reference of extractHtmlAssetReferences(htmlSource)) {
    if (isExternalReference(reference)) {
      registerExternalReference(externalReferences, reference, 'asset', example.id);
    } else {
      assetQueue.push(repositoryPathToAssetPath(resolveRepositoryReference(reference, htmlRelativePath)));
    }
  }

  while (sourceQueue.length > 0) {
    const sourceRelativePath = sourceQueue.shift();
    if (visitedSources.has(sourceRelativePath)) {
      continue;
    }
    visitedSources.add(sourceRelativePath);
    const sourceBytes = await registerFile(registry, 'upstream', sourceRelativePath, example.id);
    const dependencies = getCachedSourceDependencies(registry, sourceRelativePath, sourceBytes);
    if (dependencies.kind === 'css') {
      for (const reference of dependencies.sourceReferences) {
        if (isExternalReference(reference)) {
          registerExternalReference(externalReferences, reference, 'stylesheet', example.id);
        } else {
          sourceQueue.push(resolveRepositoryReference(reference, sourceRelativePath));
        }
      }
      for (const reference of dependencies.assetReferences) {
        if (isExternalReference(reference)) {
          registerExternalReference(externalReferences, reference, 'asset', example.id);
        } else {
          assetQueue.push(repositoryPathToAssetPath(resolveRepositoryReference(reference, sourceRelativePath)));
        }
      }
      continue;
    }
    for (const specifier of dependencies.moduleSpecifiers) {
      const resolved = resolveModuleSpecifier(specifier, sourceRelativePath, htmlRelativePath, importMap);
      if (resolved.external) {
        registerExternalReference(externalReferences, resolved.external, 'module', example.id);
      } else if (SOURCE_EXTENSIONS.has(path.posix.extname(resolved.relativePath).toLowerCase())) {
        sourceQueue.push(resolved.relativePath);
      } else {
        assetQueue.push(repositoryPathToAssetPath(resolved.relativePath));
      }
    }
    for (const reference of [...dependencies.scriptAssets.assetReferences, ...dependencies.newUrlReferences]) {
      if (isExternalReference(reference)) {
        registerExternalReference(externalReferences, reference, 'asset', example.id);
      } else if (SOURCE_EXTENSIONS.has(path.posix.extname(stripUrlSuffix(reference)).toLowerCase())) {
        sourceQueue.push(resolveRepositoryReference(reference, sourceRelativePath));
      } else {
        assetQueue.push(repositoryPathToAssetPath(resolveRepositoryReference(reference, sourceRelativePath)));
      }
    }
    for (const reference of dependencies.scriptAssets.directoryReferences) {
      if (isExternalReference(reference)) {
        registerExternalReference(externalReferences, reference, 'asset-directory', example.id);
      } else {
        const packedPath = repositoryPathToAssetPath(resolveRepositoryReference(reference, sourceRelativePath));
        await registerAssetDirectory(registry, packedPath, example.id, assetQueue);
      }
    }
  }

  while (assetQueue.length > 0) {
    const assetRelativePath = normalizeLockRelativePath(assetQueue.shift(), `${example.id} asset`);
    if (visitedAssets.has(assetRelativePath)) {
      continue;
    }
    visitedAssets.add(assetRelativePath);
    const assetBytes = await registerFile(registry, 'assetPack', assetRelativePath, example.id);
    for (const reference of getCachedAssetDependencies(registry, assetRelativePath, assetBytes)) {
      if (isExternalReference(reference)) {
        registerExternalReference(externalReferences, reference, 'asset', example.id);
      } else {
        assetQueue.push(resolvePackedAssetReference(reference, assetRelativePath));
      }
    }
  }
}

/** Returns the canonical image, metadata, and optional semantic Oracle paths for one scenario. */
export function makeLockedOraclePaths(example, scenario) {
  const basePath = path.posix.join(sanitizeLockSegment(example.id), sanitizeLockSegment(scenario.id));
  return {
    rgbaPath: `${basePath}.rgba`,
    metadataPath: `${basePath}.json`,
    semanticPath: `${basePath}.semantic.json`
  };
}

/** Returns whether one Loader or Exporter scenario must lock a semantic Oracle. */
function scenarioRequiresSemanticOracle(scenario) {
  return scenario?.kind === 'loader-snapshot' || scenario?.kind === 'export-round-trip';
}

/** Validates the immutable semantic Oracle required by Loader and Exporter scenarios. */
async function validateSemanticOracleFile(oracleRoot, oraclePaths, example, scenario) {
  if (!scenarioRequiresSemanticOracle(scenario)) return;
  let semantic;
  try {
    semantic = JSON.parse(await fs.readFile(
      resolveInsideRoot(oracleRoot, oraclePaths.semanticPath),
      'utf8'
    ));
  } catch (error) {
    throw new Error(`${example.id}/${scenario.id}: missing or invalid semantic Oracle '${oraclePaths.semanticPath}': ${error.message}`);
  }
  const exactFields = new Map([
    ['schemaVersion', 1],
    ['caseId', example.id],
    ['scenarioId', scenario.id],
    ['frame', scenario.frame],
    ['kind', scenario.kind],
    ['canonicalState', scenario.canonicalState ?? null]
  ]);
  for (const [field, expected] of exactFields) {
    if (semantic?.[field] !== expected) {
      throw new Error(`${example.id}/${scenario.id}: semantic Oracle ${field}=${JSON.stringify(semantic?.[field])}; expected ${JSON.stringify(expected)}.`);
    }
  }
  const expectedFields = [...exactFields.keys(), 'result'].sort();
  if (!semantic || typeof semantic !== 'object' || Array.isArray(semantic)
    || JSON.stringify(Object.keys(semantic).sort()) !== JSON.stringify(expectedFields)
    || !semantic.result || typeof semantic.result !== 'object' || Array.isArray(semantic.result)) {
    throw new Error(`${example.id}/${scenario.id}: semantic Oracle must contain only the locked identity and result object.`);
  }
  const sha256Pattern = /^[a-f0-9]{64}$/u;
  if (scenario.kind === 'loader-snapshot') {
    const expectedRenderableCount = Number.isInteger(example.loaderRenderableObjectCount)
      ? example.loaderRenderableObjectCount
      : example.renderableObjectCount;
    if (semantic.result.renderableObjectCount !== expectedRenderableCount
      || semantic.result.sceneRootCount !== (example.sceneRoots ?? []).length
      || !sha256Pattern.test(semantic.result.canonicalSceneSha256 ?? '')) {
      throw new Error(`${example.id}/${scenario.id}: Loader semantic Oracle does not match the locked Scene structure.`);
    }
  } else {
    if (!sha256Pattern.test(semantic.result.canonicalOutputSha256 ?? '')
      || !sha256Pattern.test(semantic.result.sourceSemanticSha256 ?? '')
      || !sha256Pattern.test(semantic.result.reimportedSemanticSha256 ?? '')
      || semantic.result.roundTripEquivalent !== true
      || semantic.result.sourceSemanticSha256 !== semantic.result.reimportedSemanticSha256) {
      throw new Error(`${example.id}/${scenario.id}: Exporter semantic Oracle does not prove an exact canonical round-trip.`);
    }
  }
}

/** Returns a lock-relative scenario artifact path, or null for symbolic canonical-state labels. */
function makeScenarioArtifactPath(value, label) {
  if (typeof value !== 'string' || value.trim() === '') return null;
  const trimmed = value.trim();
  if (!trimmed.includes('/') && path.posix.extname(trimmed) === '') return null;
  return normalizeLockRelativePath(trimmed, label);
}

/** Validates the identity and deterministic pointer payload of one locked input replay. */
function validateInputReplayBytes(bytes, relativePath, example, scenario) {
  let replay;
  try {
    replay = JSON.parse(bytes.toString('utf8'));
  } catch (error) {
    throw new Error(`${example.id}/${scenario.id}: invalid input replay '${relativePath}': ${error.message}`);
  }
  if (replay == null || typeof replay !== 'object' || Array.isArray(replay) || replay.schemaVersion !== 1) {
    throw new Error(`${example.id}/${scenario.id}: input replay '${relativePath}' must be a schemaVersion 1 object.`);
  }
  const expectedIdentity = new Map([
    ['caseId', example.id],
    ['scenarioId', scenario.id],
    ['frame', scenario.frame]
  ]);
  for (const [field, expected] of expectedIdentity) {
    if (replay[field] !== expected) {
      throw new Error(`${example.id}/${scenario.id}: input replay ${field}=${JSON.stringify(replay[field])}; expected ${JSON.stringify(expected)}.`);
    }
  }
  if (typeof replay.target !== 'string' || replay.target.trim() === '') {
    throw new Error(`${example.id}/${scenario.id}: input replay target must be a non-empty CSS selector.`);
  }
  if (!Array.isArray(replay.events) || replay.events.length === 0) {
    throw new Error(`${example.id}/${scenario.id}: input replay must contain at least one event.`);
  }
  let priorFrame = 0;
  for (let eventIndex = 0; eventIndex < replay.events.length; eventIndex += 1) {
    const event = replay.events[eventIndex];
    const eventFrame = event?.frame ?? 0;
    const isPointer = INPUT_REPLAY_POINTER_EVENTS.has(event?.type);
    const isMouse = INPUT_REPLAY_MOUSE_EVENTS.has(event?.type);
    const isKeyboard = INPUT_REPLAY_KEYBOARD_EVENTS.has(event?.type);
    const isWheel = event?.type === 'wheel';
    if (event == null || typeof event !== 'object' || Array.isArray(event)
      || !isPointer && !isMouse && !isKeyboard && !isWheel
      || !Number.isInteger(eventFrame) || eventFrame < priorFrame || eventFrame > scenario.frame) {
      throw new Error(`${example.id}/${scenario.id}: input replay event ${eventIndex} is invalid or outside its deterministic frame range.`);
    }
    if ((isPointer || isMouse || isWheel)
      && (!Number.isFinite(event.x) || event.x < 0 || !Number.isFinite(event.y) || event.y < 0)) {
      throw new Error(`${example.id}/${scenario.id}: input replay event ${eventIndex} has invalid target-relative coordinates.`);
    }
    if (event.target != null && (typeof event.target !== 'string' || event.target.trim() === '')) {
      throw new Error(`${example.id}/${scenario.id}: input replay event ${eventIndex} has an invalid target selector.`);
    }
    if (isPointer && event.pointerId != null
      && (!Number.isInteger(event.pointerId) || event.pointerId < 1)) {
      throw new Error(`${example.id}/${scenario.id}: input replay event ${eventIndex} has an invalid pointerId.`);
    }
    if (isPointer && event.button != null
      && (!Number.isInteger(event.button) || event.button < 0 || event.button > 2)) {
      throw new Error(`${example.id}/${scenario.id}: input replay event ${eventIndex} has an invalid button.`);
    }
    if (isKeyboard && (typeof event.key !== 'string' || event.key.length === 0
      || typeof event.code !== 'string' || event.code.length === 0
      || event.location != null && (!Number.isInteger(event.location) || event.location < 0 || event.location > 3)
      || event.repeat != null && typeof event.repeat !== 'boolean')) {
      throw new Error(`${example.id}/${scenario.id}: input replay keyboard event ${eventIndex} is invalid.`);
    }
    if (isWheel && (![event.deltaX ?? 0, event.deltaY ?? 0, event.deltaZ ?? 0].every(Number.isFinite)
      || event.deltaMode != null && (!Number.isInteger(event.deltaMode) || event.deltaMode < 0 || event.deltaMode > 2))) {
      throw new Error(`${example.id}/${scenario.id}: input replay wheel event ${eventIndex} is invalid.`);
    }
    if (['altKey', 'ctrlKey', 'metaKey', 'shiftKey'].some((field) => (
      event[field] != null && typeof event[field] !== 'boolean'
    ))) {
      throw new Error(`${example.id}/${scenario.id}: input replay event ${eventIndex} has an invalid modifier flag.`);
    }
    priorFrame = eventFrame;
  }
  return {
    sha256: sha256Bytes(bytes),
    eventCount: replay.events.length,
    target: replay.target.trim()
  };
}

/** Computes the exact viewport-clipped area covered by one set of canvas rectangles. */
function computeCanvasCoverageArea(canvases) {
  const rectangles = canvases.map((canvas) => ({
    left: Math.max(0, canvas.x),
    top: Math.max(0, canvas.y),
    right: Math.min(EXPECTED_CAPTURE_WIDTH, canvas.x + canvas.width),
    bottom: Math.min(EXPECTED_CAPTURE_HEIGHT, canvas.y + canvas.height)
  })).filter((rectangle) => rectangle.right > rectangle.left && rectangle.bottom > rectangle.top);
  const xCoordinates = [...new Set(rectangles.flatMap((rectangle) => [rectangle.left, rectangle.right]))]
    .sort((left, right) => left - right);
  let area = 0;
  for (let xIndex = 0; xIndex + 1 < xCoordinates.length; xIndex += 1) {
    const left = xCoordinates[xIndex];
    const right = xCoordinates[xIndex + 1];
    const intervals = rectangles.filter((rectangle) => rectangle.left < right && rectangle.right > left)
      .map((rectangle) => [rectangle.top, rectangle.bottom])
      .sort((first, second) => first[0] - second[0]);
    let coveredHeight = 0;
    let activeTop = null;
    let activeBottom = null;
    for (const [top, bottom] of intervals) {
      if (activeTop === null || top > activeBottom) {
        if (activeTop !== null) coveredHeight += activeBottom - activeTop;
        activeTop = top;
        activeBottom = bottom;
      } else {
        activeBottom = Math.max(activeBottom, bottom);
      }
    }
    if (activeTop !== null) coveredHeight += activeBottom - activeTop;
    area += (right - left) * coveredHeight;
  }
  return area;
}

/** Accepts a full-height canvas strip whose trailing gap is only CSS percentage rounding. */
function isNearCompleteHorizontalCanvasStrip(surfaces) {
  if (surfaces.length < 2
    || surfaces.some((surface) => surface.kind !== undefined && surface.kind !== 'canvas')) {
    return false;
  }
  const tolerance = 1;
  const ordered = [...surfaces].sort((left, right) => left.x - right.x);
  if (Math.abs(ordered[0].x) > tolerance
    || ordered.some((surface) => Math.abs(surface.y) > tolerance
      || Math.abs(surface.height - EXPECTED_CAPTURE_HEIGHT) > tolerance)) {
    return false;
  }
  for (let index = 1; index < ordered.length; index += 1) {
    const previousRight = ordered[index - 1].x + ordered[index - 1].width;
    if (Math.abs(ordered[index].x - previousRight) > tolerance) return false;
  }
  const finalRight = ordered.at(-1).x + ordered.at(-1).width;
  return finalRight >= EXPECTED_CAPTURE_WIDTH * 0.98
    && finalRight <= EXPECTED_CAPTURE_WIDTH + tolerance;
}

/** Validates renderer-surface or explicit DOM page-composite Oracle provenance. */
function validateReferenceCanvasCapture(metadata, example, scenario) {
  if (metadata.referenceCaptureMode === undefined) return;
  const label = `${example.id}/${scenario.id}`;
  if (metadata.referenceCaptureMode !== 'single-canvas'
    && metadata.referenceCaptureMode !== 'multi-canvas-composite'
    && metadata.referenceCaptureMode !== 'renderer-surface-composite'
    && metadata.referenceCaptureMode !== 'page-composite') {
    throw new Error(`${label}: Oracle metadata has unsupported referenceCaptureMode '${metadata.referenceCaptureMode}'.`);
  }
  const sourceSurfaces = metadata.sourceSurfaces ?? metadata.sourceCanvases;
  if (!Array.isArray(sourceSurfaces) || sourceSurfaces.length === 0) {
    throw new Error(`${label}: Oracle metadata must record sourceSurfaces for ${metadata.referenceCaptureMode}.`);
  }
  const indices = new Set();
  for (const [surfaceIndex, surface] of sourceSurfaces.entries()) {
    const isCanvas = surface.kind === undefined || surface.kind === 'canvas';
    if (!Number.isInteger(surface.index) || surface.index < 0 || indices.has(surface.index)
      || ![surface.x, surface.y, surface.width, surface.height].every(Number.isFinite)
      || surface.width <= 0 || surface.height <= 0
      || metadata.sourceSurfaces !== undefined && !['canvas', 'svg', 'css-renderer'].includes(surface.kind)
      || isCanvas && (!Number.isInteger(surface.backingWidth) || surface.backingWidth <= 0
        || !Number.isInteger(surface.backingHeight) || surface.backingHeight <= 0)) {
      throw new Error(`${label}: Oracle source surface ${surfaceIndex} has invalid geometry, backing size, type, or identity.`);
    }
    indices.add(surface.index);
  }
  if (metadata.referenceCaptureMode === 'single-canvas') {
    const canvas = sourceSurfaces[0];
    if (sourceSurfaces.length !== 1
      || canvas.kind !== undefined && canvas.kind !== 'canvas'
      || Math.abs(canvas.x) > 1 || Math.abs(canvas.y) > 1
      || Math.abs(canvas.width - EXPECTED_CAPTURE_WIDTH) > 1
      || Math.abs(canvas.height - EXPECTED_CAPTURE_HEIGHT) > 1
      || canvas.backingWidth !== EXPECTED_CAPTURE_WIDTH
      || canvas.backingHeight !== EXPECTED_CAPTURE_HEIGHT) {
      throw new Error(`${label}: single-canvas Oracle provenance must describe one full 800x500 renderer canvas.`);
    }
    return;
  }
  if (metadata.referenceCaptureMode === 'multi-canvas-composite'
    && (sourceSurfaces.length < 2 || sourceSurfaces.some((surface) => surface.kind !== undefined && surface.kind !== 'canvas'))) {
    throw new Error(`${label}: multi-canvas Oracle provenance must describe at least two renderer canvases.`);
  }
  if (metadata.referenceCaptureMode === 'page-composite') return;
  const coverageArea = computeCanvasCoverageArea(sourceSurfaces);
  if (coverageArea < EXPECTED_CAPTURE_WIDTH * EXPECTED_CAPTURE_HEIGHT * 0.999
    && !isNearCompleteHorizontalCanvasStrip(sourceSurfaces)) {
    throw new Error(`${label}: renderer-surface Oracle provenance covers only ${coverageArea} of ${EXPECTED_CAPTURE_WIDTH * EXPECTED_CAPTURE_HEIGHT} pixels.`);
  }
}

/** Validates one Oracle RGBA8 payload and its metadata against the fixed 800x500 contract. */
async function validateOracleFiles(oracleRoot, oraclePaths, example, scenario, inputReplay) {
  const caseId = example.id;
  const scenarioId = scenario.id;
  const metadataAbsolutePath = resolveInsideRoot(oracleRoot, oraclePaths.metadataPath);
  const rgbaAbsolutePath = resolveInsideRoot(oracleRoot, oraclePaths.rgbaPath);
  let metadata;
  try {
    metadata = JSON.parse(await fs.readFile(metadataAbsolutePath, 'utf8'));
  } catch (error) {
    throw new Error(`${caseId}/${scenarioId}: missing or invalid Oracle metadata '${oraclePaths.metadataPath}': ${error.message}`);
  }
  const exactMetadataFields = new Map([
    ['schemaVersion', 1],
    ['source', 'three-r185-reference'],
    ['upstreamCommit', THREE_R185_COMMIT],
    ['caseId', caseId],
    ['scenarioId', scenarioId],
    ['frame', scenario.frame],
    ['rowStrideBytes', EXPECTED_CAPTURE_WIDTH * 4],
    ['byteCount', EXPECTED_CAPTURE_BYTES],
    ['format', 'rgba8unorm'],
    ['canvasBackingWidth', EXPECTED_CAPTURE_WIDTH],
    ['canvasBackingHeight', EXPECTED_CAPTURE_HEIGHT]
  ]);
  for (const [field, expected] of exactMetadataFields) {
    if (metadata[field] !== expected) {
      throw new Error(`${caseId}/${scenarioId}: Oracle metadata ${field}=${JSON.stringify(metadata[field])}; expected ${JSON.stringify(expected)}.`);
    }
  }
  if (Number(metadata.width) !== EXPECTED_CAPTURE_WIDTH || Number(metadata.height) !== EXPECTED_CAPTURE_HEIGHT) {
    throw new Error(`${caseId}/${scenarioId}: Oracle metadata must declare 800x500; observed ${metadata.width}x${metadata.height}.`);
  }
  if (metadata.samplePolicy?.mode !== 'single-sample'
    || metadata.samplePolicy?.msaaEnabled !== false
    || metadata.samplePolicy?.simulateMsaa !== false) {
    throw new Error(`${caseId}/${scenarioId}: Oracle metadata must prove single-sample capture without MSAA simulation.`);
  }
  validateReferenceCanvasCapture(metadata, example, scenario);
  if (Number(metadata.randomSeed) !== EXPECTED_RANDOM_SEED) {
    throw new Error(`${caseId}/${scenarioId}: Oracle metadata randomSeed must be ${EXPECTED_RANDOM_SEED}; observed ${metadata.randomSeed}.`);
  }
  const expectedVirtualTimeMs = scenario.frame * (1000 / 60);
  const expectedNextFrameTimeMs = (scenario.frame + 1) * (1000 / 60);
  if (!Number.isFinite(metadata.virtualTimeMs)
    || Math.abs(metadata.virtualTimeMs - expectedVirtualTimeMs) > 1e-6
    || !Number.isFinite(metadata.nextFrameTimeMs)
    || Math.abs(metadata.nextFrameTimeMs - expectedNextFrameTimeMs) > 1e-6) {
    throw new Error(`${caseId}/${scenarioId}: Oracle virtual frame clock does not match frame ${scenario.frame} at fixed 60 Hz.`);
  }
  if (inputReplay) {
    const replayMetadata = metadata.inputReplay;
    if (replayMetadata?.sha256 !== inputReplay.sha256
      || replayMetadata?.caseId !== caseId
      || replayMetadata?.scenarioId !== scenarioId
      || Number(replayMetadata?.captureFrame) !== Number(metadata.frame)
      || replayMetadata?.eventCount !== inputReplay.eventCount
      || replayMetadata?.target !== inputReplay.target) {
      throw new Error(`${caseId}/${scenarioId}: Oracle metadata does not identify the exact locked input replay.`);
    }
  } else if (metadata.inputReplay != null) {
    throw new Error(`${caseId}/${scenarioId}: Oracle metadata declares an input replay for a scenario that has none.`);
  }
  let rgbaStatistics;
  try {
    rgbaStatistics = await fs.stat(rgbaAbsolutePath);
  } catch (error) {
    throw new Error(`${caseId}/${scenarioId}: missing Oracle RGBA '${oraclePaths.rgbaPath}': ${error.message}`);
  }
  if (!rgbaStatistics.isFile() || rgbaStatistics.size !== EXPECTED_CAPTURE_BYTES) {
    throw new Error(`${caseId}/${scenarioId}: Oracle RGBA must contain ${EXPECTED_CAPTURE_BYTES} bytes; observed ${rgbaStatistics.size}.`);
  }
}

/** Validates the immutable Three r185 manifest accounting required by the lock format. */
function validateManifestBaseline(manifest) {
  const examples = Array.isArray(manifest.examples) ? manifest.examples : [];
  if (examples.length !== EXPECTED_EXAMPLE_COUNT) {
    throw new Error(`Input lock requires exactly 588 manifest examples; found ${examples.length}.`);
  }
  const excludedCount = examples.filter((example) => example.status === 'excluded_upstream').length;
  if (excludedCount !== EXPECTED_EXCLUDED_COUNT) {
    throw new Error(`Input lock requires exactly 77 excluded_upstream examples; found ${excludedCount}.`);
  }
  const duplicateIds = examples
    .map((example) => example.id)
    .filter((id, index, ids) => ids.indexOf(id) !== index);
  if (duplicateIds.length > 0) {
    throw new Error(`Manifest contains duplicate example IDs: ${[...new Set(duplicateIds)].sort(compareStrings).join(', ')}.`);
  }
}

/** Converts the mutable registry to the stable public lock-file entry representation. */
function finalizeRegisteredFiles(registry) {
  return [...registry.files.values()]
    .map((entry) => ({
      root: entry.root,
      relativePath: entry.relativePath,
      byteSize: entry.byteSize,
      sha256: entry.sha256,
      caseIds: [...entry.caseIds].sort(compareStrings)
    }))
    .sort(compareLockFiles);
}

/** Converts external-reference evidence to deterministic sorted public records. */
function finalizeExternalReferences(externalReferences) {
  return [...externalReferences.values()]
    .map((entry) => ({
      kind: entry.kind,
      reference: entry.reference,
      caseIds: [...entry.caseIds].sort(compareStrings)
    }))
    .sort((left, right) => compareStrings(`${left.kind}:${left.reference}`, `${right.kind}:${right.reference}`));
}

/** Generates a deterministic lock for all non-excluded source inputs, packed assets, and required Oracles. */
export async function generateThreeInputLock(options) {
  const manifestBytes = await fs.readFile(options.manifestPath);
  const manifest = JSON.parse(manifestBytes.toString('utf8'));
  validateManifestBaseline(manifest);
  const roots = {
    upstream: path.resolve(options.upstreamRoot),
    assetPack: path.resolve(options.assetPackRoot),
    oracle: path.resolve(options.oracleRoot)
  };
  await validateUpstreamCommit(roots.upstream, manifest.upstream?.commit ?? '');
  const registry = createFileRegistry(roots);
  const externalReferences = new Map();
  const scannedExamples = manifest.examples
    .filter((example) => example.status !== 'excluded_upstream')
    .sort((left, right) => compareStrings(left.id, right.id));
  for (const example of scannedExamples) {
    await scanExampleInputs(example, registry, externalReferences);
  }

  const oracleScenarios = [];
  const oraclePathOwners = new Map();
  const requiredExamples = manifest.examples
    .filter((example) => example.status === 'phase1_required')
    .sort((left, right) => compareStrings(left.id, right.id));
  const finalizedExternalReferences = finalizeExternalReferences(externalReferences);
  const externalAssetMap = await loadExternalAssetMap(
    options.externalAssetMapPath ?? '',
    roots.assetPack
  );
  const externalCoverage = validateRequiredExternalAssetCoverage(
    finalizedExternalReferences,
    new Set(requiredExamples.map((example) => example.id)),
    externalAssetMap
  );
  for (const route of externalAssetMap?.routes ?? []) {
    const caseIds = externalCoverage.routeCaseIds.get(route.requestUrl) ?? new Set();
    if (caseIds.size === 0) {
      await registerFile(registry, 'assetPack', route.assetPackPath, null);
    } else {
      for (const caseId of [...caseIds].sort(compareStrings)) {
        await registerFile(registry, 'assetPack', route.assetPackPath, caseId);
      }
    }
  }
  for (const example of requiredExamples) {
    if (!Array.isArray(example.scenarios) || example.scenarios.length === 0) {
      throw new Error(`${example.id}: phase1_required case must declare at least one Oracle scenario.`);
    }
    const seenScenarioIds = new Set();
    for (const scenario of [...example.scenarios].sort((left, right) => compareStrings(left.id, right.id))) {
      if (!scenario || typeof scenario.id !== 'string' || scenario.id.trim() === '') {
        throw new Error(`${example.id}: every required Oracle scenario must have a non-empty id.`);
      }
      if (seenScenarioIds.has(scenario.id)) {
        throw new Error(`${example.id}: duplicate Oracle scenario id '${scenario.id}'.`);
      }
      seenScenarioIds.add(scenario.id);
      const inputReplayPath = makeScenarioArtifactPath(
        scenario.inputReplay,
        `${example.id}/${scenario.id} inputReplay`
      );
      const canonicalStatePath = makeScenarioArtifactPath(
        scenario.canonicalState,
        `${example.id}/${scenario.id} canonicalState`
      );
      let inputReplay = null;
      if (inputReplayPath) {
        const inputReplayBytes = await registerFile(registry, 'assetPack', inputReplayPath, example.id);
        inputReplay = validateInputReplayBytes(inputReplayBytes, inputReplayPath, example, scenario);
      }
      if (canonicalStatePath) {
        await registerFile(registry, 'assetPack', canonicalStatePath, example.id);
      }
      const oraclePaths = makeLockedOraclePaths(example, scenario);
      const semanticPath = scenarioRequiresSemanticOracle(scenario)
        ? oraclePaths.semanticPath
        : null;
      for (const relativePath of [oraclePaths.rgbaPath, oraclePaths.metadataPath, semanticPath].filter(Boolean)) {
        const owner = oraclePathOwners.get(relativePath);
        const identity = `${example.id}/${scenario.id}`;
        if (owner && owner !== identity) {
          throw new Error(`Oracle path collision '${relativePath}' between ${owner} and ${identity}.`);
        }
        oraclePathOwners.set(relativePath, identity);
      }
      await validateOracleFiles(roots.oracle, oraclePaths, example, scenario, inputReplay);
      await validateSemanticOracleFile(roots.oracle, oraclePaths, example, scenario);
      await registerFile(registry, 'oracle', oraclePaths.rgbaPath, example.id);
      await registerFile(registry, 'oracle', oraclePaths.metadataPath, example.id);
      if (semanticPath) {
        await registerFile(registry, 'oracle', semanticPath, example.id);
      }
      oracleScenarios.push({
        caseId: example.id,
        scenarioId: scenario.id,
        inputReplayPath,
        inputReplaySha256: inputReplay?.sha256 ?? null,
        canonicalStatePath,
        rgbaPath: oraclePaths.rgbaPath,
        metadataPath: oraclePaths.metadataPath,
        semanticPath
      });
    }
  }

  const excludedCount = manifest.examples.length - scannedExamples.length;
  return {
    schemaVersion: 1,
    kind: LOCK_KIND,
    upstream: {
      release: manifest.upstream?.release ?? 'r185',
      commit: manifest.upstream?.commit ?? ''
    },
    manifestSha256: sha256Bytes(manifestBytes),
    scope: {
      manifestExamples: manifest.examples.length,
      excludedExamples: excludedCount,
      scannedExamples: scannedExamples.length,
      requiredExamples: requiredExamples.length,
      requiredScenarios: oracleScenarios.length
    },
    files: finalizeRegisteredFiles(registry),
    oracleScenarios,
    externalReferences: finalizedExternalReferences,
    externalAssetMap: makeExternalAssetMapLockRecord(externalAssetMap)
  };
}

/** Serializes a lock deterministically as human-readable UTF-8 JSON. */
export function serializeThreeInputLock(lock) {
  return `${JSON.stringify(lock, null, 2)}\n`;
}

/** Writes one generated input lock after creating its explicit parent directory. */
export async function writeThreeInputLock(outputPath, lock) {
  await fs.mkdir(path.dirname(path.resolve(outputPath)), { recursive: true });
  await fs.writeFile(path.resolve(outputPath), serializeThreeInputLock(lock), 'utf8');
}

/** Returns a concise difference list between a committed lock and a fresh deterministic scan. */
function compareLockContents(locked, current) {
  const failures = [];
  if (locked.kind !== LOCK_KIND || locked.schemaVersion !== 1) {
    failures.push(`Input lock must use ${LOCK_KIND} schemaVersion 1.`);
    return failures;
  }
  if (locked.manifestSha256 !== current.manifestSha256) {
    failures.push(`Manifest hash drift: lock=${locked.manifestSha256 ?? '<missing>'}, current=${current.manifestSha256}.`);
  }
  if (JSON.stringify(locked.upstream ?? {}) !== JSON.stringify(current.upstream)) {
    failures.push('Locked Three upstream release or commit differs from the manifest.');
  }
  if (JSON.stringify(locked.scope ?? {}) !== JSON.stringify(current.scope)) {
    failures.push('Input lock scope accounting differs from the manifest scan.');
  }
  if (!Array.isArray(locked.files)) {
    failures.push('Input lock files must be an array.');
    return failures;
  }
  const lockedFiles = new Map((locked.files ?? []).map((entry) => [`${entry.root}:${entry.relativePath}`, entry]));
  const currentFiles = new Map(current.files.map((entry) => [`${entry.root}:${entry.relativePath}`, entry]));
  if (lockedFiles.size !== locked.files.length) {
    failures.push('Input lock contains duplicate root and relativePath entries.');
  }
  for (const [key, currentEntry] of currentFiles) {
    const lockedEntry = lockedFiles.get(key);
    if (!lockedEntry) {
      failures.push(`Input lock is missing dependency '${key}'.`);
      continue;
    }
    if (lockedEntry.byteSize !== currentEntry.byteSize || lockedEntry.sha256 !== currentEntry.sha256) {
      failures.push(`Hash drift for '${key}': lock=${lockedEntry.sha256}/${lockedEntry.byteSize}, current=${currentEntry.sha256}/${currentEntry.byteSize}.`);
    }
    if (JSON.stringify(lockedEntry.caseIds) !== JSON.stringify(currentEntry.caseIds)) {
      failures.push(`Referring-case drift for '${key}'.`);
    }
  }
  for (const key of lockedFiles.keys()) {
    if (!currentFiles.has(key)) {
      failures.push(`Input lock contains stale dependency '${key}'.`);
    }
  }
  if (JSON.stringify(locked.oracleScenarios ?? []) !== JSON.stringify(current.oracleScenarios)) {
    failures.push('Required Oracle scenario coverage differs from the deterministic manifest scan.');
  }
  if (JSON.stringify(locked.externalReferences ?? []) !== JSON.stringify(current.externalReferences)) {
    failures.push('External-reference evidence differs from the deterministic source scan.');
  }
  if (JSON.stringify(locked.externalAssetMap ?? null) !== JSON.stringify(current.externalAssetMap)) {
    failures.push('External asset map bytes or mapped-file inventory differ from the input lock.');
  }
  return failures;
}

/** Performs the Three run preflight and reports every lock, hash, asset, and Oracle failure. */
export async function validateThreeInputLock(options) {
  let locked;
  let lockBytes;
  try {
    lockBytes = await fs.readFile(path.resolve(options.lockPath));
    locked = JSON.parse(lockBytes.toString('utf8'));
  } catch (error) {
    return { status: 'fail', failures: [`Missing or invalid input lock '${path.resolve(options.lockPath)}': ${error.message}`] };
  }
  let current;
  try {
    current = await generateThreeInputLock(options);
  } catch (error) {
    return { status: 'fail', failures: [error instanceof Error ? error.message : String(error)] };
  }
  const failures = compareLockContents(locked, current);
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    failures,
    lockSha256: sha256Bytes(lockBytes),
    fileCount: current.files.length,
    scannedExamples: current.scope.scannedExamples,
    requiredScenarios: current.scope.requiredScenarios
  };
}

/** Enforces the Three input lock preflight and throws one actionable aggregate error on failure. */
export async function assertThreeInputLock(options) {
  const result = await validateThreeInputLock(options);
  if (result.status !== 'pass') {
    throw new Error(`Three r185 input preflight failed:\n${result.failures.map((failure) => `- ${failure}`).join('\n')}`);
  }
  return result;
}
