if (!mesh || !mesh.material || !renderer) {
  throw new Error('Matcap initial assets are not ready.');
}
render();
