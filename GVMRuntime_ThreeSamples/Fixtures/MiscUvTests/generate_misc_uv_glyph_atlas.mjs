import fs from 'node:fs';
import path from 'node:path';

import { decodePngRgba } from '../../../tests/runners/three/node/reference-capture.mjs';

/** Builds bounded 16-bit runs from one browser-rasterized grayscale glyph atlas. */
function buildCoverageRuns(pixels) {
  const runs = [];
  for (let pixel = 0; pixel < pixels.length / 4; pixel += 1) {
    const coverage = pixels[pixel * 4];
    const previous = runs.at(-1);
    if (previous && previous.coverage === coverage && previous.count < 65535) {
      previous.count += 1;
    } else {
      runs.push({ count: 1, coverage });
    }
  }
  return runs;
}

/** Formats one immutable sample-private C++ header from a locked Chrome glyph atlas PNG. */
function formatGlyphAtlasHeader(image, phaseCount) {
  const phaseColumns = Math.min(phaseCount, 8);
  const runs = buildCoverageRuns(image.pixels);
  const runLines = [];
  for (let index = 0; index < runs.length; index += 16) {
    runLines.push(`        ${runs.slice(index, index + 16)
      .map((run) => `0x${((run.coverage << 16) | run.count)
        .toString(16).padStart(8, '0')}u`)
      .join(', ')}`);
  }
  return `#ifndef GVM_THREE_MISC_UV_TESTS_GLYPH_ATLAS_DATA_HPP
#define GVM_THREE_MISC_UV_TESTS_GLYPH_ATLAS_DATA_HPP

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Stores packed 16-bit run lengths and 8-bit Chrome/Skia glyph coverage. */
    inline constexpr uint32_t MiscUvGlyphAtlasRuns[] = {
${runLines.join(',\n')}
    };

    inline constexpr uint32_t MiscUvGlyphAtlasPhaseCount = ${phaseCount}u;
    inline constexpr uint32_t MiscUvGlyphAtlasPhaseColumns = ${phaseColumns}u;
    inline constexpr uint32_t MiscUvGlyphAtlasWidth = ${image.width}u;
    inline constexpr uint32_t MiscUvGlyphAtlasHeight = ${image.height}u;
    inline constexpr uint32_t MiscUvGlyphAtlasPixelCount = ${image.width * image.height}u;
}

#endif
`;
}

/** Generates the checked-in atlas header from explicit PNG, phase-count, and output arguments. */
function generateGlyphAtlasHeader() {
  const [inputPath, phaseText, outputPath] = process.argv.slice(2);
  if (!inputPath || !phaseText || !outputPath) {
    throw new Error('Usage: node generate_misc_uv_glyph_atlas.mjs <atlas.png> <phase-count> <output.hpp>');
  }
  const phaseCount = Number.parseInt(phaseText, 10);
  const image = decodePngRgba(fs.readFileSync(inputPath));
  const expectedPixelCount = phaseCount * phaseCount * 2 * 13 * 32 * 32;
  if (image.width * image.height !== expectedPixelCount) {
    throw new Error('Glyph atlas dimensions do not match its requested phase count.');
  }
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(outputPath, formatGlyphAtlasHeader(image, phaseCount));
}

generateGlyphAtlasHeader();
