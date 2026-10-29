let wireframeController = null;
for (let attempt = 0; wireframeController === null && attempt < 1000; attempt += 1) {
  for (const controller of document.querySelectorAll('.lil-gui .controller.boolean')) {
    if (controller.querySelector('.name')?.textContent?.trim() === 'wireframe') {
      wireframeController = controller;
      break;
    }
  }
  if (wireframeController === null) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (wireframeController === null) {
  throw new Error('webgl_buffergeometry_indexed wireframe controller was not found.');
}
const wireframeInput = wireframeController.querySelector('input[type="checkbox"]');
if (!(wireframeInput instanceof HTMLInputElement)) {
  throw new Error('webgl_buffergeometry_indexed wireframe controller has no checkbox input.');
}
if (!wireframeInput.checked) {
  wireframeInput.click();
}
