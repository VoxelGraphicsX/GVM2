#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const repositoryRoot = path.resolve( toolsDirectory, '..', '..' );
const manifestDirectory = path.resolve( toolsDirectory, '..', 'Manifest' );
const defaultAuditPath = path.join( manifestDirectory, 'three-r185-capability-audit.json' );
const defaultFreezePath = path.join( manifestDirectory, 'phase1-capability-freeze.json' );
const defaultAdjudicationPath = path.join( manifestDirectory, 'three-r185-capability-adjudication.json' );
const defaultUpstreamRoot = path.join( repositoryRoot, 'build', 'three-r185-upstream' );
const expectedCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const validDecisions = new Set( [ 'deferred', 'expressible_existing_dsl', 'not_core_behavior', 'needs_further_review' ] );

/** Reads a path-valued CLI option without consulting environment variables. */
function readPathOption( argumentsList, optionName, fallbackPath ) {

	const optionIndex = argumentsList.indexOf( optionName );
	if ( optionIndex === - 1 ) return fallbackPath;
	if ( optionIndex + 1 >= argumentsList.length ) throw new Error( `Missing value for ${ optionName }.` );
	return path.resolve( argumentsList[ optionIndex + 1 ] );

}

/** Parses a JSON document from a required file. */
function readJson( filePath ) {

	return JSON.parse( fs.readFileSync( filePath, 'utf8' ) );

}

/** Builds the stable identity of one audited example/capability pair. */
function pairKey( exampleId, capability ) {

	return `${ exampleId }::${ capability }`;

}

/** Asserts a required non-empty string field. */
function requireText( value, label ) {

	if ( typeof value !== 'string' || value.trim().length === 0 ) throw new Error( `${ label } must be a non-empty string.` );

}

/** Validates one source excerpt against its exact recorded line. */
function validateEvidenceRecord( evidence, sourceRoot, label ) {

	requireText( evidence.path ?? evidence.sourcePath, `${ label }.path` );
	const relativePath = evidence.path ?? evidence.sourcePath;
	if ( path.isAbsolute( relativePath ) || relativePath.split( /[\\/]/ ).includes( '..' ) ) {

		throw new Error( `${ label} must use a repository-relative path.` );

	}
	if ( ! Number.isInteger( evidence.line ) || evidence.line <= 0 ) throw new Error( `${ label }.line must be a positive integer.` );
	requireText( evidence.excerpt, `${ label }.excerpt` );
	const sourcePath = path.join( sourceRoot, relativePath );
	if ( ! fs.existsSync( sourcePath ) ) throw new Error( `${ label } source does not exist: ${ sourcePath }.` );
	const sourceLines = fs.readFileSync( sourcePath, 'utf8' ).split( /\r?\n/ );
	const sourceLine = sourceLines[ evidence.line - 1 ];
	if ( sourceLine === undefined || ! sourceLine.includes( evidence.excerpt ) ) {

		throw new Error( `${ label } does not match ${ relativePath }:${ evidence.line }; expected excerpt '${ evidence.excerpt }'.` );

	}

}

/** Recomputes all aggregate counts from validated decision records. */
function computeCounts( decisions ) {

	const byDecision = {
		deferred: 0,
		expressible_existing_dsl: 0,
		not_core_behavior: 0,
		needs_further_review: 0
	};
	const byCapability = {};
	const candidateExamples = new Set();
	const deferredExamples = new Set();

	for ( const decision of decisions ) {

		byDecision[ decision.decision ] ++;
		candidateExamples.add( decision.exampleId );
		if ( decision.decision === 'deferred' ) deferredExamples.add( decision.exampleId );
		byCapability[ decision.capability ] ??= {
			candidateCount: 0,
			deferred: 0,
			expressible_existing_dsl: 0,
			not_core_behavior: 0,
			needs_further_review: 0
		};
		byCapability[ decision.capability ].candidateCount ++;
		byCapability[ decision.capability ][ decision.decision ] ++;

	}

	return {
		candidateExampleCount: candidateExamples.size,
		decisionCount: decisions.length,
		deferredExampleCount: deferredExamples.size,
		byDecision,
		byCapability
	};

}

