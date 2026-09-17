const expectedSkyValues = new Map([
	[ 'turbidity', 6 ],
	[ 'rayleigh', 2.2 ],
	[ 'mieCoefficient', 0.018 ],
	[ 'mieDirectionalG', 0.82 ],
	[ 'elevation', 25 ],
	[ 'azimuth', 135 ],
	[ 'exposure', 0.7 ],
	[ 'coverage', 0.68 ],
	[ 'density', 0.72 ]
]);
const skyControllers = new Map();
for ( let attempt = 0; skyControllers.size < expectedSkyValues.size && attempt < 1000; attempt ++ ) {
	skyControllers.clear();
	for ( const controller of document.querySelectorAll( '.lil-gui .controller' ) ) {
		const name = controller.querySelector( '.name' )?.textContent?.trim();
		if ( expectedSkyValues.has( name ) && ! skyControllers.has( name ) ) skyControllers.set( name, controller );
	}
	if ( skyControllers.size < expectedSkyValues.size ) {
		await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
	}
}
for ( const [ name, value ] of expectedSkyValues ) {
	const input = skyControllers.get( name )?.querySelector( 'input[type="number"]' );
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( `The sky '${ name }' numeric input was not found.` );
	}
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
}
const elevationControllers = Array.from(
	document.querySelectorAll( '.lil-gui .controller.number' )
).filter( ( controller ) =>
	controller.querySelector( '.name' )?.textContent?.trim() === 'elevation' );
const cloudElevationInput = elevationControllers.at( -1 )?.querySelector( 'input[type="number"]' );
if ( ! ( cloudElevationInput instanceof HTMLInputElement ) ) {
	throw new Error( 'The cloud elevation input was not found.' );
}
cloudElevationInput.value = '0.3';
cloudElevationInput.dispatchEvent( new Event( 'input', { bubbles: true } ) );
const sunDiscInput = document.querySelector( '.lil-gui .controller.boolean input[type="checkbox"]' );
if ( ! ( sunDiscInput instanceof HTMLInputElement ) ) {
	throw new Error( 'The sky sun-disc checkbox was not found.' );
}
sunDiscInput.checked = false;
sunDiscInput.dispatchEvent( new Event( 'change', { bubbles: true } ) );
