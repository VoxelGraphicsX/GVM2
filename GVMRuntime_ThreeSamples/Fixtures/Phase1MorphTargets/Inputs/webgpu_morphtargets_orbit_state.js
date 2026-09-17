const deadline = performance.now() + 10000;
let profilerToggle = null;
while (!(profilerToggle instanceof HTMLElement) && performance.now() < deadline) {
  profilerToggle = document.querySelector('.profiler-toggle');
  if (!(profilerToggle instanceof HTMLElement)) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (!(profilerToggle instanceof HTMLElement)) {
  throw new Error('Expected the WebGPU Inspector profiler toggle.');
}
profilerToggle.style.display = 'none';
