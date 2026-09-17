import { promises as fs } from 'node:fs';

export const comparisonThresholds = Object.freeze({
  normalizedDistance: 0.1,
  normalizedDistancePixelRatio: 0.001,
  meanAbsoluteRgb: 2.0,
  p99AbsoluteRgb: 16,
  luminanceSsim: 0.995
});

/** Validates that an RGBA8 image has internally consistent dimensions and storage. */
export function validateRgbaImage(image, label) {
  if (!Number.isInteger(image.width) || image.width <= 0 || !Number.isInteger(image.height) || image.height <= 0) {
    throw new Error(`${label} has invalid dimensions ${image.width}x${image.height}.`);
  }
  const expectedBytes = image.width * image.height * 4;
  if (!(image.pixels instanceof Uint8Array) || image.pixels.byteLength !== expectedBytes) {
    throw new Error(`${label} has ${image.pixels?.byteLength ?? 0} RGBA bytes; expected ${expectedBytes}.`);
  }
}

/** Loads an RGBA8 payload and adjacent JSON metadata produced by a Three sample host. */
export async function loadRgbaArtifact(rgbaPath, metadataPath) {
  const [pixels, metadataText] = await Promise.all([
    fs.readFile(rgbaPath),
    fs.readFile(metadataPath, 'utf8')
  ]);
  const metadata = JSON.parse(metadataText);
  const image = {
    width: Number(metadata.width),
    height: Number(metadata.height),
    pixels: new Uint8Array(pixels.buffer, pixels.byteOffset, pixels.byteLength)
  };
  validateRgbaImage(image, rgbaPath);
  return { ...image, metadata, rgbaPath, metadataPath };
}

/** Downsamples an RGBA8 image with an integer box filter while preserving straight-alpha bytes. */
export function boxDownsampleRgba(image, outputWidth, outputHeight) {
  validateRgbaImage(image, 'input image');
  if (!Number.isInteger(outputWidth) || !Number.isInteger(outputHeight) || outputWidth <= 0 || outputHeight <= 0) {
    throw new Error(`Invalid downsample dimensions ${outputWidth}x${outputHeight}.`);
  }
  if (image.width % outputWidth !== 0 || image.height % outputHeight !== 0) {
    throw new Error(`Box downsample requires integer scale factors: ${image.width}x${image.height} -> ${outputWidth}x${outputHeight}.`);
  }

  const scaleX = image.width / outputWidth;
  const scaleY = image.height / outputHeight;
  const sampleCount = scaleX * scaleY;
  const output = new Uint8Array(outputWidth * outputHeight * 4);
  for (let outputY = 0; outputY < outputHeight; outputY += 1) {
    for (let outputX = 0; outputX < outputWidth; outputX += 1) {
      const sums = [0, 0, 0, 0];
      for (let sampleY = 0; sampleY < scaleY; sampleY += 1) {
        for (let sampleX = 0; sampleX < scaleX; sampleX += 1) {
          const inputX = outputX * scaleX + sampleX;
          const inputY = outputY * scaleY + sampleY;
          const inputOffset = (inputY * image.width + inputX) * 4;
          for (let channel = 0; channel < 4; channel += 1) {
            sums[channel] += image.pixels[inputOffset + channel];
          }
        }
      }
      const outputOffset = (outputY * outputWidth + outputX) * 4;
      for (let channel = 0; channel < 4; channel += 1) {
        output[outputOffset + channel] = Math.round(sums[channel] / sampleCount);
      }
    }
  }
  return { width: outputWidth, height: outputHeight, pixels: output };
}

/** Converts one sRGB byte to a deterministic luminance contribution in display-encoded space. */
function rgbLuminance(red, green, blue) {
  return red * 0.2126 + green * 0.7152 + blue * 0.0722;
}

/** Computes mean local luminance SSIM over fixed non-overlapping windows. */
export function calculateLuminanceSsim(reference, actual, windowSize = 8) {
  validateComparableImages(reference, actual);
  const c1 = (0.01 * 255) ** 2;
  const c2 = (0.03 * 255) ** 2;
  let ssimSum = 0;
  let windowCount = 0;

  for (let originY = 0; originY < reference.height; originY += windowSize) {
    for (let originX = 0; originX < reference.width; originX += windowSize) {
      const endX = Math.min(reference.width, originX + windowSize);
      const endY = Math.min(reference.height, originY + windowSize);
      const luminanceReference = [];
      const luminanceActual = [];
      for (let y = originY; y < endY; y += 1) {
        for (let x = originX; x < endX; x += 1) {
          const offset = (y * reference.width + x) * 4;
          luminanceReference.push(rgbLuminance(
            reference.pixels[offset], reference.pixels[offset + 1], reference.pixels[offset + 2]
          ));
          luminanceActual.push(rgbLuminance(
            actual.pixels[offset], actual.pixels[offset + 1], actual.pixels[offset + 2]
          ));
        }
      }
      const count = luminanceReference.length;
      const referenceMean = luminanceReference.reduce((sum, value) => sum + value, 0) / count;
      const actualMean = luminanceActual.reduce((sum, value) => sum + value, 0) / count;
      let referenceVariance = 0;
      let actualVariance = 0;
      let covariance = 0;
      for (let index = 0; index < count; index += 1) {
        const referenceDelta = luminanceReference[index] - referenceMean;
        const actualDelta = luminanceActual[index] - actualMean;
        referenceVariance += referenceDelta * referenceDelta;
        actualVariance += actualDelta * actualDelta;
        covariance += referenceDelta * actualDelta;
      }
      const divisor = Math.max(1, count - 1);
      referenceVariance /= divisor;
      actualVariance /= divisor;
      covariance /= divisor;
      const numerator = (2 * referenceMean * actualMean + c1) * (2 * covariance + c2);
      const denominator = (referenceMean ** 2 + actualMean ** 2 + c1)
        * (referenceVariance + actualVariance + c2);
      ssimSum += denominator === 0 ? 1 : numerator / denominator;
      windowCount += 1;
    }
  }
  return windowCount === 0 ? 1 : ssimSum / windowCount;
}

