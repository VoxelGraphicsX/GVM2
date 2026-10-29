#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const GENERATED_HEADER_NAME = 'generate_result.hpp';
const DSL_HEADER_NAME = 'dsl_single_header.hpp';
const KNOWN_RENDER_SET_RESOURCE_ROLES = new Set([
  'access_bounds',
  'buffer_index_table',
  'buffer_value',
  'texture_index_table',
  'texture_value',
  'draw_info',
  'command_params'
]);

/** Escapes a literal value before it is embedded in a regular expression. */
function escapeRegularExpression(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/** Normalizes generated C++ type spelling for deterministic layout comparisons. */
function normalizeTypeSpelling(value) {
  return value.replace(/\s+/g, ' ').trim();
}

/** Adds a structured lint error when a generated-artifact contract is violated. */
function addError(errors, code, context, message) {
  errors.push({ code, context, message });
}

/** Reads a required UTF-8 artifact and records a useful error when it is absent. */
function readRequiredText(filePath, errors, context) {
  if (!fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) {
    addError(errors, 'missing_artifact', context, `Required generated artifact is missing: ${filePath}`);
    return null;
  }
  return fs.readFileSync(filePath, 'utf8');
}

/** Recursively finds files with a requested basename without following symbolic links. */
function findFilesByBasename(rootDir, basename) {
  const result = [];
  const pending = [rootDir];
  while (pending.length > 0) {
    const current = pending.pop();
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const entryPath = path.join(current, entry.name);
      if (entry.isDirectory()) {
        pending.push(entryPath);
      } else if (entry.isFile() && entry.name === basename) {
        result.push(entryPath);
      }
    }
  }
  return result.sort();
}

/** Skips one quoted C++ string or character literal while honoring escapes. */
function skipQuotedLiteral(text, startIndex, quote) {
  let index = startIndex + 1;
  while (index < text.length) {
    if (text[index] === '\\') {
      index += 2;
    } else if (text[index] === quote) {
      return index + 1;
    } else {
      index += 1;
    }
  }
  return text.length;
}

/** Skips one C++ raw string so braces in embedded shader sources do not affect parsing. */
function skipRawStringLiteral(text, startIndex) {
  const delimiterEnd = text.indexOf('(', startIndex + 2);
  if (delimiterEnd < 0 || delimiterEnd - (startIndex + 2) > 16) {
    return startIndex + 1;
  }
  const delimiter = text.slice(startIndex + 2, delimiterEnd);
  const terminator = `)${delimiter}\"`;
  const literalEnd = text.indexOf(terminator, delimiterEnd + 1);
  return literalEnd < 0 ? text.length : literalEnd + terminator.length;
}

/** Extracts a balanced C++ brace block while ignoring comments and string literals. */
function extractBraceBlock(text, openingBraceIndex) {
  if (text[openingBraceIndex] !== '{') {
    return null;
  }
  let depth = 0;
  let index = openingBraceIndex;
  while (index < text.length) {
    if (text.startsWith('//', index)) {
      const lineEnd = text.indexOf('\n', index + 2);
      index = lineEnd < 0 ? text.length : lineEnd + 1;
      continue;
    }
    if (text.startsWith('/*', index)) {
      const commentEnd = text.indexOf('*/', index + 2);
      index = commentEnd < 0 ? text.length : commentEnd + 2;
      continue;
    }
    if (text.startsWith('R\"', index)) {
      index = skipRawStringLiteral(text, index);
      continue;
    }
    if (text[index] === '\"' || text[index] === "'") {
      index = skipQuotedLiteral(text, index, text[index]);
      continue;
    }
    if (text[index] === '{') {
      depth += 1;
    } else if (text[index] === '}') {
      depth -= 1;
      if (depth === 0) {
        return text.slice(openingBraceIndex, index + 1);
      }
    }
    index += 1;
  }
  return null;
}

/** Finds all named class or struct bodies matching a generated declaration suffix. */
function findNamedTypeBlocks(text, keyword, typeName, declarationSuffix = '') {
  const expression = new RegExp(
    `\\b${keyword}\\s+${escapeRegularExpression(typeName)}\\s*${declarationSuffix}\\s*\\{`,
    'g'
  );
  const blocks = [];
  let match;
  while ((match = expression.exec(text)) !== null) {
    const openingBraceIndex = text.indexOf('{', match.index);
    const block = extractBraceBlock(text, openingBraceIndex);
    if (block !== null) {
      blocks.push(block);
      expression.lastIndex = openingBraceIndex + block.length;
    }
  }
  return blocks;
}

/** Parses every generated host RenderSet definition and its ordered component schema. */
function parseRenderSetLayouts(headerText, errors, context) {
  const layouts = new Map();
  const declaration = /\bstruct\s+([A-Za-z_]\w*)\s*:\s*public\s+GVM::Core::RenderSet\s*\{/g;
  let match;
  while ((match = declaration.exec(headerText)) !== null) {
    const typeName = match[1];
    const openingBraceIndex = headerText.indexOf('{', match.index);
    const block = extractBraceBlock(headerText, openingBraceIndex);
    if (block === null) {
      addError(errors, 'malformed_generated_cpp', context, `Unable to parse generated RenderSet ${typeName}.`);
      continue;
    }
    const vertexMatch = /createInfo\.vertexComponentName\s*=\s*\"([^\"]+)\"/.exec(block);
    const indexMatch = /createInfo\.indexComponentName\s*=\s*\"([^\"]+)\"/.exec(block);
    const components = [];
    const componentExpression = /createInfo\.componentInfos\.emplace\(\s*(\d+)\s*,\s*GVM::Core::RenderComponentCreateInfo\s*\{([^}]+)\}\s*\)/g;
    let componentMatch;
    while ((componentMatch = componentExpression.exec(block)) !== null) {
      const initializer = componentMatch[2];
      const nameMatch = /\.componentName\s*=\s*\"([^\"]+)\"/.exec(initializer);
      const storageMatch = /\.dataElementStorageSize\s*=\s*sizeof\(([^)]+)\)/.exec(initializer);
      const typeMatch = /\.type\s*=\s*GVM::Core::RenderComponentType::(BufferComponent|TextureComponent)/.exec(initializer);
      const resourceCountMatch = /\.maxResourceCount\s*=\s*(\d+)/.exec(initializer);
      if (nameMatch === null || storageMatch === null || typeMatch === null || resourceCountMatch === null) {
        addError(errors, 'malformed_generated_cpp', context, `Unable to parse generated RenderSet ${typeName} component ${componentMatch[1]}.`);
        continue;
      }
      components.push({
        index: Number.parseInt(componentMatch[1], 10),
        name: nameMatch[1],
        elementType: normalizeTypeSpelling(storageMatch[1]),
        componentType: typeMatch[1],
        maxResourceCount: Number.parseInt(resourceCountMatch[1], 10)
      });
    }
    components.sort((left, right) => left.index - right.index);
    layouts.set(typeName, {
      typeName,
      vertexComponentName: vertexMatch?.[1] ?? null,
      indexComponentName: indexMatch?.[1] ?? null,
      components
    });
    declaration.lastIndex = openingBraceIndex + block.length;
  }
  return layouts;
}

