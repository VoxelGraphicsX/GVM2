#!/usr/bin/env node
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import path from 'node:path';

const files = execFileSync('git', ['ls-files', '-z'], { encoding: 'utf8' }).split('\0').filter(Boolean);
const roots = new Set(['GVM', 'UGLC', 'cmake', 'tests', 'GVMRHI_Samples', 'GVMRuntime_Samples',
  'GVMRuntime_ThreeSamples', 'licenses', '.github']);
const rootFiles = new Set(['.gitignore', '.gitattributes', 'CMakeLists.txt', 'main.cpp',
  'LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'README.md']);
const failures = [];
const privatePaths = /(?:^|\/)(?:GVMRuntime_AdvancedSamples|\.claude|\.codex_backups|\.vscode|\.idea|\.cache|\.local|node_modules|UGLBin|aibackup|build(?:[-_][^/]*)?)\//u;
const localArtifacts = /(?:^|\/)(?:\.DS_Store|imgui\.ini|AGENTS\.md|CLAUDE\.md)$|\.(?:zip|7z|bak|orig|log|o|a|dylib|dll|exe)$/iu;
const personalDirectory = /(?:\/Users|\/home)\/[A-Za-z0-9_.-]+\/|[A-Za-z]:[\\/]Users[\\/][A-Za-z0-9_.-]+[\\/]/u;
const credential = /-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|\bAKIA[0-9A-Z]{16}\b|\bgh[pousr]_[A-Za-z0-9]{30,}\b|\bsk-(?:proj-|ant-)?[A-Za-z0-9_-]{32,}\b/u;

for (const file of files) {
  const root = file.split('/')[0];
  const basename = path.basename(file);
  const legal = file.startsWith('licenses/') || /^(?:LICENSE|NOTICE)(?:\.|$)/u.test(basename) || file === 'THIRD_PARTY_NOTICES.md';
  if ((!roots.has(root) && !rootFiles.has(file)) || privatePaths.test(file) || localArtifacts.test(file) ||
      (!legal && file !== 'README.md' && /\.(?:md|rst|adoc)$/iu.test(file))) failures.push(`${file}: excluded distribution input`);
  const content = readFileSync(file).toString('utf8');
  if (personalDirectory.test(content)) failures.push(`${file}: personal directory`);
  if (credential.test(content)) failures.push(`${file}: credential signature`);
}
for (const file of ['LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'README.md', '.gitignore', '.gitattributes']) {
  if (!files.includes(file)) failures.push(`${file}: required distribution file is not tracked`);
}
if (!files.length) failures.push('The Git index contains no distribution files.');
if (failures.length) {
  console.error(failures.join('\n'));
  process.exitCode = 1;
} else {
  console.log(`Verified ${files.length} tracked distribution files.`);
}
