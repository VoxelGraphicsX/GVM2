if (effectController === undefined || teapot === undefined || textureCube === undefined) {
  throw new Error('The teapot and Pisa cubemap must be initialized before reflective replay.');
}
effectController.newShading = 'reflective';
render();
