// Applies the canonical disabled checkbox state before the deterministic replay.
const checkbox = document.querySelector('.lil-gui input[type="checkbox"]');
if (checkbox && checkbox.checked) checkbox.click();
