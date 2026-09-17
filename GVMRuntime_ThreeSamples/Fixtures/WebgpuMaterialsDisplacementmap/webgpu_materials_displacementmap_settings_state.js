const numericInputs = Array.from(
    document.querySelectorAll( 'input[type="number"]' ) );
const changedValues = new Map( [
    [ 0, '0.65' ],
    [ 1, '0.35' ],
    [ 5, '1.25' ],
    [ 6, '0.5' ]
] );
for ( const [ index, value ] of changedValues ) {

    const input = numericInputs[ index ];
    if ( ! input ) throw new Error( `Missing displacement-map numeric control ${ index }.` );
    input.value = value;
    input.dispatchEvent( new Event( 'change', { bubbles: true } ) );

}
