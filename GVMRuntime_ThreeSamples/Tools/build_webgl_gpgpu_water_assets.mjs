#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const PACK_MAGIC = Buffer.from('GVMWDUK1', 'ascii');
const PACK_VERSION = 1;
const PACK_HEADER_SIZE = 256;
const VERTEX_STRIDE = 32;
const EXPECTED_SOURCE_GLB_SHA256 = '76c62e63a0aec09cd66f2e2c9452a6dcee428f2464dd2c6d845958a8f7f7cdd1';
const EXPECTED_DECODER_WRAPPER_SHA256 = '8bb2952d2ba7d67e1414f8df819410cb0434a666be53f671fff75f68843d76f6';
const EXPECTED_DECODER_WASM_SHA256 = 'a680d927bed9cb864ddbd63521868891af2bfbe755092761b4837487618df8ac';

/** Parses required named CLI paths without consulting environment variables. */
function parseArguments(argumentsList) {
  const options = new Map();
  for (let index = 0; index < argumentsList.length; index += 2) {
    const name = argumentsList[index];
    const value = argumentsList[index + 1];
    if (!name?.startsWith('--') || value == null) {
      throw new Error(`Expected a named option and value at argument ${index + 1}.`);
    }
    options.set(name.slice(2), value);
  }
  const result = {};
  for (const name of ['source-glb', 'decoder-wrapper', 'decoder-wasm', 'output-root']) {
    const value = options.get(name);
    if (!value) throw new Error(`Missing required --${name} path.`);
    result[name] = path.resolve(value);
  }
  return result;
}

/** Returns one lowercase SHA-256 digest for immutable source or derived bytes. */
function sha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

/** Rejects a source dependency whose content differs from the r185 lock. */
function verifySha256(label, bytes, expectedSha256) {
  const actualSha256 = sha256(bytes);
  if (actualSha256 !== expectedSha256) {
    throw new Error(`${label} SHA-256 is ${actualSha256}; expected ${expectedSha256}.`);
  }
  return actualSha256;
}

