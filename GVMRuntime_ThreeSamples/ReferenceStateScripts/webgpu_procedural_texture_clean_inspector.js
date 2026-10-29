let proceduralTextureInputCount = 0;
for ( let attempt = 0; proceduralTextureInputCount < 3 && attempt < 1000; attempt ++ ) {
	proceduralTextureInputCount =
		document.querySelectorAll( 'input[type="range"], input[type="checkbox"]' ).length;
	if ( proceduralTextureInputCount < 3 ) {
		await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
	}
}
for ( const element of Array.from( document.body.children ) ) {
	if ( element.tagName !== 'CANVAS' && element.tagName !== 'SCRIPT' ) {
		element.remove();
	}
}
