import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);

/** Loads the upstream CommonJS Basis factory even when its .js parent package is ESM. */
function loadBasisFactory(transcoderPath) {
  const source = fs.readFileSync(transcoderPath, 'utf8');
  const moduleRecord = { exports: {} };
  const evaluate = new Function(
    'module', 'exports', 'require', '__filename', '__dirname', source);
  evaluate(moduleRecord, moduleRecord.exports, require,
    transcoderPath, path.dirname(transcoderPath));
  return moduleRecord.exports;
}

/** Wraps one ASTC 4x4 base level in a minimal KTX1 container for ImageIO decoding. */
function makeCompressedKtx1(width, height, blocks, internalFormat) {
  const header = Buffer.alloc(68);
  Buffer.from([0xab, 0x4b, 0x54, 0x58, 0x20, 0x31, 0x31, 0xbb,
    0x0d, 0x0a, 0x1a, 0x0a]).copy(header, 0);
  const words = [
    0x04030201, 0, 1, 0, internalFormat, 0x1908,
    width, height, 0, 0, 1, 1, 0, blocks.byteLength,
  ];
  words.forEach((value, index) => header.writeUInt32LE(value, 12 + index * 4));
  return Buffer.concat([header, Buffer.from(blocks)]);
}

/** Transcodes the exact r185 UASTC texture to its reference ASTC 4x4 base level. */
async function decodeSandboxTexture(exampleRoot, outputPath, targetName) {
  const basisRoot = path.join(exampleRoot, 'jsm', 'libs', 'basis');
  const transcoderPath = path.join(basisRoot, 'basis_transcoder.js');
  const wasmBinary = fs.readFileSync(path.join(basisRoot, 'basis_transcoder.wasm'));
  const basisFactory = loadBasisFactory(transcoderPath);
  const basis = await basisFactory({ wasmBinary });
  basis.initializeBasis();
  const source = fs.readFileSync(path.join(
    exampleRoot, 'textures', 'ktx2', '2d_uastc.ktx2'));
  const texture = new basis.KTX2File(new Uint8Array(source));
  try {
    if (!texture.isValid() || texture.getWidth() !== 40
        || texture.getHeight() !== 40 || !texture.isUASTC()) {
      throw new Error('The r185 2d_uastc.ktx2 contract changed.');
    }
    if (!texture.startTranscoding()) {
      throw new Error('Basis could not start the locked UASTC transcode.');
    }
    const targets = {
      astc: { transcoderFormat: 10, internalFormat: 0x93b0 },
      bc7: { transcoderFormat: 7, internalFormat: 0x8e8c },
      etc2: { transcoderFormat: 1, internalFormat: 0x9278 },
    };
    const target = targets[targetName];
    if (!target) throw new Error(`Unsupported sandbox transcode target '${targetName}'.`);
    const byteCount = texture.getImageTranscodedSizeInBytes(
      0, 0, 0, target.transcoderFormat);
    const output = new Uint8Array(byteCount);
    if (!texture.transcodeImage(
      output, 0, 0, 0, target.transcoderFormat, 0, -1, -1)) {
      throw new Error('Basis could not transcode the locked UASTC base level.');
    }
    if (output.byteLength !== 10 * 10 * 16) {
      throw new Error('The locked UASTC ASTC byte count changed.');
    }
    fs.mkdirSync(path.dirname(outputPath), { recursive: true });
    fs.writeFileSync(outputPath, makeCompressedKtx1(
      40, 40, output, target.internalFormat));
  } finally {
    texture.close();
    texture.delete();
  }
}

const [exampleRootArgument, outputArgument, targetArgument = 'astc'] = process.argv.slice(2);
if (!exampleRootArgument || !outputArgument) {
  throw new Error('Usage: node decode_webgpu_sandbox_ktx2.mjs <examples-root> <output.ktx>');
}
await decodeSandboxTexture(path.resolve(exampleRootArgument),
  path.resolve(outputArgument), targetArgument);
