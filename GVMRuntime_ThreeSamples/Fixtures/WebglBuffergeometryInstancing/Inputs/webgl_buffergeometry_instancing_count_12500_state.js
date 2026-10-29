let instanceCountController = null;
for (let attempt = 0; instanceCountController === null && attempt < 1000; attempt += 1) {
  for (const controller of document.querySelectorAll('.lil-gui .controller.number')) {
    if (controller.querySelector('.name')?.textContent?.trim() === 'instanceCount') {
      instanceCountController = controller;
      break;
    }
  }
  if (instanceCountController === null) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (instanceCountController === null) {
  throw new Error('webgl_buffergeometry_instancing instanceCount controller was not found.');
}
const instanceCountInput = instanceCountController.querySelector('input[type="number"]');
if (!(instanceCountInput instanceof HTMLInputElement)) {
  throw new Error('webgl_buffergeometry_instancing instanceCount controller has no numeric input.');
}
instanceCountInput.value = '12500';
instanceCountInput.dispatchEvent(new Event('input', { bubbles: true }));
