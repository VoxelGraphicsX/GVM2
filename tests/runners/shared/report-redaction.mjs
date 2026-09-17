import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { StringDecoder } from 'node:string_decoder';

/** Buffers complete UTF-8 lines so private values split across writes cannot escape redaction. */
export class RedactedText {
  /** Sends sanitized lines to emit; oversized diagnostic lines are omitted with an explicit marker. */
  constructor(redact, emit) {
    this.redact = redact;
    this.emit = emit;
    this.decoder = new StringDecoder('utf8');
    this.pending = '';
    this.discarding = false;
  }

  /** Accepts bytes or text while retaining at most 64 KiB of an incomplete line. */
  write(chunk) {
    this.pending += this.decoder.write(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
    let newline;
    while ((newline = this.pending.indexOf('\n')) >= 0) {
      const line = this.pending.slice(0, newline + 1);
      this.pending = this.pending.slice(newline + 1);
      if (!this.discarding) this.emit(Buffer.byteLength(line) <= 64 * 1024
        ? this.redact(line) : '[diagnostic line omitted: exceeds 64 KiB redaction limit]\n');
      this.discarding = false;
    }
    if (this.discarding || Buffer.byteLength(this.pending) > 64 * 1024) {
      if (!this.discarding) this.emit('[diagnostic line omitted: exceeds 64 KiB redaction limit]\n');
      this.pending = '';
      this.discarding = true;
    }
  }

  /** Flushes a final unterminated line after the producer closes. */
  end() {
    this.pending += this.decoder.end();
    if (!this.discarding && this.pending) this.emit(this.redact(this.pending));
    this.pending = '';
  }
}

/** Removes local machine identity from diagnostic reports without changing executable inputs or image bytes. */
export class ReportRedactor {
  /** Registers local roots longest first; injected identity values are supported for deterministic tests. */
  constructor(sourceRoot, buildRoot, { home = os.homedir(), temporary = os.tmpdir(), hostname = os.hostname() } = {}) {
    this.rules = [];
    for (const [root, token] of [[sourceRoot, '<Source>'], [buildRoot, '<Build>'], [home, '<Home>'], [temporary, '<Temp>']]) {
      if (!root) continue;
      const normalized = root.replaceAll('\\', '/').replace(/\/$/u, '');
      const forms = new Set([root, normalized, normalized.replaceAll('/', '\\'), encodeURI(normalized)]);
      if (normalized.startsWith('/var/')) forms.add(`/private${normalized}`);
      for (const form of [...forms]) forms.add(JSON.stringify(form).slice(1, -1));
      for (const form of forms) if (form.length > 1) this.rules.push([form, token]);
    }
    this.rules.sort((left, right) => right[0].length - left[0].length);
    const escaped = hostname.replace(/[.*+?^${}()|[\]\\]/gu, '\\$&');
    this.hostname = hostname ? new RegExp(`(?<![\\w-])${escaped}(?![\\w-])`, 'giu') : null;
  }

  /** Sanitizes paths, temporary user identifiers and the machine hostname while preserving relative artifact links. */
  redact(value) {
    let text = String(value);
    for (const [root, token] of this.rules) text = text.replaceAll(root, token);
    text = text.replace(/\/(?:Users|home)\/[^/\s"'<>\\]+/gu, '<Home>')
      .replace(/[A-Za-z]:[\\/]+Users[\\/]+[^\\/\s"'<>]+/gu, '<Home>')
      .replace(/\/(?:private\/)?var\/folders\/[^/\s"'<>]+\/[^/\s"'<>]+/gu, '<Temp>');
    return this.hostname ? text.replace(this.hostname, '<Host>') : text;
  }

  /** Sanitizes JSON strings and keys without changing numbers, booleans or escaped string syntax. */
  redactObject(value) {
    if (typeof value === 'string') return this.redact(value);
    if (Array.isArray(value)) return value.map(entry => this.redactObject(entry));
    if (value && typeof value === 'object') return Object.fromEntries(Object.entries(value)
      .map(([key, entry]) => [this.redact(key), this.redactObject(entry)]));
    return value;
  }

  /** Sanitizes report-owned text attachments only; binary files and symbolic links are left untouched. */
  async redactDirectory(directory) {
    if (!fs.existsSync(directory)) return;
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) await this.redactDirectory(file);
      else if (entry.isFile() && /\.(?:json|html|xml|txt|log(?:\.[12])?|[ch](?:pp)?|m(?:m|sl)?|metal|hlsl|glsl|vert|frag|cmake|abc|csv|spvasm)$/iu.test(entry.name)) {
        const bytes = fs.readFileSync(file);
        if (bytes.includes(0)) continue;
        const original = bytes.toString('utf8');
        let redacted;
        if (entry.name.endsWith('.json')) {
          try { redacted = JSON.stringify(this.redactObject(JSON.parse(original)), null, 2); }
          catch { redacted = this.redact(original); }
        } else redacted = this.redact(original);
        if (redacted !== original) fs.writeFileSync(file, redacted);
      }
    }
  }
}
