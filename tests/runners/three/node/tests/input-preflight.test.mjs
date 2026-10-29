import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const cliPath = path.resolve(testDirectory, '..', 'cli.mjs');

/** Executes the Three CLI and returns its exit code and captured diagnostics. */
function executeCli(argumentsList) {
  return new Promise((resolve) => {
    execFile(process.execPath, [cliPath, ...argumentsList], { encoding: 'utf8' }, (error, stdout, stderr) => {
      resolve({ exitCode: error?.code ?? 0, stdout, stderr });
    });
  });
}

/** Writes a temporary fixed-size manifest whose entire audit population is pending. */
async function createPendingManifest() {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-pending-'));
  const manifestPath = path.join(directory, 'manifest.json');
  const examples = Array.from({ length: 588 }, (_value, index) => ({
    id: `case_${index}`,
    status: index < 77 ? 'excluded_upstream' : 'audit_pending'
  }));
  await fs.writeFile(manifestPath, JSON.stringify({ examples }), 'utf8');
  return { directory, manifestPath };
}

test('run reports audit_pending before missing input-lock paths or generated artifacts', async () => {
  const fixture = await createPendingManifest();
  try {
    const result = await executeCli(['run', '--profile', 'macos-three-r185', '--manifest', fixture.manifestPath]);
    assert.equal(result.exitCode, 1);
    assert.match(result.stderr, /capability audit is not locked: 511 candidate examples remain audit_pending/u);
    assert.doesNotMatch(result.stderr, /requires explicit --upstream-root/u);
    assert.doesNotMatch(result.stderr, /lint_generated_artifacts/u);
  } finally {
    await fs.rm(fixture.directory, { recursive: true, force: true });
  }
});
