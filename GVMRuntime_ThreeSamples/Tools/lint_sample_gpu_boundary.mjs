#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const SCAN_DIRECTORIES = Object.freeze([
  'GVMRuntime_ThreeSamples/Host',
  'GVMRuntime_ThreeSamples/Fixtures',
  'GVMRuntime_ThreeSamples/ThreeCompat'
]);

const CPP_EXTENSIONS = new Set(['.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.inl', '.ipp', '.mm']);

const CALL_RULES = Object.freeze([
  {
    ruleId: 'gpu.resource.direct-create',
    category: 'direct-resource-creation',
    identifiers: [
      'createBindGroup', 'createBuffer', 'createFramebuffer', 'createSampler',
      'createTexture', 'createTextureView', 'newBufferWithBytes', 'newBufferWithLength',
      'newSamplerStateWithDescriptor', 'newTextureWithDescriptor'
    ],
    explanation: 'Sample C++ must create GPU resources through generated DSL or RenderSet interfaces.'
  },
  {
    ruleId: 'gpu.pipeline.direct-create',
    category: 'direct-pipeline-creation',
    identifiers: [
      'createComputePipeline', 'createComputePipelineState', 'createGraphicsPipeline',
      'createPipeline', 'createPipelineLayout', 'createRenderPipeline',
      'createRenderPipelineState', 'makeComputePipeline', 'makeRenderPipeline',
      'newComputePipelineStateWithFunction', 'newRenderPipelineStateWithDescriptor',
      'vkCreateComputePipelines', 'vkCreateGraphicsPipelines'
    ],
    explanation: 'Sample C++ must not create graphics or compute pipelines directly.'
  },
  {
    ruleId: 'gpu.pass.direct-encode',
    category: 'direct-pass-encoding',
    identifiers: [
      'beginBlitPass', 'beginComputePass', 'beginRenderPass', 'beginRendering', 'blitCommandEncoder',
      'blitPass', 'computeCommandEncoder', 'computePass', 'createCommandEncoder', 'createComputePass',
      'createRenderPass', 'endRenderPass', 'endRendering', 'pixelLocalPass', 'renderCommandEncoderWithDescriptor',
      'renderPass', 'setIndexBuffer', 'setPipeline', 'setVertexBuffer', 'vkCmdBeginRenderPass',
      'vkCmdBeginRendering', 'vkCmdEndRenderPass', 'vkCmdEndRendering'
    ],
    explanation: 'Render and compute pass encoding belongs to the DSL-generated renderer.'
  },
  {
    ruleId: 'gpu.draw.direct-call',
    category: 'direct-draw',
    identifiers: [
      'Draw', 'DrawIndexed', 'DrawIndexedInstanced', 'DrawInstanced', 'draw',
      'drawFullscreenTexture', 'drawIndexed', 'drawIndexedIndirect', 'drawIndexedInstanced',
      'drawIndirect', 'drawInstanced', 'drawPixels', 'drawPrimitives', 'glDrawArrays', 'glDrawElements',
      'multiDrawArrays', 'multiDrawElements', 'vkCmdDraw', 'vkCmdDrawIndexed',
      'vkCmdDrawIndexedIndirect', 'vkCmdDrawIndirect'
    ],
    explanation: 'Draw submission must be expressed by RenderClass or RenderSet DSL code.'
  },
  {
    ruleId: 'gpu.dispatch.direct-call',
    category: 'direct-dispatch',
    identifiers: [
      'Dispatch', 'DispatchIndirect', 'dispatch', 'dispatchIndirect', 'dispatchThreadgroups',
      'dispatchThreads', 'dispatchWorkgroups', 'dispatchWorkgroupsIndirect', 'vkCmdDispatch',
      'vkCmdDispatchIndirect'
    ],
    explanation: 'Compute dispatch must be expressed by ComputeClass DSL code.'
  },
  {
    ruleId: 'gpu.blit-present.direct-call',
    category: 'direct-blit-or-present',
    identifiers: [
      'clearBuffer', 'copyBufferToBuffer', 'copyBufferToTexture', 'copyTextureToBuffer',
      'copyTextureToTexture', 'fillBuffer', 'present', 'presentDrawable', 'resolveTexture',
      'vkCmdBlitImage', 'vkCmdCopyImage', 'vkQueuePresentKHR'
    ],
    explanation: 'GPU blit, resolve, and present work must remain in the DSL pass graph.'
  },
  {
    ruleId: 'shader.direct-create',
    category: 'handwritten-shader',
    identifiers: [
      'D3DCompile', 'D3DCompileFromFile', 'createShader', 'createShaderModule',
      'glCompileShader', 'glCreateShader', 'glShaderSource', 'newLibraryWithData',
      'newLibraryWithSource', 'vkCreateShaderModule'
    ],
    explanation: 'Sample C++ must not create or compile shader modules outside UGLC.'
  },
  {
    ruleId: 'software-rasterizer.call',
    category: 'software-rasterization',
    identifiers: [
      'blendPixel', 'drawTriangle', 'fillPixel', 'fillTriangle', 'putPixel', 'rasterize',
      'rasterizeLine', 'rasterizePathStroke', 'rasterizeRetainedPathStroke', 'rasterizeStroke',
      'rasterizeTriangle', 'rasterizeTriangles', 'setPixel', 'shadePixel', 'softwareRasterize',
      'writePixel'
    ],
    explanation: 'Software rasterization cannot replace DSL-rendered scene geometry or generated texture content.'
  },
  {
    ruleId: 'reference-image.bypass-call',
    category: 'reference-image-bypass',
    identifiers: [
      'copyOracleToCapture', 'copyReferenceToCapture', 'displayOracleImage',
      'displayReferenceImage', 'loadGoldenImage', 'loadOracleImage', 'loadReferenceImage',
      'loadReferenceScreenshot', 'presentOracleImage', 'presentReferenceImage',
      'writeOracleScreenshot', 'writeReferenceScreenshot'
    ],
    explanation: 'A Three reference or Oracle image must never be displayed as sample output.'
  }
]);

