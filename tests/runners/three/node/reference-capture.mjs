import { spawn } from 'node:child_process';
import { execFile } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
import { constants as fsConstants, promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { promisify } from 'node:util';
import { inflateSync } from 'node:zlib';

import { makeLockedOraclePaths } from '../../../../GVMRuntime_ThreeSamples/Tools/asset_oracle_lock.mjs';
import {
  createExternalAssetCaptureResponse,
  loadExternalAssetMap,
  makeExternalAssetMapLockRecord
} from '../../../../GVMRuntime_ThreeSamples/Tools/external_asset_map.mjs';
import { defaultThreeRandomSeed, zeroThreeRandomSeedState } from './determinism.mjs';

const execFileAsync = promisify(execFile);
const THREE_R185_COMMIT = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const DEFAULT_FRAME_STEP_MS = 1000 / 60;
const ORACLE_WIDTH = 800;
const ORACLE_HEIGHT = 500;
const INPUT_REPLAY_POINTER_EVENTS = new Set([
  'pointerdown',
  'pointerleave',
  'pointermove',
  'pointerup'
]);
const INPUT_REPLAY_MOUSE_EVENTS = new Set(['click', 'mousemove']);
const INPUT_REPLAY_FORM_EVENTS = new Set(['change', 'input']);
const INPUT_REPLAY_KEYBOARD_EVENTS = new Set(['keydown', 'keyup']);

const MIME_TYPES = new Map([
  ['.avif', 'image/avif'],
  ['.bin', 'application/octet-stream'],
  ['.css', 'text/css; charset=utf-8'],
  ['.gif', 'image/gif'],
  ['.glb', 'model/gltf-binary'],
  ['.gltf', 'model/gltf+json'],
  ['.html', 'text/html; charset=utf-8'],
  ['.jpeg', 'image/jpeg'],
  ['.jpg', 'image/jpeg'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.ktx2', 'image/ktx2'],
  ['.mjs', 'text/javascript; charset=utf-8'],
  ['.mp3', 'audio/mpeg'],
  ['.ogg', 'audio/ogg'],
  ['.png', 'image/png'],
  ['.svg', 'image/svg+xml'],
  ['.wasm', 'application/wasm'],
  ['.wav', 'audio/wav'],
  ['.webm', 'video/webm'],
  ['.webp', 'image/webp'],
  ['.woff', 'font/woff'],
  ['.woff2', 'font/woff2']
]);

/** Returns a promise that resolves after a bounded real-time delay. */
function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

/** Reads one unsigned big-endian 32-bit PNG field. */
function readUint32(bytes, offset) {
  return bytes.readUInt32BE(offset);
}

/** Calculates the PNG Paeth predictor for one reconstructed channel byte. */
function paethPredictor(left, above, upperLeft) {
  const prediction = left + above - upperLeft;
  const leftDistance = Math.abs(prediction - left);
  const aboveDistance = Math.abs(prediction - above);
  const upperLeftDistance = Math.abs(prediction - upperLeft);
  if (leftDistance <= aboveDistance && leftDistance <= upperLeftDistance) {
    return left;
  }
  return aboveDistance <= upperLeftDistance ? above : upperLeft;
}

/** Decodes one non-interlaced 8-bit RGB/RGBA PNG into top-down RGBA8 bytes. */
export function decodePngRgba(pngBytes) {
  const bytes = Buffer.isBuffer(pngBytes) ? pngBytes : Buffer.from(pngBytes);
  const expectedSignature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  if (bytes.length < expectedSignature.length || !bytes.subarray(0, 8).equals(expectedSignature)) {
    throw new Error('Reference screenshot is not a PNG file.');
  }

  let width = 0;
  let height = 0;
  let bitDepth = 0;
  let colorType = -1;
  let compressionMethod = -1;
  let filterMethod = -1;
  let interlaceMethod = -1;
  const imageDataChunks = [];
  for (let offset = 8; offset + 12 <= bytes.length;) {
    const length = readUint32(bytes, offset);
    const type = bytes.toString('ascii', offset + 4, offset + 8);
    const dataOffset = offset + 8;
    const nextOffset = dataOffset + length + 4;
    if (nextOffset > bytes.length) {
      throw new Error(`PNG chunk '${type}' escapes the screenshot payload.`);
    }
    if (type === 'IHDR') {
      if (length !== 13) {
        throw new Error(`PNG IHDR has ${length} bytes; expected 13.`);
      }
      width = readUint32(bytes, dataOffset);
      height = readUint32(bytes, dataOffset + 4);
      bitDepth = bytes[dataOffset + 8];
      colorType = bytes[dataOffset + 9];
      compressionMethod = bytes[dataOffset + 10];
      filterMethod = bytes[dataOffset + 11];
      interlaceMethod = bytes[dataOffset + 12];
    } else if (type === 'IDAT') {
      imageDataChunks.push(bytes.subarray(dataOffset, dataOffset + length));
    } else if (type === 'IEND') {
      break;
    }
    offset = nextOffset;
  }

  if (width <= 0 || height <= 0 || imageDataChunks.length === 0) {
    throw new Error('PNG screenshot is missing IHDR dimensions or IDAT data.');
  }
  if (bitDepth !== 8 || (colorType !== 2 && colorType !== 6)
    || compressionMethod !== 0 || filterMethod !== 0 || interlaceMethod !== 0) {
    throw new Error(`Unsupported PNG encoding: bitDepth=${bitDepth} colorType=${colorType} compression=${compressionMethod} filter=${filterMethod} interlace=${interlaceMethod}.`);
  }

  const sourceChannelCount = colorType === 6 ? 4 : 3;
  const sourceStride = width * sourceChannelCount;
  const filtered = inflateSync(Buffer.concat(imageDataChunks));
  const expectedFilteredBytes = height * (sourceStride + 1);
  if (filtered.length !== expectedFilteredBytes) {
    throw new Error(`PNG scanline payload has ${filtered.length} bytes; expected ${expectedFilteredBytes}.`);
  }

  const reconstructed = Buffer.alloc(sourceStride * height);
  for (let y = 0; y < height; y += 1) {
    const filteredRowOffset = y * (sourceStride + 1);
    const filterType = filtered[filteredRowOffset];
    const sourceOffset = filteredRowOffset + 1;
    const targetOffset = y * sourceStride;
    for (let x = 0; x < sourceStride; x += 1) {
      const rawValue = filtered[sourceOffset + x];
      const left = x >= sourceChannelCount ? reconstructed[targetOffset + x - sourceChannelCount] : 0;
      const above = y > 0 ? reconstructed[targetOffset + x - sourceStride] : 0;
      const upperLeft = y > 0 && x >= sourceChannelCount
        ? reconstructed[targetOffset + x - sourceStride - sourceChannelCount]
        : 0;
      let predictor = 0;
      if (filterType === 1) predictor = left;
      else if (filterType === 2) predictor = above;
      else if (filterType === 3) predictor = Math.floor((left + above) / 2);
      else if (filterType === 4) predictor = paethPredictor(left, above, upperLeft);
      else if (filterType !== 0) throw new Error(`Unsupported PNG row filter ${filterType}.`);
      reconstructed[targetOffset + x] = (rawValue + predictor) & 0xff;
    }
  }

  const rgba = new Uint8Array(width * height * 4);
  for (let pixelIndex = 0; pixelIndex < width * height; pixelIndex += 1) {
    const sourceOffset = pixelIndex * sourceChannelCount;
    const targetOffset = pixelIndex * 4;
    rgba[targetOffset] = reconstructed[sourceOffset];
    rgba[targetOffset + 1] = reconstructed[sourceOffset + 1];
    rgba[targetOffset + 2] = reconstructed[sourceOffset + 2];
    rgba[targetOffset + 3] = sourceChannelCount === 4 ? reconstructed[sourceOffset + 3] : 255;
  }
  return { width, height, pixels: rgba };
}

/** Builds the pre-navigation virtual frame scheduler shared by every reference case. */
export function makeDeterministicFrameBootstrap(
  frameStepMs = DEFAULT_FRAME_STEP_MS,
  randomSeed = defaultThreeRandomSeed
) {
  if (!Number.isFinite(frameStepMs) || frameStepMs <= 0) {
    throw new Error(`frameStepMs must be positive; received '${frameStepMs}'.`);
  }
  if (!Number.isInteger(randomSeed) || randomSeed < 0 || randomSeed > 0xffffffff) {
    throw new Error(`randomSeed must be an unsigned 32-bit integer; received '${randomSeed}'.`);
  }
  return `(() => {
    const realSetTimeout = window.setTimeout.bind(window);
    const callbacks = new Map();
    let nextCallbackId = 1;
    let virtualTimeMs = 0;
    let lastRenderedTimeMs = null;
    const initialRandomSeed = ${randomSeed >>> 0};
    let randomState = initialRandomSeed || ${zeroThreeRandomSeedState};
    const frameStepMs = ${JSON.stringify(frameStepMs)};
    window.requestAnimationFrame = (callback) => {
      const callbackId = nextCallbackId++;
      callbacks.set(callbackId, callback);
      return callbackId;
    };
    window.cancelAnimationFrame = (callbackId) => callbacks.delete(callbackId);
    try {
      Object.defineProperty(performance, 'now', { configurable: true, value: () => virtualTimeMs });
    } catch {}
    const epochMs = 1700000000000;
    try {
      Date.now = () => epochMs + virtualTimeMs;
    } catch {}
    Math.random = () => {
      let value = randomState >>> 0;
      value ^= value << 13;
      value ^= value >>> 17;
      value ^= value << 5;
      randomState = value >>> 0;
      return (randomState >>> 8) / 16777216;
    };
    window.__gvmReferenceClock = {
      get initialRandomSeed() { return initialRandomSeed; },
      get randomState() { return randomState; },
      get pendingFrameCount() { return callbacks.size; },
      get virtualTimeMs() { return virtualTimeMs; },
      get lastRenderedTimeMs() { return lastRenderedTimeMs; },
      async advanceFrames(frameCount) {
        if (!Number.isInteger(frameCount) || frameCount < 0) throw new Error('frameCount must be a non-negative integer.');
        for (let frameIndex = 0; frameIndex < frameCount; frameIndex += 1) {
          const frameCallbacks = Array.from(callbacks.values());
          callbacks.clear();
          for (const callback of frameCallbacks) callback(virtualTimeMs);
          await Promise.resolve();
          await new Promise((resolve) => realSetTimeout(resolve, 0));
          lastRenderedTimeMs = virtualTimeMs;
          virtualTimeMs += frameStepMs;
        }
        return {
          randomState,
          virtualTimeMs,
          lastRenderedTimeMs,
          pendingFrameCount: callbacks.size
        };
      }
    };
  })();`;
}

/** Validates and normalizes one deterministic pointer, keyboard, and wheel replay document. */
export function validateInputReplayDocument(document) {
  if (document == null || typeof document !== 'object' || Array.isArray(document)) {
    throw new Error('Input replay must be a JSON object.');
  }
  if (document.schemaVersion !== 1) {
    throw new Error(`Input replay schemaVersion must be 1; received '${document.schemaVersion}'.`);
  }
  if (typeof document.target !== 'string' || document.target.trim() === '') {
    throw new Error('Input replay target must be a non-empty CSS selector.');
  }
  if (!Array.isArray(document.events) || document.events.length === 0) {
    throw new Error('Input replay events must be a non-empty array.');
  }
  if (document.events.length > 10_000) {
    throw new Error(`Input replay contains ${document.events.length} events; the limit is 10000.`);
  }

  const normalizeOptionalCanvasExtent = (value, label) => {
    if (value == null) return null;
    if (!Number.isInteger(value) || value < 1) {
      throw new Error(`Input replay ${label} must be a positive integer; received '${value}'.`);
    }
    return value;
  };
  const canvasWidth = normalizeOptionalCanvasExtent(document.canvasWidth, 'canvasWidth');
  const canvasHeight = normalizeOptionalCanvasExtent(document.canvasHeight, 'canvasHeight');
  const normalizeOptionalIdentity = (value, label) => {
    if (value == null) return null;
    if (typeof value !== 'string' || value.trim() === '') {
      throw new Error(`Input replay ${label} must be a non-empty string when declared.`);
    }
    return value.trim();
  };
  const caseId = normalizeOptionalIdentity(document.caseId, 'caseId');
  const scenarioId = normalizeOptionalIdentity(document.scenarioId, 'scenarioId');
  const captureFrame = document.frame ?? document.captureFrame ?? null;
  if (captureFrame != null && (!Number.isInteger(captureFrame) || captureFrame < 0)) {
    throw new Error(`Input replay frame must be a non-negative integer; received '${captureFrame}'.`);
  }
  const events = [];
  let previousFrame = 0;
  for (let eventIndex = 0; eventIndex < document.events.length; eventIndex += 1) {
    const event = document.events[eventIndex];
    const label = `Input replay event ${eventIndex}`;
    if (event == null || typeof event !== 'object' || Array.isArray(event)) {
      throw new Error(`${label} must be an object.`);
    }
    const isPointer = INPUT_REPLAY_POINTER_EVENTS.has(event.type);
    const isMouse = INPUT_REPLAY_MOUSE_EVENTS.has(event.type);
    const isForm = INPUT_REPLAY_FORM_EVENTS.has(event.type);
    const isKeyboard = INPUT_REPLAY_KEYBOARD_EVENTS.has(event.type);
    const isWheel = event.type === 'wheel';
    if (!isPointer && !isMouse && !isForm && !isKeyboard && !isWheel) {
      throw new Error(`${label} type '${event.type}' is unsupported.`);
    }
    const frame = event.frame ?? 0;
    if (!Number.isInteger(frame) || frame < 0) {
      throw new Error(`${label} frame must be a non-negative integer; received '${frame}'.`);
    }
    if (frame < previousFrame) {
      throw new Error(`${label} frame ${frame} precedes the prior event frame ${previousFrame}.`);
    }
    previousFrame = frame;
    if (event.target != null && (typeof event.target !== 'string' || event.target.trim() === '')) {
      throw new Error(`${label} target must be a non-empty CSS selector when declared.`);
    }
    if (isPointer || isMouse || isWheel) {
      if (!Number.isFinite(event.x) || !Number.isFinite(event.y) || event.x < 0 || event.y < 0) {
        throw new Error(`${label} x/y must be finite, non-negative target-relative coordinates.`);
      }
      if (canvasWidth != null && event.x > canvasWidth) {
        throw new Error(`${label} x=${event.x} exceeds canvasWidth=${canvasWidth}.`);
      }
      if (canvasHeight != null && event.y > canvasHeight) {
        throw new Error(`${label} y=${event.y} exceeds canvasHeight=${canvasHeight}.`);
      }
      const settleMs = event.settleMs ?? 0;
      if (!Number.isInteger(settleMs) || settleMs < 0 || settleMs > 5000) {
        throw new Error(`${label} settleMs must be an integer from 0 through 5000.`);
      }
    }
    const modifiers = {};
    for (const modifier of ['altKey', 'ctrlKey', 'metaKey', 'shiftKey']) {
      if (event[modifier] != null && typeof event[modifier] !== 'boolean') {
        throw new Error(`${label} ${modifier} must be boolean when declared.`);
      }
      modifiers[modifier] = event[modifier] ?? false;
    }
    if (isPointer) {
      const pointerId = event.pointerId ?? 1;
      if (!Number.isInteger(pointerId) || pointerId < 1) {
        throw new Error(`${label} pointerId must be a positive integer; received '${pointerId}'.`);
      }
      const button = event.button
        ?? (event.type === 'pointermove' || event.type === 'pointerleave' ? -1 : 0);
      if (!Number.isInteger(button) || button < -1 || button > 2) {
        throw new Error(`${label} button must be -1, 0, 1, or 2; received '${button}'.`);
      }
      events.push({
        type: event.type,
        frame,
        ...(event.target == null ? {} : { target: event.target.trim() }),
        x: event.x,
        y: event.y,
        pointerId,
        button,
        settleMs: event.settleMs ?? 0,
        ...modifiers
      });
    } else if (isMouse) {
      events.push({
        type: event.type,
        frame,
        ...(event.target == null ? {} : { target: event.target.trim() }),
        x: event.x,
        y: event.y,
        settleMs: event.settleMs ?? 0,
        ...modifiers
      });
    } else if (isForm) {
      if (typeof event.value !== 'string') {
        throw new Error(`${label} form value must be a string.`);
      }
      const settleMs = event.settleMs ?? 0;
      if (!Number.isInteger(settleMs) || settleMs < 0 || settleMs > 5000) {
        throw new Error(`${label} form settleMs must be an integer from 0 through 5000.`);
      }
      events.push({
        type: event.type,
        frame,
        ...(event.target == null ? {} : { target: event.target.trim() }),
        value: event.value,
        settleMs
      });
    } else if (isKeyboard) {
      if (typeof event.key !== 'string' || event.key.length === 0
        || typeof event.code !== 'string' || event.code.length === 0) {
        throw new Error(`${label} keyboard key and code must be non-empty strings.`);
      }
      const location = event.location ?? 0;
      if (!Number.isInteger(location) || location < 0 || location > 3) {
        throw new Error(`${label} keyboard location must be an integer from 0 through 3.`);
      }
      if (event.repeat != null && typeof event.repeat !== 'boolean') {
        throw new Error(`${label} keyboard repeat must be boolean when declared.`);
      }
      events.push({
        type: event.type,
        frame,
        key: event.key,
        code: event.code,
        location,
        repeat: event.repeat ?? false,
        ...modifiers
      });
    } else {
      const deltaX = event.deltaX ?? 0;
      const deltaY = event.deltaY ?? 0;
      const deltaZ = event.deltaZ ?? 0;
      const deltaMode = event.deltaMode ?? 0;
      if (![deltaX, deltaY, deltaZ].every(Number.isFinite)
        || !Number.isInteger(deltaMode) || deltaMode < 0 || deltaMode > 2) {
        throw new Error(`${label} wheel delta values and deltaMode are invalid.`);
      }
      events.push({
        type: 'wheel', frame, x: event.x, y: event.y,
        ...(event.target == null ? {} : { target: event.target.trim() }),
        deltaX, deltaY, deltaZ, deltaMode,
        settleMs: event.settleMs ?? 0,
        ...modifiers
      });
    }
  }

  return {
    schemaVersion: 1,
    caseId,
    scenarioId,
    frame: captureFrame,
    target: document.target.trim(),
    canvasWidth,
    canvasHeight,
    events
  };
}

/** Returns true unless Chrome canceled a request after receiving a successful response. */
export function isMaterialFailedReferenceRequest(request) {
  return request.canceled !== true
    || request.receivedSuccessfulResponse !== true;
}

/** Builds an in-page replay that interleaves input events with deterministic frame callbacks. */
export function makeInputReplayScript(inputReplay, totalFrameCount) {
  const replay = validateInputReplayDocument(inputReplay);
  if (!Number.isInteger(totalFrameCount) || totalFrameCount < 1) {
    throw new Error(`Input replay totalFrameCount must be positive; received '${totalFrameCount}'.`);
  }
  const finalCaptureFrame = totalFrameCount - 1;
  const eventBeyondCapture = replay.events.find((event) => event.frame > finalCaptureFrame);
  if (eventBeyondCapture) {
    throw new Error(`Input replay event frame ${eventBeyondCapture.frame} exceeds capture frame ${finalCaptureFrame}.`);
  }
  const serializedReplay = JSON.stringify(replay)
    .replaceAll('\u2028', '\\u2028')
    .replaceAll('\u2029', '\\u2029');
  return `(async () => {
    const replay = ${serializedReplay};
    const targets = document.querySelectorAll(replay.target);
    if (targets.length !== 1) throw new Error('Input replay target selector must match exactly one element; matched ' + targets.length + '.');
    const target = targets[0];
    if (replay.canvasWidth !== null || replay.canvasHeight !== null) {
      if (!(target instanceof HTMLCanvasElement)) throw new Error('Input replay canvas dimensions require an HTMLCanvasElement target.');
      if (replay.canvasWidth !== null && target.width !== replay.canvasWidth) {
        throw new Error('Input replay target backing width is ' + target.width + '; expected ' + replay.canvasWidth + '.');
      }
      if (replay.canvasHeight !== null && target.height !== replay.canvasHeight) {
        throw new Error('Input replay target backing height is ' + target.height + '; expected ' + replay.canvasHeight + '.');
      }
    }
    const clock = window.__gvmReferenceClock;
    let advancedFrameCount = 0;
    const pressedPointerButtons = new Map();
    let dispatchedEventCount = 0;
    for (const event of replay.events) {
      const framesBeforeEvent = event.frame - advancedFrameCount;
      if (framesBeforeEvent > 0) {
        await clock.advanceFrames(framesBeforeEvent);
        advancedFrameCount += framesBeforeEvent;
      }
      const isPointer = event.type.startsWith('pointer');
      const isMouse = event.type === 'click' || event.type === 'mousemove';
      const isForm = event.type === 'change' || event.type === 'input';
      if (isPointer && event.type === 'pointerdown') {
        pressedPointerButtons.set(event.pointerId, event.button);
      }
      const pointerPressed = isPointer && pressedPointerButtons.has(event.pointerId);
      const pressedButton = pointerPressed ? pressedPointerButtons.get(event.pointerId) : event.button;
      const pressedButtons = pointerPressed ? 1 << pressedButton : 0;
      // A replay may use one stable canvas target for pointer coordinates while
      // dispatching a consent or GUI event to a different DOM control.
      const eventTarget = event.target == null
        ? target
        : (() => {
          const eventTargets = document.querySelectorAll(event.target);
          if (eventTargets.length !== 1) {
            throw new Error('Input replay event target selector must match exactly one element; matched ' + eventTargets.length + '.');
          }
          return eventTargets[0];
        })();
      const rect = eventTarget.getBoundingClientRect();
      // Consent and GUI controls can be zero-sized or temporarily detached
      // while a replay is advancing the page.  A DOM click still has a valid
      // target in that state; use a finite origin instead of passing NaN to
      // the MouseEvent constructor.
      const rectLeft = Number.isFinite(rect.left) ? rect.left : 0;
      const rectTop = Number.isFinite(rect.top) ? rect.top : 0;
      const rectWidth = Number.isFinite(rect.width) ? rect.width : 0;
      const rectHeight = Number.isFinite(rect.height) ? rect.height : 0;
      const coordinateWidth = replay.canvasWidth ?? rectWidth;
      const coordinateHeight = replay.canvasHeight ?? rectHeight;
      const hasCoordinates = Number.isFinite(event.x) && Number.isFinite(event.y);
      const clientX = hasCoordinates && coordinateWidth > 0
        ? rectLeft + event.x * rectWidth / coordinateWidth : rectLeft;
      const clientY = hasCoordinates && coordinateHeight > 0
        ? rectTop + event.y * rectHeight / coordinateHeight : rectTop;
      let replayEvent;
      if (isPointer) {
        replayEvent = new PointerEvent(event.type, {
          bubbles: true, cancelable: true, composed: true, view: window,
          pointerId: event.pointerId, pointerType: 'mouse', isPrimary: event.pointerId === 1,
          button: event.type === 'pointerdown' || event.type === 'pointerup' ? event.button : -1,
          buttons: event.type === 'pointerup' || event.type === 'pointerleave' ? 0 : pressedButtons,
          clientX, clientY, screenX: clientX, screenY: clientY,
          pressure: pointerPressed && event.type !== 'pointerup' ? 0.5 : 0,
          altKey: event.altKey, ctrlKey: event.ctrlKey, metaKey: event.metaKey, shiftKey: event.shiftKey
        });
      } else if (isMouse) {
        replayEvent = new MouseEvent(event.type, {
          bubbles: true, cancelable: true, composed: true, view: window,
          button: 0, buttons: 0,
          clientX, clientY, screenX: clientX, screenY: clientY,
          altKey: event.altKey, ctrlKey: event.ctrlKey, metaKey: event.metaKey, shiftKey: event.shiftKey
        });
      } else if (isForm) {
        if (!('value' in eventTarget)) throw new Error('Input replay change target does not expose a value.');
        eventTarget.value = event.value;
        replayEvent = new Event(event.type, {
          bubbles: true, cancelable: true, composed: true
        });
      } else if (event.type === 'wheel') {
        replayEvent = new WheelEvent('wheel', {
          bubbles: true, cancelable: true, composed: true, view: window,
          clientX, clientY, screenX: clientX, screenY: clientY,
          deltaX: event.deltaX, deltaY: event.deltaY, deltaZ: event.deltaZ, deltaMode: event.deltaMode,
          altKey: event.altKey, ctrlKey: event.ctrlKey, metaKey: event.metaKey, shiftKey: event.shiftKey
        });
      } else {
        if (typeof eventTarget.focus === 'function') eventTarget.focus({ preventScroll: true });
        replayEvent = new KeyboardEvent(event.type, {
          bubbles: true, cancelable: true, composed: true, view: window,
          key: event.key, code: event.code, location: event.location, repeat: event.repeat,
          altKey: event.altKey, ctrlKey: event.ctrlKey, metaKey: event.metaKey, shiftKey: event.shiftKey
        });
        const legacyKeyCode = event.code.startsWith('Key') && event.code.length === 4
          ? event.code.charCodeAt(3)
          : event.code.startsWith('Digit') && event.code.length === 6
            ? event.code.charCodeAt(5)
            : ({ Enter: 13, Escape: 27, Space: 32, ArrowLeft: 37,
              ArrowUp: 38, ArrowRight: 39, ArrowDown: 40 })[event.code] ?? 0;
        Object.defineProperties(replayEvent, {
          keyCode: { value: legacyKeyCode },
          which: { value: legacyKeyCode }
        });
      }
      eventTarget.dispatchEvent(replayEvent);
      if (event.settleMs > 0) {
        await new Promise((resolve) => setTimeout(resolve, event.settleMs));
      }
      dispatchedEventCount += 1;
      if (isPointer && (event.type === 'pointerup' || event.type === 'pointerleave')) {
        pressedPointerButtons.delete(event.pointerId);
      }
    }
    const remainingFrameCount = ${totalFrameCount} - advancedFrameCount;
    if (remainingFrameCount > 0) await clock.advanceFrames(remainingFrameCount);
    const rect = target.getBoundingClientRect();
    return {
      clock: {
      randomState: clock.randomState,
      virtualTimeMs: clock.virtualTimeMs,
        lastRenderedTimeMs: clock.lastRenderedTimeMs,
        pendingFrameCount: clock.pendingFrameCount
      },
      replay: {
        target: replay.target,
        dispatchedEventCount,
        cssWidth: rect.width,
        cssHeight: rect.height
      }
    };
  })()`;
}

/** Builds the renderer-surface isolation script and removes Inspector chrome that must never enter an image oracle. */
export function makeReferenceSurfaceIsolationScript() {
  return `(() => {
    for (const element of document.querySelectorAll('.profiler-toggle, .profiler-mini-panel, .profiler-panel, .detached-tab-panel')) {
      element.remove();
    }
    const style = document.createElement('style');
    style.setAttribute('data-gvm-reference-capture', '');
    style.textContent = 'body * { visibility: hidden !important; } [data-gvm-reference-surface], [data-gvm-reference-surface] * { visibility: visible !important; }';
    document.head.appendChild(style);
  })()`;
}

/** Resolves one URL pathname while keeping source files and packed assets in separate roots. */
export async function resolveStaticFile(upstreamRoot, assetPackRoot, pathname) {
  let decoded;
  try {
    decoded = decodeURIComponent(pathname);
  } catch {
    return null;
  }
  const normalized = path.posix.normalize(decoded).replace(/^\/+/, '');
  if (normalized === '' || normalized === '.' || normalized.startsWith('../')) {
    return null;
  }
  const extension = path.posix.extname(normalized).toLowerCase();
  const isPinnedSource = normalized.startsWith('build/')
    || normalized.startsWith('examples/jsm/')
    || ['.css', '.html', '.js', '.mjs'].includes(extension);
  const candidates = isPinnedSource
    ? [path.join(upstreamRoot, ...normalized.split('/'))]
    : [path.join(
        assetPackRoot,
        ...(normalized.startsWith('examples/')
          ? normalized.slice('examples/'.length)
          : normalized).split('/')
      )];
  for (const candidate of candidates) {
    const absolute = path.resolve(candidate);
    for (const root of isPinnedSource ? [upstreamRoot] : [assetPackRoot]) {
      const absoluteRoot = path.resolve(root);
      if (absolute !== absoluteRoot && !absolute.startsWith(`${absoluteRoot}${path.sep}`)) {
        continue;
      }
      try {
        const statistics = await fs.stat(absolute);
        if (statistics.isFile()) return absolute;
      } catch (error) {
        if (error.code !== 'ENOENT') throw error;
      }
    }
  }
  return null;
}

/** Returns whether one CDP request must participate in network-idle tracking. */
export function shouldTrackReferenceRequest(requestUrl) {
  return !requestUrl.startsWith('about:')
    && !requestUrl.startsWith('blob:')
    && !requestUrl.startsWith('data:');
}

/**
 * Rewrites incidental renderer and render-target multisampling requests so the
 * pinned Three.js reference uses the same single-sample contract as GVM.
 */
export function rewriteReferenceHtmlForSingleSample(source) {
  let rendererAntialiasOverrides = 0;
  let renderTargetSampleOverrides = 0;
  let rewritten = source.replace(
    /(\bantialias\s*:\s*)true\b/gu,
    (_match, prefix) => {
      rendererAntialiasOverrides += 1;
      return `${prefix}false`;
    }
  );
  rewritten = rewritten.replace(
    /(\.\s*samples\s*=\s*)([^;\r\n]+)/gu,
    (_match, prefix, value) => {
      if (/^\s*0(?:\s*)$/u.test(value)) return `${prefix}${value}`;
      renderTargetSampleOverrides += 1;
      return `${prefix}0`;
    }
  );
  rewritten = rewritten.split(/(?<=\n)/u).map((line) => {
    if (!/\bnew\s+(?:THREE\.)?(?:WebGL)?(?:Multiple)?RenderTarget\s*\(/u.test(line)) {
      return line;
    }
    return line.replace(
      /(\bsamples\s*:\s*)([1-9]\d*)\b/gu,
      (_match, prefix) => {
        renderTargetSampleOverrides += 1;
        return `${prefix}0`;
      }
    );
  }).join('');
  return {
    source: rewritten,
    rendererAntialiasOverrides,
    renderTargetSampleOverrides
  };
}

/**
 * Injects a canonical-state hook into the last module script so the hook can
 * access module-local example state after asynchronous loaders have completed.
 */
export function injectModuleScopeStateHook(source, stateScript) {
  const moduleScriptPattern =
    /<script\b(?=[^>]*\btype\s*=\s*["']module["'])[^>]*>[\s\S]*?<\/script\s*>/giu;
  const matches = [...source.matchAll(moduleScriptPattern)];
  if (matches.length === 0) {
    throw new Error('Canonical module state requires an inline <script type="module"> block.');
  }
  const target = matches.at(-1);
  const targetSource = target[0];
  const closingOffset = targetSource.toLowerCase().lastIndexOf('</script');
  if (closingOffset < 0) {
    throw new Error('Canonical module state could not locate the module script closing tag.');
  }
  const hookSource = `

window.__gvmApplyModuleCanonicalState = async () => {
${stateScript}
return true;
};
`;
  const insertionOffset = target.index + closingOffset;
  return `${source.slice(0, insertionOffset)}${hookSource}${source.slice(insertionOffset)}`;
}

/** Starts an ephemeral static server over the exact source and asset roots. */
async function startReferenceServer(
  upstreamRoot,
  assetPackRoot,
  caseId,
  moduleStateScript
) {
  const examplePathname = `/examples/${caseId}.html`;
  let singleSampleTransform = null;
  const server = createServer(async (request, response) => {
    try {
      const requestUrl = new URL(request.url ?? '/', 'http://127.0.0.1');
      if (requestUrl.pathname === '/favicon.ico') {
        response.writeHead(204, { 'cache-control': 'no-store' });
        response.end();
        return;
      }
      const filePath = await resolveStaticFile(upstreamRoot, assetPackRoot, requestUrl.pathname);
      if (!filePath) {
        response.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' });
        response.end('Not found');
        return;
      }
      let bytes = await fs.readFile(filePath);
      if (requestUrl.pathname === examplePathname) {
        const originalSource = bytes.toString('utf8');
        const transform = rewriteReferenceHtmlForSingleSample(originalSource);
        const servedSource = moduleStateScript == null
          ? transform.source
          : injectModuleScopeStateHook(transform.source, moduleStateScript.source);
        bytes = Buffer.from(servedSource, 'utf8');
        singleSampleTransform = {
          mode: 'single-sample',
          msaaEnabled: false,
          simulateMsaa: false,
          rendererAntialiasOverrides: transform.rendererAntialiasOverrides,
          renderTargetSampleOverrides: transform.renderTargetSampleOverrides,
          moduleCanonicalStateSha256: moduleStateScript?.sha256 ?? null,
          originalSourceSha256: createHash('sha256').update(originalSource).digest('hex'),
          servedSourceSha256: createHash('sha256').update(bytes).digest('hex')
        };
      }
      response.writeHead(200, {
        'cache-control': 'no-store',
        'content-length': bytes.byteLength,
        'content-type': MIME_TYPES.get(path.extname(filePath).toLowerCase()) ?? 'application/octet-stream',
        'cross-origin-opener-policy': 'same-origin',
        'cross-origin-embedder-policy': 'require-corp'
      });
      response.end(bytes);
    } catch (error) {
      response.writeHead(500, { 'content-type': 'text/plain; charset=utf-8' });
      response.end(error instanceof Error ? error.message : String(error));
    }
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const address = server.address();
  if (address == null || typeof address === 'string') {
    server.close();
    throw new Error('Reference server did not expose a TCP address.');
  }
  return {
    origin: `http://127.0.0.1:${address.port}`,
    getSingleSampleTransform: () => singleSampleTransform,
    close: () => new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()))
  };
}

/** Owns one flattened Chrome DevTools Protocol connection. */
class CdpConnection {
  constructor(webSocketUrl) {
    this.webSocket = new WebSocket(webSocketUrl);
    this.nextRequestId = 1;
    this.pendingRequests = new Map();
    this.eventListeners = new Map();
  }

  /** Opens the WebSocket connection before the first CDP request. */
  async open(timeoutMs) {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Timed out connecting to Chrome DevTools.')), timeoutMs);
      this.webSocket.addEventListener('open', () => {
        clearTimeout(timer);
        resolve();
      }, { once: true });
      this.webSocket.addEventListener('error', () => {
        clearTimeout(timer);
        reject(new Error('Chrome DevTools WebSocket failed to open.'));
      }, { once: true });
    });
    this.webSocket.addEventListener('message', (event) => this.handleMessage(event.data));
    this.webSocket.addEventListener('close', () => this.rejectPending(new Error('Chrome DevTools connection closed.')));
  }

  /** Dispatches one CDP command and returns its protocol result. */
  send(method, params = {}, sessionId = undefined) {
    const id = this.nextRequestId++;
    const message = { id, method, params };
    if (sessionId !== undefined) message.sessionId = sessionId;
    const result = new Promise((resolve, reject) => this.pendingRequests.set(id, { resolve, reject, method }));
    this.webSocket.send(JSON.stringify(message));
    return result;
  }

  /** Registers an event callback and returns a function that removes it. */
  on(method, callback) {
    const listeners = this.eventListeners.get(method) ?? new Set();
    listeners.add(callback);
    this.eventListeners.set(method, listeners);
    return () => listeners.delete(callback);
  }

  /** Waits for one matching CDP event under a hard timeout. */
  waitForEvent(method, predicate, timeoutMs) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        removeListener();
        reject(new Error(`Timed out waiting for CDP event '${method}'.`));
      }, timeoutMs);
      const removeListener = this.on(method, (message) => {
        if (!predicate(message)) return;
        clearTimeout(timer);
        removeListener();
        resolve(message.params);
      });
    });
  }

  /** Closes the protocol socket and rejects commands that never completed. */
  close() {
    if (this.webSocket.readyState === WebSocket.OPEN || this.webSocket.readyState === WebSocket.CONNECTING) {
      this.webSocket.close();
    }
    this.rejectPending(new Error('Chrome DevTools connection was closed by the capture harness.'));
  }

  /** Routes one raw protocol message to its request or event consumer. */
  handleMessage(rawMessage) {
    const message = JSON.parse(String(rawMessage));
    if (message.id !== undefined) {
      const pending = this.pendingRequests.get(message.id);
      if (!pending) return;
      this.pendingRequests.delete(message.id);
      if (message.error) {
        pending.reject(new Error(`${pending.method}: ${message.error.message}`));
      } else {
        pending.resolve(message.result ?? {});
      }
      return;
    }
    const listeners = this.eventListeners.get(message.method);
    if (!listeners) return;
    for (const listener of [...listeners]) listener(message);
  }

  /** Rejects every command still waiting on a disconnected browser. */
  rejectPending(error) {
    for (const pending of this.pendingRequests.values()) pending.reject(error);
    this.pendingRequests.clear();
  }
}

