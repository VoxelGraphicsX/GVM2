// Applies the shared non-default LightProbe GUI state inside the frozen r185
// example module before deterministic frame advancement and input replay.
API.lightProbeIntensity = 0.72;
API.directionalLightIntensity = 0.34;
API.envMapIntensity = 0.58;
lightProbe.intensity = API.lightProbeIntensity;
directionalLight.intensity = API.directionalLightIntensity;
mesh.material.envMapIntensity = API.envMapIntensity;
renderer.render(scene, camera);
