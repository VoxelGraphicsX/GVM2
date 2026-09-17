const deadline = performance.now() + 10000;
let intensityInput = null;
while (intensityInput === null && performance.now() < deadline) {
  intensityInput = document.querySelector('input[type="range"]');
  if (intensityInput === null) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (intensityInput === null) {
  throw new Error('Expected the equirectangular background intensity input.');
}
intensityInput.value = '0.55';
intensityInput.dispatchEvent(new Event('input', { bubbles: true }));
intensityInput.dispatchEvent(new Event('change', { bubbles: true }));
const profilerToggle = document.querySelector('.profiler-toggle');
if (profilerToggle instanceof HTMLElement) {
  profilerToggle.style.display = 'none';
}
