/** Returns the median of finite nonnegative measurements, rejecting absent evidence. */
export function median(values) {
  if (!values.length || values.some(value => !Number.isFinite(value) || value < 0)) throw new Error('Missing or invalid performance measurements.');
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

/** Summarizes optional paired measurements without making timing or memory ratios acceptance gates. */
export function comparePairedMeasurements(pairs) {
  if (pairs.length < 5) throw new Error('Performance comparison requires at least five paired measurements.');
  const metrics = {};
  for (const metric of ['compileMs', 'peakRssBytes', 'computeClassCreationUs', 'gpuPassNs']) {
    const legacy = median(pairs.map(pair => pair.legacy?.[metric]));
    const experimental = median(pairs.map(pair => pair.experimental?.[metric]));
    if (legacy <= 0 || experimental <= 0) throw new Error(`Zero ${metric} cannot prove a meaningful performance ratio.`);
    const ratio = experimental / legacy;
    metrics[metric] = { legacyMedian: legacy, experimentalMedian: experimental, ratio };
  }
  return { metrics };
}

/** Records RSS window medians and slope for review; a positive slope alone does not establish a leak. */
export function inspectMemoryTrend(samples, durationSeconds) {
  if (durationSeconds < 1800) throw new Error('Sustained acceptance requires at least 1800 seconds.');
  const steady = samples.filter(sample => sample.seconds >= 60 && sample.seconds <= durationSeconds);
  if (steady.length < 120) throw new Error('Insufficient RSS samples for sustained memory validation.');
  const initial = median(steady.filter(sample => sample.seconds < 120).map(sample => sample.rssBytes));
  const final = median(steady.filter(sample => sample.seconds > durationSeconds - 60).map(sample => sample.rssBytes));
  const meanTime = steady.reduce((sum, sample) => sum + sample.seconds, 0) / steady.length;
  const meanRss = steady.reduce((sum, sample) => sum + sample.rssBytes, 0) / steady.length;
  const numerator = steady.reduce((sum, sample) => sum + (sample.seconds - meanTime) * (sample.rssBytes - meanRss), 0);
  const denominator = steady.reduce((sum, sample) => sum + (sample.seconds - meanTime) ** 2, 0);
  const bytesPerMinute = numerator / denominator * 60;
  return { initialWindowMedianBytes: initial, finalWindowMedianBytes: final, bytesPerMinute,
    peakRssBytes: Math.max(...samples.map(sample => sample.rssBytes)), windowDeltaBytes: final - initial };
}
