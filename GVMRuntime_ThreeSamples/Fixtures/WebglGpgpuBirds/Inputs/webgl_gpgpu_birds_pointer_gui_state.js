const expectedNumericControllers = new Map([
  ['separation', 28],
  ['alignment', 17],
  ['cohesion', 31]
]);

const numericControllers = new Map();
for (let attempt = 0; numericControllers.size < expectedNumericControllers.size && attempt < 1000; attempt += 1) {
  numericControllers.clear();
  for (const controller of document.querySelectorAll('.lil-gui .controller.number')) {
    const name = controller.querySelector('.name')?.textContent?.trim();
    if (expectedNumericControllers.has(name)) {
      numericControllers.set(name, controller);
    }
  }
  if (numericControllers.size < expectedNumericControllers.size) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}

if (numericControllers.size !== expectedNumericControllers.size) {
  throw new Error('webgl_gpgpu_birds numeric GUI controllers were not found.');
}

for (const [name, expectedValue] of expectedNumericControllers) {
  const input = numericControllers.get(name)?.querySelector('input[type="number"]');
  if (!(input instanceof HTMLInputElement)) {
    throw new Error(`webgl_gpgpu_birds '${name}' controller has no numeric input.`);
  }
  input.value = String(expectedValue);
  input.dispatchEvent(new Event('input', { bubbles: true }));
  if (Number(input.value) !== expectedValue) {
    throw new Error(`webgl_gpgpu_birds '${name}' controller rejected ${expectedValue}.`);
  }
}