/** Starts a private Chrome process and extracts its ephemeral DevTools endpoint. */
async function launchReferenceChrome(chromePath, temporaryRoot, width, height, timeoutMs) {
  await fs.access(chromePath, fsConstants.X_OK);
  const userDataDir = path.join(temporaryRoot, 'chrome-profile');
  await fs.mkdir(userDataDir, { recursive: true });
  const argumentsList = [
    '--headless=new',
    '--remote-debugging-port=0',
    `--user-data-dir=${userDataDir}`,
    '--disable-background-networking',
    '--disable-component-update',
    '--disable-default-apps',
    '--disable-extensions',
    '--disable-features=Translate,MediaRouter,OptimizationHints',
    '--disable-sync',
    '--enable-unsafe-webgpu',
    '--enable-webgl',
    '--force-color-profile=srgb',
    '--force-device-scale-factor=1',
    '--hide-scrollbars',
    '--metrics-recording-only',
    '--no-first-run',
    '--no-default-browser-check',
    `--window-size=${width},${height}`,
    'about:blank'
  ];
  const child = spawn(chromePath, argumentsList, {
    env: process.env,
    shell: false,
    stdio: ['ignore', 'ignore', 'pipe']
  });
  let stderr = '';
  let webSocketUrl;
  try {
    webSocketUrl = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        reject(new Error(`Chrome did not publish a DevTools endpoint within ${timeoutMs} ms.`));
      }, timeoutMs);
      const finish = (error, value) => {
        clearTimeout(timer);
        child.stderr.off('data', onData);
        child.off('error', onError);
        child.off('exit', onExit);
        if (error) reject(error);
        else resolve(value);
      };
      const onData = (chunk) => {
        stderr += chunk.toString();
        const match = /DevTools listening on (ws:\/\/[^\s]+)/u.exec(stderr);
        if (match) finish(null, match[1]);
      };
      const onError = (error) => finish(error);
      const onExit = (code, signal) => finish(new Error(`Chrome exited before DevTools became ready: code=${code} signal=${signal}.`));
      child.stderr.on('data', onData);
      child.once('error', onError);
      child.once('exit', onExit);
    });
  } catch (error) {
    if (child.exitCode == null) child.kill('SIGKILL');
    throw error;
  }
  return { child, webSocketUrl, stderr: () => stderr };
}

