const numberInputs = [...document.querySelectorAll('.list-item-wrapper input[type="number"]')];
if (numberInputs.length !== 2) {
  throw new Error(`Height fog canonical state expected two number inputs, found ${numberInputs.length}.`);
}
numberInputs[0].value = '0.0725';
numberInputs[0].dispatchEvent(new Event('change', { bubbles: true }));
numberInputs[1].value = '-1.5';
numberInputs[1].dispatchEvent(new Event('change', { bubbles: true }));
