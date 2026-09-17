#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { applySingleSamplePolicy, buildManifest } from './generate_manifest.mjs';
import {
	THREE_R185_COMMIT,
	defaultExclusionsPath,
	defaultInventoryPath,
	defaultManifestPath,
	readJson,
	readOption,
	serializeJson
} from './manifest_common.mjs';
import { buildPhase1ManifestLock } from './phase1_manifest_lock_common.mjs';
import { validateCapabilityAdjudication } from './validate_capability_adjudication.mjs';
import { validateManifest } from './validate_manifest.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const repositoryRoot = path.resolve( toolsDirectory, '..', '..' );
const manifestDirectory = path.resolve( toolsDirectory, '..', 'Manifest' );
const defaultAuditPath = path.join( manifestDirectory, 'three-r185-capability-audit.json' );
const defaultAdjudicationPath = path.join( manifestDirectory, 'three-r185-capability-adjudication.json' );
const defaultStatusLockPath = path.join( manifestDirectory, 'three-r185-phase1-status-lock.json' );
const defaultReportPath = path.join( manifestDirectory, 'three-r185-phase1-lock-report.json' );
const validDeferredReasonCodes = new Set( [
	'deferred_missing_dsl_capability',
	'deferred_missing_renderset_capability',
	'deferred_missing_renderset_ordering',
	'deferred_missing_rhi_capability',
	'deferred_missing_native_line_rasterization',
	'deferred_unsupported_quadrant'
] );

/** Reads a mandatory explicit path option used for external source roots. */
function readRequiredPathOption( argumentsList, optionName ) {

	const optionIndex = argumentsList.indexOf( optionName );
	if ( optionIndex === - 1 || optionIndex + 1 >= argumentsList.length ) throw new Error( `${ optionName } is required.` );
	return path.resolve( argumentsList[ optionIndex + 1 ] );

}

/** Verifies the explicit Three.js checkout is exactly the pinned r185 source. */
function validateUpstreamRevision( upstreamRoot ) {

	const revision = execFileSync( 'git', [ '-C', upstreamRoot, 'rev-parse', 'HEAD' ], { encoding: 'utf8' } ).trim();
	if ( revision !== THREE_R185_COMMIT ) throw new Error( `Three.js upstream revision must be ${ THREE_R185_COMMIT }, got ${ revision }.` );

}

/** Validates one manual lock source excerpt against the exact source line. */
function validateLockEvidence( evidence, sourceRoot, label ) {

	if ( typeof evidence.sourcePath !== 'string' || evidence.sourcePath.length === 0 || path.isAbsolute( evidence.sourcePath ) || evidence.sourcePath.split( /[\\/]/ ).includes( '..' ) || ! Number.isInteger( evidence.line ) || evidence.line <= 0 || typeof evidence.excerpt !== 'string' || evidence.excerpt.length === 0 ) {

		throw new Error( `${ label } is not a complete source evidence record.` );

	}
	const sourcePath = path.join( sourceRoot, evidence.sourcePath );
	if ( ! fs.existsSync( sourcePath ) ) throw new Error( `${ label } source does not exist: ${ evidence.sourcePath }.` );
	const sourceLine = fs.readFileSync( sourcePath, 'utf8' ).split( /\r?\n/ )[ evidence.line - 1 ];
	if ( sourceLine === undefined || ! sourceLine.includes( evidence.excerpt ) ) {

		throw new Error( `${ label } does not match ${ evidence.sourcePath }:${ evidence.line }.` );

	}

}

