// Turns off the final HalftonePass through the real lil-gui checkbox.
const checkbox = [...document.querySelectorAll('.lil-gui input[type="checkbox"]')].at(-1);
if (!checkbox) throw new Error('RGB Halftone disable checkbox was not found.');
if (!checkbox.checked) checkbox.click();
