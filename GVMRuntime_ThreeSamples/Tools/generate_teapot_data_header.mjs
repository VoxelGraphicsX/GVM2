#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

/** Extracts one numeric array literal from the pinned Three.js module. */
function extractNumericArray(source, declarationName) {
	const declaration = `const ${declarationName} = [`;
	const start = source.indexOf(declaration);
	if (start < 0) throw new Error(`Missing ${declarationName} declaration.`);
	const valuesStart = start + declaration.length;
	const valuesEnd = source.indexOf('];', valuesStart);
	if (valuesEnd < 0) throw new Error(`Unterminated ${declarationName} declaration.`);
	const normalized = source
		.slice(valuesStart, valuesEnd)
		.replace(/\/\*[\s\S]*?\*\//g, '')
		.replace(/-\s+/g, '-');
	return normalized.match(/-?(?:\d+\.?\d*|\.\d+)/g)?.map(Number) ?? [];
}

/** Formats one numeric array as a compact generated C++ initializer. */
function formatCppArray(values, suffix, valuesPerLine) {
	const lines = [];
	for (let offset = 0; offset < values.length; offset += valuesPerLine) {
		lines.push(`    ${values.slice(offset, offset + valuesPerLine)
			.map((value) => `${Number.isInteger(value) && suffix === 'f'
				? `${value}.0`
				: value.toString()}${suffix}`)
			.join(', ')}`);
	}
	return lines.join(',\n');
}

/** Generates immutable patch indices and control points from the frozen r185 source. */
function main() {
	const [inputPath, outputPath] = process.argv.slice(2);
	if (!inputPath || !outputPath) {
		throw new Error('Usage: generate_teapot_data_header.mjs <TeapotGeometry.js> <output.hpp>');
	}
	const source = fs.readFileSync(inputPath, 'utf8');
	const patches = extractNumericArray(source, 'teapotPatches');
	const vertices = extractNumericArray(source, 'teapotVertices');
	if (patches.length !== 512 || vertices.length !== 870) {
		throw new Error(`Unexpected teapot data sizes: ${patches.length}, ${vertices.length}.`);
	}
	const header = `#pragma once

#include <EASTL/array.h>

#include <cstdint>

namespace GVM::ThreeSamples
{
    /** Stores the immutable r185 Utah teapot patch control-point indices. */
    inline constexpr eastl::array<uint16_t, ${patches.length}u> TeapotPatchIndices = {
${formatCppArray(patches, 'u', 16)}
    };

    /** Stores the immutable r185 Utah teapot XYZ control-point stream. */
    inline constexpr eastl::array<float, ${vertices.length}u> TeapotControlPoints = {
${formatCppArray(vertices, 'f', 9)}
    };
} // namespace GVM::ThreeSamples
`;
	fs.mkdirSync(path.dirname(outputPath), { recursive: true });
	fs.writeFileSync(outputPath, header);
}

main();
