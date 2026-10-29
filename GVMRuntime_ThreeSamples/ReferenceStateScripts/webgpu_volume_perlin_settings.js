let inputs = [];
for ( let attempt = 0; inputs.length < 2 && attempt < 1000; attempt ++ ) {
	inputs = Array.from( document.querySelectorAll( 'input[type="range"]' ) );
	if ( inputs.length < 2 ) {
		await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
	}
}
if ( inputs.length < 2 ) {
	throw new Error( 'The WebGPU volume threshold and step inputs were not found.' );
}
for ( const [ input, value ] of [
	[ inputs[ 0 ], 0.55 ],
	[ inputs[ 1 ], 160 ]
] ) {
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
const profilerToggle = document.querySelector( '.profiler-toggle' );
if ( profilerToggle instanceof HTMLElement ) {
	profilerToggle.style.display = 'none';
}
