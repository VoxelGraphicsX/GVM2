const expectedControllers = new Map([
  [ 'offset.x', 0.2 ],
  [ 'offset.y', 0.1 ],
  [ 'repeat.x', 0.75 ],
  [ 'repeat.y', 0.5 ],
  [ 'rotation', -0.6 ],
  [ 'center.x', 0.3 ],
  [ 'center.y', 0.7 ]
]);

const deadline = performance.now() + 10000;
let controllers = [];
while (controllers.length !== expectedControllers.size && performance.now() < deadline) {
  controllers = Array.from(document.querySelectorAll('.lil-gui .controller.number'));
  if (controllers.length !== expectedControllers.size) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (controllers.length !== expectedControllers.size) {
  throw new Error(`Expected ${expectedControllers.size} texture-transform controllers; found ${controllers.length}.`);
}

for (const controller of controllers) {
  const label = controller.querySelector('.name')?.textContent?.trim();
  if (!expectedControllers.has(label)) {
    throw new Error(`Unexpected texture-transform controller '${label}'.`);
  }
  const input = controller.querySelector('input[type="number"]');
  if (!(input instanceof HTMLInputElement)) {
    throw new Error(`Texture-transform controller '${label}' has no numeric input.`);
  }
  input.value = String(expectedControllers.get(label));
  input.dispatchEvent(new Event('input', { bubbles: true }));
}