/** Parses the unique RenderSet constructor binding for one generated scene pass. */
function parseScenePassBinding(headerText, passName, renderSetLayouts, errors, context) {
  const blocks = findNamedTypeBlocks(
    headerText,
    'class',
    passName,
    ':\\s*public\\s+GVM::Core::IRenderClass(?:\\s*,[^\\{]+)?'
  );
  if (blocks.length !== 1) {
    addError(errors, 'scene_pass_definition_count', context, `Expected one generated IRenderClass named ${passName}, found ${blocks.length}.`);
    return null;
  }
  const block = blocks[0];
  const createMatch = /\bvoid\s+create\s*\(([^)]*)\)/s.exec(block);
  if (createMatch === null) {
    addError(errors, 'scene_pass_missing_create', context, `Scene pass ${passName} has no generated create method.`);
    return null;
  }
  const renderSetBindings = [];
  const parameterExpression = /eastl::intrusive_ptr<([A-Za-z_]\w*)>\s+([A-Za-z_]\w*)\s*\[\[\s*Slot(\d+)\s*\]\]/g;
  let parameterMatch;
  while ((parameterMatch = parameterExpression.exec(createMatch[1])) !== null) {
    if (renderSetLayouts.has(parameterMatch[1])) {
      renderSetBindings.push({
        typeName: parameterMatch[1],
        variableName: parameterMatch[2],
        slot: Number.parseInt(parameterMatch[3], 10)
      });
    }
  }
  if (renderSetBindings.length !== 1) {
    addError(errors, 'render_set_binding_count', context, `Scene pass ${passName} must bind exactly one generated RenderSet, found ${renderSetBindings.length}.`);
    return null;
  }
  const binding = renderSetBindings[0];
  const assignment = new RegExp(`this->mRenderSet\\s*=\\s*${escapeRegularExpression(binding.variableName)}\\s*;`);
  const slotAssignment = new RegExp(`this->mRenderSetBindGroupIndex\\s*=\\s*${binding.slot}\\s*;`);
  if (!assignment.test(block) || !slotAssignment.test(block)) {
    addError(errors, 'render_set_base_binding_missing', context, `Scene pass ${passName} does not lower its Slot${binding.slot} RenderSet into IRenderClass RenderSet state.`);
  }
  return { ...binding, block };
}

/** Verifies that generated scene-pass call sites use only the no-argument RenderSet draw entry. */
function lintScenePassCalls(headerText, passName, errors, context) {
  const variableExpression = new RegExp(`eastl::intrusive_ptr<${escapeRegularExpression(passName)}>\\s+([A-Za-z_]\\w*)\\s*;`, 'g');
  const variables = new Set();
  let variableMatch;
  while ((variableMatch = variableExpression.exec(headerText)) !== null) {
    variables.add(variableMatch[1]);
  }
  let parameterlessRunCount = 0;
  for (const variableName of variables) {
    const escapedVariable = escapeRegularExpression(variableName);
    const runExpression = new RegExp(`\\b${escapedVariable}\\s*->\\s*run\\s*\\(([^)]*)\\)`, 'g');
    let runMatch;
    while ((runMatch = runExpression.exec(headerText)) !== null) {
      if (runMatch[1].trim() === '') {
        parameterlessRunCount += 1;
      } else {
        addError(errors, 'explicit_scene_draw_count', context, `Scene pass ${passName} calls run(...) with explicit draw arguments.`);
      }
    }
    for (const forbiddenMethod of ['setVertexBuffer', 'setIndexBuffer', 'drawIndirect']) {
      const forbiddenExpression = new RegExp(`\\b${escapedVariable}\\s*->\\s*${forbiddenMethod}\\s*\\(`);
      if (forbiddenExpression.test(headerText)) {
        addError(errors, 'standalone_scene_geometry', context, `Scene pass ${passName} calls forbidden ${forbiddenMethod}(...).`);
      }
    }
  }
  if (parameterlessRunCount === 0) {
    addError(errors, 'render_set_indirect_entry_missing', context, `Scene pass ${passName} never calls the no-argument IRenderClass::run() RenderSet entry.`);
  }
}

/** Verifies that a generated scene pass embeds non-empty SPIR-V for Vulkan runtime creation. */
function lintEmbeddedSpirv(headerText, passName, errors, context) {
  const blocks = findNamedTypeBlocks(
    headerText,
    'class',
    passName,
    ':\\s*public\\s+GVM::Core::IRenderClass(?:\\s*,[^\\{]+)?'
  );
  if (blocks.length !== 1) {
    return;
  }
  for (const stage of ['vertex', 'fragment']) {
    const arrayName = `${stage}ShaderArtifact_SpirvWords`;
    const arrayExpression = new RegExp(`static\\s+constexpr\\s+uint32_t\\s+${arrayName}\\s*\\[\\]\\s*=\\s*\\{\\s*0x[0-9A-Fa-f]+`);
    const artifactExpression = new RegExp(`${arrayName}\\s*,\\s*(?:sizeof\\s*\\([^)]*\\)|[1-9][0-9]*)`, 's');
    if (!arrayExpression.test(blocks[0]) || !artifactExpression.test(blocks[0])) {
      addError(
        errors,
        'embedded_spirv_missing',
        context,
        `Scene pass ${passName} has no non-empty embedded ${stage} SPIR-V; its Vulkan quadrant cannot run.`
      );
    }
  }
}

/** Verifies entity builtin declarations in the merged DSL artifact for one scene pass. */
function lintDslEntityBuiltins(dslText, passName, requiresInstancing, errors, context) {
  const blocks = findNamedTypeBlocks(dslText, 'class', passName, '[^\\{]*public\\s+IRenderClass[^\\{]*');
  if (blocks.length !== 1) {
    addError(errors, 'dsl_scene_pass_definition_count', context, `Expected one merged DSL IRenderClass named ${passName}, found ${blocks.length}.`);
    return;
  }
  if (!/\[\[\s*RenderEntityID\s*\]\]/.test(blocks[0])) {
    addError(errors, 'render_entity_id_missing', context, `Scene pass ${passName} does not declare RenderEntityID in generated merged DSL.`);
  }
  if (requiresInstancing && !/\[\[\s*RenderEntityInstanceID\s*\]\]/.test(blocks[0])) {
    addError(errors, 'render_entity_instance_id_missing', context, `Instanced scene pass ${passName} does not declare RenderEntityInstanceID in generated merged DSL.`);
  }
}

