import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { deflateSync } from 'node:zlib';

import {
  decodePngRgba,
  inspectReferencePixels,
  isMaterialFailedReferenceRequest,
  makeDeterministicFrameBootstrap,
  makeInputReplayScript,
  makeReferenceSurfaceIsolationScript,
  resolveStaticFile,
  rewriteReferenceHtmlForSingleSample,
  injectModuleScopeStateHook,
  selectReferenceCanvasCapture,
  selectReferenceSurfaceCapture,
  shouldTrackReferenceRequest,
  validateInputReplayDocument
} from '../reference-capture.mjs';
import {
  advanceThreeRandomState,
  defaultThreeRandomSeed,
  threeRandomFloatFromState
} from '../determinism.mjs';

const PNG_SIGNATURE = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

/** Returns the Paeth predictor used to construct a filtered PNG row. */
function paeth(left, above, upperLeft) {
  const prediction = left + above - upperLeft;
  const leftDistance = Math.abs(prediction - left);
  const aboveDistance = Math.abs(prediction - above);
  const upperLeftDistance = Math.abs(prediction - upperLeft);
  if (leftDistance <= aboveDistance && leftDistance <= upperLeftDistance) return left;
  return aboveDistance <= upperLeftDistance ? above : upperLeft;
}

/** Creates one PNG chunk; decoder tests intentionally do not depend on CRC validation. */
function makeChunk(type, data) {
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length);
  return Buffer.concat([length, Buffer.from(type, 'ascii'), data, Buffer.alloc(4)]);
}

