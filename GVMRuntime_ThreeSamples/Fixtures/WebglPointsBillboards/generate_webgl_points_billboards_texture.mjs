#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { inflateSync } from 'node:zlib';
import { pathToFileURL } from 'node:url';

const extent = 32;

/** Reads one network-order uint32 from an exact PNG byte offset. */
function readUint32(bytes, offset) {
  if (offset < 0 || offset + 4 > bytes.length) throw new Error('PNG uint32 read is out of range.');
  return bytes.readUInt32BE(offset);
}

/** Returns PNG's Paeth predictor for one filtered byte. */
function paeth(left, above, upperLeft) {
  const prediction = left + above - upperLeft;
  const leftDistance = Math.abs(prediction - left);
  const aboveDistance = Math.abs(prediction - above);
  const upperLeftDistance = Math.abs(prediction - upperLeft);
  if (leftDistance <= aboveDistance && leftDistance <= upperLeftDistance) return left;
  if (aboveDistance <= upperLeftDistance) return above;
  return upperLeft;
}

/** Decodes the pinned 32x32 noninterlaced RGBA8 disc texture. */
export function decodeDiscPng(bytes) {
  const signature = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  if (!bytes.subarray(0, 8).equals(signature)) throw new Error('disc.png signature is invalid.');
  const chunks = [];
  let offset = 8;
  let validHeader = false;
  while (offset + 12 <= bytes.length) {
    const length = readUint32(bytes, offset);
    const type = bytes.toString('ascii', offset + 4, offset + 8);
    const dataOffset = offset + 8;
    if (dataOffset + length + 4 > bytes.length) throw new Error('disc.png chunk is truncated.');
    if (type === 'IHDR') {
      validHeader = length === 13 && readUint32(bytes, dataOffset) === extent
        && readUint32(bytes, dataOffset + 4) === extent
        && bytes[dataOffset + 8] === 8 && bytes[dataOffset + 9] === 6
        && bytes[dataOffset + 10] === 0 && bytes[dataOffset + 11] === 0
        && bytes[dataOffset + 12] === 0;
    } else if (type === 'IDAT') {
      chunks.push(bytes.subarray(dataOffset, dataOffset + length));
    } else if (type === 'IEND') {
      break;
    }
    offset = dataOffset + length + 4;
  }
  if (!validHeader || chunks.length === 0) throw new Error('disc.png encoding differs from the locked RGBA8 asset.');
  const rowBytes = extent * 4;
  const filtered = inflateSync(Buffer.concat(chunks));
  if (filtered.length !== extent * (rowBytes + 1)) throw new Error('disc.png inflated byte count is invalid.');
  const decoded = Buffer.alloc(extent * rowBytes);
  for (let y = 0; y < extent; y += 1) {
    const rowOffset = y * (rowBytes + 1);
    const filter = filtered[rowOffset];
    if (filter > 4) throw new Error(`Unsupported PNG row filter ${filter}.`);
    for (let x = 0; x < rowBytes; x += 1) {
      const source = filtered[rowOffset + 1 + x];
      const destination = y * rowBytes + x;
      const left = x >= 4 ? decoded[destination - 4] : 0;
      const above = y > 0 ? decoded[destination - rowBytes] : 0;
      const upperLeft = y > 0 && x >= 4 ? decoded[destination - rowBytes - 4] : 0;
      let predictor = 0;
      if (filter === 1) predictor = left;
      else if (filter === 2) predictor = above;
      else if (filter === 3) predictor = Math.floor((left + above) / 2);
      else if (filter === 4) predictor = paeth(left, above, upperLeft);
      decoded[destination] = (source + predictor) & 0xff;
    }
  }
  const flipped = Buffer.alloc(decoded.length);
  for (let y = 0; y < extent; y += 1) {
    decoded.copy(flipped, y * rowBytes, (extent - 1 - y) * rowBytes, (extent - y) * rowBytes);
  }
  return flipped;
}

/** Writes the decoded browser-upload bytes in one tiny versioned binary envelope. */
export async function generateTextureAsset(assetPackRoot, outputPath) {
  const pngPath = path.join(assetPackRoot, 'textures', 'sprites', 'disc.png');
  const decoded = decodeDiscPng(await fs.readFile(pngPath));
  const output = Buffer.alloc(16 + decoded.length);
  output.write('DISC185\0', 0, 8, 'ascii');
  output.writeUInt32LE(1, 8);
  output.writeUInt32LE(extent, 12);
  decoded.copy(output, 16);
  await fs.mkdir(path.dirname(outputPath), { recursive: true });
  await fs.writeFile(outputPath, output);
  return { outputPath, extent, byteCount: output.length };
}

/** Parses required standalone generator options. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) throw new Error(`Duplicate --${name}.`);
    options[name] = value;
  }
  return options;
}

/** Runs the standalone point-texture asset generator. */
async function main() {
  const options = parseArguments(process.argv);
  if (!options['asset-pack-root'] || !options.output) throw new Error('Missing --asset-pack-root or --output.');
  console.log(JSON.stringify(await generateTextureAsset(
    path.resolve(options['asset-pack-root']), path.resolve(options.output)), null, 2));
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