/** Returns a canonical JSON-compatible RenderSet layout used for pipeline equality. */
function canonicalRenderSetLayout(layout) {
  return {
    typeName: layout.typeName,
    vertexComponentName: layout.vertexComponentName,
    indexComponentName: layout.indexComponentName,
    components: layout.components.map((component) => ({ ...component }))
  };
}

/** Builds the complete argument-buffer ABI expected from a generated RenderSet layout. */
function createExpectedRenderSetResources(layout, bindGroupIndex) {
  const resources = [{
    binding: 0,
    bindGroupIndex,
    role: 'access_bounds',
    componentName: null,
    names: ['AccessBounds', 'RenderSetAccessBoundData'],
    arrayCount: 1,
    resourceIndex: 0
  }];
  let binding = 1;
  for (const component of layout.components) {
    const isTexture = component.componentType === 'TextureComponent';
    resources.push({
      binding,
      bindGroupIndex,
      role: isTexture ? 'texture_index_table' : 'buffer_index_table',
      componentName: component.name,
      names: [`${component.name}IndexTable`, `${component.name}ComponentList`],
      arrayCount: 1,
      resourceIndex: component.index + 1
    });
    binding += 1;
    resources.push({
      binding,
      bindGroupIndex,
      role: isTexture ? 'texture_value' : 'buffer_value',
      componentName: component.name,
      names: [component.name],
      arrayCount: isTexture ? component.maxResourceCount : 1,
      resourceIndex: component.index + 1
    });
    binding += 1;
  }
  resources.push({
    binding,
    bindGroupIndex,
    role: 'draw_info',
    componentName: null,
    names: ['DrawInfo', 'RenderEntityInfo'],
    arrayCount: 1,
    resourceIndex: 0
  });
  binding += 1;
  resources.push({
    binding,
    bindGroupIndex,
    role: 'command_params',
    componentName: null,
    names: ['CommandParams', 'RenderEntityCMDParams'],
    arrayCount: 1,
    resourceIndex: 0
  });
  return resources;
}

/** Parses argument-buffer field names and binding ids from one generated MSL struct. */
function parseMslArgumentBufferFields(structBlock) {
  const fields = new Map();
  const expression = /([A-Za-z_]\w*)\s*\[\[\s*id\s*\(\s*(\d+)\s*\)\s*\]\]\s*;/g;
  let match;
  while ((match = expression.exec(structBlock)) !== null) {
    fields.set(Number.parseInt(match[2], 10), match[1]);
  }
  return fields;
}

/** Verifies the fixed eight-word draw metadata record in generated MSL. */
function lintMslDrawInfoLayout(mslText, errors, context) {
  const expectedFields = [
    'indexCount',
    'instanceCount',
    'firstIndex',
    'vertexOffset',
    'globalInstanceBase',
    'vertexCount',
    'entityVersion',
    'cmdParamsOffset'
  ];
  const blocks = [
    ...findNamedTypeBlocks(mslText, 'struct', 'UGL_DrawInfo_'),
    ...findNamedTypeBlocks(mslText, 'struct', 'UGL_RenderEntityInfo_')
  ];
  if (blocks.length === 0) {
    addError(errors, 'msl_draw_info_type_missing', context, 'MSL artifact has no RenderSet draw-info record.');
    return;
  }
  for (const block of blocks) {
    const fieldExpression = /\b(?:u?int)\s+([A-Za-z_]\w*)\s*;/g;
    const actualFields = [];
    let match;
    while ((match = fieldExpression.exec(block)) !== null) {
      actualFields.push(match[1]);
    }
    if (JSON.stringify(actualFields) !== JSON.stringify(expectedFields)) {
      addError(errors, 'msl_draw_info_layout_mismatch', context, `MSL draw-info fields are ${actualFields.join(', ')}; expected ${expectedFields.join(', ')}.`);
    }
  }
}

/** Checks one MSL RenderSet argument-buffer struct against the expected binding ABI. */
function lintMslArgumentBuffer(mslText, setTypeName, expectedResources, errors, context) {
  const blocks = findNamedTypeBlocks(mslText, 'struct', setTypeName);
  const fieldMaps = blocks
    .map((block) => ({ block, fields: parseMslArgumentBufferFields(block) }))
    .filter(({ fields }) => fields.size > 0);
  if (fieldMaps.length === 0) {
    addError(errors, 'msl_render_set_struct_missing', context, `MSL artifact has no argument-buffer struct ${setTypeName}.`);
    return;
  }
  for (const { block, fields } of fieldMaps) {
    if (fields.size !== expectedResources.length) {
      addError(errors, 'msl_binding_count_mismatch', context, `MSL ${setTypeName} exposes ${fields.size} bindings; expected ${expectedResources.length}.`);
    }
    for (const expected of expectedResources) {
      const actualName = fields.get(expected.binding);
      if (actualName === undefined || !expected.names.includes(actualName)) {
        addError(errors, 'msl_binding_mismatch', context, `MSL binding ${expected.binding} must be ${expected.names.join(' or ')}, found ${actualName ?? 'missing'}.`);
      }
      if (expected.role === 'texture_value' && actualName !== undefined) {
        const pointerExpression = new RegExp(`\\*\\s*${escapeRegularExpression(actualName)}\\s*\\[\\[\\s*id\\s*\\(\\s*${expected.binding}\\s*\\)`);
        if (!pointerExpression.test(block)) {
          addError(errors, 'msl_texture_pool_not_pointer', context, `MSL texture pool ${actualName} is not emitted as an argument-buffer descriptor pointer.`);
        }
      }
    }
  }
  lintMslDrawInfoLayout(mslText, errors, context);
}

/** Reads and validates UGLIR reflection resources for one render stage. */
function lintUglirReflection(jsonText, expectedResources, errors, context) {
  let document;
  try {
    document = JSON.parse(jsonText);
  } catch (error) {
    addError(errors, 'invalid_uglir_json', context, `UGLIR reflection is not valid JSON: ${error.message}`);
    return [];
  }
  const resources = Array.isArray(document?.reflection?.resources)
    ? document.reflection.resources.filter((resource) => KNOWN_RENDER_SET_RESOURCE_ROLES.has(resource.resourceRole))
    : [];
  if (resources.length !== expectedResources.length) {
    addError(errors, 'uglir_resource_count_mismatch', context, `UGLIR exposes ${resources.length} RenderSet resources; expected ${expectedResources.length}.`);
  }
  const resourcesByBinding = new Map(resources.map((resource) => [resource.bindingIndex, resource]));
  for (const expected of expectedResources) {
    const actual = resourcesByBinding.get(expected.binding);
    const actualSuffix = actual?.name?.split('.').at(-1);
    if (actual === undefined) {
      addError(errors, 'uglir_resource_missing', context, `UGLIR is missing RenderSet binding ${expected.binding} (${expected.role}).`);
      continue;
    }
    if (actual.bindGroupIndex !== expected.bindGroupIndex
        || actual.resourceRole !== expected.role
        || !expected.names.includes(actualSuffix)) {
      addError(errors, 'uglir_resource_mismatch', context, `UGLIR binding ${expected.binding} does not match the ${expected.role} ABI.`);
    }
    if (actual.arrayCount !== expected.arrayCount || actual.resourceIndex !== expected.resourceIndex) {
      addError(errors, 'uglir_resource_shape_mismatch', context, `UGLIR binding ${expected.binding} has array_count=${actual.arrayCount}, resource_index=${actual.resourceIndex}; expected ${expected.arrayCount}, ${expected.resourceIndex}.`);
    }
  }
  return resources;
}

