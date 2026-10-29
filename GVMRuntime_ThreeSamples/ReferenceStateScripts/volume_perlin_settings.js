const expectedValues = new Map([
	[ 'threshold', 0.55 ],
	[ 'steps', 160 ]
]);
const controllers = new Map();
for ( let attempt = 0; controllers.size < expectedValues.size && attempt < 1000; attempt ++ ) {
	controllers.clear();
	for ( const controller of document.querySelectorAll( '.lil-gui .controller.number' ) ) {
		const name = controller.querySelector( '.name' )?.textContent?.trim();
		if ( expectedValues.has( name ) ) {
			controllers.set( name, controller );
		}
	}
	if ( controllers.size < expectedValues.size ) {
		await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
	}
}
if ( controllers.size !== expectedValues.size ) {
	throw new Error( 'The volume threshold and step controllers were not found.' );
}
for ( const [ name, value ] of expectedValues ) {
	const input = controllers.get( name )?.querySelector( 'input[type="number"]' );
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( `The volume '${ name }' controller has no numeric input.` );
	}
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
const profilerToggle = document.querySelector( '.profiler-toggle' );
if ( profilerToggle instanceof HTMLElement ) {
	profilerToggle.style.display = 'none';
}
