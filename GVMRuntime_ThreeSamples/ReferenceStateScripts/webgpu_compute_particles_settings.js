let inputs = [];
for ( let attempt = 0; inputs.length < 4 && attempt < 1000; attempt ++ ) {
	inputs = Array.from( document.querySelectorAll( 'input[type="range"]' ) );
	if ( inputs.length < 4 ) await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
}

/** Finds the Inspector range whose parameter row contains the requested label. */
function findParticleRange( label ) {

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
	[ findParticleRange( 'gravity' ), -0.006 ],
	[ findParticleRange( 'bounce' ), 0.72 ],
	[ findParticleRange( 'friction' ), 0.97 ],
	[ findParticleRange( 'size' ), 0.12 ]
] ) {
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( 'A WebGPU particle settings input was not found.' );
	}
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
