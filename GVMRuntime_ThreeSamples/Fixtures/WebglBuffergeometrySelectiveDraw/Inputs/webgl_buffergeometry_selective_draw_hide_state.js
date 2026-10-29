const hideLinesButton = document.getElementById('hideLines');
if (!(hideLinesButton instanceof HTMLAnchorElement)) {
  throw new Error('webgl_buffergeometry_selective_draw hide-lines control was not found.');
}
hideLinesButton.click();
