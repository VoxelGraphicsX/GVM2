const normalMapCheckbox = document.querySelector(
    '.lil-gui input[type="checkbox"]');
if (!(normalMapCheckbox instanceof HTMLInputElement)) {
    throw new Error('Normal-map checkbox was not available.');
}
if (normalMapCheckbox.checked) {
    normalMapCheckbox.click();
}
