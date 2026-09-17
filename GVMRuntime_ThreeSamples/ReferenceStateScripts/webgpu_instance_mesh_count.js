for ( let attempt = 0; mesh === undefined || mesh === null; attempt ++ ) {
	if ( attempt >= 1000 ) {
		throw new Error( 'The WebGPU instance mesh did not finish loading.' );
	}
	await new Promise( ( resolve ) => setTimeout( resolve, 10 ) );
}
mesh.count = 125;