const TYPE_RULES = Object.freeze([
  {
    ruleId: 'gpu.pipeline.direct-descriptor',
    category: 'direct-pipeline-creation',
    identifiers: [
      'ComputePipelineDescriptor', 'GraphicsPipelineDescriptor', 'MTLComputePipelineDescriptor',
      'MTLRenderPipelineDescriptor', 'RenderPipelineDescriptor', 'ShaderModuleDescriptor',
      'VkComputePipelineCreateInfo', 'VkGraphicsPipelineCreateInfo'
    ],
    explanation: 'Pipeline descriptors must not be assembled in Sample C++.'
  },
  {
    ruleId: 'gpu.pass.direct-encoder-type',
    category: 'direct-pass-encoding',
    identifiers: [
      'BlitPassDescriptor', 'BlitPassEncoder', 'BlitPassTaskDescriptor', 'CommandEncoder',
      'ComputeCommandEncoder', 'ComputePassDescriptor', 'ComputePassEncoder', 'ComputePassTaskDescriptor',
      'MTLBlitCommandEncoder', 'MTLComputeCommandEncoder', 'MTLRenderCommandEncoder',
      'PixelLocalPassTaskDescriptor', 'RenderCommandEncoder', 'RenderPassDescriptor',
      'RenderPassEncoder', 'RenderPassTaskDescriptor'
    ],
    explanation: 'Sample C++ must not own a native render or compute pass encoder.'
  },
  {
    ruleId: 'software-rasterizer.type',
    category: 'software-rasterization',
    identifiers: [
      'CpuRasterizer', 'ReferenceRasterizer', 'SoftwareRasterizer', 'SoftwareRenderer'
    ],
    explanation: 'Software renderer types are forbidden in Three sample implementation code.'
  },
  {
    ruleId: 'shader.embedded-bytecode-identifier',
    category: 'handwritten-shader',
    identifiers: [
      'DxilBytecode', 'MetallibBytes', 'ShaderBytecode', 'SpirvBytecode', 'SpirvWords'
    ],
    explanation: 'Embedded shader bytecode must come from generated UGLC artifacts, not Sample C++.'
  },
  {
    ruleId: 'reference-image.bypass-identifier',
    category: 'reference-image-bypass',
    identifiers: [
      'baselinePixels', 'goldenImageBytes', 'goldenPixels', 'oracleImageBytes', 'oraclePixels',
      'referenceImageBytes', 'referencePixels', 'referenceScreenshot', 'threeReferenceImage',
      'threeReferencePixels'
    ],
    explanation: 'Reference-image payloads are forbidden in Sample C++.'
  }
]);