/** Evaluates one expression in the captured page and unwraps its by-value result. */
async function evaluatePage(connection, sessionId, expression, timeoutMs) {
  const result = await connection.send('Runtime.evaluate', {
    expression,
    awaitPromise: true,
    returnByValue: true,
    userGesture: false,
    timeout: timeoutMs
  }, sessionId);
  if (result.exceptionDetails) {
    const description = result.exceptionDetails.exception?.description
      ?? result.exceptionDetails.text
      ?? 'Unknown page evaluation error.';
    throw new Error(description);
  }
  return result.result?.value;
}

/** Waits until the page has no active local requests for a stable quiet window. */
async function waitForNetworkIdle(activeRequests, timeoutMs, quietWindowMs = 250) {
  const deadline = Date.now() + timeoutMs;
  let quietSince = activeRequests.size === 0 ? Date.now() : null;
  while (Date.now() < deadline) {
    if (activeRequests.size === 0) {
      quietSince ??= Date.now();
      if (Date.now() - quietSince >= quietWindowMs) return;
    } else {
      quietSince = null;
    }
    await delay(25);
  }
  const pendingUrls = [...activeRequests.values()].join(' | ');
  throw new Error(`Reference page did not reach network idle; ${activeRequests.size} requests remain active: ${pendingUrls}`);
}