/** Parses SPIR-V names, descriptor bindings, variables, and pointer/array types. */
function parseSpirvAssembly(assemblyText) {
  const names = new Map();
  const descriptorSets = new Map();
  const bindings = new Map();
  const variableTypes = new Map();
  const pointerElements = new Map();
  const runtimeArrays = new Set();
  for (const line of assemblyText.split(/\r?\n/)) {
    let match = /^\s*OpName\s+(%\S+)\s+\"([^\"]+)\"/.exec(line);
    if (match !== null) {
      names.set(match[1], match[2]);
      continue;
    }
    match = /^\s*OpDecorate\s+(%\S+)\s+DescriptorSet\s+(\d+)/.exec(line);
    if (match !== null) {
      descriptorSets.set(match[1], Number.parseInt(match[2], 10));
      continue;
    }
    match = /^\s*OpDecorate\s+(%\S+)\s+Binding\s+(\d+)/.exec(line);
    if (match !== null) {
      bindings.set(match[1], Number.parseInt(match[2], 10));
      continue;
    }
    match = /^\s*(%\S+)\s*=\s*OpVariable\s+(%\S+)/.exec(line);
    if (match !== null) {
      variableTypes.set(match[1], match[2]);
      continue;
    }
    match = /^\s*(%\S+)\s*=\s*OpTypePointer\s+\S+\s+(%\S+)/.exec(line);
    if (match !== null) {
      pointerElements.set(match[1], match[2]);
      continue;
    }
    match = /^\s*(%\S+)\s*=\s*OpTypeRuntimeArray\b/.exec(line);
    if (match !== null) {
      runtimeArrays.add(match[1]);
    }
  }
  return { names, descriptorSets, bindings, variableTypes, pointerElements, runtimeArrays };
}

/** Finds a SPIR-V variable whose debug name matches one reflected UGLIR resource. */
function findSpirvResourceId(parsed, reflectedName) {
  const expectedName = reflectedName.replace('.', '_');
  for (const [identifier, name] of parsed.names) {
    if (name === expectedName) {
      return identifier;
    }
  }
  return null;
}

/** Verifies that one SPIR-V variable is backed by a runtime descriptor array. */
function isRuntimeArrayResource(parsed, variableId) {
  const pointerType = parsed.variableTypes.get(variableId);
  const elementType = pointerType === undefined ? undefined : parsed.pointerElements.get(pointerType);
  return elementType !== undefined && parsed.runtimeArrays.has(elementType);
}

/** Verifies direct SPIR-V RenderSet descriptors and draw-metadata structure layout. */
function lintRawSpirv(rawAssembly, reflectedResources, expectedResources, errors, context) {
  const parsed = parseSpirvAssembly(rawAssembly);
  const reflectionByBinding = new Map(reflectedResources.map((resource) => [resource.bindingIndex, resource]));
  for (const expected of expectedResources) {
    const reflected = reflectionByBinding.get(expected.binding);
    if (reflected === undefined) {
      continue;
    }
    const variableId = findSpirvResourceId(parsed, reflected.name);
    if (variableId === null
        || parsed.descriptorSets.get(variableId) !== expected.bindGroupIndex
        || parsed.bindings.get(variableId) !== expected.binding) {
      addError(errors, 'spirv_binding_mismatch', context, `Direct SPIR-V does not expose ${reflected.name} at set ${expected.bindGroupIndex}, binding ${expected.binding}.`);
      continue;
    }
    if (expected.role === 'texture_value' && !isRuntimeArrayResource(parsed, variableId)) {
      addError(errors, 'spirv_texture_pool_not_runtime_array', context, `Direct SPIR-V texture pool ${reflected.name} is not a runtime descriptor array.`);
    }
  }
  const drawInfoName = [...parsed.names].find(([, name]) => name === 'UGL_DrawInfo_')?.[0];
  if (drawInfoName === undefined) {
    addError(errors, 'spirv_draw_info_type_missing', context, 'Direct SPIR-V has no UGL_DrawInfo_ metadata type.');
    return;
  }
  for (let memberIndex = 0; memberIndex < 8; memberIndex += 1) {
    const offsetExpression = new RegExp(`OpMemberDecorate\\s+${escapeRegularExpression(drawInfoName)}\\s+${memberIndex}\\s+Offset\\s+${memberIndex * 4}\\b`);
    if (!offsetExpression.test(rawAssembly)) {
      addError(errors, 'spirv_draw_info_layout_mismatch', context, `UGL_DrawInfo_ member ${memberIndex} must use byte offset ${memberIndex * 4}.`);
    }
  }
  const drawArrayExpression = new RegExp(`^\\s*(%\\S+)\\s*=\\s*OpTypeRuntimeArray\\s+${escapeRegularExpression(drawInfoName)}\\s*$`, 'm');
  const drawArrayMatch = drawArrayExpression.exec(rawAssembly);
  const strideExpression = drawArrayMatch === null
    ? null
    : new RegExp(`OpDecorate\\s+${escapeRegularExpression(drawArrayMatch[1])}\\s+ArrayStride\\s+32\\b`);
  if (strideExpression === null || !strideExpression.test(rawAssembly)) {
    addError(errors, 'spirv_draw_info_stride_missing', context, 'Direct SPIR-V does not declare the 32-byte draw-info array stride.');
  }
}

