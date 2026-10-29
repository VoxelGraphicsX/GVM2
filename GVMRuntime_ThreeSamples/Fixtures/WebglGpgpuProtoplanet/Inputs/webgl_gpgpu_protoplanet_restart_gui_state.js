const expectedNumericControllers = new Map([
  ['gravityConstant', 175],
  ['density', 0.72],
  ['radius', 260],
  ['height', 12]
]);

const numericControllers = new Map();
let restartController = null;
for (let attempt = 0; attempt < 1000; attempt += 1) {
  numericControllers.clear();
  restartController = null;
  for (const controller of document.querySelectorAll('.lil-gui .controller')) {
    const name = controller.querySelector('.name')?.textContent?.trim();
    if (expectedNumericControllers.has(name)) {
      numericControllers.set(name, controller);
    } else if (name === 'restartSimulation') {
      restartController = controller;
    }
  }
  if (numericControllers.size === expectedNumericControllers.size && restartController !== null) {
    break;
  }
  await new Promise((resolve) => setTimeout(resolve, 10));
}

if (numericControllers.size !== expectedNumericControllers.size || restartController === null) {
  throw new Error('webgl_gpgpu_protoplanet canonical GUI controllers were not found.');
}

for (const [name, expectedValue] of expectedNumericControllers) {
  const input = numericControllers.get(name)?.querySelector('input[type="number"]');
  if (!(input instanceof HTMLInputElement)) {
    throw new Error(`webgl_gpgpu_protoplanet '${name}' controller has no numeric input.`);
  }
  input.value = String(expectedValue);
  input.dispatchEvent(new Event('input', { bubbles: true }));
  if (Number(input.value) !== expectedValue) {
    throw new Error(`webgl_gpgpu_protoplanet '${name}' controller rejected ${expectedValue}.`);
  }
}

const restartButton = restartController.querySelector('button');
if (!(restartButton instanceof HTMLButtonElement)) {
  throw new Error('webgl_gpgpu_protoplanet restart controller has no button.');
}
restartButton.click();