/** Encodes deterministic scanlines with all five PNG filters. */
function makeTestPng(width, height, colorType, pixels, filters) {
  const channelCount = colorType === 6 ? 4 : 3;
  const stride = width * channelCount;
  const filtered = Buffer.alloc(height * (stride + 1));
  for (let y = 0; y < height; y += 1) {
    const filterType = filters[y];
    const rowOffset = y * (stride + 1);
    filtered[rowOffset] = filterType;
    for (let x = 0; x < stride; x += 1) {
      const sourceOffset = y * stride + x;
      const left = x >= channelCount ? pixels[sourceOffset - channelCount] : 0;
      const above = y > 0 ? pixels[sourceOffset - stride] : 0;
      const upperLeft = y > 0 && x >= channelCount ? pixels[sourceOffset - stride - channelCount] : 0;
      let predictor = 0;
      if (filterType === 1) predictor = left;
      else if (filterType === 2) predictor = above;
      else if (filterType === 3) predictor = Math.floor((left + above) / 2);
      else if (filterType === 4) predictor = paeth(left, above, upperLeft);
      filtered[rowOffset + 1 + x] = (pixels[sourceOffset] - predictor + 256) & 0xff;
    }
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = colorType;
  return Buffer.concat([
    PNG_SIGNATURE,
    makeChunk('IHDR', header),
    makeChunk('IDAT', deflateSync(filtered)),
    makeChunk('IEND', Buffer.alloc(0))
  ]);
}

test('decodePngRgba reconstructs all PNG row filters and preserves RGBA', () => {
  const width = 2;
  const height = 5;
  const pixels = Buffer.from(Array.from({ length: width * height * 4 }, (_, index) => (index * 37 + 11) & 0xff));
  const decoded = decodePngRgba(makeTestPng(width, height, 6, pixels, [0, 1, 2, 3, 4]));
  assert.equal(decoded.width, width);
  assert.equal(decoded.height, height);
  assert.deepEqual([...decoded.pixels], [...pixels]);
});

test('decodePngRgba expands RGB screenshots to opaque RGBA', () => {
  const rgb = Buffer.from([5, 10, 15, 20, 25, 30]);
  const decoded = decodePngRgba(makeTestPng(2, 1, 2, rgb, [0]));
  assert.deepEqual([...decoded.pixels], [5, 10, 15, 255, 20, 25, 30, 255]);
});

test('deterministic bootstrap exposes a virtual frame scheduler without case branches', () => {
  const source = makeDeterministicFrameBootstrap(20);
  assert.match(source, /const frameStepMs = 20;/u);
  assert.match(source, /const initialRandomSeed = 305419896;/u);
  assert.match(source, /__gvmReferenceClock/u);
  assert.match(source, /requestAnimationFrame/u);
  assert.match(source, /Math\.random/u);
  assert.match(source, /randomState,\n\s+virtualTimeMs/u);
  assert.doesNotMatch(source, /caseId|scenarioId|legacy|experimental/iu);
});

test('single-sample reference rewrite disables renderer and render-target MSAA without changing effect sample counts', () => {
  const source = `
    const renderer = new THREE.WebGLRenderer( { antialias: true } );
    const gpu = new WebGPURenderer( { antialias: true } );
    const direct = new THREE.WebGLRenderTarget( width, height, { samples: 4, type: THREE.HalfFloatType } );
    renderTarget.samples = parameters.samples;
    const ao = { samples: 16 };
  `;
  const result = rewriteReferenceHtmlForSingleSample(source);
  assert.equal(result.rendererAntialiasOverrides, 2);
  assert.equal(result.renderTargetSampleOverrides, 2);
  assert.match(result.source, /antialias: false/u);
  assert.match(result.source, /samples: 0, type:/u);
  assert.match(result.source, /renderTarget\.samples = 0;/u);
  assert.match(result.source, /const ao = \{ samples: 16 \}/u);
});

test('module canonical-state hook is injected into module lexical scope', () => {
  const source = `
    <script type="importmap">{}</script>
    <script type="module">
      let mesh = null;
    </script>
  `;
  const result = injectModuleScopeStateHook(source, 'mesh.count = 125;');
  assert.match(result, /let mesh = null;[\s\S]*__gvmApplyModuleCanonicalState/u);
  assert.match(result, /mesh\.count = 125;[\s\S]*<\/script>/u);
  assert.equal((result.match(/__gvmApplyModuleCanonicalState/gu) ?? []).length, 1);
});

test('JavaScript xorshift32 stream matches the ThreeCompat sequence', () => {
  const first = advanceThreeRandomState(defaultThreeRandomSeed);
  const second = advanceThreeRandomState(first);
  assert.equal(first, 0x87985aa5);
  assert.equal(second, 0x155b24a3);
  assert.equal(threeRandomFloatFromState(first), 0.5296684503555298);
  assert.equal(threeRandomFloatFromState(second), 0.08342194557189941);
});

test('reference pixel inspection distinguishes colored content from black clear', () => {
  const image = {
    width: 2,
    height: 1,
    pixels: new Uint8Array([0, 0, 0, 255, 8, 4, 2, 255])
  };
  assert.deepEqual(inspectReferencePixels(image), {
    uniqueRgbColorCount: 2,
    nonTransparentPixels: 2,
    nonBlackPixels: 1
  });
});

test('reference request tracking ignores persistent local URLs but retains HTTP assets', () => {
  assert.equal(shouldTrackReferenceRequest('blob:http://127.0.0.1:8080/worker'), false);
  assert.equal(shouldTrackReferenceRequest('data:text/javascript,postMessage(1)'), false);
  assert.equal(shouldTrackReferenceRequest('about:blank'), false);
  assert.equal(shouldTrackReferenceRequest('http://127.0.0.1:8080/duck.glb'), true);
  assert.equal(shouldTrackReferenceRequest('https://example.test/environment.hdr'), true);
});

test('reference request tracking accepts only cancellations with a successful response', () => {
  assert.equal(isMaterialFailedReferenceRequest({
    canceled: true,
    receivedSuccessfulResponse: true
  }), false);
  assert.equal(isMaterialFailedReferenceRequest({
    canceled: true,
    receivedSuccessfulResponse: false
  }), true);
  assert.equal(isMaterialFailedReferenceRequest({
    canceled: false,
    receivedSuccessfulResponse: true
  }), true);
});

test('reference canvas selection preserves a fullscreen renderer and ignores diagnostic canvases', () => {
  const selection = selectReferenceCanvasCapture([
    { index: 0, x: 0, y: 0, width: 800, height: 500, backingWidth: 800, backingHeight: 500 },
    { index: 1, x: 0, y: 0, width: 80, height: 48, backingWidth: 80, backingHeight: 48 }
  ], 800, 500);
  assert.equal(selection.mode, 'single-canvas');
  assert.deepEqual(selection.canvasIndices, [0]);
  assert.deepEqual(selection.clip, { x: 0, y: 0, width: 800, height: 500 });
});

test('reference canvas selection composes split renderers while filtering a Stats canvas', () => {
  const selection = selectReferenceCanvasCapture([
    { index: 0, x: 0, y: 0, width: 200, height: 500, backingWidth: 200, backingHeight: 500 },
    { index: 1, x: 200, y: 0, width: 600, height: 500, backingWidth: 600, backingHeight: 500 },
    { index: 2, x: 0, y: 0, width: 80, height: 48, backingWidth: 80, backingHeight: 48 }
  ], 800, 500);
  assert.equal(selection.mode, 'multi-canvas-composite');
  assert.deepEqual(selection.canvasIndices, [0, 1]);
  assert.equal(selection.backingWidth, 800);
  assert.equal(selection.backingHeight, 500);
});

test('reference canvas selection preserves a contiguous 33-percent strip and its rounded page edge', () => {
  const selection = selectReferenceCanvasCapture([
    { index: 0, x: 0, y: 0, width: 264, height: 500, backingWidth: 264, backingHeight: 500 },
    { index: 1, x: 264, y: 0, width: 264, height: 500, backingWidth: 264, backingHeight: 500 },
    { index: 2, x: 528, y: 0, width: 264, height: 500, backingWidth: 264, backingHeight: 500 },
    { index: 3, x: 0, y: 0, width: 80, height: 48, backingWidth: 80, backingHeight: 48 }
  ], 800, 500);
  assert.equal(selection.mode, 'multi-canvas-composite');
  assert.deepEqual(selection.canvasIndices, [0, 1, 2]);
  assert.deepEqual(selection.clip, { x: 0, y: 0, width: 800, height: 500 });
});

test('reference canvas selection rejects partial multi-canvas coverage', () => {
  assert.throws(() => selectReferenceCanvasCapture([
    { index: 0, x: 0, y: 0, width: 200, height: 500, backingWidth: 200, backingHeight: 500 },
    { index: 1, x: 300, y: 0, width: 500, height: 500, backingWidth: 500, backingHeight: 500 }
  ], 800, 500), /do not cover/u);
});

test('reference surface selection explicitly permits a DOM page composite', () => {
  const selection = selectReferenceSurfaceCapture([
    { index: 0, kind: 'canvas', x: 8, y: 111, width: 784, height: 784, backingWidth: 1024, backingHeight: 1024 }
  ], 800, 500, { allowPageComposite: true });
  assert.equal(selection.mode, 'page-composite');
  assert.deepEqual(selection.surfaceIndices, [0]);
  assert.deepEqual(selection.clip, { x: 0, y: 0, width: 800, height: 500 });
});

test('reference surface selection preserves a fullscreen CSS label layer over a canvas', () => {
  const selection = selectReferenceSurfaceCapture([
    { index: 0, kind: 'canvas', x: 0, y: 0, width: 800, height: 500, backingWidth: 800, backingHeight: 500 },
    { index: 1, kind: 'css-renderer', x: 0, y: 0, width: 800, height: 500, backingWidth: null, backingHeight: null }
  ], 800, 500);
  assert.equal(selection.mode, 'renderer-surface-composite');
  assert.deepEqual(selection.surfaceIndices, [0, 1]);
  assert.equal(selection.sourceSurfaces[1].kind, 'css-renderer');
});

test('reference surface selection accepts a fullscreen SVG renderer without a canvas', () => {
  const selection = selectReferenceSurfaceCapture([
    { index: 0, kind: 'svg', x: 0, y: 0, width: 800, height: 500, backingWidth: null, backingHeight: null }
  ], 800, 500);
  assert.equal(selection.mode, 'renderer-surface-composite');
  assert.deepEqual(selection.surfaceIndices, [0]);
  assert.equal(selection.backingWidth, 800);
  assert.equal(selection.backingHeight, 500);
});

test('input replay validation normalizes pointer frames and target dimensions', () => {
  const replay = validateInputReplayDocument({
    schemaVersion: 1,
    caseId: 'webgl_materials_texture_canvas',
    scenarioId: 'painted',
    frame: 30,
    target: ' #drawing-canvas ',
    canvasWidth: 128,
    canvasHeight: 128,
    events: [
      { type: 'pointerdown', x: 16, y: 16 },
      { type: 'pointermove', frame: 2, x: 96, y: 112 },
      { type: 'pointerup', frame: 2, x: 96, y: 112 }
    ]
  });
  assert.equal(replay.target, '#drawing-canvas');
  assert.equal(replay.caseId, 'webgl_materials_texture_canvas');
  assert.equal(replay.scenarioId, 'painted');
  assert.equal(replay.frame, 30);
  assert.deepEqual(replay.events.map((event) => event.frame), [0, 2, 2]);
  assert.deepEqual(replay.events.map((event) => event.pointerId), [1, 1, 1]);
  assert.deepEqual(replay.events.map((event) => event.button), [0, -1, 0]);
});

test('input replay validation preserves right-button and per-event GUI targets', () => {
  const replay = validateInputReplayDocument({
    schemaVersion: 1,
    target: 'canvas',
    canvasWidth: 800,
    canvasHeight: 500,
    events: [
      { type: 'click', frame: 0, target: '.lil-gui input', x: 4, y: 4 },
      { type: 'pointerdown', frame: 1, x: 400, y: 250, button: 2 },
      { type: 'pointermove', frame: 2, x: 460, y: 210, button: 2 },
      { type: 'pointerup', frame: 2, x: 460, y: 210, button: 2 }
    ]
  });
  assert.equal(replay.events[0].target, '.lil-gui input');
  assert.deepEqual(replay.events.slice(1).map((event) => event.button), [2, 2, 2]);
  assert.match(makeInputReplayScript(replay, 3), /1 << pressedButton/u);
});

test('input replay validation preserves browser pointer hover button -1', () => {
  const replay = validateInputReplayDocument({
    schemaVersion: 1,
    target: 'body canvas',
    canvasWidth: 800,
    canvasHeight: 500,
    events: [
      { type: 'pointermove', frame: 0, x: 400, y: 250, button: -1 },
      { type: 'pointerleave', frame: 1, x: 400, y: 250, button: -1 }
    ]
  });
  assert.deepEqual(replay.events.map((event) => event.button), [-1, -1]);
});

test('input replay validation accepts the legacy captureFrame identity field', () => {
  const replay = validateInputReplayDocument({
    schemaVersion: 1,
    caseId: 'webgpu_materials_displacementmap',
    scenarioId: 'material-settings',
    captureFrame: 61,
    target: 'canvas',
    events: [
      { type: 'pointermove', frame: 0, x: 400, y: 250 }
    ]
  });
  assert.equal(replay.frame, 61);
});

test('input replay script interleaves target-relative events before the capture frame', () => {
  const script = makeInputReplayScript({
    schemaVersion: 1,
    target: '#drawing-canvas',
    events: [
      { type: 'pointerdown', frame: 0, x: 1, y: 2 },
      { type: 'pointerup', frame: 3, x: 5, y: 6 }
    ]
  }, 5);
  assert.match(script, /querySelectorAll/u);
  assert.match(script, /new PointerEvent/u);
  assert.match(script, /event\.x \* rectWidth \/ coordinateWidth/u);
  assert.match(script, /const remainingFrameCount = 5 - advancedFrameCount/u);
  assert.match(script, /randomState: clock\.randomState/u);
  assert.doesNotMatch(script, /eval\(|Function\(/u);
});

test('input replay normalizes keyboard and wheel events on the same fixed frame timeline', () => {
  const document = {
    schemaVersion: 1,
    target: 'body',
    events: [
      { type: 'keydown', frame: 1, key: 'w', code: 'KeyW', shiftKey: true },
      { type: 'wheel', frame: 2, x: 400, y: 250, deltaY: -120 },
      { type: 'keyup', frame: 3, key: 'w', code: 'KeyW' }
    ]
  };
  const replay = validateInputReplayDocument(document);
  assert.deepEqual(replay.events.map((event) => event.type), ['keydown', 'wheel', 'keyup']);
  assert.equal(replay.events[0].shiftKey, true);
  assert.equal(replay.events[0].location, 0);
  assert.equal(replay.events[1].deltaX, 0);
  assert.equal(replay.events[1].deltaY, -120);
  const script = makeInputReplayScript(document, 4);
  assert.match(script, /new KeyboardEvent/u);
  assert.match(script, /new WheelEvent/u);
  assert.match(script, /eventTarget\.focus/u);
});

test('input replay emits deterministic mouse clicks for GUI controls', () => {
  const document = {
    schemaVersion: 1,
    target: '.lil-gui input',
    events: [
      { type: 'click', frame: 0, x: 5, y: 5 }
    ]
  };
  const replay = validateInputReplayDocument(document);
  assert.equal(replay.events[0].type, 'click');
  assert.equal(replay.events[0].x, 5);
  const script = makeInputReplayScript(document, 1);
  assert.match(script, /new MouseEvent/u);
});

test('input replay emits deterministic mouse movement for legacy listeners', () => {
  const document = {
    schemaVersion: 1,
    target: 'canvas',
    canvasWidth: 800,
    canvasHeight: 500,
    events: [
      { type: 'mousemove', frame: 0, x: 505, y: 185 }
    ]
  };
  const replay = validateInputReplayDocument(document);
  assert.equal(replay.events[0].type, 'mousemove');
  const script = makeInputReplayScript(document, 1);
  assert.match(script, /new MouseEvent\(event\.type/u);
});

test('reference surface isolation removes Inspector chrome before selecting renderer surfaces', () => {
  const script = makeReferenceSurfaceIsolationScript();
  assert.match(script, /\.profiler-toggle/u);
  assert.match(script, /\.profiler-mini-panel/u);
  assert.match(script, /element\.remove\(\)/u);
  assert.match(script, /data-gvm-reference-surface/u);
});

test('input replay preserves deterministic pointer settle delays', () => {
  const document = {
    schemaVersion: 1,
    target: 'canvas',
    canvasWidth: 800,
    canvasHeight: 500,
    events: [
      { type: 'pointermove', frame: 0, x: 400, y: 250, settleMs: 250 }
    ]
  };
  const replay = validateInputReplayDocument(document);
  assert.equal(replay.events[0].settleMs, 250);
  const script = makeInputReplayScript(document, 1);
  assert.match(script, /eventTarget\.dispatchEvent\(replayEvent\)/u);
  assert.match(script, /setTimeout\(resolve, event\.settleMs\)/u);
});

test('input replay assigns deterministic select values before form changes', () => {
  const document = {
    schemaVersion: 1,
    target: '.lil-gui select',
    events: [
      { type: 'change', frame: 0, value: 'MERGED', settleMs: 250 }
    ]
  };
  const replay = validateInputReplayDocument(document);
  assert.deepEqual(replay.events[0], {
    type: 'change',
    frame: 0,
    value: 'MERGED',
    settleMs: 250
  });
  const script = makeInputReplayScript(document, 1);
  assert.match(script, /eventTarget\.value = event\.value/u);
  assert.match(script, /new Event\(event\.type/u);
  assert.match(script, /setTimeout\(resolve, event\.settleMs\)/u);
});

test('input replay dispatches deterministic input events for live number controls', () => {
  const document = {
    schemaVersion: 1,
    target: 'canvas',
    events: [
      { type: 'input', frame: 0, target: '.lil-gui input[type=number]', value: '10' }
    ]
  };
  const replay = validateInputReplayDocument(document);
  assert.equal(replay.events[0].type, 'input');
  assert.equal(replay.events[0].value, '10');
  const script = makeInputReplayScript(document, 1);
  assert.match(script, /event\.type === 'input'/u);
  assert.match(script, /eventTarget\.value = event\.value/u);
});

test('input replay preserves explicit form targets for GUI controls', () => {
  const document = {
    schemaVersion: 1,
    target: 'canvas',
    events: [
      {
        type: 'change',
        frame: 0,
        target: ' .three-inspector select ',
        value: 'RefractionMapping'
      }
    ]
  };
  const replay = validateInputReplayDocument(document);
  assert.equal(replay.events[0].target, '.three-inspector select');
  const script = makeInputReplayScript(document, 1);
  assert.match(script, /event\.target == null/u);
  assert.match(script, /document\.querySelectorAll\(event\.target\)/u);
});

test('input replay rejects ambiguous coordinates, ordering, and post-capture events', () => {
  assert.throws(() => validateInputReplayDocument({
    schemaVersion: 1,
    target: '#canvas',
    events: [{ type: 'pointermove', x: Number.NaN, y: 0 }]
  }), /finite/u);
  assert.throws(() => validateInputReplayDocument({
    schemaVersion: 1,
    target: '#canvas',
    events: [
      { type: 'pointermove', frame: 2, x: 0, y: 0 },
      { type: 'pointermove', frame: 1, x: 0, y: 0 }
    ]
  }), /precedes/u);
  assert.throws(() => makeInputReplayScript({
    schemaVersion: 1,
    target: '#canvas',
    events: [{ type: 'pointerup', frame: 4, x: 0, y: 0 }]
  }, 4), /exceeds capture frame/u);
  assert.throws(() => validateInputReplayDocument({
    schemaVersion: 1,
    target: 'body',
    events: [{ type: 'keydown', key: '', code: 'KeyW' }]
  }), /key and code/u);
  assert.throws(() => validateInputReplayDocument({
    schemaVersion: 1,
    target: '#canvas',
    events: [{ type: 'wheel', x: 0, y: 0, deltaY: Number.NaN }]
  }), /wheel delta/u);
  assert.throws(() => validateInputReplayDocument({
    schemaVersion: 1,
    target: '#canvas',
    events: [{ type: 'pointermove', x: 0, y: 0, settleMs: 5001 }]
  }), /settleMs/u);
});

test('reference file routing never falls back from the asset pack to upstream assets', async (context) => {
  const temporaryRoot = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-reference-routing-'));
  context.after(() => fs.rm(temporaryRoot, { recursive: true, force: true }));
  const upstreamRoot = path.join(temporaryRoot, 'upstream');
  const assetPackRoot = path.join(temporaryRoot, 'assets');
  await fs.mkdir(path.join(upstreamRoot, 'examples', 'textures'), { recursive: true });
  await fs.mkdir(path.join(assetPackRoot, 'textures'), { recursive: true });
  await fs.writeFile(path.join(upstreamRoot, 'examples', 'case.html'), 'pinned source');
  await fs.writeFile(path.join(upstreamRoot, 'examples', 'textures', 'crate.gif'), 'unlocked upstream asset');
  await fs.writeFile(path.join(assetPackRoot, 'textures', 'crate.gif'), 'locked packed asset');
  assert.equal(
    await resolveStaticFile(upstreamRoot, assetPackRoot, '/examples/case.html'),
    path.join(upstreamRoot, 'examples', 'case.html')
  );
  assert.equal(
    await resolveStaticFile(upstreamRoot, assetPackRoot, '/examples/textures/crate.gif'),
    path.join(assetPackRoot, 'textures', 'crate.gif')
  );
  await fs.rm(path.join(assetPackRoot, 'textures', 'crate.gif'));
  assert.equal(await resolveStaticFile(upstreamRoot, assetPackRoot, '/examples/textures/crate.gif'), null);
});