/** Verifies surviving optimized SPIR-V resources retain their reflected binding numbers. */
function lintOptimizedSpirv(assemblyText, reflectedResources, expectedResources, requireEntityBuiltins, errors, context) {
  const parsed = parseSpirvAssembly(assemblyText);
  const reflectionByName = new Map(reflectedResources.map((resource) => [resource.name.replace('.', '_'), resource]));
  for (const [identifier, name] of parsed.names) {
    const reflected = reflectionByName.get(name);
    if (reflected === undefined || !parsed.bindings.has(identifier)) {
      continue;
    }
    if (parsed.descriptorSets.get(identifier) !== reflected.bindGroupIndex
        || parsed.bindings.get(identifier) !== reflected.bindingIndex) {
      addError(errors, 'optimized_spirv_binding_mismatch', context, `Optimized SPIR-V moved surviving resource ${name}.`);
    }
  }
  if (requireEntityBuiltins) {
    const accessBounds = expectedResources.find((resource) => resource.role === 'access_bounds');
    const commandParams = expectedResources.find((resource) => resource.role === 'command_params');
    for (const required of [accessBounds, commandParams]) {
      const reflected = reflectedResources.find((resource) => resource.bindingIndex === required.binding);
      const variableId = reflected === undefined ? null : findSpirvResourceId(parsed, reflected.name);
      if (variableId === null || parsed.bindings.get(variableId) !== required.binding) {
        addError(errors, 'optimized_spirv_entity_resource_missing', context, `Optimized vertex SPIR-V is missing ${required.role}.`);
      }
    }
    if (!/OpDecorate\s+%\S+\s+BuiltIn\s+InstanceIndex\b/.test(assemblyText)) {
      addError(errors, 'spirv_instance_index_missing', context, 'Optimized vertex SPIR-V does not consume BuiltIn InstanceIndex.');
    }
  }
}

/** Verifies Metal entity decode builtins and per-instance decode when required. */
function lintMslEntityDecode(mslText, requiresInstancing, errors, context) {
  if (!/\[\[\s*instance_id\s*\]\]/.test(mslText)) {
    addError(errors, 'msl_instance_id_missing', context, 'MSL vertex artifact does not consume instance_id for RenderSet command decoding.');
  }
  // Legacy and Experimental emitters use the same ABI but may choose either
  // the DSL builtin spelling (renderEntityID) or the lowered local spelling
  // (entityID).  Both are valid when the assignment is sourced from
  // CommandParams.x; lint the semantic decode rather than one identifier.
  if (!/(?:renderEntityID|entityID)\s*=\s*[^;]*\.x\s*;/.test(mslText)) {
    addError(errors, 'msl_render_entity_decode_missing', context, 'MSL vertex artifact does not decode RenderEntityID from CommandParams.x.');
  }
  if (requiresInstancing
      && !/(?:renderEntityInstanceID|instanceID)\s*=\s*[^;]*\.y\s*;/.test(mslText)) {
    addError(errors, 'msl_render_entity_instance_decode_missing', context, 'Instanced MSL vertex artifact does not decode RenderEntityInstanceID from CommandParams.y.');
  }
}

/** Creates a searchable index of generated headers below one pipeline root. */
function indexGeneratedRoot(rootDir, errors, pipelineName) {
  if (!fs.existsSync(rootDir) || !fs.statSync(rootDir).isDirectory()) {
    addError(errors, 'missing_generated_root', pipelineName, `Generated root does not exist: ${rootDir}`);
    return [];
  }
  return findFilesByBasename(rootDir, GENERATED_HEADER_NAME).map((headerPath) => ({
    headerPath,
    artifactDir: path.dirname(headerPath),
    text: fs.readFileSync(headerPath, 'utf8')
  }));
}

/** Finds the one generated header that defines a requested scene pass. */
function findPassArtifact(index, passName, errors, context) {
  const declaration = new RegExp(`\\bclass\\s+${escapeRegularExpression(passName)}\\s*:\\s*public\\s+GVM::Core::IRenderClass\\b`);
  const matches = index.filter((artifact) => declaration.test(artifact.text));
  if (matches.length !== 1) {
    addError(errors, 'scene_pass_artifact_count', context, `Expected one generated header for ${passName}, found ${matches.length}.`);
    return null;
  }
  return matches[0];
}

/** Loads pass specifications from audited complex examples in the pinned manifest. */
function loadManifestPasses(manifestPath, requestedCases, errors) {
  if (manifestPath === null) {
    return [];
  }
  const text = readRequiredText(manifestPath, errors, 'manifest');
  if (text === null) {
    return [];
  }
  let manifest;
  try {
    manifest = JSON.parse(text);
  } catch (error) {
    addError(errors, 'invalid_manifest_json', 'manifest', `Manifest is not valid JSON: ${error.message}`);
    return [];
  }
  const examples = Array.isArray(manifest.examples) ? manifest.examples : [];
  const requested = new Set(requestedCases);
  if (requested.size > 0) {
    for (const caseId of requested) {
      if (!examples.some((example) => example.id === caseId)) {
        addError(errors, 'unknown_manifest_case', 'manifest', `Requested case is absent from manifest: ${caseId}`);
      }
    }
  }
  const selectedExamples = examples.filter((example) => {
    if (requested.size > 0 && !requested.has(example.id)) {
      return false;
    }
    return example.status === 'phase1_required' && example.renderSetPolicy === 'required';
  });
  const passes = [];
  for (const example of selectedExamples) {
    if (!Array.isArray(example.scenePasses) || example.scenePasses.length === 0) {
      addError(errors, 'manifest_scene_pass_missing', example.id, 'Required complex example declares no scene passes.');
      continue;
    }
    const sceneRootTypes = new Map((example.sceneRoots ?? []).map((root) => [root.name, root.renderSetType]));
    for (const scenePass of example.scenePasses) {
      // A required example may contain ordinary single-object roots alongside
      // complex roots.  Ordinary roots are validated by the runtime scene
      // contract; this generated-artifact linter only inspects scene passes
      // that are required to bind the scene's unique RenderSet.
      if (sceneRootTypes.get(scenePass.sceneRoot) === null) {
        continue;
      }
      passes.push({
        name: scenePass.renderClass ?? scenePass.name,
        logicalName: scenePass.name,
        caseId: example.id,
        expectedSetType: sceneRootTypes.get(scenePass.sceneRoot) ?? example.renderSetType ?? null,
        expectedComponentSchema: Array.isArray(example.componentSchema)
          ? example.componentSchema.map((component) => ({ ...component }))
          : [],
        requiresInstancing: example.containsInstancing === true
      });
    }
  }
  return passes;
}

/** Parses one fixture selector with optional :instancing suffix. */
function parseFixtureSelector(selector) {
  const instancingSuffix = ':instancing';
  const screenSuffix = ':screen';
  if (selector.endsWith(instancingSuffix)) {
    return {
      name: selector.slice(0, -instancingSuffix.length),
      requiresInstancing: true,
      screenOnly: false
    };
  }
  if (selector.endsWith(screenSuffix)) {
    return {
      name: selector.slice(0, -screenSuffix.length),
      requiresInstancing: false,
      screenOnly: true
    };
  }
  return { name: selector, requiresInstancing: false, screenOnly: false };
}

