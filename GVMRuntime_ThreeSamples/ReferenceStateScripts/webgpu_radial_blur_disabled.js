let ranges = [];
let checks = [];
for ( let attempt = 0; ( ranges.length < 4 || checks.length < 2 ) && attempt < 1000; attempt ++ ) {
	ranges = Array.from( document.querySelectorAll( 'input[type="range"]' ) );
	checks = Array.from( document.querySelectorAll( 'input[type="checkbox"]' ) );
	if ( ranges.length < 4 || checks.length < 2 ) {
		await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
	}
}

/** Finds one Inspector input whose parameter row contains the requested label. */
function findLabeledInput( inputs, label ) {

	return inputs.find( ( input ) => {

		let node = input;
		for ( let depth = 0; node && depth < 8; depth ++, node = node.parentElement ) {

			const text = node.textContent.trim().toLowerCase();
			if ( text.startsWith( label ) ) return true;

		}

		return false;

	} );

}

for ( const [ input, value ] of [
	[ findLabeledInput( ranges, 'weight' ), 0.9 ],
	[ findLabeledInput( ranges, 'decay' ), 0.95 ],
	[ findLabeledInput( ranges, 'sample count' ), 32 ],
	[ findLabeledInput( ranges, 'exposure' ), 5 ]
] ) {
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( 'A radial-blur numeric input was not found.' );
	}
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
for ( const input of [
	findLabeledInput( checks, 'enabled' ),
	findLabeledInput( checks, 'animated' )
] ) {
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( 'A radial-blur boolean input was not found.' );
	}
	input.checked = false;
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