/** Rejects mapped-byte failures and every internet request without an explicit local route. */
function assertOfflineExternalRequests(externalRequests, externalRouteFailures) {
  if (externalRouteFailures.length > 0) {
    throw new Error(`Reference page external asset routing failed: ${externalRouteFailures.join(' | ')}`);
  }
  if (externalRequests.length > 0) {
    throw new Error(`Reference page attempted unmapped external network access: ${[...new Set(externalRequests)].join(' | ')}`);
  }
}

/** Returns basic image diversity statistics used to reject accidental clear-only Oracles. */
export function inspectReferencePixels(image) {
  const colors = new Set();
  let nonTransparentPixels = 0;
  let nonBlackPixels = 0;
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    const red = image.pixels[offset];
    const green = image.pixels[offset + 1];
    const blue = image.pixels[offset + 2];
    const alpha = image.pixels[offset + 3];
    colors.add((red << 16) | (green << 8) | blue);
    if (alpha !== 0) nonTransparentPixels += 1;
    if (red !== 0 || green !== 0 || blue !== 0) nonBlackPixels += 1;
  }
  return {
    uniqueRgbColorCount: colors.size,
    nonTransparentPixels,
    nonBlackPixels
  };
}

/** Computes the exact viewport-clipped union area of axis-aligned canvas rectangles. */
function computeRectangleUnionArea(rectangles, width, height) {
  const clipped = rectangles.map((rectangle) => ({
    left: Math.max(0, rectangle.x),
    top: Math.max(0, rectangle.y),
    right: Math.min(width, rectangle.x + rectangle.width),
    bottom: Math.min(height, rectangle.y + rectangle.height)
  })).filter((rectangle) => rectangle.right > rectangle.left && rectangle.bottom > rectangle.top);
  const xCoordinates = [...new Set(clipped.flatMap((rectangle) => [rectangle.left, rectangle.right]))]
    .sort((left, right) => left - right);
  let area = 0;
  for (let xIndex = 0; xIndex + 1 < xCoordinates.length; xIndex += 1) {
    const left = xCoordinates[xIndex];
    const right = xCoordinates[xIndex + 1];
    const intervals = clipped.filter((rectangle) => rectangle.left < right && rectangle.right > left)
      .map((rectangle) => [rectangle.top, rectangle.bottom])
      .sort((first, second) => first[0] - second[0]);
    let coveredHeight = 0;
    let activeTop = null;
    let activeBottom = null;
    for (const [top, bottom] of intervals) {
      if (activeTop === null || top > activeBottom) {
        if (activeTop !== null) coveredHeight += activeBottom - activeTop;
        activeTop = top;
        activeBottom = bottom;
      } else {
        activeBottom = Math.max(activeBottom, bottom);
      }
    }
    if (activeTop !== null) coveredHeight += activeBottom - activeTop;
    area += (right - left) * coveredHeight;
  }
  return area;
}

