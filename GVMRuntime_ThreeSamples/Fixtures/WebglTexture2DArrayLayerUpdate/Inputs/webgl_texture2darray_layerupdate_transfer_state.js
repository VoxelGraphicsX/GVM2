let sourceController;
let destinationController;
let transferButton;

for (let attempt = 0; attempt < 1000; attempt += 1) {
  for (const controller of document.querySelectorAll('.lil-gui .controller')) {
    const name = controller.querySelector('.name')?.textContent?.trim();
    if (name === 'srcLayer') sourceController = controller;
    if (name === 'destLayer') destinationController = controller;
    if (name === 'transfer') {
      transferButton = controller.querySelector('button');
    }
  }
  if (sourceController && destinationController && transferButton) break;
  await new Promise((resolve) => setTimeout(resolve, 10));
}

const sourceInput = sourceController?.querySelector('input[type="number"]');
const destinationInput = destinationController?.querySelector('input[type="number"]');
if (!(sourceInput instanceof HTMLInputElement)
    || !(destinationInput instanceof HTMLInputElement)
    || !(transferButton instanceof HTMLButtonElement)) {
  throw new Error('Texture-array transfer GUI controls were not found.');
}

sourceInput.value = '4';
sourceInput.dispatchEvent(new Event('input', { bubbles: true }));
destinationInput.value = '1';
destinationInput.dispatchEvent(new Event('input', { bubbles: true }));
transferButton.click();
