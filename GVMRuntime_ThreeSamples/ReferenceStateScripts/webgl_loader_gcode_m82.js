let assetSelect = null;
for (let attempt = 0; assetSelect === null && attempt < 1000; attempt += 1) {
  assetSelect = document.querySelector('.lil-gui .controller.option select');
  if (assetSelect === null) await new Promise((resolve) => setTimeout(resolve, 10));
}
if (!(assetSelect instanceof HTMLSelectElement)) throw new Error('Missing GCode asset selector.');
assetSelect.value = 'test_m82';
assetSelect.dispatchEvent(new Event('change', { bubbles: true }));
await new Promise((resolve) => setTimeout(resolve, 100));