/** Verifies that the checked-out upstream source matches the locked Three.js revision. */
function validateUpstreamRevision( upstreamRoot ) {

	if ( ! fs.existsSync( upstreamRoot ) ) throw new Error( `Three.js upstream root does not exist: ${ upstreamRoot }.` );
	let revision;
	try {

		revision = execFileSync( 'git', [ '-C', upstreamRoot, 'rev-parse', 'HEAD' ], { encoding: 'utf8' } ).trim();

	} catch ( error ) {

		throw new Error( `Cannot resolve Three.js upstream revision at ${ upstreamRoot }: ${ error.message }` );

	}
	if ( revision !== expectedCommit ) throw new Error( `Three.js upstream revision must be ${ expectedCommit }, got ${ revision }.` );

}

/** Validates complete pair coverage, semantic fields, source evidence, and deterministic counts. */
export function validateCapabilityAdjudication( { audit, freeze, adjudication, upstreamRoot, sourceRoot } ) {

	if ( audit.upstream.commit !== expectedCommit || adjudication.upstream.commit !== expectedCommit ) {

		throw new Error( `Audit and adjudication must both use Three.js commit ${ expectedCommit }.` );

	}
	if ( adjudication.policy?.manifestMutationAllowed !== false ) throw new Error( 'Adjudication must explicitly prohibit manifest mutation.' );
	if ( adjudication.policy?.publicCapabilityExpansionAllowed !== false ) throw new Error( 'Adjudication must explicitly prohibit public capability expansion.' );
	if ( ! Array.isArray( adjudication.decisions ) ) throw new Error( 'Adjudication decisions must be an array.' );

	const expectedPairs = new Map();
	const expectedOrder = [];
	for ( const example of audit.examples ) {

		for ( const candidate of example.missingCapabilityCandidates ) {

			const key = pairKey( example.id, candidate.capability );
			expectedPairs.set( key, { example, candidate } );
			expectedOrder.push( key );

		}

	}

	const seenPairs = new Set();
	const deferredReasonCodes = new Set( freeze.deferredReasonCodes );

	for ( let decisionIndex = 0; decisionIndex < adjudication.decisions.length; decisionIndex ++ ) {

		const decision = adjudication.decisions[ decisionIndex ];
		requireText( decision.exampleId, `decisions[${ decisionIndex }].exampleId` );
		requireText( decision.capability, `decisions[${ decisionIndex }].capability` );
		const key = pairKey( decision.exampleId, decision.capability );
		if ( seenPairs.has( key ) ) throw new Error( `Duplicate adjudication decision for ${ key }.` );
		seenPairs.add( key );
		const expected = expectedPairs.get( key );
		if ( ! expected ) throw new Error( `Adjudication contains a pair absent from the audit: ${ key }.` );
		if ( expectedOrder[ decisionIndex ] !== key ) throw new Error( `Decision order is not deterministic at index ${ decisionIndex }: expected ${ expectedOrder[ decisionIndex ] }, got ${ key }.` );
		if ( decision.upstreamPath !== expected.example.upstreamPath ) throw new Error( `${ key } has an incorrect upstreamPath.` );
		if ( ! validDecisions.has( decision.decision ) ) throw new Error( `${ key } has invalid decision '${ decision.decision }'.` );
		if ( decision.manifestMutationAllowed !== false ) throw new Error( `${ key } must prohibit manifest mutation.` );
		requireText( decision.coreBehaviorSummary, `${ key}.coreBehaviorSummary` );
		requireText( decision.reentryTest, `${ key}.reentryTest` );

		if ( ! Array.isArray( decision.upstreamSourceEvidence ) || decision.upstreamSourceEvidence.length === 0 ) {

			throw new Error( `${ key } must include upstream source evidence.` );

		}
		const recordedEvidenceKeys = new Set( decision.upstreamSourceEvidence.map( ( evidence ) => `${ evidence.sourcePath }:${ evidence.line }:${ evidence.excerpt }` ) );
		for ( const evidence of expected.candidate.evidence ) {

			const evidenceKey = `${ evidence.sourcePath }:${ evidence.line }:${ evidence.excerpt }`;
			if ( ! recordedEvidenceKeys.has( evidenceKey ) ) throw new Error( `${ key } omits detector evidence ${ evidenceKey }.` );

		}
		for ( let evidenceIndex = 0; evidenceIndex < decision.upstreamSourceEvidence.length; evidenceIndex ++ ) {

			validateEvidenceRecord( decision.upstreamSourceEvidence[ evidenceIndex ], upstreamRoot, `${ key}.upstreamSourceEvidence[${ evidenceIndex }]` );

		}

		if ( ! Array.isArray( decision.repositoryCapabilityEvidence ) || decision.repositoryCapabilityEvidence.length === 0 ) {

			throw new Error( `${ key } must include repository capability evidence.` );

		}
		for ( let evidenceIndex = 0; evidenceIndex < decision.repositoryCapabilityEvidence.length; evidenceIndex ++ ) {

			validateEvidenceRecord( decision.repositoryCapabilityEvidence[ evidenceIndex ], sourceRoot, `${ key}.repositoryCapabilityEvidence[${ evidenceIndex }]` );

		}

		if ( decision.decision === 'deferred' ) {

			if ( ! deferredReasonCodes.has( decision.reasonCode ) ) throw new Error( `${ key} has an invalid deferred reasonCode.` );
			requireText( decision.missingCapability, `${ key }.missingCapability` );
			requireText( decision.whyCurrentDslCannotExpressIt, `${ key }.whyCurrentDslCannotExpressIt` );
			requireText( decision.minimumFutureApi, `${ key }.minimumFutureApi` );
			if ( decision.existingDslEquivalentPath !== undefined ) throw new Error( `${ key } cannot contain an equivalent path when deferred.` );

		} else if ( decision.decision === 'needs_further_review' ) {

			requireText( decision.openQuestion, `${ key }.openQuestion` );

		} else {

			requireText( decision.existingDslEquivalentPath, `${ key }.existingDslEquivalentPath` );
			if ( decision.decision === 'not_core_behavior' ) requireText( decision.whyNotCoreBehavior, `${ key }.whyNotCoreBehavior` );
			if ( decision.reasonCode !== undefined || decision.minimumFutureApi !== undefined ) throw new Error( `${ key } contains deferred-only fields.` );

		}

	}

	if ( seenPairs.size !== expectedPairs.size ) {

		const missingPairs = [ ...expectedPairs.keys() ].filter( ( key ) => ! seenPairs.has( key ) );
		throw new Error( `Adjudication misses ${ missingPairs.length } pair(s): ${ missingPairs.join( ', ' ) }.` );

	}
	const recomputedCounts = computeCounts( adjudication.decisions );
	if ( JSON.stringify( recomputedCounts ) !== JSON.stringify( adjudication.counts ) ) throw new Error( 'Adjudication aggregate counts are stale or inconsistent.' );
	if ( recomputedCounts.candidateExampleCount !== audit.counts.examplesWithMissingCapabilityCandidates ) {

		throw new Error( `Adjudication candidate count ${ recomputedCounts.candidateExampleCount } does not match audit count ${ audit.counts.examplesWithMissingCapabilityCandidates }.` );

	}
	return recomputedCounts;

}

/** Runs the standalone validator with explicit file and upstream-root arguments. */
function main() {

	const argumentsList = process.argv.slice( 2 );
	const auditPath = readPathOption( argumentsList, '--audit', defaultAuditPath );
	const freezePath = readPathOption( argumentsList, '--freeze', defaultFreezePath );
	const adjudicationPath = readPathOption( argumentsList, '--adjudication', defaultAdjudicationPath );
	const upstreamRoot = readPathOption( argumentsList, '--upstream-root', defaultUpstreamRoot );
	validateUpstreamRevision( upstreamRoot );
	const counts = validateCapabilityAdjudication( {
		audit: readJson( auditPath ),
		freeze: readJson( freezePath ),
		adjudication: readJson( adjudicationPath ),
		upstreamRoot,
		sourceRoot: repositoryRoot
	} );
	console.log( `Capability adjudication valid: examples=${ counts.candidateExampleCount } decisions=${ counts.decisionCount } deferred_examples=${ counts.deferredExampleCount } needs_further_review=${ counts.byDecision.needs_further_review }.` );

}

if ( process.argv[ 1 ] && path.resolve( process.argv[ 1 ] ) === fileURLToPath( import.meta.url ) ) main();