/** Merges duplicate pass specifications while preserving the strictest requirements. */
function mergePassSpecifications(passes, forcedInstancingPasses, errors) {
  const merged = new Map();
  for (const pass of passes) {
    if (!/^[A-Za-z_]\w*$/.test(pass.name)) {
      addError(errors, 'invalid_scene_pass_name', pass.caseId ?? 'fixture', `Invalid generated C++ pass name: ${pass.name}`);
      continue;
    }
    const existing = merged.get(pass.name);
    if (existing !== undefined
        && existing.expectedSetType !== null
        && pass.expectedSetType !== null
        && existing.expectedSetType !== pass.expectedSetType) {
      addError(errors, 'conflicting_render_set_type', pass.name, `Pass is associated with both ${existing.expectedSetType} and ${pass.expectedSetType}.`);
      continue;
    }
    if (existing !== undefined
        && existing.expectedComponentSchema.length > 0
        && (pass.expectedComponentSchema?.length ?? 0) > 0
        && JSON.stringify(existing.expectedComponentSchema) !== JSON.stringify(pass.expectedComponentSchema)) {
      addError(errors, 'conflicting_manifest_component_schema', pass.name, 'Pass is associated with incompatible manifest component schemas.');
      continue;
    }
    if (existing !== undefined && existing.screenOnly !== Boolean(pass.screenOnly)) {
      addError(errors, 'conflicting_pass_kind', pass.name, 'Pass is selected as both a RenderSet scene pass and a screen-only pass.');
      continue;
    }
    merged.set(pass.name, {
      name: pass.name,
      logicalName: existing?.logicalName ?? pass.logicalName ?? pass.name,
      caseId: existing?.caseId ?? pass.caseId,
      expectedSetType: existing?.expectedSetType ?? pass.expectedSetType ?? null,
      expectedComponentSchema: existing?.expectedComponentSchema?.length > 0
        ? existing.expectedComponentSchema
        : pass.expectedComponentSchema ?? [],
      requiresInstancing: Boolean(existing?.requiresInstancing || pass.requiresInstancing || forcedInstancingPasses.has(pass.name)),
      screenOnly: Boolean(existing?.screenOnly || pass.screenOnly)
    });
  }
  for (const forcedPassName of forcedInstancingPasses) {
    if (!merged.has(forcedPassName)) {
      addError(errors, 'unknown_instancing_pass', forcedPassName, '--instancing-pass must name a selected manifest or fixture pass.');
    }
  }
  return [...merged.values()].sort((left, right) => left.name.localeCompare(right.name));
}

/** Extracts one generated RenderClass create signature for pipeline ABI comparison. */
function extractRenderClassCreateSignature(headerText, passName, errors, context) {
  const blocks = findNamedTypeBlocks(
    headerText,
    'class',
    passName,
    ':\\s*public\\s+GVM::Core::IRenderClass(?:\\s*,[^\\{]+)?'
  );
  if (blocks.length !== 1) {
    addError(errors, 'screen_pass_definition_count', context, `Expected one generated IRenderClass named ${passName}, found ${blocks.length}.`);
    return null;
  }
  const createMatch = /\bvoid\s+create\s*\(([^)]*)\)/s.exec(blocks[0]);
  if (createMatch === null) {
    addError(errors, 'screen_pass_missing_create', context, `Screen pass ${passName} has no generated create method.`);
    return null;
  }
  return normalizeTypeSpelling(createMatch[1]);
}

/** Verifies that one generated screen pass is invoked with an explicit fullscreen draw. */
function lintScreenPassCalls(headerText, passName, errors, context) {
  const variableExpression = new RegExp(`eastl::intrusive_ptr<${escapeRegularExpression(passName)}>\\s+([A-Za-z_]\\w*)\\s*;`, 'g');
  const variables = new Set();
  let variableMatch;
  while ((variableMatch = variableExpression.exec(headerText)) !== null) {
    variables.add(variableMatch[1]);
  }
  let explicitRunCount = 0;
  for (const variableName of variables) {
    const runExpression = new RegExp(`\\b${escapeRegularExpression(variableName)}\\s*->\\s*run\\s*\\(([^)]*)\\)`, 'g');
    let runMatch;
    while ((runMatch = runExpression.exec(headerText)) !== null) {
      if (runMatch[1].trim() !== '') {
        explicitRunCount += 1;
      }
    }
  }
  if (explicitRunCount === 0) {
    addError(errors, 'screen_draw_entry_missing', context, `Screen pass ${passName} never calls an explicit IRenderClass::run(...) draw entry.`);
  }
}

/** Lints one screen-only pass across Legacy and Experimental generated products. */
function lintScreenPass(pass, legacyArtifact, experimentalArtifact, errors) {
  const context = pass.name;
  const legacySignature = extractRenderClassCreateSignature(
    legacyArtifact.text,
    pass.name,
    errors,
    `${context}:legacy`
  );
  const experimentalSignature = extractRenderClassCreateSignature(
    experimentalArtifact.text,
    pass.name,
    errors,
    `${context}:experimental`
  );
  if (legacySignature !== null
      && experimentalSignature !== null
      && legacySignature !== experimentalSignature) {
    addError(
      errors,
      'pipeline_screen_create_abi_mismatch',
      context,
      `Legacy create ABI '${legacySignature}' differs from Experimental '${experimentalSignature}'.`
    );
  }
  lintScreenPassCalls(legacyArtifact.text, pass.name, errors, `${context}:legacy`);
  lintScreenPassCalls(experimentalArtifact.text, pass.name, errors, `${context}:experimental`);
  lintEmbeddedSpirv(legacyArtifact.text, pass.name, errors, `${context}:legacy`);
  lintEmbeddedSpirv(experimentalArtifact.text, pass.name, errors, `${context}:experimental`);
  for (const artifact of [
    [legacyArtifact, 'legacy'],
    [experimentalArtifact, 'experimental']
  ]) {
    const mergedDsl = readRequiredText(
      path.join(artifact[0].artifactDir, DSL_HEADER_NAME),
      errors,
      `${context}:${artifact[1]}`
    );
    if (mergedDsl !== null) {
      const blocks = findNamedTypeBlocks(
        mergedDsl,
        'class',
        pass.name,
        '[^\\{]*public\\s+IRenderClass[^\\{]*'
      );
      if (blocks.length !== 1) {
        addError(errors, 'dsl_screen_pass_definition_count', `${context}:${artifact[1]}`, `Expected one merged DSL screen pass named ${pass.name}, found ${blocks.length}.`);
      }
    }
  }
  for (const stage of ['vertex', 'fragment']) {
    const stem = `${pass.name}__${stage}`;
    for (const relativePath of [
      path.join('msl', `${stem}.msl`),
      path.join('uglir', `${stem}.uglir.json`),
      path.join('spv', `${stem}.raw.spvasm`),
      path.join('spv', `${stem}.spvasm`)
    ]) {
      readRequiredText(
        path.join(experimentalArtifact.artifactDir, relativePath),
        errors,
        `${context}:experimental:${stage}`
      );
    }
  }
}