/** Verifies that two RGBA8 images have identical dimensions. */
function validateComparableImages(reference, actual) {
  validateRgbaImage(reference, 'reference image');
  validateRgbaImage(actual, 'actual image');
  if (reference.width !== actual.width || reference.height !== actual.height) {
    throw new Error(`Image dimensions differ: reference=${reference.width}x${reference.height}, actual=${actual.width}x${actual.height}.`);
  }
}

/** Calculates all immutable Three r185 RGB and luminance comparison metrics. */
export function compareRgbaImages(reference, actual) {
  validateComparableImages(reference, actual);
  const pixelCount = reference.width * reference.height;
  const absoluteChannelErrors = new Uint8Array(pixelCount * 3);
  let absoluteErrorSum = 0;
  let normalizedDistancePixels = 0;
  let maximumAbsoluteRgb = 0;
  const normalization = 255 * Math.sqrt(3);

  for (let pixelIndex = 0; pixelIndex < pixelCount; pixelIndex += 1) {
    const rgbaOffset = pixelIndex * 4;
    let squaredDistance = 0;
    for (let channel = 0; channel < 3; channel += 1) {
      const error = Math.abs(reference.pixels[rgbaOffset + channel] - actual.pixels[rgbaOffset + channel]);
      absoluteChannelErrors[pixelIndex * 3 + channel] = error;
      absoluteErrorSum += error;
      maximumAbsoluteRgb = Math.max(maximumAbsoluteRgb, error);
      squaredDistance += error * error;
    }
    if (Math.sqrt(squaredDistance) / normalization > comparisonThresholds.normalizedDistance) {
      normalizedDistancePixels += 1;
    }
  }

  absoluteChannelErrors.sort();
  const p99Index = absoluteChannelErrors.length === 0
    ? 0
    : Math.min(absoluteChannelErrors.length - 1, Math.floor(absoluteChannelErrors.length * 0.99));
  return {
    width: reference.width,
    height: reference.height,
    pixelCount,
    normalizedDistancePixels,
    normalizedDistancePixelRatio: pixelCount === 0 ? 0 : normalizedDistancePixels / pixelCount,
    meanAbsoluteRgb: absoluteChannelErrors.length === 0 ? 0 : absoluteErrorSum / absoluteChannelErrors.length,
    p99AbsoluteRgb: absoluteChannelErrors.length === 0 ? 0 : absoluteChannelErrors[p99Index],
    maximumAbsoluteRgb,
    luminanceSsim: calculateLuminanceSsim(reference, actual)
  };
}

/** Applies the single repository-wide Three r185 comparison thresholds to calculated metrics. */
export function validateComparisonMetrics(metrics) {
  const failures = [];
  if (metrics.normalizedDistancePixelRatio >= comparisonThresholds.normalizedDistancePixelRatio) {
    failures.push(`normalized RGB distance pixel ratio ${metrics.normalizedDistancePixelRatio.toFixed(6)} must be < ${comparisonThresholds.normalizedDistancePixelRatio}`);
  }
  if (metrics.meanAbsoluteRgb > comparisonThresholds.meanAbsoluteRgb) {
    failures.push(`RGB MAE ${metrics.meanAbsoluteRgb.toFixed(4)} exceeds ${comparisonThresholds.meanAbsoluteRgb}`);
  }
  if (metrics.p99AbsoluteRgb > comparisonThresholds.p99AbsoluteRgb) {
    failures.push(`RGB P99 ${metrics.p99AbsoluteRgb} exceeds ${comparisonThresholds.p99AbsoluteRgb}`);
  }
  if (metrics.luminanceSsim < comparisonThresholds.luminanceSsim) {
    failures.push(`luminance SSIM ${metrics.luminanceSsim.toFixed(6)} is below ${comparisonThresholds.luminanceSsim}`);
  }
  return failures;
}

/** Compares two 800x500 RGBA8 captures after the mandatory 2x box downsample. */
export function compareThreeCaptures(reference, actual) {
  if (reference.width !== 800 || reference.height !== 500 || actual.width !== 800 || actual.height !== 500) {
    throw new Error(`Three capture gate requires 800x500 inputs; received reference=${reference.width}x${reference.height}, actual=${actual.width}x${actual.height}.`);
  }
  const downsampledReference = boxDownsampleRgba(reference, 400, 250);
  const downsampledActual = boxDownsampleRgba(actual, 400, 250);
  const metrics = compareRgbaImages(downsampledReference, downsampledActual);
  return { metrics, failures: validateComparisonMetrics(metrics) };
}
