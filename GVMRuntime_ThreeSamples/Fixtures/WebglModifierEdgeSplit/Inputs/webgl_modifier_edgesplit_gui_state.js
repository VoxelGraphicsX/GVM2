if (mesh === undefined || map === undefined) {
  throw new Error('Cerberus geometry and albedo must be loaded before canonical GUI state.');
}
params.showMap = true;
params.smoothShading = false;
params.edgeSplit = true;
params.cutOffAngle = 60;
params.tryKeepNormals = false;
updateMesh();
