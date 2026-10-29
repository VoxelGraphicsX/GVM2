const numericInputs = Array.from(
    document.querySelectorAll( 'input[type="number"]' ) );
const changedValues = new Map( [
    [ 0, '0.42' ],
    [ 1, '-0.4' ],
    [ 2, '0.35' ],
    [ 3, '5' ],
    [ 4, '2.1' ],
    [ 5, '0.23' ],
    [ 8, '5' ],
    [ 9, '3.7' ],
    [ 10, '0.55' ]
] );
for ( const [ index, value ] of changedValues ) {

    const input = numericInputs[ index ];
    if ( ! input ) throw new Error( `Missing raging-sea numeric control ${ index }.` );
    input.value = value;
    input.dispatchEvent( new Event( 'change', { bubbles: true } ) );

}
const colorInputs = Array.from(
    document.querySelectorAll( 'input[type="color"]' ) );
if ( colorInputs.length < 2 ) throw new Error( 'Missing raging-sea emissive color control.' );
colorInputs[ 1 ].value = '#00d5ff';
colorInputs[ 1 ].dispatchEvent( new Event( 'input', { bubbles: true } ) );
colorInputs[ 1 ].dispatchEvent( new Event( 'change', { bubbles: true } ) );
