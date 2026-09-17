const expectedProcedure = 'noiseRandom2D';
let procedureSelect = null;
for (let attempt = 0; procedureSelect === null && attempt < 1000; attempt += 1) {
  procedureSelect = document.querySelector('.lil-gui .controller.option select');
  if (procedureSelect === null) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (!(procedureSelect instanceof HTMLSelectElement)) {
  throw new Error('webgl_postprocessing_procedural procedure controller was not found.');
}
procedureSelect.addEventListener('pointerup', () => {
  procedureSelect.value = expectedProcedure;
  procedureSelect.dispatchEvent(new Event('change', { bubbles: true }));
  if (procedureSelect.value !== expectedProcedure) {
    throw new Error(`Could not select procedural mode '${expectedProcedure}'.`);
  }
}, { once: true });
