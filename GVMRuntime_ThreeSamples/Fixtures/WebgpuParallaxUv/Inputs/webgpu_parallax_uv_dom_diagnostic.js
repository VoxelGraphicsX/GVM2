const inputs = [...document.querySelectorAll('input')].map((input) => ({
  type: input.type,
  value: input.value,
  min: input.min,
  max: input.max,
  step: input.step,
  parent: input.parentElement?.outerHTML.slice(0, 500) ?? ''
}));
const customElements = [...new Set(
  [...document.querySelectorAll('*')]
    .map((element) => element.tagName.toLowerCase())
    .filter((tagName) => tagName.includes('-'))
)];
throw new Error(JSON.stringify({ inputs, customElements }));
