renderer.setAnimationLoop( null );
await renderer.compileAsync( scene, camera );
renderer.render( scene, camera );