/** Validates a canonical asset against both its Git blob identity and raw byte digest. */
function validateAssetEvidence( evidence, upstreamRoot, label ) {

	if ( typeof evidence.sourcePath !== 'string' || evidence.sourcePath.length === 0
		|| path.isAbsolute( evidence.sourcePath ) || evidence.sourcePath.split( /[\\/]/ ).includes( '..' )
		|| ! /^[a-f0-9]{40}$/.test( evidence.gitBlobSha1 ?? '' )
		|| ! /^[a-f0-9]{64}$/.test( evidence.sha256 ?? '' )
		|| typeof evidence.semantic !== 'string' || evidence.semantic.length === 0 ) {

		throw new Error( `${ label } is not a complete canonical asset evidence record.` );

	}
	const assetPath = path.join( upstreamRoot, evidence.sourcePath );
	let bytes;
	let gitBlobSha1;
	if ( fs.existsSync( assetPath ) ) {

		bytes = fs.readFileSync( assetPath );
		gitBlobSha1 = crypto.createHash( 'sha1' ).update( `blob ${ bytes.length }\0` ).update( bytes ).digest( 'hex' );

	} else {

		try {

			gitBlobSha1 = execFileSync( 'git', [ '-C', upstreamRoot, 'rev-parse', `HEAD:${ evidence.sourcePath }` ], { encoding: 'utf8' } ).trim();
			bytes = execFileSync( 'git', [ '-C', upstreamRoot, 'cat-file', 'blob', gitBlobSha1 ], { maxBuffer: 256 * 1024 * 1024 } );

		} catch ( error ) {

			throw new Error( `${ label } asset does not exist in the pinned checkout: ${ evidence.sourcePath } (${ error.message }).` );

		}

	}
	const sha256 = crypto.createHash( 'sha256' ).update( bytes ).digest( 'hex' );
	if ( sha256 !== evidence.sha256 ) throw new Error( `${ label } SHA-256 does not match ${ evidence.sourcePath }.` );
	if ( gitBlobSha1 !== evidence.gitBlobSha1 ) throw new Error( `${ label } Git blob SHA-1 does not match ${ evidence.sourcePath }.` );

}