/** Verifies that one generated RenderSet layout exactly implements the manifest component ABI. */
function lintManifestComponentSchema(layout, expectedSchema, errors, context) {
  if (!Array.isArray(expectedSchema) || expectedSchema.length === 0) {
    return;
  }
  const expectedByName = new Map(expectedSchema.map((component) => [component.name, component]));
  const generatedByName = new Map(layout.components.map((component) => [component.name, component]));
  if (expectedByName.size !== expectedSchema.length) {
    addError(errors, 'manifest_component_name_duplicate', context, 'Manifest component names must be unique.');
  }
  if (generatedByName.size !== expectedByName.size) {
    addError(errors, 'manifest_component_count_mismatch', context, `Generated RenderSet has ${generatedByName.size} components; manifest declares ${expectedByName.size}.`);
  }
  for (const [componentName, expected] of expectedByName) {
    const generated = generatedByName.get(componentName);
    if (!generated) {
      addError(errors, 'manifest_component_missing', context, `Generated RenderSet is missing manifest component '${componentName}'.`);
      continue;
    }
    const expectedType = expected.kind === 'texture' ? 'TextureComponent' : 'BufferComponent';
    if (generated.componentType !== expectedType) {
      addError(errors, 'manifest_component_kind_mismatch', context, `Component '${componentName}' is ${generated.componentType}; manifest requires ${expectedType}.`);
    }
  }
  const vertexComponent = expectedSchema.find((component) => component.role === 'vertex');
  const indexComponent = expectedSchema.find((component) => component.role === 'index');
  if (vertexComponent?.name !== layout.vertexComponentName) {
    addError(errors, 'manifest_vertex_component_mismatch', context, `Generated vertex component '${layout.vertexComponentName}' differs from manifest '${vertexComponent?.name ?? '<missing>'}'.`);
  }
  if (indexComponent?.name !== layout.indexComponentName) {
    addError(errors, 'manifest_index_component_mismatch', context, `Generated index component '${layout.indexComponentName}' differs from manifest '${indexComponent?.name ?? '<missing>'}'.`);
  }
}

/** Lints one scene pass across Legacy host/MSL and Experimental UGLIR/MSL/SPIR-V artifacts. */
function lintScenePass(pass, legacyArtifact, experimentalArtifact, errors) {
  const passLabel = pass.logicalName === pass.name ? pass.name : `${pass.logicalName}->${pass.name}`;
  const context = pass.caseId === null ? passLabel : `${pass.caseId}:${passLabel}`;
  const legacyLayouts = parseRenderSetLayouts(legacyArtifact.text, errors, `${context}:legacy`);
  const experimentalLayouts = parseRenderSetLayouts(experimentalArtifact.text, errors, `${context}:experimental`);
  const legacyBinding = parseScenePassBinding(legacyArtifact.text, pass.name, legacyLayouts, errors, `${context}:legacy`);
  const experimentalBinding = parseScenePassBinding(experimentalArtifact.text, pass.name, experimentalLayouts, errors, `${context}:experimental`);
  lintScenePassCalls(legacyArtifact.text, pass.name, errors, `${context}:legacy`);
  lintScenePassCalls(experimentalArtifact.text, pass.name, errors, `${context}:experimental`);
  lintEmbeddedSpirv(legacyArtifact.text, pass.name, errors, `${context}:legacy`);
  lintEmbeddedSpirv(experimentalArtifact.text, pass.name, errors, `${context}:experimental`);
  const legacyDsl = readRequiredText(path.join(legacyArtifact.artifactDir, DSL_HEADER_NAME), errors, `${context}:legacy`);
  const experimentalDsl = readRequiredText(path.join(experimentalArtifact.artifactDir, DSL_HEADER_NAME), errors, `${context}:experimental`);
  if (legacyDsl !== null) {
    lintDslEntityBuiltins(legacyDsl, pass.name, pass.requiresInstancing, errors, `${context}:legacy`);
  }
  if (experimentalDsl !== null) {
    lintDslEntityBuiltins(experimentalDsl, pass.name, pass.requiresInstancing, errors, `${context}:experimental`);
  }
  if (legacyBinding === null || experimentalBinding === null) {
    return;
  }
  if (legacyBinding.typeName !== experimentalBinding.typeName || legacyBinding.slot !== experimentalBinding.slot) {
    addError(errors, 'pipeline_render_set_binding_mismatch', context, `Legacy binds ${legacyBinding.typeName}@Slot${legacyBinding.slot}; Experimental binds ${experimentalBinding.typeName}@Slot${experimentalBinding.slot}.`);
    return;
  }
  if (pass.expectedSetType !== null && legacyBinding.typeName !== pass.expectedSetType) {
    addError(errors, 'manifest_render_set_type_mismatch', context, `Generated pass binds ${legacyBinding.typeName}; manifest requires ${pass.expectedSetType}.`);
  }
  const legacyLayout = legacyLayouts.get(legacyBinding.typeName);
  const experimentalLayout = experimentalLayouts.get(experimentalBinding.typeName);
  if (legacyLayout === undefined || experimentalLayout === undefined) {
    addError(errors, 'render_set_layout_missing', context, `Unable to resolve generated layout for ${legacyBinding.typeName}.`);
    return;
  }
  if (JSON.stringify(canonicalRenderSetLayout(legacyLayout)) !== JSON.stringify(canonicalRenderSetLayout(experimentalLayout))) {
    addError(errors, 'pipeline_component_layout_mismatch', context, 'Legacy and Experimental RenderSet component layouts differ.');
  }
  if (legacyLayout.vertexComponentName === null || legacyLayout.indexComponentName === null) {
    addError(errors, 'scene_geometry_components_missing', context, 'Scene RenderSet must declare generated vertex and index components.');
  }
  lintManifestComponentSchema(legacyLayout, pass.expectedComponentSchema, errors, `${context}:legacy-manifest-abi`);
  lintManifestComponentSchema(experimentalLayout, pass.expectedComponentSchema, errors, `${context}:experimental-manifest-abi`);
  const expectedResources = createExpectedRenderSetResources(experimentalLayout, experimentalBinding.slot);
  lintMslArgumentBuffer(legacyArtifact.text, legacyBinding.typeName, expectedResources, errors, `${context}:legacy-msl`);
  for (const stage of ['vertex', 'fragment']) {
    const stem = `${pass.name}__${stage}`;
    const mslPath = path.join(experimentalArtifact.artifactDir, 'msl', `${stem}.msl`);
    const uglirPath = path.join(experimentalArtifact.artifactDir, 'uglir', `${stem}.uglir.json`);
    const rawSpirvPath = path.join(experimentalArtifact.artifactDir, 'spv', `${stem}.raw.spvasm`);
    const optimizedSpirvPath = path.join(experimentalArtifact.artifactDir, 'spv', `${stem}.spvasm`);
    const mslText = readRequiredText(mslPath, errors, `${context}:${stage}`);
    const uglirText = readRequiredText(uglirPath, errors, `${context}:${stage}`);
    const rawSpirvText = readRequiredText(rawSpirvPath, errors, `${context}:${stage}`);
    const optimizedSpirvText = readRequiredText(optimizedSpirvPath, errors, `${context}:${stage}`);
    if (mslText !== null) {
      lintMslArgumentBuffer(mslText, experimentalBinding.typeName, expectedResources, errors, `${context}:experimental-msl:${stage}`);
      if (stage === 'vertex') {
        lintMslEntityDecode(mslText, pass.requiresInstancing, errors, `${context}:experimental-msl:vertex`);
      }
    }
    if (uglirText === null) {
      continue;
    }
    const reflectedResources = lintUglirReflection(uglirText, expectedResources, errors, `${context}:uglir:${stage}`);
    if (rawSpirvText !== null) {
      lintRawSpirv(rawSpirvText, reflectedResources, expectedResources, errors, `${context}:direct-spirv:${stage}`);
    }
    if (optimizedSpirvText !== null) {
      lintOptimizedSpirv(optimizedSpirvText, reflectedResources, expectedResources, stage === 'vertex', errors, `${context}:spirv:${stage}`);
    }
  }
}