/** Accepts contiguous full-height canvas strips whose only gap is CSS rounding at the viewport edge. */
function isNearCompleteHorizontalCanvasStrip(surfaces, width, height) {
  if (surfaces.length < 2 || surfaces.some((surface) => surface.kind !== 'canvas')) return false;
  const tolerance = 1;
  const ordered = [...surfaces].sort((left, right) => left.x - right.x);
  if (Math.abs(ordered[0].x) > tolerance
    || ordered.some((surface) => Math.abs(surface.y) > tolerance
      || Math.abs(surface.height - height) > tolerance)) {
    return false;
  }
  for (let index = 1; index < ordered.length; index += 1) {
    const previousRight = ordered[index - 1].x + ordered[index - 1].width;
    if (Math.abs(ordered[index].x - previousRight) > tolerance) return false;
  }
  const finalRight = ordered.at(-1).x + ordered.at(-1).width;
  return finalRight >= width * 0.98 && finalRight <= width + tolerance;
}

/** Selects renderer surfaces whose rectangle union forms one complete reference viewport. */
export function selectReferenceSurfaceCapture(
  surfaces,
  width,
  height,
  options = {}
) {
  if (!Array.isArray(surfaces) || surfaces.length === 0) {
    throw new Error('Reference page did not expose any renderer surface geometry.');
  }
  const tolerance = 1;
  const viewportArea = width * height;
  const visible = surfaces.filter((surface) => Number.isInteger(surface.index)
    && typeof surface.kind === 'string'
    && Number.isFinite(surface.x) && Number.isFinite(surface.y)
    && Number.isFinite(surface.width) && Number.isFinite(surface.height)
    && surface.width > 0 && surface.height > 0);
  if (visible.length === 0) throw new Error('Reference renderer surfaces have no visible CSS area.');
  const byArea = [...visible].sort((left, right) => right.width * right.height - left.width * left.height);
  const primary = byArea[0];
  const significant = visible.filter((canvas) => canvas.width * canvas.height >= viewportArea * 0.02
    && canvas.x < width && canvas.y < height
    && canvas.x + canvas.width > 0 && canvas.y + canvas.height > 0);
  const coverageArea = computeRectangleUnionArea(significant, width, height);
  if (coverageArea < viewportArea * 0.999
    && !isNearCompleteHorizontalCanvasStrip(significant, width, height)) {
    if (options.allowPageComposite === true) {
      return {
        mode: 'page-composite',
        surfaceIndices: significant.map((surface) => surface.index),
        clip: { x: 0, y: 0, width, height },
        backingWidth: width,
        backingHeight: height,
        sourceSurfaces: significant
      };
    }
    const geometry = visible.map((surface) => `${surface.kind}:${surface.index}:${surface.x},${surface.y},${surface.width}x${surface.height}`).join(' | ');
    throw new Error(`Reference renderer surfaces do not cover the ${width}x${height} viewport: coverage=${coverageArea}/${viewportArea}; ${geometry}.`);
  }
  const allCanvases = significant.every((surface) => surface.kind === 'canvas');
  const singleCanvas = significant.length === 1 && allCanvases
    && Math.abs(primary.x) <= tolerance && Math.abs(primary.y) <= tolerance
    && Math.abs(primary.width - width) <= tolerance && Math.abs(primary.height - height) <= tolerance;
  return {
    mode: singleCanvas ? 'single-canvas'
      : allCanvases ? 'multi-canvas-composite' : 'renderer-surface-composite',
    surfaceIndices: significant.map((surface) => surface.index),
    clip: { x: 0, y: 0, width, height },
    backingWidth: singleCanvas ? primary.backingWidth : width,
    backingHeight: singleCanvas ? primary.backingHeight : height,
    sourceSurfaces: significant
  };
}