const SHADER_PATH_PATTERN = /(?:^|[\\/])[^\s"']+\.(?:comp|frag|fx|glsl|hlsl|metal|msl|spv|spvasm|spirv|vert|wgsl)(?:$|[?#\s])/iu;
const REFERENCE_IMAGE_PATH_PATTERN = /(?:golden|oracle|reference[-_ ]?(?:capture|image|screenshot))[^\n]*\.(?:avif|bmp|jpeg|jpg|png|rgba|webp)(?:$|[?#\s])/iu;
const SHADER_SOURCE_PATTERNS = Object.freeze([
  /#\s*version\s+\d+/iu,
  /\bgl_Position\b/u,
  /\blayout\s*\(\s*location\s*=/u,
  /\busing\s+namespace\s+metal\b/u,
  /\b(?:fragment|kernel|vertex)\s+(?:[A-Za-z_]\w*\s+){1,3}[A-Za-z_]\w*\s*\(/u,
  /\[\[\s*(?:fragment|kernel|vertex)\s*\]\]/u,
  /\bSV_(?:Position|Target|VertexID|InstanceID)\b/u,
  /\bcbuffer\s+[A-Za-z_]\w*/u,
  /\bOpCapability\s+Shader\b/u,
  /@(fragment|compute|vertex)\b/u
]);
const NATIVE_GPU_HEADER_PATTERN = /#\s*include\s*<\s*(?:GL\/|GLES|Metal\/Metal|d3d1[12]\.h|vulkan\/vulkan|webgpu\/)/giu;
const NATIVE_GPU_QUOTED_HEADER_PATTERN = /^(?:GL\/|GLES|Metal\/Metal|d3d1[12]\.h|vulkan\/vulkan|webgpu\/)/iu;
const SPIRV_MAGIC_PATTERN = /\b0x0?7230203\b/giu;

/** Escapes an exact identifier for use in a generated regular expression. */
function escapeRegularExpression(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/gu, '\\$&');
}

/** Builds one deterministic identifier-matching expression from a rule declaration. */
function makeIdentifierExpression(identifiers, callOnly) {
  const alternatives = [...identifiers]
    .sort((left, right) => right.length - left.length || left.localeCompare(right))
    .map(escapeRegularExpression)
    .join('|');
  return new RegExp(`\\b(?:${alternatives})\\b${callOnly ? '\\s*\\(' : ''}`, 'gu');
}

/** Replaces one source range with spaces while preserving line ending offsets. */
function maskRange(characters, source, start, end) {
  for (let index = start; index < end; index += 1) {
    if (source[index] !== '\n' && source[index] !== '\r') characters[index] = ' ';
  }
}

/** Returns whether an offset can begin a prefixed C++ string token. */
function isTokenBoundary(source, offset) {
  return offset === 0 || !/[A-Za-z0-9_]/u.test(source[offset - 1]);
}

/** Locates one raw C++ string token beginning at the supplied source offset. */
function findRawString(source, offset) {
  if (!isTokenBoundary(source, offset)) return null;
  const match = /^(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(/u.exec(source.slice(offset));
  if (!match) return null;
  const contentStart = offset + match[0].length;
  const terminator = `)${match[1]}"`;
  const terminatorOffset = source.indexOf(terminator, contentStart);
  if (terminatorOffset < 0) return { start: offset, end: source.length, contentStart, contentEnd: source.length };
  return {
    start: offset,
    end: terminatorOffset + terminator.length,
    contentStart,
    contentEnd: terminatorOffset
  };
}

/** Locates one ordinary quoted C++ string or character token. */
function findQuotedToken(source, offset) {
  if (!isTokenBoundary(source, offset)) return null;
  const match = /^(?:u8|u|U|L)?(["'])/u.exec(source.slice(offset));
  if (!match) return null;
  const quote = match[1];
  const contentStart = offset + match[0].length;
  let escaped = false;
  for (let index = contentStart; index < source.length; index += 1) {
    const current = source[index];
    if (escaped) {
      escaped = false;
    } else if (current === '\\') {
      escaped = true;
    } else if (current === quote) {
      return { start: offset, end: index + 1, contentStart, contentEnd: index, quote };
    } else if ((current === '\n' || current === '\r') && quote === "'") {
      break;
    }
  }
  return { start: offset, end: source.length, contentStart, contentEnd: source.length, quote };
}

/** Masks comments and literals while retaining every original byte offset and extracted string. */
export function tokenizeCppSource(source) {
  const characters = source.split('');
  const strings = [];
  for (let index = 0; index < source.length;) {
    if (source[index] === '/' && source[index + 1] === '/') {
      const newlineOffset = source.indexOf('\n', index + 2);
      const end = newlineOffset < 0 ? source.length : newlineOffset;
      maskRange(characters, source, index, end);
      index = end;
      continue;
    }
    if (source[index] === '/' && source[index + 1] === '*') {
      const terminatorOffset = source.indexOf('*/', index + 2);
      const end = terminatorOffset < 0 ? source.length : terminatorOffset + 2;
      maskRange(characters, source, index, end);
      index = end;
      continue;
    }
    const rawString = findRawString(source, index);
    if (rawString) {
      strings.push({
        offset: rawString.start,
        value: source.slice(rawString.contentStart, rawString.contentEnd),
        raw: true
      });
      maskRange(characters, source, rawString.start, rawString.end);
      index = rawString.end;
      continue;
    }
    const quotedToken = findQuotedToken(source, index);
    if (quotedToken) {
      if (quotedToken.quote === '"') {
        strings.push({
          offset: quotedToken.start,
          value: source.slice(quotedToken.contentStart, quotedToken.contentEnd),
          raw: false
        });
      }
      maskRange(characters, source, quotedToken.start, quotedToken.end);
      index = quotedToken.end;
      continue;
    }
    index += 1;
  }
  return { code: characters.join(''), strings };
}

/** Builds source line offsets for deterministic location and snippet reporting. */
function buildLineStarts(source) {
  const starts = [0];
  for (let index = 0; index < source.length; index += 1) {
    if (source[index] === '\n') starts.push(index + 1);
  }
  return starts;
}

/** Resolves one source offset to a one-based line and column. */
function locateOffset(lineStarts, offset) {
  let lower = 0;
  let upper = lineStarts.length;
  while (lower + 1 < upper) {
    const middle = Math.floor((lower + upper) / 2);
    if (lineStarts[middle] <= offset) lower = middle;
    else upper = middle;
  }
  return { line: lower + 1, column: offset - lineStarts[lower] + 1 };
}

/** Returns one compact source line suitable for a machine and human-readable violation. */
function makeSnippet(source, lineStarts, line) {
  const start = lineStarts[line - 1];
  const nextStart = line < lineStarts.length ? lineStarts[line] : source.length;
  const text = source.slice(start, nextStart).replace(/[\r\n]+$/u, '').trim();
  return text.length <= 240 ? text : `${text.slice(0, 237)}...`;
}

/** Appends one normalized boundary violation at an exact source offset. */
function addViolation(violations, context, rule, offset, match) {
  const location = locateOffset(context.lineStarts, offset);
  violations.push({
    ruleId: rule.ruleId,
    category: rule.category,
    file: context.relativePath,
    line: location.line,
    column: location.column,
    match,
    explanation: rule.explanation,
    snippet: makeSnippet(context.source, context.lineStarts, location.line)
  });
}

/** Applies exact call and type rules to comment- and literal-free C++ code. */
function scanCodeIdentifiers(context, violations) {
  for (const rule of CALL_RULES) {
    const expression = makeIdentifierExpression(rule.identifiers, true);
    for (const match of context.code.matchAll(expression)) {
      addViolation(violations, context, rule, match.index, match[0].trim());
    }
  }
  for (const rule of TYPE_RULES) {
    const expression = makeIdentifierExpression(rule.identifiers, false);
    for (const match of context.code.matchAll(expression)) {
      addViolation(violations, context, rule, match.index, match[0]);
    }
  }
  for (const match of context.code.matchAll(NATIVE_GPU_HEADER_PATTERN)) {
    addViolation(violations, context, {
      ruleId: 'gpu.native-header',
      category: 'native-gpu-api',
      explanation: 'Three Sample C++ must not include native GPU API headers.'
    }, match.index, match[0].trim());
  }
  for (const match of context.code.matchAll(SPIRV_MAGIC_PATTERN)) {
    addViolation(violations, context, {
      ruleId: 'shader.embedded-spirv-magic',
      category: 'handwritten-shader',
      explanation: 'SPIR-V bytecode must only appear in generated UGLC artifacts.'
    }, match.index, match[0]);
  }
}

/** Applies shader-source, shader-path, and reference-image rules to C++ string literals. */
function scanStringLiterals(context, strings, violations) {
  for (const stringToken of strings) {
    const location = locateOffset(context.lineStarts, stringToken.offset);
    const lineStart = context.lineStarts[location.line - 1];
    const beforeStringOnLine = context.source.slice(lineStart, stringToken.offset);
    if (/#\s*include\s*$/u.test(beforeStringOnLine)
      && NATIVE_GPU_QUOTED_HEADER_PATTERN.test(stringToken.value)) {
      addViolation(violations, context, {
        ruleId: 'gpu.native-header',
        category: 'native-gpu-api',
        explanation: 'Three Sample C++ must not include native GPU API headers.'
      }, stringToken.offset, stringToken.value);
    }
    if (SHADER_PATH_PATTERN.test(stringToken.value)) {
      addViolation(violations, context, {
        ruleId: 'shader.handwritten-path',
        category: 'handwritten-shader',
        explanation: 'Sample C++ must not load handwritten shader source or bytecode files.'
      }, stringToken.offset, stringToken.value);
    }
    if (REFERENCE_IMAGE_PATH_PATTERN.test(stringToken.value)) {
      addViolation(violations, context, {
        ruleId: 'reference-image.bypass-path',
        category: 'reference-image-bypass',
        explanation: 'Sample C++ must not load an Oracle or reference screenshot as render output.'
      }, stringToken.offset, stringToken.value);
    }
    if (SHADER_SOURCE_PATTERNS.some((pattern) => pattern.test(stringToken.value))) {
      addViolation(violations, context, {
        ruleId: 'shader.handwritten-source',
        category: 'handwritten-shader',
        explanation: 'Inline MSL, HLSL, SPIR-V assembly, GLSL, or WGSL is forbidden in Sample C++.'
      }, stringToken.offset, stringToken.value.slice(0, 120));
    }
  }
}

/** Scans one C++ source document and returns sorted structured violations. */
export function scanSampleCppSource(source, relativePath = '<memory>') {
  const tokenized = tokenizeCppSource(source);
  const context = {
    source,
    code: tokenized.code,
    relativePath: relativePath.replaceAll(path.sep, '/'),
    lineStarts: buildLineStarts(source)
  };
  const violations = [];
  scanCodeIdentifiers(context, violations);
  scanStringLiterals(context, tokenized.strings, violations);
  const unique = new Map();
  for (const violation of violations) {
    unique.set(`${violation.ruleId}|${violation.line}|${violation.column}|${violation.match}`, violation);
  }
  return [...unique.values()].sort(compareViolations);
}

/** Compares two violations by stable filesystem and source location order. */
function compareViolations(left, right) {
  return left.file.localeCompare(right.file)
    || left.line - right.line
    || left.column - right.column
    || left.ruleId.localeCompare(right.ruleId);
}

/** Recursively enumerates C++ source files and rejects symlink-based scan bypasses. */
async function collectCppFiles(directory, sourceRoot, output) {
  const entries = await fs.readdir(directory, { withFileTypes: true });
  entries.sort((left, right) => left.name.localeCompare(right.name));
  for (const entry of entries) {
    const entryPath = path.join(directory, entry.name);
    if (entry.isSymbolicLink()) {
      throw new Error(`Symbolic links are not allowed in scanned Three sample source roots: ${path.relative(sourceRoot, entryPath)}.`);
    }
    if (entry.isDirectory()) {
      await collectCppFiles(entryPath, sourceRoot, output);
    } else if (entry.isFile() && CPP_EXTENSIONS.has(path.extname(entry.name).toLowerCase())) {
      output.push(entryPath);
    }
  }
}

/** Builds deterministic violation totals grouped by category and rule identifier. */
function summarizeViolations(violations) {
  const categories = {};
  const rules = {};
  for (const violation of violations) {
    categories[violation.category] = (categories[violation.category] ?? 0) + 1;
    rules[violation.ruleId] = (rules[violation.ruleId] ?? 0) + 1;
  }
  return {
    categories: Object.fromEntries(Object.entries(categories).sort()),
    rules: Object.fromEntries(Object.entries(rules).sort())
  };
}

/** Scans the fixed Host, Fixtures, and ThreeCompat boundary below one explicit repository root. */
export async function lintSampleGpuBoundary(sourceRoot) {
  const absoluteRoot = path.resolve(sourceRoot);
  const files = [];
  for (const relativeDirectory of SCAN_DIRECTORIES) {
    const absoluteDirectory = path.join(absoluteRoot, ...relativeDirectory.split('/'));
    const statistics = await fs.stat(absoluteDirectory);
    if (!statistics.isDirectory()) {
      throw new Error(`Three sample scan root is not a directory: ${absoluteDirectory}.`);
    }
    await collectCppFiles(absoluteDirectory, absoluteRoot, files);
  }
  files.sort((left, right) => left.localeCompare(right));
  const violations = [];
  for (const filePath of files) {
    const relativePath = path.relative(absoluteRoot, filePath).replaceAll(path.sep, '/');
    const source = await fs.readFile(filePath, 'utf8');
    violations.push(...scanSampleCppSource(source, relativePath));
  }
  violations.sort(compareViolations);
  return {
    schemaVersion: 1,
    status: violations.length === 0 ? 'pass' : 'fail',
    sourceRoot: absoluteRoot,
    scanDirectories: [...SCAN_DIRECTORIES],
    filesScanned: files.length,
    violationCount: violations.length,
    summary: summarizeViolations(violations),
    violations
  };
}

/** Parses the standalone linter's explicit command-line contract. */
export function parseSampleGpuBoundaryArguments(argv) {
  const options = { sourceRoot: '', format: 'text', help: false };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === '--help' || argument === '-h') {
      options.help = true;
      continue;
    }
    if (argument !== '--source-root' && argument !== '--format') {
      throw new Error(`Unknown argument: ${argument}`);
    }
    const value = argv[index + 1];
    if (value === undefined || value.startsWith('--')) {
      throw new Error(`Missing value for ${argument}.`);
    }
    if (argument === '--source-root') options.sourceRoot = value;
    else options.format = value;
    index += 1;
  }
  if (!options.help && options.sourceRoot === '') {
    throw new Error('--source-root is required.');
  }
  if (!['json', 'text'].includes(options.format)) {
    throw new Error(`--format must be text or json; received '${options.format}'.`);
  }
  return options;
}

/** Prints the explicit standalone command contract without consulting environment variables. */
function printUsage(stream = process.stdout) {
  stream.write(`Usage:
  node GVMRuntime_ThreeSamples/Tools/lint_sample_gpu_boundary.mjs \\
    --source-root <repository-root> [--format text|json]
`);
}

/** Formats one structured violation as a concise compiler-style diagnostic. */
function formatViolation(violation) {
  return `ERROR [${violation.ruleId}] ${violation.file}:${violation.line}:${violation.column} ${violation.explanation} Match: ${JSON.stringify(violation.match)}`;
}

/** Executes the standalone linter and returns its conventional process exit code. */
async function main(argv) {
  let options;
  try {
    options = parseSampleGpuBoundaryArguments(argv);
  } catch (error) {
    process.stderr.write(`sample GPU boundary lint: ${error.message}\n`);
    printUsage(process.stderr);
    return 2;
  }
  if (options.help) {
    printUsage();
    return 0;
  }
  let result;
  try {
    result = await lintSampleGpuBoundary(options.sourceRoot);
  } catch (error) {
    process.stderr.write(`sample GPU boundary lint: ${error.message}\n`);
    return 2;
  }
  if (options.format === 'json') {
    process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
  } else if (result.status === 'pass') {
    process.stdout.write(`Sample GPU boundary lint passed: files=${result.filesScanned}, violations=0.\n`);
  } else {
    for (const violation of result.violations) process.stderr.write(`${formatViolation(violation)}\n`);
    process.stderr.write(`Sample GPU boundary lint failed: files=${result.filesScanned}, violations=${result.violationCount}.\n`);
  }
  return result.status === 'pass' ? 0 : 1;
}

const isCommandLineEntry = process.argv[1] !== undefined
  && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isCommandLineEntry) process.exitCode = await main(process.argv.slice(2));
