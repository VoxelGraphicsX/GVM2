// Applies the locked orbit camera while the replay disables pixel-aligned
// panning through the real r185 Inspector checkbox.
camera.position.set(0.25, 2 * Math.tan(Math.PI / 6), 2);
camera.lookAt(0, 0, 0);
camera.updateMatrixWorld();
