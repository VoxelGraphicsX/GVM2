let raycastCanvas = document.querySelector('body > canvas');
for (let attempt = 0; !(raycastCanvas instanceof HTMLCanvasElement) && attempt < 1000; attempt += 1) {
  await new Promise((resolve) => setTimeout(resolve, 10));
  raycastCanvas = document.querySelector('body > canvas');
}
if (!(raycastCanvas instanceof HTMLCanvasElement)) {
  throw new Error('webgl_instancing_raycast did not create its canvas.');
}
const raycastRect = raycastCanvas.getBoundingClientRect();
raycastCanvas.dispatchEvent(new MouseEvent('mousemove', {
  bubbles: true,
  cancelable: true,
  composed: true,
  view: window,
  clientX: raycastRect.left + raycastRect.width * 0.5,
  clientY: raycastRect.top + raycastRect.height * 0.5
}));
