/** Applies the canonical cubemap-only GUI state for the orbit replay. */
function findController(label) {
  return Array.from(document.querySelectorAll('.lil-gui .controller'))
    .find((controller) => controller.textContent.trim().toLowerCase().startsWith(label)) ?? null;
}

function setCheckbox(label, value) {
  const input = findController(label)?.querySelector('input[type="checkbox"]');
  if (!(input instanceof HTMLInputElement)) throw new Error('Missing GUI checkbox: ' + label);
  if (input.checked !== value) input.click();
}

function setNumber(label, value) {
  const input = findController(label)?.querySelector('input[type="number"]');
  if (!(input instanceof HTMLInputElement)) throw new Error('Missing GUI number: ' + label);
  input.value = String(value);
  input.dispatchEvent(new Event('input', { bubbles: true }));
  input.dispatchEvent(new Event('change', { bubbles: true }));
}

for (let attempt = 0; attempt < 1000 && document.querySelectorAll('.lil-gui .controller').length < 8; attempt += 1) {
  await new Promise((resolve) => setTimeout(resolve, 10));
}
setCheckbox('clearpass', true);
setCheckbox('texturepass', false);
setNumber('texturepassopacity', 1.0);
setCheckbox('cubetexturepass', true);
setNumber('cubetexturepassopacity', 0.7);
setCheckbox('renderpass', true);