/** Aligns one binary section offset to a positive power-of-two boundary. */
function alignTo(value, alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

/** Parses the exact single-buffer GLB container used by the r185 duck asset. */
function parseDuckGlb(glbBytes) {
  if (glbBytes.subarray(0, 4).toString('ascii') !== 'glTF'
      || glbBytes.readUInt32LE(4) !== 2
      || glbBytes.readUInt32LE(8) !== glbBytes.byteLength) {
    throw new Error('Duck source is not a complete glTF 2.0 binary container.');
  }
  const jsonLength = glbBytes.readUInt32LE(12);
  if (glbBytes.subarray(16, 20).toString('ascii') !== 'JSON') {
    throw new Error('Duck GLB does not begin with a JSON chunk.');
  }
  const document = JSON.parse(glbBytes.toString('utf8', 20, 20 + jsonLength));
  const binaryHeaderOffset = 20 + jsonLength;
  const binaryLength = glbBytes.readUInt32LE(binaryHeaderOffset);
  if (glbBytes.subarray(binaryHeaderOffset + 4, binaryHeaderOffset + 8).toString('ascii') !== 'BIN\u0000') {
    throw new Error('Duck GLB does not contain the expected BIN chunk.');
  }
  const binaryOffset = binaryHeaderOffset + 8;
  if (binaryOffset + binaryLength > glbBytes.byteLength) {
    throw new Error('Duck GLB BIN chunk exceeds its container.');
  }
  return { document, binaryOffset, binaryLength };
}

/** Evaluates the locked Emscripten CommonJS wrapper without rewriting upstream bytes. */
function loadDracoDecoderFactory(wrapperPath, wrapperSource) {
  const module = { exports: {} };
  const evaluateWrapper = new Function(
    'module',
    'exports',
    'require',
    '__filename',
    '__dirname',
    `${wrapperSource}\nreturn module.exports;`
  );
  const factory = evaluateWrapper(
    module,
    module.exports,
    require,
    wrapperPath,
    path.dirname(wrapperPath)
  );
  if (typeof factory !== 'function') {
    throw new Error('Locked Draco wrapper did not expose its decoder factory.');
  }
  return factory;
}

/** Copies one decoded float attribute into a tightly packed JavaScript array. */
function decodeFloatAttribute(decoderModule, decoder, mesh, uniqueId, componentCount, vertexCount) {
  const attribute = decoder.GetAttributeByUniqueId(mesh, uniqueId);
  if (attribute.ptr === 0 || attribute.num_components() !== componentCount) {
    throw new Error(`Draco attribute ${uniqueId} does not have ${componentCount} components.`);
  }
  const values = new decoderModule.DracoFloat32Array();
  try {
    if (!decoder.GetAttributeFloatForAllPoints(mesh, attribute, values)
        || values.size() !== vertexCount * componentCount) {
      throw new Error(`Draco attribute ${uniqueId} has an unexpected decoded length.`);
    }
    return Float32Array.from(
      { length: values.size() },
      (_, index) => values.GetValue(index)
    );
  } finally {
    decoderModule.destroy(values);
  }
}

/** Decodes the locked Draco primitive into exact indexed position, normal, and UV arrays. */
async function decodeDuckPrimitive(glb, glbBytes, wrapperPath, wrapperSource, wasmBytes) {
  const primitive = glb.document.meshes?.[0]?.primitives?.[0];
  const extension = primitive?.extensions?.KHR_draco_mesh_compression;
  if (!extension || primitive.mode !== 4) {
    throw new Error('Duck GLB must contain one Draco-compressed triangle primitive.');
  }
  const compressedView = glb.document.bufferViews[extension.bufferView];
  const compressedOffset = glb.binaryOffset + (compressedView.byteOffset ?? 0);
  const compressedBytes = new Int8Array(
    glbBytes.buffer,
    glbBytes.byteOffset + compressedOffset,
    compressedView.byteLength
  );
  const decoderFactory = loadDracoDecoderFactory(wrapperPath, wrapperSource);
  const decoderModule = await decoderFactory({ wasmBinary: wasmBytes });
  const decoder = new decoderModule.Decoder();
  const decoderBuffer = new decoderModule.DecoderBuffer();
  const mesh = new decoderModule.Mesh();
  let status = null;
  try {
    decoderBuffer.Init(compressedBytes, compressedBytes.byteLength);
    if (decoder.GetEncodedGeometryType(decoderBuffer) !== decoderModule.TRIANGULAR_MESH) {
      throw new Error('Duck Draco payload is not a triangular mesh.');
    }
    status = decoder.DecodeBufferToMesh(decoderBuffer, mesh);
    if (!status.ok()) throw new Error(`Duck Draco decode failed: ${status.error_msg()}`);
    const vertexCount = mesh.num_points();
    const faceCount = mesh.num_faces();
    if (vertexCount !== 2277 || faceCount * 3 !== 12636) {
      throw new Error(`Duck Draco topology is ${vertexCount} vertices/${faceCount * 3} indices.`);
    }
    const positions = decodeFloatAttribute(
      decoderModule, decoder, mesh, extension.attributes.POSITION, 3, vertexCount
    );
    const normals = decodeFloatAttribute(
      decoderModule, decoder, mesh, extension.attributes.NORMAL, 3, vertexCount
    );
    const texCoords = decodeFloatAttribute(
      decoderModule, decoder, mesh, extension.attributes.TEXCOORD_0, 2, vertexCount
    );
    const face = new decoderModule.DracoInt32Array();
    const indices = new Uint32Array(faceCount * 3);
    try {
      for (let faceIndex = 0; faceIndex < faceCount; faceIndex += 1) {
        if (!decoder.GetFaceFromMesh(mesh, faceIndex, face)) {
          throw new Error(`Could not decode duck face ${faceIndex}.`);
        }
        for (let corner = 0; corner < 3; corner += 1) {
          indices[faceIndex * 3 + corner] = face.GetValue(corner);
        }
      }
    } finally {
      decoderModule.destroy(face);
    }
    return { vertexCount, indices, positions, normals, texCoords };
  } finally {
    if (status) decoderModule.destroy(status);
    decoderModule.destroy(mesh);
    decoderModule.destroy(decoderBuffer);
    decoderModule.destroy(decoder);
  }
}

/** Extracts the locked embedded PNG and validates its expected 512-square extent. */
function extractDuckPng(glb, glbBytes) {
  const image = glb.document.images?.[0];
  if (image?.mimeType !== 'image/png') throw new Error('Duck GLB image is not PNG.');
  const imageView = glb.document.bufferViews[image.bufferView];
  const start = glb.binaryOffset + (imageView.byteOffset ?? 0);
  const pngBytes = glbBytes.subarray(start, start + imageView.byteLength);
  const pngSignature = '89504e470d0a1a0a';
  if (pngBytes.subarray(0, 8).toString('hex') !== pngSignature
      || pngBytes.readUInt32BE(16) !== 512
      || pngBytes.readUInt32BE(20) !== 512) {
    throw new Error('Duck embedded PNG does not have the locked 512x512 layout.');
  }
  return pngBytes;
}

/** Writes the versioned derived mesh pack with immutable source provenance in its header. */
function buildDuckMeshPack(decoded, material, provenance) {
  const vertexOffset = PACK_HEADER_SIZE;
  const vertexByteCount = decoded.vertexCount * VERTEX_STRIDE;
  const indexOffset = alignTo(vertexOffset + vertexByteCount, 16);
  const indexByteCount = decoded.indices.length * 4;
  const byteCount = indexOffset + indexByteCount;
  const pack = Buffer.alloc(byteCount);
  PACK_MAGIC.copy(pack, 0);
  pack.writeUInt32LE(PACK_VERSION, 8);
  pack.writeUInt32LE(PACK_HEADER_SIZE, 12);
  pack.writeUInt32LE(decoded.vertexCount, 16);
  pack.writeUInt32LE(decoded.indices.length, 20);
  pack.writeUInt32LE(VERTEX_STRIDE, 24);
  pack.writeUInt32LE(4, 28);
  pack.writeUInt32LE(vertexOffset, 32);
  pack.writeUInt32LE(indexOffset, 36);
  material.baseColorFactor.forEach((value, index) => pack.writeFloatLE(value, 48 + index * 4));
  pack.writeFloatLE(material.metallicFactor, 64);
  pack.writeFloatLE(material.roughnessFactor, 68);
  material.emissiveFactor.forEach((value, index) => pack.writeFloatLE(value, 72 + index * 4));
  pack.writeUInt32LE(material.doubleSided ? 1 : 0, 84);
  Buffer.from(provenance.sourceGlbSha256, 'hex').copy(pack, 88);
  Buffer.from(provenance.decoderWasmSha256, 'hex').copy(pack, 120);
  Buffer.from(provenance.decoderWrapperSha256, 'hex').copy(pack, 152);
  for (let vertexIndex = 0; vertexIndex < decoded.vertexCount; vertexIndex += 1) {
    const outputOffset = vertexOffset + vertexIndex * VERTEX_STRIDE;
    for (let component = 0; component < 3; component += 1) {
      pack.writeFloatLE(decoded.positions[vertexIndex * 3 + component], outputOffset + component * 4);
      pack.writeFloatLE(decoded.normals[vertexIndex * 3 + component], outputOffset + 12 + component * 4);
    }
    pack.writeFloatLE(decoded.texCoords[vertexIndex * 2], outputOffset + 24);
    pack.writeFloatLE(decoded.texCoords[vertexIndex * 2 + 1], outputOffset + 28);
  }
  for (let index = 0; index < decoded.indices.length; index += 1) {
    pack.writeUInt32LE(decoded.indices[index], indexOffset + index * 4);
  }
  return pack;
}

/** Derives the canonical C++-readable duck pack and provenance sidecar. */
async function main() {
  const options = parseArguments(process.argv.slice(2));
  const [glbBytes, wrapperBytes, wasmBytes] = await Promise.all([
    fs.readFile(options['source-glb']),
    fs.readFile(options['decoder-wrapper']),
    fs.readFile(options['decoder-wasm'])
  ]);
  const provenance = {
    sourceGlbSha256: verifySha256('Duck GLB', glbBytes, EXPECTED_SOURCE_GLB_SHA256),
    decoderWrapperSha256: verifySha256(
      'Draco wrapper', wrapperBytes, EXPECTED_DECODER_WRAPPER_SHA256
    ),
    decoderWasmSha256: verifySha256('Draco WASM', wasmBytes, EXPECTED_DECODER_WASM_SHA256)
  };
  const glb = parseDuckGlb(glbBytes);
  const decoded = await decodeDuckPrimitive(
    glb,
    glbBytes,
    options['decoder-wrapper'],
    wrapperBytes.toString('utf8'),
    wasmBytes
  );
  const sourceMaterial = glb.document.materials[0];
  const pbr = sourceMaterial.pbrMetallicRoughness;
  const material = {
    baseColorFactor: pbr.baseColorFactor,
    metallicFactor: pbr.metallicFactor,
    roughnessFactor: pbr.roughnessFactor,
    emissiveFactor: sourceMaterial.emissiveFactor,
    doubleSided: sourceMaterial.doubleSided
  };
  const pngBytes = extractDuckPng(glb, glbBytes);
  const packBytes = buildDuckMeshPack(decoded, material, provenance);
  const outputDirectory = path.join(
    options['output-root'], 'derived', 'webgl_gpgpu_water'
  );
  const packPath = path.join(outputDirectory, 'duck_mesh.bin');
  const pngPath = path.join(outputDirectory, 'duck.png');
  const metadataPath = path.join(outputDirectory, 'duck_mesh.json');
  const metadata = {
    schemaVersion: 1,
    semantic: 'Three r185 duck: one Draco triangle primitive and one embedded 512x512 sRGB PNG.',
    source: provenance,
    derived: {
      meshPackSha256: sha256(packBytes),
      pngSha256: sha256(pngBytes),
      vertexCount: decoded.vertexCount,
      indexCount: decoded.indices.length,
      vertexStride: VERTEX_STRIDE,
      material
    }
  };
  await fs.mkdir(outputDirectory, { recursive: true });
  await Promise.all([
    fs.writeFile(packPath, packBytes),
    fs.writeFile(pngPath, pngBytes),
    fs.writeFile(metadataPath, `${JSON.stringify(metadata, null, 2)}\n`)
  ]);
  console.log(`Derived duck mesh: ${packPath}`);
  console.log(`Derived duck texture: ${pngPath}`);
  console.log(`Derived metadata: ${metadataPath}`);
}

await main();
