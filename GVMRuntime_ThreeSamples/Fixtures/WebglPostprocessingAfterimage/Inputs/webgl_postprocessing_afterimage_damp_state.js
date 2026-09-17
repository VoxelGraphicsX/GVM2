const dampInput = document.querySelector('.lil-gui input[type="number"]');
if (!(dampInput instanceof HTMLInputElement)) {
  throw new Error('Expected the Afterimage damp range input.');
}
dampInput.value = '0.82';
dampInput.dispatchEvent(new Event('input', { bubbles: true }));
dampInput.dispatchEvent(new Event('change', { bubbles: true }));