/** Validates uniqueness, completeness, and real-source grounding of the manual status lock. */
export function validateStatusLock( statusLock, capabilityAudit, adjudication, upstreamRoot, sourceRoot ) {

	if ( statusLock.upstreamCommit !== THREE_R185_COMMIT ) throw new Error( `Status lock commit must be ${ THREE_R185_COMMIT }.` );
	if ( statusLock.policy?.publicCapabilityExpansionAllowed !== false ) throw new Error( 'Status lock must prohibit public capability expansion.' );
	if ( statusLock.policy?.requiredStatusNeedsCompleteEntryReview !== true ) throw new Error( 'Status lock must require complete-entry review before phase1_required.' );
	if ( statusLock.policy?.absenceOfDetectorSignalIsSupportEvidence !== false ) throw new Error( 'Status lock must reject absence of detector signals as support evidence.' );
	if ( statusLock.policy?.workloadOrMissingLoaderImplementationCanDefer !== false ) throw new Error( 'Status lock must reject workload and missing loader implementation as deferred reasons.' );
	const auditedIds = new Set( capabilityAudit.examples.map( function mapId( example ) { return example.id; } ) );
	const deferredIds = new Set( adjudication.decisions.filter( function isDeferred( decision ) { return decision.decision === 'deferred'; } ).map( function mapId( decision ) { return decision.exampleId; } ) );
	const deferredReclassificationIds = new Set();
	const seenIds = new Set();
	for ( const [ collectionName, reviews ] of [
		[ 'phase1RequiredReviews', statusLock.phase1RequiredReviews ],
		[ 'phase1DeferredReviews', statusLock.phase1DeferredReviews ],
		[ 'renderSetOverrides', statusLock.renderSetOverrides ]
	] ) {

		if ( ! Array.isArray( reviews ) ) throw new Error( `${ collectionName } must be an array.` );
		for ( let reviewIndex = 0; reviewIndex < reviews.length; reviewIndex ++ ) {

			const review = reviews[ reviewIndex ];
			const identity = `${ collectionName}:${ review.id }`;
			if ( ! auditedIds.has( review.id ) ) throw new Error( `${ identity } is not in the 511-case audit.` );
			if ( seenIds.has( review.id ) ) throw new Error( `Duplicate status lock record for ${ review.id }.` );
			seenIds.add( review.id );
			if ( typeof review.reviewScope !== 'string' || ! review.reviewScope.startsWith( 'complete_entry' ) ) throw new Error( `${ identity } lacks complete-entry review scope.` );
			if ( ! Array.isArray( review.upstreamSourceEvidence ) || review.upstreamSourceEvidence.length === 0 ) throw new Error( `${ identity } lacks upstream evidence.` );
			for ( let evidenceIndex = 0; evidenceIndex < review.upstreamSourceEvidence.length; evidenceIndex ++ ) {

				validateLockEvidence( review.upstreamSourceEvidence[ evidenceIndex ], upstreamRoot, `${ identity }.upstreamSourceEvidence[${ evidenceIndex }]` );

			}
			if ( review.renderSetPolicy !== 'required' && review.renderSetPolicy !== 'not-required' ) throw new Error( `${ identity } has an invalid RenderSet policy.` );

		}

	}
	for ( const review of statusLock.phase1RequiredReviews ) {

		if ( deferredIds.has( review.id ) ) throw new Error( `${ review.id } cannot be both phase1_required and deferred.` );
		if ( ! Array.isArray( review.requiredCapabilities ) || ! Array.isArray( review.availableCapabilities ) || review.requiredCapabilities.length === 0 || review.availableCapabilities.length === 0 ) throw new Error( `${ review.id } must enumerate reviewed capabilities.` );
		for ( const capability of review.requiredCapabilities ) if ( ! review.availableCapabilities.includes( capability ) ) throw new Error( `${ review.id } required capability '${ capability }' is not available.` );
		if ( ! Array.isArray( review.notes ) ) throw new Error( `${ review.id } must include review notes.` );
		if ( ! Array.isArray( review.repositoryCapabilityEvidence ) || review.repositoryCapabilityEvidence.length === 0 ) throw new Error( `${ review.id } lacks repository capability evidence.` );
		for ( let evidenceIndex = 0; evidenceIndex < review.repositoryCapabilityEvidence.length; evidenceIndex ++ ) {

			validateLockEvidence( review.repositoryCapabilityEvidence[ evidenceIndex ], sourceRoot, `phase1RequiredReviews:${ review.id }.repositoryCapabilityEvidence[${ evidenceIndex }]` );

		}
		if ( review.assetEvidence !== undefined && ! Array.isArray( review.assetEvidence ) ) throw new Error( `${ review.id } assetEvidence must be an array.` );
		for ( let evidenceIndex = 0; evidenceIndex < ( review.assetEvidence ?? [] ).length; evidenceIndex ++ ) {

			validateAssetEvidence( review.assetEvidence[ evidenceIndex ], upstreamRoot, `phase1RequiredReviews:${ review.id }.assetEvidence[${ evidenceIndex }]` );

		}
		if ( review.dslShard === null || review.scenarios.length === 0 ) throw new Error( `${ review.id } must lock a shard and deterministic scenarios.` );

	}
	const deferredReclassifications = statusLock.phase1DeferredReclassifications ?? [];
	if ( ! Array.isArray( deferredReclassifications ) ) throw new Error( 'phase1DeferredReclassifications must be an array.' );
	for ( const reclassification of deferredReclassifications ) {

		if ( deferredReclassificationIds.has( reclassification.id ) ) throw new Error( `${ reclassification.id } has duplicate deferred reclassifications.` );
		deferredReclassificationIds.add( reclassification.id );
		if ( ! auditedIds.has( reclassification.id ) ) throw new Error( `Deferred reclassification ${ reclassification.id } is not in the 511-case audit.` );
		if ( ! statusLock.phase1RequiredReviews.some( function matchesRequiredReview( review ) { return review.id === reclassification.id; } ) ) throw new Error( `${ reclassification.id } cannot be reclassified without a complete required review.` );
		if ( statusLock.phase1DeferredReviews.some( function matchesDeferredReview( review ) { return review.id === reclassification.id; } ) || deferredIds.has( reclassification.id ) ) throw new Error( `${ reclassification.id } already has a deferred decision.` );
		if ( ! Array.isArray( reclassification.missingCapabilities ) || reclassification.missingCapabilities.length === 0 ) throw new Error( `${ reclassification.id } reclassification must enumerate missingCapabilities.` );
		if ( ! validDeferredReasonCodes.has( reclassification.reasonCode ) ) throw new Error( `${ reclassification.id } has invalid deferred reasonCode '${ reclassification.reasonCode }'.` );
		for ( const fieldName of [ 'whyCurrentDslCannotExpressIt', 'minimumFutureApi', 'reentryTest' ] ) {

			if ( typeof reclassification[ fieldName ] !== 'string' || reclassification[ fieldName ].trim().length === 0 ) throw new Error( `${ reclassification.id } reclassification must populate ${ fieldName }.` );

		}
		if ( ! Array.isArray( reclassification.notes ) || reclassification.notes.length === 0 ) throw new Error( `${ reclassification.id } reclassification must include notes.` );

	}
	for ( const review of statusLock.phase1DeferredReviews ) {

		if ( deferredIds.has( review.id ) ) throw new Error( `${ review.id } duplicates a detector-adjudicated deferred decision.` );
		if ( ! Array.isArray( review.requiredCapabilities ) || review.requiredCapabilities.length === 0 ) throw new Error( `${ review.id } must enumerate every required capability.` );
		if ( ! Array.isArray( review.availableCapabilities ) ) throw new Error( `${ review.id } availableCapabilities must be an array.` );
		if ( ! Array.isArray( review.missingCapabilities ) || review.missingCapabilities.length === 0 ) throw new Error( `${ review.id } must enumerate at least one missing capability.` );
		for ( const capability of review.availableCapabilities ) if ( ! review.requiredCapabilities.includes( capability ) ) throw new Error( `${ review.id } available capability '${ capability }' is not required.` );
		for ( const capability of review.missingCapabilities ) if ( ! review.requiredCapabilities.includes( capability ) ) throw new Error( `${ review.id } missing capability '${ capability }' is not required.` );
		for ( const capability of review.requiredCapabilities ) {

			const available = review.availableCapabilities.includes( capability );
			const missing = review.missingCapabilities.includes( capability );
			if ( available === missing ) throw new Error( `${ review.id } capability '${ capability }' must be exactly one of available or missing.` );

		}
		if ( ! validDeferredReasonCodes.has( review.reasonCode ) ) throw new Error( `${ review.id } has invalid deferred reasonCode '${ review.reasonCode }'.` );
		for ( const fieldName of [ 'whyCurrentDslCannotExpressIt', 'minimumFutureApi', 'reentryTest' ] ) {

			if ( typeof review[ fieldName ] !== 'string' || review[ fieldName ].trim().length === 0 ) throw new Error( `${ review.id } must populate ${ fieldName }.` );

		}
		if ( ! Array.isArray( review.notes ) || review.notes.length === 0 ) throw new Error( `${ review.id } must include review notes.` );
		if ( ! Array.isArray( review.repositoryCapabilityEvidence ) || review.repositoryCapabilityEvidence.length === 0 ) throw new Error( `${ review.id } lacks repository capability evidence.` );
		for ( let evidenceIndex = 0; evidenceIndex < review.repositoryCapabilityEvidence.length; evidenceIndex ++ ) {

			validateLockEvidence( review.repositoryCapabilityEvidence[ evidenceIndex ], sourceRoot, `phase1DeferredReviews:${ review.id }.repositoryCapabilityEvidence[${ evidenceIndex }]` );

		}
		if ( review.assetEvidence !== undefined && ! Array.isArray( review.assetEvidence ) ) throw new Error( `${ review.id } assetEvidence must be an array.` );
		for ( let evidenceIndex = 0; evidenceIndex < ( review.assetEvidence ?? [] ).length; evidenceIndex ++ ) {

			validateAssetEvidence( review.assetEvidence[ evidenceIndex ], upstreamRoot, `phase1DeferredReviews:${ review.id }.assetEvidence[${ evidenceIndex }]` );

		}

	}

}

