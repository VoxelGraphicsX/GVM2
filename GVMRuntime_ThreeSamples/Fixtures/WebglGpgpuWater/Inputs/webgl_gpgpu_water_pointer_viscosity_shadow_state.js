const expectedNumericControllers = new Map([
  ['viscosity', 0.97]
]);

let viscosityController = null;
let shadowController = null;
for (let attempt = 0;
     (viscosityController === null || shadowController === null) && attempt < 1000;
     attempt += 1) {
  for (const controller of document.querySelectorAll('.lil-gui .controller')) {
    const name = controller.querySelector('.name')?.textContent?.trim();
    if (name === 'viscosity') viscosityController = controller;
    if (name === 'shadow') shadowController = controller;
  }
  if (viscosityController === null || shadowController === null) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}

if (viscosityController === null || shadowController === null) {
  throw new Error('webgl_gpgpu_water viscosity or shadow controller was not found.');
}

const viscosityInput = viscosityController.querySelector('input[type="number"]');
if (!(viscosityInput instanceof HTMLInputElement)) {
  throw new Error('webgl_gpgpu_water viscosity controller has no numeric input.');
}
viscosityInput.value = String(expectedNumericControllers.get('viscosity'));
viscosityInput.dispatchEvent(new Event('input', { bubbles: true }));
if (Number(viscosityInput.value) !== expectedNumericControllers.get('viscosity')) {
  throw new Error('webgl_gpgpu_water viscosity controller rejected 0.97.');
}

const shadowInput = shadowController.querySelector('input[type="checkbox"]');
if (!(shadowInput instanceof HTMLInputElement)) {
  throw new Error('webgl_gpgpu_water shadow controller has no checkbox input.');
}
shadowInput.checked = true;
shadowInput.dispatchEvent(new Event('change', { bubbles: true }));
if (!shadowInput.checked) {
  throw new Error('webgl_gpgpu_water shadow controller rejected true.');
}
