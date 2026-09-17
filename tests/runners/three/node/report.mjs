import { promises as fs } from 'node:fs';
import path from 'node:path';

/** Escapes untrusted manifest and diagnostic text for an HTML report. */
function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

/** Formats a finite ratio as a percentage suitable for gate summaries. */
function formatPercent(value) {
  return Number.isFinite(value) ? `${(value * 100).toFixed(2)}%` : '0.00%';
}

/** Renders one compact HTML table row for a quadrant result. */
function renderQuadrantRow(result) {
  const failures = result.failures.length === 0
    ? ''
    : `<ul>${result.failures.map((failure) => `<li>${escapeHtml(failure)}</li>`).join('')}</ul>`;
  const metrics = result.validation?.metrics;
  const metricsText = metrics
    ? `MAE ${metrics.meanAbsoluteRgb.toFixed(3)}, P99 ${metrics.p99AbsoluteRgb}, SSIM ${metrics.luminanceSsim.toFixed(6)}, distance ratio ${metrics.normalizedDistancePixelRatio.toFixed(6)}`
    : '';
  return `<tr class="${escapeHtml(result.status)}">
    <td>${escapeHtml(result.caseId)}</td>
    <td>${escapeHtml(result.scenarioId)}</td>
    <td>${escapeHtml(result.pipeline)}</td>
    <td>${escapeHtml(result.backend)}</td>
    <td>${result.repetition}</td>
    <td>${escapeHtml(result.status)}</td>
    <td>${escapeHtml(metricsText)}${failures}</td>
  </tr>`;
}

/** Renders a self-contained Three r185 gate report with both required and real coverage. */
export function renderReportHtml(report) {
  const quadrantRows = report.quadrants.map(renderQuadrantRow).join('\n');
  const failedCrossComparisons = report.crossComparisons.filter((entry) => entry.status !== 'pass');
  const crossFailures = failedCrossComparisons.length === 0
    ? '<p>All requested cross-pipeline and cross-backend comparisons passed.</p>'
    : `<ul>${failedCrossComparisons.map((entry) => (
      `<li>${escapeHtml(entry.caseId)}/${escapeHtml(entry.scenarioId)} ${escapeHtml(entry.relation)}: ${escapeHtml(entry.failures.join('; '))}</li>`
    )).join('')}</ul>`;
  const stabilityComparisons = report.stabilityComparisons ?? [];
  const failedStability = stabilityComparisons.filter((entry) => entry.status !== 'pass');
  const stabilitySummary = stabilityComparisons.length === 0
    ? '<p>Repeat stability was not evaluated.</p>'
    : failedStability.length === 0
      ? `<p>All ${stabilityComparisons.length} repeat comparisons are byte-exact.</p>`
      : `<ul>${failedStability.map((entry) => (
          `<li>${escapeHtml(entry.caseId)}/${escapeHtml(entry.scenarioId)} ${escapeHtml(entry.pipeline)}/${escapeHtml(entry.backend)} repeat ${entry.candidateRepetition}: ${escapeHtml(entry.failures.join('; '))}</li>`
        )).join('')}</ul>`;
  const gateEvaluable = report.coverage.matrixComplete
    && report.coverage.selectionComplete
    && report.coverage.pendingTotal === 0;
  const gateMetric = gateEvaluable
    ? `${report.coverage.passingRequired}/${report.coverage.requiredTotal}`
    : 'not evaluated';
  const gatePercent = gateEvaluable
    ? formatPercent(report.coverage.gatePassRate)
    : 'full required selection and four-quadrant matrix required';
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>GVM Three.js r185 Phase 1 Report</title>
  <style>
    body { margin: 0; padding: 24px; color: #e5edf6; background: #07111f; font: 13px ui-monospace, SFMono-Regular, Menlo, monospace; }
    h1, h2 { color: #f8fafc; }
    .summary { display: grid; grid-template-columns: repeat(auto-fit, minmax(190px, 1fr)); gap: 12px; }
    .card { padding: 16px; border: 1px solid #26364a; border-radius: 12px; background: #0c1929; }
    .metric { margin-top: 8px; font-size: 26px; font-weight: 700; }
    table { width: 100%; border-collapse: collapse; margin-top: 12px; }
    th, td { padding: 10px; border: 1px solid #26364a; text-align: left; vertical-align: top; }
    th { background: #0c1929; }
    tr.fail { background: #3a141a; }
    tr.pass { background: #102b22; }
    ul { margin: 6px 0; padding-left: 20px; }
    code { color: #7dd3fc; }
  </style>
</head>
<body>
  <h1>GVM Three.js r185 Phase 1</h1>
  <p>Status: <strong>${escapeHtml(report.status)}</strong> · ${escapeHtml(report.coverage.manifestEquation)}</p>
  <div class="summary">
    <section class="card"><div>Required gate (P/P)</div><div class="metric">${gateMetric}</div><div>${gatePercent}</div></section>
    <section class="card"><div>Real coverage (P/511)</div><div class="metric">${report.coverage.passingRequired}/511</div><div>${formatPercent(report.coverage.realCoverageRate)}</div></section>
    <section class="card"><div>Deferred debt</div><div class="metric">${report.coverage.deferredTotal}</div><div>Never counted as pass</div></section>
    <section class="card"><div>Audit pending</div><div class="metric">${report.coverage.pendingTotal}</div><div>Must be zero before execution</div></section>
  </div>
  <h2>Cross-quadrant parity</h2>
  ${crossFailures}
  <h2>Repeat stability</h2>
  ${stabilitySummary}
  <h2>Oracle comparisons</h2>
  <table>
    <thead><tr><th>Case</th><th>Scenario</th><th>Pipeline</th><th>Backend</th><th>Repeat</th><th>Status</th><th>Metrics / failures</th></tr></thead>
    <tbody>${quadrantRows}</tbody>
  </table>
</body>
</html>\n`;
}

/** Writes JSON and HTML reports to the immutable run directory. */
export async function writeReports(runDir, report) {
  await fs.mkdir(runDir, { recursive: true });
  const jsonPath = path.join(runDir, 'summary.json');
  const htmlPath = path.join(runDir, 'index.html');
  await Promise.all([
    fs.writeFile(jsonPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8'),
    fs.writeFile(htmlPath, renderReportHtml(report), 'utf8')
  ]);
  return { jsonPath, htmlPath };
}
