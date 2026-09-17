const sliders = [...document.querySelectorAll('.param-control input[type="range"]')];
if (sliders.length !== 2) {
  throw new Error(`Expected two compute-points bound sliders; found ${sliders.length}.`);
}
for (const [slider, value] of [[sliders[0], '0.85'], [sliders[1], '0.62']]) {
  slider.value = value;
  slider.dispatchEvent(new Event('input', {
    bubbles: true,
    cancelable: true,
    composed: true
  }));
}
