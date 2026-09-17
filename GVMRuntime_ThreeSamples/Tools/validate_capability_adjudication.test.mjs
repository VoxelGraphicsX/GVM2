import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { buildCapabilityAdjudication } from './generate_capability_adjudication.mjs';
import { validateCapabilityAdjudication } from './validate_capability_adjudication.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const repositoryRoot = path.resolve( toolsDirectory, '..', '..' );
const manifestDirectory = path.resolve( toolsDirectory, '..', 'Manifest' );

/** Reads one checked-in JSON input for validator tests. */
function readManifestJson( fileName ) {

	return JSON.parse( fs.readFileSync( path.join( manifestDirectory, fileName ), 'utf8' ) );

}

/** Clones a JSON-compatible document so negative tests cannot mutate shared fixtures. */
function cloneDocument( document ) {

	return JSON.parse( JSON.stringify( document ) );

}

/** Materializes only the exact upstream evidence lines needed by the structural validator. */
function createSyntheticUpstream( adjudication ) {

	const upstreamRoot = fs.mkdtempSync( path.join( os.tmpdir(), 'three-r185-adjudication-' ) );
	const sourceMap = new Map();
	for ( const decision of adjudication.decisions ) {

		for ( const evidence of decision.upstreamSourceEvidence ) {

			const lineMap = sourceMap.get( evidence.sourcePath ) ?? new Map();
			const excerpts = lineMap.get( evidence.line ) ?? [];
			excerpts.push( evidence.excerpt );
			lineMap.set( evidence.line, excerpts );
			sourceMap.set( evidence.sourcePath, lineMap );

		}

	}
	for ( const [ sourcePath, lineMap ] of sourceMap ) {

		const maximumLine = Math.max( ...lineMap.keys() );
		const lines = Array.from( { length: maximumLine }, () => '' );
		for ( const [ line, excerpts ] of lineMap ) lines[ line - 1 ] = excerpts.join( ' ' );
		const destinationPath = path.join( upstreamRoot, sourcePath );
		fs.mkdirSync( path.dirname( destinationPath ), { recursive: true } );
		fs.writeFileSync( destinationPath, `${ lines.join( '\n' ) }\n` );

	}
	return upstreamRoot;

}

const audit = readManifestJson( 'three-r185-capability-audit.json' );
const freeze = readManifestJson( 'phase1-capability-freeze.json' );
const adjudication = readManifestJson( 'three-r185-capability-adjudication.json' );

test( 'generated capability adjudication exactly matches the checked-in decision file', () => {

	assert.deepEqual( buildCapabilityAdjudication( audit, freeze ), adjudication );

} );

test( 'validator covers every audited pair and verifies exact source evidence', () => {

	const upstreamRoot = createSyntheticUpstream( adjudication );
	try {

		const counts = validateCapabilityAdjudication( {
			audit,
			freeze,
			adjudication,
			upstreamRoot,
			sourceRoot: repositoryRoot
		} );
		assert.equal( counts.candidateExampleCount, audit.counts.examplesWithMissingCapabilityCandidates );
		assert.equal( counts.decisionCount, audit.examples.reduce( ( total, example ) => (
			total + example.missingCapabilityCandidates.length
		), 0 ) );
		assert.equal( counts.byDecision.needs_further_review, 0 );

	} finally {

		fs.rmSync( upstreamRoot, { recursive: true, force: true } );

	}

} );

test( 'validator rejects a missing candidate decision', () => {

	const invalidAdjudication = cloneDocument( adjudication );
	invalidAdjudication.decisions.pop();
	const upstreamRoot = createSyntheticUpstream( adjudication );
	try {

		assert.throws( () => validateCapabilityAdjudication( {
			audit,
			freeze,
			adjudication: invalidAdjudication,
			upstreamRoot,
			sourceRoot: repositoryRoot
		} ), /misses 1 pair/ );

	} finally {

		fs.rmSync( upstreamRoot, { recursive: true, force: true } );

	}

} );

test( 'validator rejects evidence that no longer matches its recorded line', () => {

	const invalidAdjudication = cloneDocument( adjudication );
	invalidAdjudication.decisions[ 0 ].upstreamSourceEvidence[ 0 ].excerpt = 'not present in the upstream source';
	const upstreamRoot = createSyntheticUpstream( adjudication );
	try {

		assert.throws( () => validateCapabilityAdjudication( {
			audit,
			freeze,
			adjudication: invalidAdjudication,
			upstreamRoot,
			sourceRoot: repositoryRoot
		} ), /omits detector evidence/ );

	} finally {

		fs.rmSync( upstreamRoot, { recursive: true, force: true } );

	}

} );
