if (!mesh || !mesh.material || !renderer) {
  throw new Error('Matcap custom assets are not ready.');
}
const droppedMatcap = await new Promise((resolve, reject) => {
  new THREE.TextureLoader().load(
    'textures/matcaps/matcap-porcelain-white.jpg',
    resolve,
    undefined,
    reject
  );
});
droppedMatcap.colorSpace = THREE.SRGBColorSpace;
updateMatcap(droppedMatcap);
API.color = 0x80c0ff;
API.exposure = 1.35;
mesh.material.color.set(API.color);
renderer.toneMappingExposure = API.exposure;
render();
