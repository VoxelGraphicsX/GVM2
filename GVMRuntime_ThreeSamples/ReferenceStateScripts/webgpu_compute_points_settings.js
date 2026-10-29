let inputs = [];
for ( let attempt = 0; inputs.length < 2 && attempt < 1000; attempt ++ ) {
	inputs = Array.from( document.querySelectorAll( 'input[type="range"]' ) );
	if ( inputs.length < 2 ) await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
}
for ( const [ input, value ] of [
	[ inputs[ 0 ], 0.85 ],
	[ inputs[ 1 ], 0.62 ]
] ) {
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( 'A WebGPU compute-points scale input was not found.' );
	}
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
