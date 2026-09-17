let inputs = [];
for ( let attempt = 0; inputs.length < 3 && attempt < 1000; attempt ++ ) {
	inputs = Array.from( document.querySelectorAll( 'input[type="range"]' ) );
	if ( inputs.length < 3 ) await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
}
for ( const [ input, value ] of [
	[ inputs[ 0 ], 2 ],
	[ inputs[ 1 ], 9 ],
	[ inputs[ 2 ], 1.8 ]
] ) {
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( 'A WebGPU instance-points settings input was not found.' );
	}
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
