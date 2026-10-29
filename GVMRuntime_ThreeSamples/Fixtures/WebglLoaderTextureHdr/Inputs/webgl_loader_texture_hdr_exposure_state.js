const deadline = performance.now() + 10000;
let input = null;
while (!(input instanceof HTMLInputElement) && performance.now() < deadline) {
  input = document.querySelector('.lil-gui .controller.number input[type="number"]');
  if (!(input instanceof HTMLInputElement)) {
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}
if (!(input instanceof HTMLInputElement)) {
  throw new Error('HDR exposure controller did not become available.');
}
input.value = '0.65';
input.dispatchEvent(new Event('input', { bubbles: true }));
if (Number(input.value) !== 0.65) {
  throw new Error(`HDR exposure controller resolved to ${input.value}; expected 0.65.`);
}
