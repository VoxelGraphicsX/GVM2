const expectedParallaxValues = [0.8, 0.25, 4.5];
let parallaxControls = [];
for (let attempt = 0; parallaxControls.length < expectedParallaxValues.length && attempt < 1000; attempt += 1) {
  parallaxControls = [...document.querySelectorAll('.param-control')]
    .filter((control) => control.querySelector('input[type="range"]') && control.querySelector('input[type="number"]'))
    .slice(0, expectedParallaxValues.length);
  if (parallaxControls.length < expectedParallaxValues.length) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (parallaxControls.length !== expectedParallaxValues.length) {
  throw new Error('webgpu_parallax_uv canonical Inspector controllers were not found.');
}
for (let index = 0; index < expectedParallaxValues.length; index += 1) {
  const value = expectedParallaxValues[index];
  for (const input of parallaxControls[index].querySelectorAll('input[type="range"], input[type="number"]')) {
    input.value = String(value);
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new Event('change', { bubbles: true }));
  }
}
