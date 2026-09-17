const expectedMorphValues = [ 0.65, 0.4 ];
const deadline = performance.now() + 10000;
let morphInputs = [];
while (morphInputs.length < expectedMorphValues.length && performance.now() < deadline) {
  morphInputs = Array.from(document.querySelectorAll('input[type="range"]'));
  if (morphInputs.length < expectedMorphValues.length) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (morphInputs.length < expectedMorphValues.length) {
  throw new Error(`Expected two webgpu morph inputs; found ${morphInputs.length}.`);
}
for (let index = 0; index < expectedMorphValues.length; index += 1) {
  const input = morphInputs[index];
  input.value = String(expectedMorphValues[index]);
  input.dispatchEvent(new Event('input', { bubbles: true }));
  input.dispatchEvent(new Event('change', { bubbles: true }));
}
const profilerToggle = document.querySelector('.profiler-toggle');
if (profilerToggle instanceof HTMLElement) {
  profilerToggle.style.display = 'none';
}
