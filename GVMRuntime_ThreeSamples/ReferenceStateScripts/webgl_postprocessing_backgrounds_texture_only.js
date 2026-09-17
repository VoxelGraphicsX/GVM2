/** Applies the canonical texture-only GUI state to the r185 example. */
function findController(label) {
  const controllers = Array.from(document.querySelectorAll('.lil-gui .controller'));
  return controllers.find((controller) => controller.textContent.trim().toLowerCase().startsWith(label)) ?? null;
}

function setCheckbox(label, value) {
  const input = findController(label)?.querySelector('input[type="checkbox"]');
  if (!(input instanceof HTMLInputElement)) throw new Error('Missing GUI checkbox: ' + label + '; html=' + (findController(label)?.outerHTML ?? 'none'));
  if (input.checked !== value) input.click();
}

function setNumber(label, value) {
  const input = findController(label)?.querySelector('input[type="number"]');
  if (!(input instanceof HTMLInputElement)) throw new Error(`Missing GUI number: ${label}`);
  input.value = String(value);
  input.dispatchEvent(new Event('input', { bubbles: true }));
  input.dispatchEvent(new Event('change', { bubbles: true }));
}

function setOption(label, value) {
  const input = findController(label)?.querySelector('select');
  if (!(input instanceof HTMLSelectElement)) throw new Error(`Missing GUI option: ${label}`);
  input.value = value;
  input.dispatchEvent(new Event('change', { bubbles: true }));
}

for (let attempt = 0; attempt < 1000 && document.querySelectorAll('.lil-gui .controller').length < 8; attempt += 1) {
  await new Promise((resolve) => setTimeout(resolve, 10));
}
if (document.querySelectorAll('.lil-gui .controller').length < 8) {
  throw new Error(`Background GUI did not initialize; inputs=${document.querySelectorAll('input').length}, selects=${document.querySelectorAll('select').length}, body=${document.body.textContent.slice(0, 500)}`);
}
setCheckbox('clearpass', true);
setOption('clearcolor', 'blue');
setNumber('clearalpha', 0.5);
setCheckbox('texturepass', true);
setNumber('texturepassopacity', 0.65);
setCheckbox('cubetexturepass', false);
setNumber('cubetexturepassopacity', 1.0);
setCheckbox('renderpass', false);
