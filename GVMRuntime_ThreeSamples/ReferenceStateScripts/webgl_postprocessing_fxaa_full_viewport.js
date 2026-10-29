const container = document.getElementById('container');
if (container === null) {
  throw new Error('webgl_postprocessing_fxaa container was not found.');
}
container.style.top = '0px';
container.style.height = '500px';
container.style.bottom = 'auto';
window.dispatchEvent(new Event('resize'));
await new Promise((resolve) => setTimeout(resolve, 0));
if (container.offsetWidth !== 800 || container.offsetHeight !== 500) {
  throw new Error(
    `Expected an 800x500 FXAA render surface, received ${container.offsetWidth}x${container.offsetHeight}.`
  );
}
