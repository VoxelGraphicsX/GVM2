// Locks the r185 transition controls before the first deterministic frame.
params.sceneAnimate = true;
params.transitionAnimate = false;
params.transition = 0;
params.useTexture = true;
params.texture = 5;
params.threshold = 0.1;
renderTransitionPass.setTransition(params.transition);
renderTransitionPass.useTexture(params.useTexture);
renderTransitionPass.setTexture(textures[params.texture]);
renderTransitionPass.setTextureThreshold(params.threshold);
