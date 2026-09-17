let countController = null;
for (let attempt = 0; countController === null && attempt < 1000; attempt += 1) {
  for (const controller of document.querySelectorAll('.lil-gui .controller.number')) {
    if (controller.querySelector('.name')?.textContent?.trim() === 'count') {
      countController = controller;
      break;
    }
  }
  if (countController === null) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (countController === null) {
  throw new Error('webgl_instancing_raycast count controller was not found.');
}
const countInput = countController.querySelector('input[type="number"]');
if (!(countInput instanceof HTMLInputElement)) {
  throw new Error('webgl_instancing_raycast count controller has no numeric input.');
}
countInput.value = '125';
countInput.dispatchEvent(new Event('input', { bubbles: true }));
