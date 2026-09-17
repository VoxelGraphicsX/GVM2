#!/usr/bin/env node

import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { THREE_R185_COMMIT, readJson, readOption } from './manifest_common.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const repositoryRoot = path.resolve( toolsDirectory, '..', '..' );
const manifestDirectory = path.join( repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest' );
const defaultEvidencePath = path.join( manifestDirectory, 'three-r185-geometry-group-scene-roots.json' );
const defaultDecisionsPath = path.join( manifestDirectory, 'three-r185-builtin-geometry-group-decisions.json' );

/** Requires one repository-relative path without traversal. */
function requireRelativePath( sourcePath, label ) {

	if ( typeof sourcePath !== 'string' || sourcePath.length === 0 || path.isAbsolute( sourcePath ) || sourcePath.split( /[\\/]/u ).includes( '..' ) ) throw new Error( `${ label } must be a repository-relative path without traversal.` );
	return sourcePath;

}

/** Reads pinned upstream bytes from the checkout or its Git object. */
function readUpstreamBytes( upstreamRoot, sourcePath ) {

	const filePath = path.join( upstreamRoot, requireRelativePath( sourcePath, 'sourcePath' ) );
	if ( fs.existsSync( filePath ) ) return fs.readFileSync( filePath );
	return execFileSync( 'git', [ '-C', upstreamRoot, 'show', `HEAD:${ sourcePath }` ], { maxBuffer: 64 * 1024 * 1024 } );

}

/** Validates one exact pinned-source evidence record and returns its path. */
function validateSourceEvidence( upstreamRoot, evidence, label ) {

	if ( evidence === null || typeof evidence !== 'object' || Array.isArray( evidence ) ) throw new Error( `${ label } must be an object.` );
	const sourcePath = requireRelativePath( evidence.sourcePath, `${ label }.sourcePath` );
	if ( ! Number.isInteger( evidence.line ) || evidence.line <= 0 ) throw new Error( `${ label }.line must be a positive integer.` );
	if ( typeof evidence.excerpt !== 'string' || evidence.excerpt.length === 0 ) throw new Error( `${ label }.excerpt must be a non-empty string.` );
	const sourceLine = readUpstreamBytes( upstreamRoot, sourcePath ).toString( 'utf8' ).split( /\r?\n/u )[ evidence.line - 1 ];
	if ( sourceLine === undefined || ! sourceLine.includes( evidence.excerpt ) ) throw new Error( `${ label } does not match ${ sourcePath}:${ evidence.line}.` );
	return sourcePath;

}

/** Validates canonical root-scoped geometry-group evidence against decisions and pinned bytes. */
export function validateGeometryGroupSceneRootEvidence( inputs ) {

	const { evidenceDocument, decisions, upstreamRoot, decisionsPath = defaultDecisionsPath } = inputs;
	if ( evidenceDocument.schemaVersion !== 1 ) throw new Error( 'Geometry-group evidence schemaVersion must be 1.' );
	if ( evidenceDocument.evidenceKind !== 'three-r185-root-scoped-builtin-geometry-groups' ) throw new Error( 'Geometry-group evidence kind is invalid.' );
	if ( evidenceDocument.upstreamCommit !== THREE_R185_COMMIT ) throw new Error( `Geometry-group evidence commit must be ${ THREE_R185_COMMIT}.` );
	if ( decisions.schemaVersion !== 1 ) throw new Error( 'Geometry-group decisions schemaVersion must be 1.' );
	const revision = execFileSync( 'git', [ '-C', upstreamRoot, 'rev-parse', 'HEAD' ], { encoding: 'utf8' } ).trim();
	if ( revision !== THREE_R185_COMMIT ) throw new Error( `Three.js checkout must be ${ THREE_R185_COMMIT}, got ${ revision}.` );

	const decisionBytes = fs.readFileSync( decisionsPath );
	const decisionSha256 = crypto.createHash( 'sha256' ).update( decisionBytes ).digest( 'hex' );
	if ( evidenceDocument.decisionSource?.sourcePath !== 'GVMRuntime_ThreeSamples/Manifest/three-r185-builtin-geometry-group-decisions.json' || evidenceDocument.decisionSource?.sha256 !== decisionSha256 ) throw new Error( 'Geometry-group decision source identity is stale.' );

	const scope = evidenceDocument.scope;
	if ( scope?.auditedExamples !== 511 || scope.preCorrectionGroupedExamples !== 135 || scope.preCorrectionUngroupedExamples !== 376
		|| scope.ungroupedWithoutConstructorCandidate !== 309 || scope.maskedConstructorCandidates !== 67
		|| scope.falseToTrueCorrections !== 57 || scope.excludedAfterFlowAnalysis !== 10
		|| scope.existingTrueRootCorrections !== 1 || scope.correctedReviews !== 58
		|| scope.correctedGroupedSceneRoots !== 60 ) throw new Error( 'Geometry-group evidence scope accounting is stale.' );

	if ( ! Array.isArray( evidenceDocument.geometryGroupSceneRootEvidence ) || evidenceDocument.geometryGroupSceneRootEvidence.length !== 58 ) throw new Error( 'Geometry-group evidence must contain 58 corrected reviews.' );
	if ( ! Array.isArray( evidenceDocument.excludedCandidates ) || evidenceDocument.excludedCandidates.length !== 10 ) throw new Error( 'Geometry-group evidence must contain 10 excluded candidates.' );
	if ( ! Array.isArray( evidenceDocument.constructorContracts ) || evidenceDocument.constructorContracts.length === 0 ) throw new Error( 'Geometry-group evidence must contain constructor contracts.' );

	const decisionsById = new Map( decisions.positiveRootEvidence.map( function mapDecision( decision ) { return [ decision.id, decision ]; } ) );
	const seenIds = new Set();
	let groupedRootCount = 0;
	const referencedSourcePaths = new Set();
	for ( const [ evidenceIndex, record ] of evidenceDocument.geometryGroupSceneRootEvidence.entries() ) {

		const label = `geometryGroupSceneRootEvidence[${ evidenceIndex }]`;
		if ( typeof record.id !== 'string' || seenIds.has( record.id ) ) throw new Error( `${ label }.id must be unique.` );
		seenIds.add( record.id );
		const decision = decisionsById.get( record.id );
		if ( decision === undefined ) throw new Error( `${ label } has no canonical decision.` );
		if ( ! Array.isArray( record.sceneRoots ) || record.sceneRoots.length === 0 || new Set( record.sceneRoots ).size !== record.sceneRoots.length ) throw new Error( `${ label }.sceneRoots must be a non-empty unique list.` );
		if ( JSON.stringify( record.sceneRoots ) !== JSON.stringify( decision.sceneRoots ) ) throw new Error( `${ label}.sceneRoots does not match its canonical decision.` );
		if ( ! Array.isArray( record.evidence ) || record.evidence.length !== record.sceneRoots.length ) throw new Error( `${ label}.evidence must provide one record per grouped root.` );
		for ( const [ rootIndex, sourceEvidence ] of record.evidence.entries() ) {

			const selector = decision.selectors?.[ record.sceneRoots[ rootIndex ] ];
			if ( selector?.sourcePath !== undefined && selector.sourcePath !== sourceEvidence.sourcePath ) throw new Error( `${ label}.evidence[${ rootIndex }] violates its source selector.` );
			if ( selector?.contains !== undefined && ! sourceEvidence.excerpt.includes( selector.contains ) ) throw new Error( `${ label}.evidence[${ rootIndex }] violates its excerpt selector.` );
			referencedSourcePaths.add( validateSourceEvidence( upstreamRoot, sourceEvidence, `${ label}.evidence[${ rootIndex }]` ) );

		}
		groupedRootCount += record.sceneRoots.length;

	}
	if ( seenIds.size !== decisions.positiveRootEvidence.length || groupedRootCount !== 60 ) throw new Error( 'Geometry-group positive decision coverage is incomplete.' );

	const excludedDecisionsById = new Map( decisions.excludedCandidates.map( function mapDecision( decision ) { return [ decision.id, decision ]; } ) );
	for ( const [ exclusionIndex, exclusion ] of evidenceDocument.excludedCandidates.entries() ) {

		const label = `excludedCandidates[${ exclusionIndex }]`;
		const decision = excludedDecisionsById.get( exclusion.id );
		if ( decision === undefined || decision.reasonCode !== exclusion.reasonCode || decision.explanation !== exclusion.explanation ) throw new Error( `${ label } does not match its canonical exclusion decision.` );
		referencedSourcePaths.add( validateSourceEvidence( upstreamRoot, exclusion.candidateEvidence, `${ label}.candidateEvidence` ) );

	}
	for ( const [ contractIndex, contract ] of evidenceDocument.constructorContracts.entries() ) {

		if ( typeof contract.contract !== 'string' || ! Array.isArray( contract.evidence ) || contract.evidence.length === 0 ) throw new Error( `constructorContracts[${ contractIndex }] is incomplete.` );
		for ( const [ evidenceIndex, sourceEvidence ] of contract.evidence.entries() ) referencedSourcePaths.add( validateSourceEvidence( upstreamRoot, sourceEvidence, `constructorContracts[${ contractIndex }].evidence[${ evidenceIndex }]` ) );

	}

	if ( ! Array.isArray( evidenceDocument.sourceFiles ) ) throw new Error( 'Geometry-group evidence sourceFiles must be an array.' );
	const hashedSourcePaths = new Set();
	for ( const [ sourceIndex, sourceFile ] of evidenceDocument.sourceFiles.entries() ) {

		const sourcePath = requireRelativePath( sourceFile.sourcePath, `sourceFiles[${ sourceIndex }].sourcePath` );
		if ( hashedSourcePaths.has( sourcePath ) ) throw new Error( `sourceFiles repeats ${ sourcePath}.` );
		hashedSourcePaths.add( sourcePath );
		const sha256 = crypto.createHash( 'sha256' ).update( readUpstreamBytes( upstreamRoot, sourcePath ) ).digest( 'hex' );
		if ( sourceFile.sha256 !== sha256 ) throw new Error( `sourceFiles hash is stale for ${ sourcePath}.` );

	}
	if ( JSON.stringify( [ ...hashedSourcePaths ].sort() ) !== JSON.stringify( [ ...referencedSourcePaths ].sort() ) ) throw new Error( 'sourceFiles must exactly cover every referenced upstream source.' );
	return { correctedReviews: seenIds.size, correctedGroupedSceneRoots: groupedRootCount, excludedCandidates: evidenceDocument.excludedCandidates.length, sourceFiles: hashedSourcePaths.size };

}

/** Parses explicit validator inputs and reports canonical evidence counts. */
function main() {

	const argumentsList = process.argv.slice( 2 );
	const upstreamRoot = readOption( argumentsList, '--upstream-root', null );
	if ( upstreamRoot === null ) throw new Error( '--upstream-root is required.' );
	const evidencePath = readOption( argumentsList, '--evidence', defaultEvidencePath );
	const decisionsPath = readOption( argumentsList, '--decisions', defaultDecisionsPath );
	const result = validateGeometryGroupSceneRootEvidence( {
		evidenceDocument: readJson( evidencePath ),
		decisions: readJson( decisionsPath ),
		upstreamRoot,
		decisionsPath
	} );
	console.log( `Geometry-group root evidence passed: reviews=${ result.correctedReviews }, grouped_roots=${ result.correctedGroupedSceneRoots }, excluded=${ result.excludedCandidates }, source_files=${ result.sourceFiles }.` );

}

if ( process.argv[ 1 ] && path.resolve( process.argv[ 1 ] ) === fileURLToPath( import.meta.url ) ) main();
