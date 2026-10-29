const controllers = new Map();
for ( let attempt = 0; controllers.size < 7 && attempt < 1000; attempt ++ ) {
	controllers.clear();
	for ( const controller of document.querySelectorAll( '.lil-gui .controller' ) ) {
		const name = controller.querySelector( '.name' )?.textContent?.trim();
		if ( name ) controllers.set( name, controller );
	}
	if ( controllers.size < 7 ) {
		await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
	}
}
const environmentButton = controllers.get( 'Equirectangular' )?.querySelector( 'button' );
if ( ! ( environmentButton instanceof HTMLButtonElement ) ) {
	throw new Error( 'The equirectangular environment button was not found.' );
}
environmentButton.click();
for ( const name of [
	'Refraction',
	'backgroundRotationX',
	'backgroundRotationY',
	'backgroundRotationZ',
	'syncMaterial'
] ) {
	const input = controllers.get( name )?.querySelector( 'input[type="checkbox"]' );
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( `The environment '${ name }' checkbox was not found.` );
	}
	if ( ! input.checked ) input.click();
}
