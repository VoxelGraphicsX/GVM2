const modelResponse = await fetch( 'models/json/suzanne_buffergeometry.json' );
if ( ! modelResponse.ok ) throw new Error( 'Could not preload the Suzanne geometry.' );
await modelResponse.arrayBuffer();
let methodSelect = null;
let countInput = null;
for ( let attempt = 0; ( methodSelect === null || countInput === null ) && attempt < 1000; attempt ++ ) {
	methodSelect = document.querySelector( '.lil-gui .controller.option select' );
	countInput = document.querySelector( '.lil-gui .controller.number input[type="number"]' );
	if ( methodSelect === null || countInput === null ) {
		await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
	}
}
if ( ! ( methodSelect instanceof HTMLSelectElement ) ||
	! ( countInput instanceof HTMLInputElement ) ) {
	throw new Error( 'The instancing method and count controls were not found.' );
}
methodSelect.value = 'NAIVE';
methodSelect.dispatchEvent( new Event( 'change', { bubbles: true } ) );
countInput.value = '1000';
countInput.dispatchEvent( new Event( 'input', { bubbles: true } ) );
await new Promise( ( resolve ) => setTimeout( resolve, 100 ) );
