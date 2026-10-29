const expectedMorphValues = new Map([
  [ 'Spherify', 0.65 ],
  [ 'Twist', 0.4 ]
]);
const deadline = performance.now() + 10000;
let morphControllers = [];
while (morphControllers.length !== expectedMorphValues.size && performance.now() < deadline) {
  morphControllers = Array.from(document.querySelectorAll('.lil-gui .controller.number'));
  if (morphControllers.length !== expectedMorphValues.size) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (morphControllers.length !== expectedMorphValues.size) {
  throw new Error(`Expected two webgl morph controllers; found ${morphControllers.length}.`);
}
for (const controller of morphControllers) {
  const label = controller.querySelector('.name')?.textContent?.trim();
  if (!expectedMorphValues.has(label)) {
    throw new Error(`Unexpected webgl morph controller '${label}'.`);
  }
  const input = controller.querySelector('input[type="number"]');
  if (!(input instanceof HTMLInputElement)) {
    throw new Error(`Webgl morph controller '${label}' has no numeric input.`);
  }
  input.value = String(expectedMorphValues.get(label));
  input.dispatchEvent(new Event('input', { bubbles: true }));
}
