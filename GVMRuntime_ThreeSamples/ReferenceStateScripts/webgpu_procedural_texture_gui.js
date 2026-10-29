let ranges = [];
let checks = [];
for ( let attempt = 0; ( ranges.length < 2 || checks.length < 1 ) && attempt < 1000; attempt ++ ) {
	ranges = Array.from( document.querySelectorAll( 'input[type="range"]' ) );
	checks = Array.from( document.querySelectorAll( 'input[type="checkbox"]' ) );
	if ( ranges.length < 2 || checks.length < 1 ) {
		await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
	}
}
for ( const [ input, value ] of [
	[ ranges[ 0 ], 7.25 ],
	[ ranges[ 1 ], 1.35 ]
] ) {
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( 'A WebGPU procedural-texture numeric input was not found.' );
	}
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
if ( ! ( checks[ 0 ] instanceof HTMLInputElement ) ) {
	throw new Error( 'The WebGPU procedural-texture auto-update input was not found.' );
}
checks[ 0 ].checked = false;
checks[ 0 ].dispatchEvent( new Event( 'input', { bubbles: true } ) );
checks[ 0 ].dispatchEvent( new Event( 'change', { bubbles: true } ) );
for ( const element of Array.from( document.body.children ) ) {
	if ( element.tagName !== 'CANVAS' && element.tagName !== 'SCRIPT' ) {
		element.remove();
	}
}
