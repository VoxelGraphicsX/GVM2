import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { ReportRedactor, RedactedText } from './report-redaction.mjs';
import { parseOutputPolicy, RunOutput, runLoggedCommand } from './output-policy.mjs';

const home = path.posix.join('/', 'Users', 'review-user');
const source = `${home}/Project/GVM`;
const build = `${home}/Build With Spaces`;
const hostname = 'review-workstation.local';

/** Creates an isolated redactor with synthetic identity values, independent of the test machine. */
function redactor() { return new ReportRedactor(source, build, { home, temporary: '/tmp/review-cache', hostname }); }

/** Creates small temporary report fixtures and removes them after the test. */
function outputRoot(context) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-report-privacy-'));
  context.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  return directory;
}

test('report values and keys hide identity while preserving result types and artifact links', () => {
  const input = { host: hostname, command: `c++ -I"${build}/include" ${source}/tests/main.cpp`,
    files: { [`${home}/external.hpp`]: { line: 42, passed: true } },
    href: 'artifacts/render/generated/exports.hpp', sha256: '0123456789abcdef' };
  const result = redactor().redactObject(input);
  assert.equal(result.host, '<Host>');
  assert.equal(result.command, 'c++ -I"<Build>/include" <Source>/tests/main.cpp');
  assert.deepEqual(result.files, { '<Home>/external.hpp': { line: 42, passed: true } });
  assert.equal(result.href, input.href);
  assert.equal(result.sha256, input.sha256);
  assert.deepEqual(redactor().redactObject(result), result);
  assert.equal(input.host, hostname);
});

test('escaped Windows paths, encoded paths and macOS temporary user identifiers are redacted', () => {
  const windowsHome = ['C:', 'Users', 'review-user'].join('\\');
  const windows = new ReportRedactor(`${windowsHome}\\GVM`, '', { home: windowsHome, hostname });
  const input = JSON.stringify({ path: `${windowsHome}\\GVM\\sample.cpp` });
  assert.equal(JSON.parse(windows.redact(input)).path, '<Source>\\sample.cpp');
  assert.equal(redactor().redact(encodeURI(`${build}/sample.cpp`)), '<Build>/sample.cpp');
  const temporary = path.posix.join('/', 'private', 'var', 'folders', 'ab', 'user-identifier', 'T', 'test.hpp');
  assert.equal(redactor().redact(temporary), '<Temp>/T/test.hpp');
});

test('split writes and split UTF-8 characters never expose fragments of private values', () => {
  let output = '';
  const stream = new RedactedText(text => redactor().redact(text), text => { output += text; });
  const bytes = Buffer.from(`路径 ${source}/main.cpp\n${hostname}`);
  for (const byte of bytes) stream.write(Buffer.from([byte]));
  assert.equal(output, '路径 <Source>/main.cpp\n');
  stream.end();
  assert.equal(output, '路径 <Source>/main.cpp\n<Host>');
});

test('oversized unterminated lines are omitted with bounded memory and later lines survive', () => {
  let output = '';
  const stream = new RedactedText(text => redactor().redact(text), text => { output += text; });
  stream.write('x'.repeat(65 * 1024));
  stream.write(`${home}\n${source}/next.cpp\n`);
  stream.end();
  assert.match(output, /diagnostic line omitted/);
  assert.ok(output.endsWith('<Source>/next.cpp\n'));
  assert.ok(!output.includes(home));
  assert.equal(stream.pending.length, 0);
});

test('raw JSON, generated text and rotated logs are sanitized without touching images or compiler inputs', async context => {
  const root = outputRoot(context);
  const report = path.join(root, 'report');
  fs.mkdirSync(report);
  const originalHeader = path.join(root, 'compiler-output.hpp');
  const generated = `#line 12 "${source}/Fixture.hpp"\n`;
  fs.writeFileSync(originalHeader, generated);
  fs.writeFileSync(path.join(report, 'exports.hpp'), generated);
  fs.writeFileSync(path.join(report, 'target.json'), JSON.stringify({ tests: 1, file: `${source}/Fixture.hpp`, host: hostname }));
  fs.writeFileSync(path.join(report, 'test.log.2'), `${hostname} ${build}/target`);
  fs.writeFileSync(path.join(report, 'partial.json'), `{"file":"${source}/`);
  const pixels = Buffer.from(`P6\n1 1\n255\n${home}`);
  fs.writeFileSync(path.join(report, 'image.ppm'), pixels);
  fs.writeFileSync(path.join(report, 'image.rgba'), pixels);
  await redactor().redactDirectory(report);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(report, 'target.json'))), { tests: 1, file: '<Source>/Fixture.hpp', host: '<Host>' });
  assert.equal(fs.readFileSync(path.join(report, 'exports.hpp'), 'utf8'), '#line 12 "<Source>/Fixture.hpp"\n');
  assert.equal(fs.readFileSync(path.join(report, 'test.log.2'), 'utf8'), '<Host> <Build>/target');
  assert.ok(!fs.readFileSync(path.join(report, 'partial.json'), 'utf8').includes(home));
  assert.deepEqual(fs.readFileSync(path.join(report, 'image.ppm')), pixels);
  assert.deepEqual(fs.readFileSync(path.join(report, 'image.rgba')), pixels);
  assert.equal(fs.readFileSync(originalHeader, 'utf8'), generated);
});

test('child-process logs redact split output while raw diagnostic capture and failure status remain intact', async context => {
  const root = outputRoot(context);
  const output = new RunOutput(root, 'privacy', parseOutputPolicy({}));
  try {
    const logPath = path.join(output.logDir, 'child.log');
    const diagnostic = `${source}/missing.hpp:12: error\n${hostname}`;
    const command = `process.stdout.write(${JSON.stringify(diagnostic.slice(0, 8))});
      setTimeout(() => { process.stdout.write(${JSON.stringify(diagnostic.slice(8))}); process.exitCode = 7; }, 30);`;
    const result = await runLoggedCommand(process.execPath, ['-e', command],
      { logPath, redact: text => redactor().redact(text) }, output);
    assert.equal(result.exitCode, 7);
    assert.equal(result.output, diagnostic);
    assert.equal(fs.readFileSync(logPath, 'utf8'), '<Source>/missing.hpp:12: error\n<Host>');
  } finally { output.close(); }
});

test('CI keeps reports local and does not publish report or cache artifacts', () => {
  const workflow = fs.readFileSync(new URL('../../../.github/workflows/ci.yml', import.meta.url), 'utf8');
  assert.doesNotMatch(workflow, /uses:\s*actions\/upload-artifact/u);
});