/** Serializes the blocker report deterministically. */
function serializeReport( report ) {

	return `${ JSON.stringify( report, null, 2 ) }\n`;

}

/** Writes or byte-checks both locked manifest and concrete blocker report. */
function main() {

	const argumentsList = process.argv.slice( 2 );
	const upstreamRoot = readRequiredPathOption( argumentsList, '--upstream-root' );
	const inventoryPath = readOption( argumentsList, '--inventory', defaultInventoryPath );
	const exclusionsPath = readOption( argumentsList, '--exclusions', defaultExclusionsPath );
	const auditPath = readOption( argumentsList, '--audit', defaultAuditPath );
	const adjudicationPath = readOption( argumentsList, '--adjudication', defaultAdjudicationPath );
	const statusLockPath = readOption( argumentsList, '--status-lock', defaultStatusLockPath );
	const manifestOutputPath = readOption( argumentsList, '--manifest-output', defaultManifestPath );
	const reportOutputPath = readOption( argumentsList, '--report-output', defaultReportPath );
	const inventory = readJson( inventoryPath );
	const exclusions = readJson( exclusionsPath );
	const capabilityAudit = readJson( auditPath );
	const adjudication = readJson( adjudicationPath );
	const statusLock = readJson( statusLockPath );
	validateUpstreamRevision( upstreamRoot );
	validateCapabilityAdjudication( { audit: capabilityAudit, freeze: readJson( path.join( manifestDirectory, 'phase1-capability-freeze.json' ) ), adjudication, upstreamRoot, sourceRoot: repositoryRoot } );
	validateStatusLock( statusLock, capabilityAudit, adjudication, upstreamRoot, repositoryRoot );
	const baselineManifest = buildManifest( inventory, exclusions );
	const lockedResult = buildPhase1ManifestLock( baselineManifest, capabilityAudit, adjudication, statusLock );
	const manifest = applySingleSamplePolicy( lockedResult.manifest );
	const report = {
		...lockedResult.report,
		statusCounts: manifest.counts
	};
	const validation = validateManifest( manifest, exclusions );
	if ( validation.failures.length > 0 ) throw new Error( `Locked manifest validation failed:\n${ validation.failures.join( '\n' ) }` );
	const manifestText = serializeJson( manifest );
	const reportText = serializeReport( report );

	if ( argumentsList.includes( '--check' ) ) {

		if ( fs.readFileSync( manifestOutputPath, 'utf8' ) !== manifestText ) throw new Error( `${ manifestOutputPath } is stale.` );
		if ( fs.readFileSync( reportOutputPath, 'utf8' ) !== reportText ) throw new Error( `${ reportOutputPath } is stale.` );
		console.log( `Phase-1 manifest lock is deterministic: pending=${ manifest.counts.auditPending } required=${ manifest.counts.phase1Required } deferred=${ manifest.counts.deferredMissingCapability }.` );
		return;

	}

	fs.writeFileSync( manifestOutputPath, manifestText );
	fs.writeFileSync( reportOutputPath, reportText );
	console.log( `Wrote phase-1 manifest lock: pending=${ manifest.counts.auditPending } required=${ manifest.counts.phase1Required } deferred=${ manifest.counts.deferredMissingCapability }.` );

}

if ( process.argv[ 1 ] && path.resolve( process.argv[ 1 ] ) === fileURLToPath( import.meta.url ) ) main();
