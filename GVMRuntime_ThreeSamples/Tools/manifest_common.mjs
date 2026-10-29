import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const THREE_R185_COMMIT = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
export const THREE_R185_RELEASE = 'r185';
export const EXPECTED_TOTAL = 588;
export const EXPECTED_EXCLUDED = 77;
export const EXPECTED_AUDIT_POPULATION = 511;
export const VALID_STATUSES = new Set( [
	'excluded_upstream',
	'audit_pending',
	'phase1_required',
	'deferred_missing_capability'
] );

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
export const manifestDirectory = path.resolve( toolsDirectory, '..', 'Manifest' );
export const defaultManifestPath = path.join( manifestDirectory, 'three-r185-manifest.json' );
export const defaultInventoryPath = path.join( manifestDirectory, 'upstream-files-r185.json' );
export const defaultExclusionsPath = path.join( manifestDirectory, 'three-r185-exclusions.json' );

/** Reads and parses a JSON document from an explicit filesystem path. */
export function readJson( filePath ) {

	return JSON.parse( fs.readFileSync( filePath, 'utf8' ) );

}

/** Serializes a manifest deterministically with one reviewable line per example. */
export function serializeJson( value ) {

	if ( ! Array.isArray( value.examples ) ) return `${ JSON.stringify( value, null, 2 ) }\n`;
	const header = { ...value };
	delete header.examples;
	const headerLines = JSON.stringify( header, null, 2 ).split( '\n' );
	headerLines.pop();
	const exampleLines = value.examples.map( function serializeExample( example, index ) {

		const suffix = index + 1 === value.examples.length ? '' : ',';
		return `    ${ JSON.stringify( example ) }${ suffix }`;

	} );
	return `${ headerLines.join( '\n' ) },\n  "examples": [\n${ exampleLines.join( '\n' ) }\n  ]\n}\n`;

}

/** Parses a value-bearing command-line option without consulting environment variables. */
export function readOption( argumentsList, optionName, fallbackValue ) {

	const optionIndex = argumentsList.indexOf( optionName );
	if ( optionIndex === - 1 ) return fallbackValue;
	if ( optionIndex + 1 >= argumentsList.length ) {

		throw new Error( `Missing value for ${ optionName }.` );

	}

	return path.resolve( argumentsList[ optionIndex + 1 ] );

}

/** Returns a stable map from excluded example ID to its complete reason record. */
export function buildExclusionMap( exclusionSource ) {

	if ( exclusionSource.upstreamCommit !== THREE_R185_COMMIT ) {

		throw new Error( `Exclusion source commit must be ${ THREE_R185_COMMIT }.` );

	}

	const exclusionMap = new Map();
	for ( const [ reasonCode, exampleIds ] of Object.entries( exclusionSource.excludedByReason ) ) {

		for ( const exampleId of exampleIds ) {

			const existing = exclusionMap.get( exampleId );
			if ( existing ) {

				existing.secondaryReasonCodes.push( reasonCode );
				continue;

			}

			exclusionMap.set( exampleId, {
				reasonCode,
				secondaryReasonCodes: [],
				upstreamSourceEvidence: [
					exclusionSource.upstreamEvidence.inventory,
					exclusionSource.upstreamEvidence.officialExceptionList
				]
			} );

		}

	}

	return exclusionMap;

}

/** Counts manifest statuses and derives the fixed phase-1 accounting values. */
export function countStatuses( examples ) {

	const counts = {
		total: examples.length,
		excludedUpstream: 0,
		auditPopulation: 0,
		auditPending: 0,
		phase1Required: 0,
		deferredMissingCapability: 0
	};

	for ( const example of examples ) {

		switch ( example.status ) {

			case 'excluded_upstream': counts.excludedUpstream ++; break;
			case 'audit_pending': counts.auditPending ++; break;
			case 'phase1_required': counts.phase1Required ++; break;
			case 'deferred_missing_capability': counts.deferredMissingCapability ++; break;
			default: throw new Error( `Unknown status '${ example.status }' for ${ example.id }.` );

		}

	}

	counts.auditPopulation = counts.auditPending + counts.phase1Required + counts.deferredMissingCapability;
	return counts;

}

/** Produces a concise human-readable status summary for command-line validation. */
export function formatCounts( counts ) {

	return [
		`total=${ counts.total }`,
		`excluded_upstream=${ counts.excludedUpstream }`,
		`audit_population=${ counts.auditPopulation }`,
		`audit_pending=${ counts.auditPending }`,
		`phase1_required=${ counts.phase1Required }`,
		`deferred_missing_capability=${ counts.deferredMissingCapability }`
	].join( ' ' );

}
