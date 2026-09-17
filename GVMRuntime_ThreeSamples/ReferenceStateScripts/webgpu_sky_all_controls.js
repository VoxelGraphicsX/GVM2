let ranges = [];
let checks = [];
for ( let attempt = 0; ( ranges.length < 10 || checks.length < 1 ) && attempt < 1000; attempt ++ ) {
	ranges = Array.from( document.querySelectorAll( 'input[type="range"]' ) );
	checks = Array.from( document.querySelectorAll( 'input[type="checkbox"]' ) );
	if ( ranges.length < 10 || checks.length < 1 ) {
		await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
	}
}
for ( const [ input, value ] of [
	[ ranges[ 0 ], 6 ],
	[ ranges[ 1 ], 2.2 ],
	[ ranges[ 2 ], 0.018 ],
	[ ranges[ 3 ], 0.82 ],
	[ ranges[ 4 ], 25 ],
	[ ranges[ 5 ], 135 ],
	[ ranges[ 6 ], 0.7 ],
	[ ranges[ 7 ], 0.68 ],
	[ ranges[ 8 ], 0.72 ],
	[ ranges[ 9 ], 0.3 ]
] ) {
	if ( ! ( input instanceof HTMLInputElement ) ) {
		throw new Error( 'A WebGPU sky numeric input was not found.' );
	}
	input.value = String( value );
	input.dispatchEvent( new Event( 'input', { bubbles: true } ) );
	input.dispatchEvent( new Event( 'change', { bubbles: true } ) );
}
checks[ 0 ].checked = false;
checks[ 0 ].dispatchEvent( new Event( 'change', { bubbles: true } ) );
