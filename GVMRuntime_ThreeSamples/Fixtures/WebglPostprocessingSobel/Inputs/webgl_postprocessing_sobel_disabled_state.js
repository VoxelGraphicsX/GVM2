const enableInput = document.querySelector('.lil-gui input[type="checkbox"]');
if (!(enableInput instanceof HTMLInputElement)) {
  throw new Error('Expected the Sobel enable checkbox.');
}
enableInput.checked = false;
enableInput.dispatchEvent(new Event('change', { bubbles: true }));