/** Preserves the canvas-only selection API used by focused layout tests and callers. */
export function selectReferenceCanvasCapture(canvases, width, height) {
  const selection = selectReferenceSurfaceCapture(
    canvases.map((canvas) => ({ ...canvas, kind: 'canvas' })),
    width,
    height
  );
  return {
    ...selection,
    canvasIndices: selection.surfaceIndices,
    sourceCanvases: selection.sourceSurfaces
  };
}

/** Verifies that the reference source checkout is pinned to the exact r185 commit. */
async function verifyUpstreamCommit(upstreamRoot) {
  const { stdout } = await execFileAsync('git', ['-C', upstreamRoot, 'rev-parse', 'HEAD']);
  const commit = stdout.trim();
  if (commit !== THREE_R185_COMMIT) {
    throw new Error(`Reference source commit is ${commit}; expected Three.js r185 ${THREE_R185_COMMIT}.`);
  }
}

/** Captures one deterministic Three r185 canvas into the canonical RGBA8 Oracle format. */
export async function captureThreeReference(options) {
  const width = options.width ?? ORACLE_WIDTH;
  const height = options.height ?? ORACLE_HEIGHT;
  const timeoutMs = options.timeoutMs ?? 30_000;
  const randomSeed = options.randomSeed ?? defaultThreeRandomSeed;
  const sampleMode = options.sampleMode ?? 'single';
  const captureMode = options.captureMode ?? 'renderer-surfaces';
  if (sampleMode !== 'single') {
    throw new Error(`Three reference sampleMode must be 'single'; received '${sampleMode}'.`);
  }
  if (!['renderer-surfaces', 'page-composite'].includes(captureMode)) {
    throw new Error(`Reference captureMode must be 'renderer-surfaces' or 'page-composite'; received '${captureMode}'.`);
  }
  if (width !== ORACLE_WIDTH || height !== ORACLE_HEIGHT) {
    throw new Error(`Three reference captures are fixed at ${ORACLE_WIDTH}x${ORACLE_HEIGHT}; received ${width}x${height}.`);
  }
  if (!Number.isInteger(options.frame) || options.frame < 0) {
    throw new Error(`Reference frame must be a non-negative integer; received '${options.frame}'.`);
  }
  let inputReplay = null;
  let inputReplayMetadata = null;
  if (options.inputReplayPath) {
    const inputReplayBytes = await fs.readFile(options.inputReplayPath);
    let inputReplayDocument;
    try {
      inputReplayDocument = JSON.parse(inputReplayBytes.toString('utf8'));
    } catch (error) {
      throw new Error(`Input replay '${options.inputReplayPath}' is not valid JSON: ${error.message}`);
    }
    inputReplay = validateInputReplayDocument(inputReplayDocument);
    const replayIdentityFields = [
      ['caseId', options.caseId],
      ['scenarioId', options.scenarioId],
      ['frame', options.frame]
    ];
    for (const [field, expected] of replayIdentityFields) {
      if (inputReplay[field] != null && inputReplay[field] !== expected) {
        throw new Error(`Input replay ${field}=${JSON.stringify(inputReplay[field])}; expected ${JSON.stringify(expected)}.`);
      }
    }
    const lastEventFrame = inputReplay.events.at(-1)?.frame ?? 0;
    if (lastEventFrame > options.frame) {
      throw new Error(`Input replay ends at frame ${lastEventFrame}; capture frame is ${options.frame}.`);
    }
    inputReplayMetadata = {
      schemaVersion: inputReplay.schemaVersion,
      caseId: inputReplay.caseId,
      scenarioId: inputReplay.scenarioId,
      captureFrame: inputReplay.frame,
      sha256: createHash('sha256').update(inputReplayBytes).digest('hex'),
      target: inputReplay.target,
      eventCount: inputReplay.events.length,
      lastEventFrame
    };
  }
  await verifyUpstreamCommit(options.upstreamRoot);
  const htmlPath = path.join(options.upstreamRoot, 'examples', `${options.caseId}.html`);
  await fs.access(htmlPath);
  const externalAssetMap = await loadExternalAssetMap(
    options.externalAssetMapPath ?? '',
    options.assetPackRoot
  );
  if (options.stateScriptPath && options.moduleStateScriptPath) {
    throw new Error('Reference capture accepts either stateScriptPath or moduleStateScriptPath, not both.');
  }
  let moduleStateScript = null;
  if (options.moduleStateScriptPath) {
    const source = await fs.readFile(options.moduleStateScriptPath, 'utf8');
    moduleStateScript = {
      source,
      sha256: createHash('sha256').update(source).digest('hex')
    };
  }

  const temporaryRoot = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-reference-'));
  const server = await startReferenceServer(
    options.upstreamRoot,
    options.assetPackRoot,
    options.caseId,
    moduleStateScript
  );
  let chrome;
  let connection;
  try {
    chrome = await launchReferenceChrome(options.chromePath, temporaryRoot, width, height, timeoutMs);
    connection = new CdpConnection(chrome.webSocketUrl);
    await connection.open(timeoutMs);
    const { targetId } = await connection.send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await connection.send('Target.attachToTarget', { targetId, flatten: true });

    const activeRequests = new Map();
    const successfulResponseRequestIds = new Set();
    const failedResponses = [];
    const failedRequests = [];
    const externalRequests = [];
    const externalRouteFailures = [];
    const externalRouteTasks = new Set();
    const runtimeExceptions = [];
    connection.on('Network.requestWillBeSent', (message) => {
      if (message.sessionId !== sessionId) return;
      const requestUrl = message.params.request.url;
      if (shouldTrackReferenceRequest(requestUrl)) {
        activeRequests.set(message.params.requestId, requestUrl);
      }
      if (!requestUrl.startsWith(`${server.origin}/`)
        && !requestUrl.startsWith('about:')
        && !requestUrl.startsWith('blob:')
        && !requestUrl.startsWith('data:')) {
        let protocol = '';
        try {
          protocol = new URL(requestUrl).protocol;
        } catch {}
        if (protocol !== 'http:' && protocol !== 'https:') externalRequests.push(requestUrl);
      }
    });
    connection.on('Network.loadingFinished', (message) => {
      if (message.sessionId === sessionId) activeRequests.delete(message.params.requestId);
    });
    connection.on('Network.loadingFailed', (message) => {
      if (message.sessionId !== sessionId) return;
      const requestUrl = activeRequests.get(message.params.requestId) ?? '<unknown-url>';
      activeRequests.delete(message.params.requestId);
      failedRequests.push({
        url: requestUrl,
        errorText: message.params.errorText,
        canceled: message.params.canceled === true,
        blockedReason: message.params.blockedReason ?? null,
        corsErrorStatus: message.params.corsErrorStatus ?? null,
        receivedSuccessfulResponse:
          successfulResponseRequestIds.has(message.params.requestId)
      });
    });
    connection.on('Network.responseReceived', (message) => {
      if (message.sessionId !== sessionId) return;
      if (message.params.response.status < 400) {
        successfulResponseRequestIds.add(message.params.requestId);
        return;
      }
      failedResponses.push({ status: message.params.response.status, url: message.params.response.url });
    });
    connection.on('Runtime.exceptionThrown', (message) => {
      if (message.sessionId !== sessionId) return;
      runtimeExceptions.push(message.params.exceptionDetails.exception?.description
        ?? message.params.exceptionDetails.text
        ?? 'Unknown runtime exception.');
    });
    connection.on('Fetch.requestPaused', (message) => {
      if (message.sessionId !== sessionId) return;
      const requestId = message.params.requestId;
      const requestUrl = message.params.request.url;
      let routeTask;
      routeTask = (async () => {
        if (requestUrl.startsWith(`${server.origin}/`)
          || requestUrl.startsWith('about:')
          || requestUrl.startsWith('blob:')
          || requestUrl.startsWith('data:')) {
          await connection.send('Fetch.continueRequest', { requestId }, sessionId);
          return;
        }
        const response = await createExternalAssetCaptureResponse(externalAssetMap, requestUrl);
        if (!response) {
          externalRequests.push(requestUrl);
          await connection.send('Fetch.failRequest', { requestId, errorReason: 'BlockedByClient' }, sessionId);
          return;
        }
        await connection.send('Fetch.fulfillRequest', {
          requestId,
          responseCode: response.responseCode,
          responseHeaders: response.responseHeaders,
          body: response.body
        }, sessionId);
      })().catch(async (error) => {
        externalRouteFailures.push(`${requestUrl}: ${error instanceof Error ? error.message : String(error)}`);
        try {
          await connection.send('Fetch.failRequest', { requestId, errorReason: 'Failed' }, sessionId);
        } catch {}
      }).finally(() => externalRouteTasks.delete(routeTask));
      externalRouteTasks.add(routeTask);
    });

    await Promise.all([
      connection.send('Page.enable', {}, sessionId),
      connection.send('Runtime.enable', {}, sessionId),
      connection.send('Network.enable', {}, sessionId),
      connection.send('Fetch.enable', {
        patterns: [{ urlPattern: '*', requestStage: 'Request' }]
      }, sessionId),
      connection.send('Log.enable', {}, sessionId),
      connection.send('Emulation.setDeviceMetricsOverride', {
        width,
        height,
        deviceScaleFactor: 1,
        mobile: false,
        screenWidth: width,
        screenHeight: height
      }, sessionId)
    ]);
    await connection.send('Page.addScriptToEvaluateOnNewDocument', {
      source: makeDeterministicFrameBootstrap(
        options.frameStepMs ?? DEFAULT_FRAME_STEP_MS,
        randomSeed
      )
    }, sessionId);

    const loadEvent = connection.waitForEvent(
      'Page.loadEventFired',
      (message) => message.sessionId === sessionId,
      timeoutMs
    );
    const pageUrl = `${server.origin}/examples/${encodeURIComponent(options.caseId)}.html`;
    await connection.send('Page.navigate', { url: pageUrl }, sessionId);
    await loadEvent;
    await waitForNetworkIdle(activeRequests, timeoutMs);
    while (externalRouteTasks.size > 0) await Promise.all([...externalRouteTasks]);
    assertOfflineExternalRequests(externalRequests, externalRouteFailures);
    const pageReady = await evaluatePage(connection, sessionId, `(() => ({
      title: document.title,
      hasClock: typeof window.__gvmReferenceClock?.advanceFrames === 'function',
      canvasCount: document.querySelectorAll('canvas').length
    }))()`, timeoutMs);
    if (!pageReady?.hasClock) throw new Error('Deterministic frame bootstrap was not installed before the example started.');

    if (options.stateScriptPath) {
      const stateScript = await fs.readFile(options.stateScriptPath, 'utf8');
      await evaluatePage(connection, sessionId, `(async () => { ${stateScript}\n })()`, timeoutMs);
    }
    if (moduleStateScript != null) {
      const moduleStateResult = await evaluatePage(
        connection,
        sessionId,
        `window.__gvmApplyModuleCanonicalState?.()`,
        timeoutMs
      );
      if (moduleStateResult == null) {
        throw new Error('Reference module canonical-state hook was not installed.');
      }
    }
    let clock;
    let replayRuntime = null;
    if (inputReplay) {
      const replayResult = await evaluatePage(
        connection,
        sessionId,
        makeInputReplayScript(inputReplay, options.frame + 1),
        timeoutMs
      );
      clock = replayResult.clock;
      replayRuntime = replayResult.replay;
    } else {
      clock = await evaluatePage(
        connection,
        sessionId,
        `window.__gvmReferenceClock.advanceFrames(${options.frame + 1})`,
        timeoutMs
      );
    }
    await waitForNetworkIdle(activeRequests, timeoutMs);
    while (externalRouteTasks.size > 0) await Promise.all([...externalRouteTasks]);
    assertOfflineExternalRequests(externalRequests, externalRouteFailures);
    await evaluatePage(
      connection,
      sessionId,
      makeReferenceSurfaceIsolationScript(),
      timeoutMs
    );
    const surfaceRecords = await evaluatePage(connection, sessionId, `(() => {
      const candidates = [];
      for (const element of document.querySelectorAll('canvas, svg, div')) {
        let kind = null;
        const tagName = element.tagName.toLowerCase();
        const rect = element.getBoundingClientRect();
        if (tagName === 'canvas' || tagName === 'svg') {
          kind = tagName;
        } else {
          const style = getComputedStyle(element);
          const clipsChildren = style.overflow === 'hidden' || style.overflowX === 'hidden' || style.overflowY === 'hidden';
          const hasCss3dChild = Array.from(element.querySelectorAll('*')).some((child) => getComputedStyle(child).transformStyle === 'preserve-3d');
          const hasCss2dChild = Array.from(element.querySelectorAll('*')).some((child) => {
            const childStyle = getComputedStyle(child);
            return childStyle.position === 'absolute' && childStyle.transform !== 'none';
          });
          // AsciiEffect replaces the visible canvas with a table of glyph cells.
          // Treat a populated, viewport-sized table wrapper as a renderer surface
          // so the deterministic page-composite oracle includes the ASCII output.
          const asciiTable = element.querySelector('table');
          const tableRect = asciiTable?.getBoundingClientRect();
          const hasAsciiOutput = asciiTable != null
            && asciiTable.rows.length > 0
            && tableRect != null
            && tableRect.width >= window.innerWidth * 0.5
            && tableRect.height >= window.innerHeight * 0.5;
          if (hasAsciiOutput) kind = 'ascii-effect';
          else if (clipsChildren && (hasCss3dChild || hasCss2dChild)) kind = 'css-renderer';
        }
        if (kind === null) continue;
        const index = candidates.length;
        element.setAttribute('data-gvm-reference-surface-index', String(index));
        candidates.push({
          index,
          kind,
          tagName,
          x: rect.x,
          y: rect.y,
          width: rect.width,
          height: rect.height,
          backingWidth: tagName === 'canvas' ? element.width : null,
          backingHeight: tagName === 'canvas' ? element.height : null
        });
      }
      return candidates;
    })()`, timeoutMs);
    const surfaceCapture = selectReferenceSurfaceCapture(
      surfaceRecords,
      width,
      height,
      { allowPageComposite: captureMode === 'page-composite' }
    );
    await evaluatePage(connection, sessionId, `(() => {
      const selected = new Set(${JSON.stringify(surfaceCapture.surfaceIndices)});
      for (const element of document.querySelectorAll('[data-gvm-reference-surface-index]')) {
        element.removeAttribute('data-gvm-reference-surface');
        if (selected.has(Number(element.getAttribute('data-gvm-reference-surface-index')))) {
          element.setAttribute('data-gvm-reference-surface', '');
        }
      }
    })()`, timeoutMs);
    if (runtimeExceptions.length > 0) {
      throw new Error(`Reference page raised ${runtimeExceptions.length} runtime exception(s): ${runtimeExceptions.join(' | ')}`);
    }
    if (failedResponses.length > 0) {
      const failures = failedResponses.map((response) => `${response.status} ${response.url}`).join(' | ');
      throw new Error(`Reference page has failed HTTP responses: ${failures}`);
    }
    const materialFailedRequests =
      failedRequests.filter(isMaterialFailedReferenceRequest);
    if (materialFailedRequests.length > 0) {
      const failures = materialFailedRequests.map((request) =>
        `${request.errorText} ${request.url} `
        + `(canceled=${request.canceled}, blocked=${request.blockedReason ?? 'none'}, `
        + `cors=${request.corsErrorStatus?.corsError ?? 'none'}, `
        + `successfulResponse=${request.receivedSuccessfulResponse})`).join(' | ');
      throw new Error(`Reference page has failed network requests: ${failures}`);
    }

    const screenshotOptions = {
      format: 'png',
      fromSurface: true,
      captureBeyondViewport: false
    };
    if (surfaceCapture.mode !== 'page-composite') {
      screenshotOptions.clip = { ...surfaceCapture.clip, scale: 1 };
    }
    const screenshot = await connection.send(
      'Page.captureScreenshot',
      screenshotOptions,
      sessionId
    );
    const image = decodePngRgba(Buffer.from(screenshot.data, 'base64'));
    if (image.width !== width || image.height !== height) {
      throw new Error(`Decoded reference screenshot is ${image.width}x${image.height}; expected ${width}x${height}.`);
    }
    const pixelInspection = inspectReferencePixels(image);
    if (pixelInspection.uniqueRgbColorCount < 2 || pixelInspection.nonBlackPixels === 0) {
      throw new Error(`Reference screenshot is clear-only or black: ${JSON.stringify(pixelInspection)}.`);
    }
    const samplePolicy = server.getSingleSampleTransform();
    if (samplePolicy == null) {
      throw new Error('Reference example HTML was not served through the mandatory single-sample transform.');
    }

    const oraclePaths = makeLockedOraclePaths(
      { id: options.caseId },
      { id: options.scenarioId }
    );
    const rgbaPath = path.join(options.oracleRoot, ...oraclePaths.rgbaPath.split('/'));
    const metadataPath = path.join(options.oracleRoot, ...oraclePaths.metadataPath.split('/'));
    await fs.mkdir(path.dirname(rgbaPath), { recursive: true });
    const metadata = {
      schemaVersion: 1,
      source: 'three-r185-reference',
      upstreamCommit: THREE_R185_COMMIT,
      caseId: options.caseId,
      scenarioId: options.scenarioId,
      frame: options.frame,
      randomSeed,
      randomState: clock.randomState,
      virtualTimeMs: clock.lastRenderedTimeMs,
      nextFrameTimeMs: clock.virtualTimeMs,
      width,
      height,
      rowStrideBytes: width * 4,
      byteCount: image.pixels.byteLength,
      format: 'rgba8unorm',
      canvasBackingWidth: surfaceCapture.backingWidth,
      canvasBackingHeight: surfaceCapture.backingHeight,
      referenceCaptureMode: surfaceCapture.mode,
      sourceSurfaces: surfaceCapture.sourceSurfaces,
      sourceCanvases: surfaceCapture.sourceSurfaces.every((surface) => surface.kind === 'canvas')
        ? surfaceCapture.sourceSurfaces
        : undefined,
      samplePolicy,
      inputReplay: inputReplayMetadata == null
        ? null
        : { ...inputReplayMetadata, runtime: replayRuntime },
      externalAssetMap: makeExternalAssetMapLockRecord(externalAssetMap),
      pixelInspection
    };
    await Promise.all([
      fs.writeFile(rgbaPath, image.pixels),
      fs.writeFile(metadataPath, `${JSON.stringify(metadata, null, 2)}\n`)
    ]);
    return { rgbaPath, metadataPath, metadata, pageTitle: pageReady.title };
  } finally {
    connection?.close();
    if (chrome?.child && chrome.child.exitCode == null) {
      chrome.child.kill('SIGTERM');
      await Promise.race([
        new Promise((resolve) => chrome.child.once('exit', resolve)),
        delay(2000)
      ]);
      if (chrome.child.exitCode == null) {
        chrome.child.kill('SIGKILL');
        await new Promise((resolve) => chrome.child.once('exit', resolve));
      }
    }
    await server.close();
    await fs.rm(temporaryRoot, { recursive: true, force: true });
  }
}