/** Runs the generated-artifact lint and returns all violations without mutating inputs. */
export function lintGeneratedArtifacts(options) {
  const errors = [];
  const legacyRoot = path.resolve(options.legacyRoot);
  const experimentalRoot = path.resolve(options.experimentalRoot);
  const manifestPath = options.manifestPath === null ? null : path.resolve(options.manifestPath);
  const manifestPasses = loadManifestPasses(manifestPath, options.caseIds, errors);
  const fixturePasses = options.fixtureSelectors.map((selector) => ({
    ...parseFixtureSelector(selector),
    caseId: null,
    expectedSetType: null
  }));
  const passes = mergePassSpecifications(manifestPasses.concat(fixturePasses), new Set(options.instancingPasses), errors);
  if (passes.length === 0) {
    addError(errors, 'no_scene_passes_selected', 'selection', 'No phase1_required complex scene passes or --fixture selectors were selected.');
  }
  const legacyIndex = indexGeneratedRoot(legacyRoot, errors, 'legacy');
  const experimentalIndex = indexGeneratedRoot(experimentalRoot, errors, 'experimental');
  for (const pass of passes) {
    const legacyArtifact = findPassArtifact(legacyIndex, pass.name, errors, `${pass.name}:legacy`);
    const experimentalArtifact = findPassArtifact(experimentalIndex, pass.name, errors, `${pass.name}:experimental`);
    if (legacyArtifact !== null && experimentalArtifact !== null) {
      if (pass.screenOnly) {
        lintScreenPass(pass, legacyArtifact, experimentalArtifact, errors);
      } else {
        lintScenePass(pass, legacyArtifact, experimentalArtifact, errors);
      }
    }
  }
  return {
    errors,
    checkedPasses: passes.map((pass) => pass.name),
    legacyRoot,
    experimentalRoot
  };
}

/** Parses explicit generated-artifact lint command-line options. */
export function parseArguments(argv) {
  const options = {
    legacyRoot: null,
    experimentalRoot: null,
    manifestPath: null,
    caseIds: [],
    fixtureSelectors: [],
    instancingPasses: [],
    help: false
  };
  const valueOptions = new Map([
    ['--legacy-root', 'legacyRoot'],
    ['--experimental-root', 'experimentalRoot'],
    ['--manifest', 'manifestPath']
  ]);
  const repeatedOptions = new Map([
    ['--case', 'caseIds'],
    ['--fixture', 'fixtureSelectors'],
    ['--instancing-pass', 'instancingPasses']
  ]);
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === '--help' || argument === '-h') {
      options.help = true;
      continue;
    }
    const property = valueOptions.get(argument);
    const repeatedProperty = repeatedOptions.get(argument);
    if (property === undefined && repeatedProperty === undefined) {
      throw new Error(`Unknown argument: ${argument}`);
    }
    const value = argv[index + 1];
    if (value === undefined || value.startsWith('--')) {
      throw new Error(`Missing value for ${argument}`);
    }
    if (property !== undefined) {
      options[property] = value;
    } else {
      options[repeatedProperty].push(value);
    }
    index += 1;
  }
  return options;
}

/** Prints generated-artifact lint usage with only command-line configuration. */
function printUsage() {
  process.stdout.write(`Usage:
  node GVMRuntime_ThreeSamples/Tools/lint_generated_artifacts.mjs \\
    --legacy-root <generated-root> \\
    --experimental-root <generated-root> \\
    [--manifest <three-r185-manifest.json>] \\
    [--case <example-id>]... \\
    [--fixture <RenderPass[:instancing|:screen]>]... \\
    [--instancing-pass <RenderPass>]...

The manifest path is required unless at least one --fixture is supplied.
All generated headers, merged DSL, UGLIR JSON, MSL, and raw/final direct-SPIR-V
artifacts are inspected directly; no declaration-only ABI sidecar is accepted.
`);
}

/** Executes the command-line lint and reports every failing contract. */
function main(argv) {
  let options;
  try {
    options = parseArguments(argv);
  } catch (error) {
    process.stderr.write(`generated-artifact lint: ${error.message}\n`);
    printUsage();
    return 2;
  }
  if (options.help) {
    printUsage();
    return 0;
  }
  if (options.legacyRoot === null || options.experimentalRoot === null
      || (options.manifestPath === null && options.fixtureSelectors.length === 0)) {
    process.stderr.write('generated-artifact lint: --legacy-root, --experimental-root, and --manifest or --fixture are required.\n');
    printUsage();
    return 2;
  }
  const result = lintGeneratedArtifacts(options);
  if (result.errors.length > 0) {
    for (const error of result.errors) {
      process.stderr.write(`ERROR [${error.code}] ${error.context}: ${error.message}\n`);
    }
    process.stderr.write(`Generated-artifact lint failed: ${result.errors.length} violation(s), ${result.checkedPasses.length} pass(es) selected.\n`);
    return 1;
  }
  process.stdout.write(`Generated-artifact lint passed: ${result.checkedPasses.length} scene pass(es); Legacy/Experimental layout and Experimental MSL/direct-SPIR-V ABI agree.\n`);
  return 0;
}

const isCommandLineEntry = process.argv[1] !== undefined
  && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (isCommandLineEntry) {
  process.exitCode = main(process.argv.slice(2));
}
