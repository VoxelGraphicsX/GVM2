// Locks the texture-threshold midpoint used by the deterministic transition.
params.sceneAnimate = true;
params.transitionAnimate = false;
params.transition = 0.5;
params.useTexture = true;
params.texture = 5;
params.threshold = 0.1;
renderTransitionPass.setTransition(params.transition);
renderTransitionPass.useTexture(params.useTexture);
renderTransitionPass.setTexture(textures[params.texture]);
renderTransitionPass.setTextureThreshold(params.threshold);
