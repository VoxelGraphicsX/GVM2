// Locks the linear, no-mask transition after two deterministic seconds.
params.sceneAnimate = true;
params.transitionAnimate = false;
params.transition = 0.5;
params.useTexture = false;
params.texture = 5;
params.threshold = 0.1;
renderTransitionPass.setTransition(params.transition);
renderTransitionPass.useTexture(params.useTexture);
renderTransitionPass.setTexture(textures[params.texture]);
renderTransitionPass.setTextureThreshold(params.threshold);
