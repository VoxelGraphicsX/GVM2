let ranges = [];
for ( let attempt = 0; ranges.length < 4 && attempt < 1000; attempt ++ ) {
	ranges = Array.from( document.querySelectorAll( 'input[type="range"]' ) );
	if ( ranges.length < 4 ) await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
}

/** Finds the Inspector range whose parameter row contains the requested label. */
function findLabeledRange( label ) {

	return ranges.find( ( input ) => {

		let node = input;
		for ( let depth = 0; node && depth < 8; depth ++, node = node.parentElement ) {

			const text = node.textContent.trim().toLowerCase();
			if ( text.startsWith( label ) ) return true;

		}

		return false;

	} );

}

for ( const [ input, value ] of [
	[ findLabeledRange( 'weight' ), 0.7 ],
	[ findLabeledRange( 'decay' ), 0.9 ],
	[ findLabeledRange( 'sample count' ), 64 ],
	[ findLabeledRange( 'exposure' ), 8 ]
] ) {
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( 'A radial-blur numeric input was not found.' );
	}
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
